/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
 * Copyright notice:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
	Used when reading data only to skip it
*/
#define UNUSED __attribute__((unused))
#include "util/dtassert.h"
#include "section.h"
#include "packetstream.h"
#include "psi.h"
#include "si.h"
#include "sidebug.h"
#include "streamwriter.h"

#include "dvbtext.h"
#include "opentv_string_decoder.h"
#include "psi_impl.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

using namespace dtdemux;
using namespace dtypes;

#define BCDCHARTOINT(x) (10 * ((x & 0xF0) >> 4) + (x & 0xF))
#define lang_iso639(a, b, c) ((a) << 16 | (b) << 8 | (c))

inline static int BCD2INT(int x) {
	return ((1000000 * BCDCHARTOINT((x >> 24) & 0xFF)) + (10000 * BCDCHARTOINT((x >> 16) & 0xFF)) +
					(100 * BCDCHARTOINT((x >> 8) & 0xFF)) + BCDCHARTOINT(x & 0xFF));
}

namespace dtdemux {

// taken and adapted from libdtv, (c) Rolf Hakenes
// CRC32 lookup table for polynomial 0x04c11db7
	static uint32_t crc_table[256] = {
		0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005, 0x2608edb8,
		0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
		0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f, 0x639b0da6,
		0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
		0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84,
		0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
		0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a,
		0xec7dd02d, 0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
		0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
		0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
		0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba, 0xaca5c697, 0xa864db20, 0xa527fdf9,
		0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
		0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b,
		0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
		0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c,
		0x774bb0eb, 0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
		0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b, 0x0315d626,
		0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
		0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
		0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
		0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a,
		0x8cf30bad, 0x81b02d74, 0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
		0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093,
		0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
		0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679,
		0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
		0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09, 0x8d79e0be, 0x803ac667,
		0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
		0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

	uint32_t crc32(const uint8_t* data, int size) {
		int i;
		uint32_t crc = 0xFFFFFFFF;
		if (size < 4)
			return false;

		for (i = 0; i < size; i++)
			crc = (crc << 8) ^ crc_table[((crc >> 24) ^ (uint8_t)data[i])];

		return crc;
	}

	template <> descriptor_t stored_section_t::get<descriptor_t>() {
		descriptor_t ret{};
		if (available() < 2) {
			throw_bad_data();
		} else {
			ret.tag = get<uint8_t>();
			ret.len = get<uint8_t>();
		}
		return ret;
	}

	bool pmt_info_t::has_ca_pid(uint16_t pid) const {
		for (auto& desc : ca_descriptors) {
			if (desc.ca_pid == pid)
				return true;
		}
		return false;
	}

/* See iso13818.pdf p. 87
	 The conditional access descriptor is used to specify both system-wide conditional access management information such
	 as EMMs and elementary stream-specific information such as ECMs. It may be used in both the
	 TS_program_map_section (refer to 2.4.4.8 == in a PMT section) and the program_stream_map (refer to 2.5.3).
	 If any elementary stream is scrambled, a CA descriptor shall be present for the program containing that
	 elementary stream.

	 If any system-wide	 conditional access management information exists within a Transport Stream, a CA
	 descriptor shall be present in the conditional access table.

	 When the CA descriptor is found in the TS_program_map_section (table_id = 0x02 == PMT section), the CA_PID
	 points to packets containing program related access control information, such as ECMs. Its presence as
	 program information (== in the common descriptor loop) indicates applicability to the entire program. In
	 the same case, its presence as extended ES  information indicates applicability to the associated
	 program element. Provision is also made for private data.
	 => In other words: it is possilble for video, audio, ... to have different ca pids.

	 When the CA descriptor is found in the CA_section (table_id = 0x01), the CA_PID points to packets
	 containing systemwide and/or access control management information, such as EMMs.
	 => In other words: This applies to the conditional access table.
*/

	ca_info_t pmt_info_t::get_ca(stored_section_t& s, const descriptor_t& desc, uint16_t stream_pid) {
		ca_info_t ret;
		auto start = s.bytes_read;
		ret.stream_pid = stream_pid;
		ret.ca_system_id = s.get<uint16_t>();
		ret.ca_pid = s.get<uint16_t>() & 0x1fff;
		capmt_data.push_back(desc.tag);
		capmt_data.push_back(desc.len);
		capmt_data.push_back(uint8_t(ret.ca_system_id >> 8));
		capmt_data.push_back(uint8_t(ret.ca_system_id & 0xff));
		capmt_data.push_back(uint8_t(ret.ca_pid >> 8) | 0xe0);
		capmt_data.push_back(uint8_t(ret.ca_pid & 0xff));
		auto len = desc.len - (s.bytes_read - start);
		capmt_data.reserve(capmt_data.size() + len);
		auto numread = s.get_buffer((uint8_t*)capmt_data.buffer() + capmt_data.size(), len);
		capmt_data.resize_no_init(numread + capmt_data.size());
		assert(numread == len);

		return ret;
	}

	template <>
	service_move_info_t stored_section_t::get<service_move_info_t>(const descriptor_t& desc, uint16_t stream_pid) {
		service_move_info_t ret;
		ret.stream_pid = stream_pid;
		ret.new_original_network_id = get<uint16_t>();
		ret.new_transport_stream_id = get<uint16_t>();
		ret.new_service_id = get<uint16_t>();
		return ret;
	}

	template <> int stored_section_t::get_fields<void>(ss::bytebuffer_& ret) {
		auto len = get<uint8_t>();
		ret.resize_no_init(len);
		if (this->get_buffer((uint8_t*)ret.buffer(), len) < 0) {
			throw_bad_data();
			return -1;
		}
		return 0;
	}

	inline uint8_t bcdToDec(unsigned char b) {
		return (uint8_t)(((b >> 4) & 0x0F) * 10 + (b & 0x0F));
	}

	template <> int stored_section_t::get_fields<start_time_duration_t>(epgdb::epg_record_t& rec) {
		auto mjd = get<uint16_t>();
		struct tm t;
		t.tm_hour = bcdToDec(get<uint8_t>());
		t.tm_min = bcdToDec(get<uint8_t>());
		t.tm_sec = bcdToDec(get<uint8_t>());

		int k;
		t.tm_year = (int)((mjd - 15078.2) / 365.25);
		t.tm_mon = (int)((mjd - 14956.1 - (int)(t.tm_year * 365.25)) / 30.6001);
		t.tm_mday = (int)(mjd - 14956 - (int)(t.tm_year * 365.25) - (int)(t.tm_mon * 30.6001));
		k = (t.tm_mon == 14 || t.tm_mon == 15) ? 1 : 0;
		t.tm_year = t.tm_year + k;
		t.tm_mon = t.tm_mon - 1 - k * 12;
		t.tm_mon--;

		t.tm_isdst = -1;
		t.tm_gmtoff = 0;
		rec.k.start_time = timegm(&t);

		// be careful: do not merge the folloing three statements into one (order of evnaluation undefined)
		auto duration = bcdToDec(get<uint8_t>()) * 3600;
		duration += bcdToDec(get<uint8_t>()) * 60;
		duration += bcdToDec(get<uint8_t>());
		rec.end_time = rec.k.start_time + duration;
		return has_error();
	}

	template <> int stored_section_t::get_fields<mhw2_dvb_text_t>(ss::string_& ret) {
		auto len = get<uint16_t>() & 0xfff;
		auto* p = this->current_pointer(len);
		if (!p)
			return -1;
		decodeText(ret, p, len);
		return 0;
	}

	template <> int stored_section_t::get_fields<mhw2_start_time_duration_t>(epgdb::epg_record_t& rec) {
		auto mjd = get<uint16_t>();
		struct tm t;
		t.tm_hour = bcdToDec(get<uint8_t>());
		t.tm_min = bcdToDec(get<uint8_t>());
		t.tm_sec = bcdToDec(get<uint8_t>());

		int k;
		t.tm_year = (int)((mjd - 15078.2) / 365.25);
		t.tm_mon = (int)((mjd - 14956.1 - (int)(t.tm_year * 365.25)) / 30.6001);
		t.tm_mday = (int)(mjd - 14956 - (int)(t.tm_year * 365.25) - (int)(t.tm_mon * 30.6001));
		k = (t.tm_mon == 14 || t.tm_mon == 15) ? 1 : 0;
		t.tm_year = t.tm_year + k;
		t.tm_mon = t.tm_mon - 1 - k * 12;
		t.tm_mon--;

		t.tm_isdst = -1;
		t.tm_gmtoff = 0;
		rec.k.start_time = timegm(&t);

		// be careful: do not merge the folloing three statements into one (order of evnaluation undefined)
		auto duration = ((uint32_t)get<uint8_t>() << 8);
		duration |= get<uint8_t>();
		duration >>= 4;
		rec.end_time = rec.k.start_time + duration * 60;
		return has_error();
	}

	template <typename U, typename T, int size>
	int stored_section_t::get_vector_fields(ss::vector<T, size>& ret, const descriptor_t& desc) {
		auto len = desc.len;
		auto end = available() - desc.len;

		while (available() > end) {
			auto x = get<T>();
			if (has_error())
				return -1;
			ret.push_back(x);
		}
		return 0;
	}

	template <>
	int stored_section_t::get_fields<frequency_list_descriptor_t>(frequency_list_t& ret, const descriptor_t& desc) {
		auto end = available() - desc.len;
		auto coding_type = get<uint8_t>();
		coding_type &= 0x3;
		while (available() > end) {
			auto f = get<uint32_t>();
			ret.frequencies.push_back(f);
		}
		switch (coding_type) {
		case 0:
			// undefined
			break;
		case 1:		// satellite
		case 2: { // cable
			int scale = (coding_type == 1) ? 100 : 10;
			for (auto& f : ret.frequencies)
				f = BCD2INT(f) / scale;
		} break;
		case 3: { // terrestrial
			int scale = 10;
			for (auto& f : ret.frequencies)
				f *= scale; // now in Hz
		} break;
		}
		return 0;
	}

	template <> int stored_section_t::get_fields<dvb_text_t>(ss::string_& ret, const descriptor_t& desc) {
		auto* p = this->current_pointer(desc.len);
		if (!p)
			return -1;
		decodeText(ret, p, desc.len);
		return 0;
	}

	template <> int stored_section_t::get_fields<dvb_text_t>(ss::string_& ret) {
		auto len = get<uint8_t>();
		auto* p = this->current_pointer(len);
		if (!p)
			return -1;
		decodeText(ret, p, len);
		return 0;
	}

	template <> int stored_section_t::get_fields<s2_satellite_delivery_system_descriptor_t>(chdb::dvbs_mux_t& mux) {
		auto flags = get<uint8_t>();
		bool scrambling_sequence_selector = (flags & 0x80);
		bool mis = (flags & 0x40); // multistream
		bool backward_compatibility = (flags & 20);
		if (scrambling_sequence_selector) {
			auto x = get<uint8_t>();
			auto y = get<uint16_t>();
			mux.pls_code = ((x & 0x3) << 16) | y;
			mux.pls_mode = fe_pls_mode_t::ROOT; //???? no idea if this is correct
		}
		if (mis) {
			mux.k.stream_id = get<uint8_t>();
		} else {
			mux.k.stream_id = -1;
		}
		if (has_error())
			return -1;
		return 0;
	}

	template <> int stored_section_t::get_fields<satellite_delivery_system_descriptor_t>(chdb::dvbs_mux_t& mux) {
		auto frequency = get<uint32_t>();
		frequency = BCD2INT(frequency) * 10; // in kHz

		auto orbital_position = get<uint16_t>();
		orbital_position = BCD2INT(orbital_position) * 10; // in 0.01 degrees

		auto flags = get<uint8_t>();
		bool is_east = (flags & 0x80);
		if (!is_east)
			orbital_position = -orbital_position;
		uint8_t pol = (flags >> 5) & 0x3; // 00=horizontal 01=vertical 10=left 11=right
		uint8_t roll_off = (flags >> 3) & 0x3;// 00=0.35 01=0.25 10=0.2 11=reserved
		bool is_dvbs2 = (flags>>2) & 0x1;
		auto modulation = dvbs_modulation_from_stream(flags & 0x1f); // auto, qpsk, 8psk, 16-qam
		auto symbol_rate = get<uint32_t>();
		uint8_t fec_inner = symbol_rate & 0xf;
		symbol_rate = BCD2INT(symbol_rate >> 4); // in symbols/s
		if (has_error())
			return -1;
		mux.k.sat_pos = orbital_position;
		mux.frequency = frequency; // in kHz
		mux.pol = (chdb::fe_polarisation_t)pol;
		mux.symbol_rate = symbol_rate * 100; // in Symbols/s
		mux.delivery_system = is_dvbs2 ? chdb::fe_delsys_dvbs_t::SYS_DVBS2 : chdb::fe_delsys_dvbs_t::SYS_DVBS;
		mux.rolloff = (roll_off == 0) ? chdb::fe_rolloff_t::ROLLOFF_35 :
			(roll_off == 1) ? chdb::fe_rolloff_t::ROLLOFF_20 :
			(roll_off == 2) ? chdb::fe_rolloff_t::ROLLOFF_25 :
			chdb::fe_rolloff_t::ROLLOFF_AUTO;
		mux.modulation = (chdb::fe_modulation_t)modulation;
		mux.fec = (chdb::fe_code_rate_t)fec_inner;
		return 0;
	}

	template <> int stored_section_t::get_fields<cable_delivery_system_descriptor_t>(chdb::dvbc_mux_t& mux) {
		auto frequency = get<uint32_t>();
		// For cable systems, retrieved frequency is in 1/10000 MHz -- ETSI EN 300 468 V1.15.1 (2016-03)
		frequency = BCD2INT(frequency) / 10; // now in kHz

		auto fec_outer = get<uint16_t>();
		fec_outer &= 0xf;

		uint8_t modulation_ = get<uint8_t>();
		auto modulation = dvbc_modulation_from_stream(modulation_); // auto, qpsk, 8psk, 16-qam
		auto symbol_rate = get<uint32_t>();
		auto fec_inner = dvbc_inner_code_rate_from_stream(symbol_rate & 0xf); // to check
		symbol_rate = BCD2INT(symbol_rate >> 4);															// to check
		RETURN_ON_ERROR - 1;

		mux.k.sat_pos = sat_pos_dvbc;
		// The DVB-C Annex-A is the widely used cable standard. Transmission uses QAM modulation.
		// SYS_DVBC_ANNEX_B: used in the US
		// The DVB-C Annex-C is optimized for 6MHz, and is used in Japan. It supports a subset of the
		// Annex A modulation types, and a roll-off of 0.13, instead of 0.15
		// TODO: find a method to distinguish between A B and C
		mux.delivery_system = fe_delsys_dvbc_t::SYS_DVBC;
		mux.modulation = (fe_modulation_t)modulation;
		mux.frequency = frequency;					 // in kHz
		mux.symbol_rate = symbol_rate * 100; // in Symbols/s

		mux.fec_inner = (fe_code_rate_t)fec_inner;
		mux.fec_outer = (fe_code_rate_t)fec_outer;
		return 0;
	}

	template <> int stored_section_t::get_fields<terrestrial_delivery_system_descriptor_t>(dvbt_mux_t& mux) {
		int ret = 0;
		/////////////////////////////
		auto frequency = get<uint32_t>();
		if (frequency == 0xffffffff) {
			/*this happens on 5.0W on the French multistream transponders. The frequency
				is obviously wrong and multiple muxes with overlapping frequency are in the
				NIT, causing constant database overrides
			*/
			ret = -1;
		}
		// For dvbt systems, retrieved frequency is in 10Hz -- ETSI EN 300 468 V1.15.1 (2016-03)
		frequency = frequency / 100; // now ink Hz

		auto flags = get<uint8_t>();
		auto bandwidth = dvt_bandwidth_from_stream((flags >> 5));
		bool priority = flags & 0x10;
		bool time_slicing_indicator = flags & 0x8;
		bool mpe_fec_indicator = flags & 0x4;

		auto flags2 = get<uint8_t>();
		auto modulation = dvt_modulation_from_stream(flags2 >> 6); // constellation
		auto hierarchy = dvbt_hierarchy_from_stream((flags2 >> 3) & 0x7);
		auto code_rate_hp = dvbt_code_rate_from_stream(flags2 & 0x7);

		auto flags3 = get<uint8_t>();
		auto code_rate_lp = dvbt_code_rate_from_stream(flags3 >> 5);
		auto guard_interval = dvbt_transmission_guard_interval_from_stream((flags3 >> 3) & 0x3);
		auto transmission_mode = dvbt_transmission_mode_from_stream((flags3 >> 1) & 0x3);
		bool other_frequency_flag = flags3 & 0x1;
		auto reserved UNUSED = get<uint32_t>();

		mux.k.sat_pos = sat_pos_dvbt;
		// The DVB-C Annex-A is the widely used cable standard. Transmission uses QAM modulation.
		// SYS_DVBC_ANNEX_B: used in the US
		// The DVB-C Annex-C is optimized for 6MHz, and is used in Japan. It supports a subset of the
		// Annex A modulation types, and a roll-off of 0.13, instead of 0.15
		// TODO: find a method to distinguish between A B and C
		mux.delivery_system = fe_delsys_dvbt_t::SYS_DVBT;
		mux.modulation = (fe_modulation_t)modulation;
		mux.frequency = frequency; // in Hz
		mux.HP_code_rate = (fe_code_rate_t)code_rate_hp;
		mux.LP_code_rate = (fe_code_rate_t)code_rate_lp;
		mux.transmission_mode = (fe_transmit_mode_t)transmission_mode;
		mux.guard_interval = (fe_guard_interval_t)guard_interval;
		mux.bandwidth = (fe_bandwidth_t)bandwidth;
		mux.hierarchy = (fe_hierarchy_t)hierarchy;

		return ret;
	}

	template <> int stored_section_t::get_fields<multiprotocol_encapsulation_info_structure_t>(service_t& ret) {
		auto num_mac_bytes = get<uint8_t>();
		auto mac_ip_mapping_flag UNUSED =  (num_mac_bytes>>3)&1;
		num_mac_bytes &= 0x7;
		auto max_sections_per_datagram UNUSED =  get<uint8_t>();
		return 0;
	}



	template <> int stored_section_t::get_fields<service_descriptor_t>(service_t& ret) {
		if (available() < 2) {
			throw_bad_data();
			return -1;
		}
		ret.service_type = get<uint8_t>();
		ret.media_mode = chdb::media_mode_for_service_type(ret.service_type);
		if (get_fields<dvb_text_t>(ret.provider))
			return -1;
		if (get_fields<dvb_text_t>(ret.name))
			return -1;
		return 0;
	}

	template <> int stored_section_t::get_fields<data_broadcast_descriptor_t>(service_t& ret) {
		if (available() < 7) {
			throw_bad_data();
			return -1;
		}
		auto data_broadcast_id UNUSED = get<uint16_t>();
		auto component_tag UNUSED = get<uint8_t>();
		auto selector_len = get<uint8_t>();
		if(available() < 5) {
			throw_bad_data();
			return -1;
		}
		switch (data_broadcast_id) {

		case 0x3: //
			if (selector_len != 0) {
				throw_bad_data();
				return -1;
			}
			ret.service_type = 0x0c; //data service
			ret.media_mode = chdb::media_mode_for_service_type(ret.service_type);
			break;
		case 0x5: //Multi protocol encapsulation
			if (selector_len != 2) {
				throw_bad_data();
				return -1;
			}
			get_fields<multiprotocol_encapsulation_info_structure_t>(ret);
			ret.service_type = 0x0c; //data service
			ret.media_mode = chdb::media_mode_for_service_type(ret.service_type);
			break;
		default:
			skip(selector_len);
			break;
		}

		if(available() < 4) {
			throw_bad_data();
			return -1;
		}
		auto* p = current_pointer(3);
		auto langcode UNUSED = lang_iso639(p[0], p[1], p[2]);
		auto text_len = get<uint8_t>();

		if(available() < text_len) {
			throw_bad_data();
			return -1;
		}
		skip(text_len);
		return 0;
	}

	template <> int stored_section_t::get_fields<service_list_descriptor_t>(service_list_t& ret, const descriptor_t& desc) {

		auto end = available() - desc.len;
		while (available() >= 3 + end) {
			chdb::service_key_t service;
			service.service_id = get<uint16_t>();
			service.network_id = ret.network_id;
			service.ts_id = ret.ts_id;
			auto service_type = get<uint8_t>();
			auto lcn = 0;
			auto channel_id = service.service_id;
			ret.bouquet.channels[channel_id] = {service, (uint16_t)lcn, (uint8_t)service_type};
		}
		if(available() > end)
			skip(available() - end);
		assert(available() == end);
		return has_error();
	}

	template <> int stored_section_t::get_fields<otv_service_descriptor_t>(service_list_t& ret) {
		if (available() < 9) {
			throw_bad_data();
			return -1;
		}
		chdb::service_key_t service;
		service.service_id = get<uint16_t>();
		service.network_id = ret.network_id;
		service.ts_id = ret.ts_id;
		auto service_type = get<uint8_t>();
		auto channel_id = get<uint16_t>();
		auto lcn = get<uint16_t>();
		auto unk UNUSED = get<uint16_t>();
		ret.bouquet.channels[channel_id] = {service, (uint16_t)lcn, (uint8_t)service_type, (uint8_t) true};
		return has_error();
	}

	template <>
	int stored_section_t::get_fields<fst_region_list_descriptor_t>(fst_region_list_t& ret, const descriptor_t& desc) {
		auto end = available() - desc.len;
		bool found = false;
		while (available() > end) {
			auto region_id = get<uint16_t>();
			auto* p = current_pointer(3);
			int langcode = lang_iso639(p[0], p[1], p[2]);
			if (region_id == ret.selected_region_id ||
					(region_id == 0xffff && !found)) { // 0xfffff is default region, but can be overridden by region_id
				get_fields<dvb_text_t>(ret.bouquet_name);
				if (region_id != 0xffff)
					found = true;
			} else {
				ss::string<256> description;
				get_fields<dvb_text_t>(description);
			}
		}
		assert(available() == end);
		return has_error();
	}

	template <>
	int stored_section_t::get_fields<fst_category_description_list_descriptor_t>(fst_channel_category_t& ret,
																																							 const descriptor_t& desc) {
		auto end = available() - desc.len;
		while (available() > end) {
			auto category_group = get<uint8_t>();
			auto category_id = get<uint8_t>();
			auto x UNUSED = get<uint16_t>();
			auto y UNUSED = get<uint16_t>();
			ss::string<256> description;
			get_fields<dvb_text_t>(description);
			dtdebugx("FSTX: group=%d id=%d cat=%s", category_group, category_id, description.c_str());
		}
		assert(available() == end);
		return has_error();
	}

	template <>
	int stored_section_t::get_fields<fst_channel_category_list_descriptor_t>(fst_channel_category_list_t& ret,
																																					 const descriptor_t& desc) {
		auto end = available() - desc.len;
		while (available() > end + 2) {
			auto category_group = get<uint8_t>();
			auto category_id = get<uint8_t>();
			auto desc_loop_len = get<uint8_t>();

			auto end1 = available() - desc_loop_len;
			assert(desc_loop_len % 2 == 0);
			while (available() > end1) {
				auto channel_id UNUSED = get<uint16_t>() & 0xfff;
				dtdebugx("FST: ChannelCategory group=%d cat_id=%d channel_id=%d\n", category_group, category_id, channel_id);
			}
			assert(available() == end1);
		}

		assert(available() >= end);
		if (available() > end) {
			// this is not right! sometimes desc.len=12 and we end up here...
			dterrorx("desc.len=%d skipping %d\n", desc.len, available() - end);
			skip(available() - end);
		}
		return has_error();
	}

/*
	https://blog.nexusuk.org/2014/07/decoding-freesat-part-2.html
*/
	template <> int stored_section_t::get_fields<fst_service_descriptor_t>(fst_service_list_t& ret) {
		auto service_id = get<uint16_t>();
		auto channel_id = get<uint16_t>();

		auto desc_loop_len = get<uint8_t>();
		auto end1 = available() - desc_loop_len;
		int count = 0;
		while (available() > end1) {
			auto lcn = get<uint16_t>() & 0xfff;
			auto service_type = lcn >> 12; // something with regions or service_type?
			lcn &= 0x0fff;
			auto region_id = get<uint16_t>(); // Region number 65535 appears to be a fallback or default region.
			if (region_id == ret.selected_region_id) {
				chdb::service_key_t service;
				service.network_id = ret.network_id;
				service.ts_id = ret.ts_id;
				service.service_id = service_id;
				ret.bouquet.channels[channel_id] = {service, (uint16_t)lcn, (uint8_t)service_type};
			}
		}
		assert(end1 == available());

		return has_error();
	}

	template <>
	int stored_section_t::get_fields<fst_service_list_descriptor_t>(fst_service_list_t& ret, const descriptor_t& desc) {
		if (available() < 7) {
			throw_bad_data();
			return -1;
		}
		auto end = available() - desc.len;
		while (available() > end) {
			stored_section_t::get_fields<fst_service_descriptor_t>(ret);
		}
		assert(end == available());
		return has_error();
	}

	template <>
	int stored_section_t::get_fields<otv_service_list_descriptor_t>(service_list_t& ret, const descriptor_t& desc) {
		auto end = available() - desc.len;
		auto x UNUSED = get<uint16_t>();
		while (available() > end) {
			stored_section_t::get_fields<otv_service_descriptor_t>(ret);
		}
		assert(available() == end);
		return has_error();
	}

	template <> int stored_section_t::get_fields<ca_identifier_descriptor_t>(service_t& ret, const descriptor_t& desc) {
		if (available() < desc.len) {
			throw_bad_data();
			return -1;
		}
		ss::vector<uint8_t, 16> ca_systems;
		if (get_vector_fields<void>(ca_systems, desc))
			return -1;
		// TODO: do something more useful here
		return 0;
	}

	template <> int stored_section_t::get_fields<linkage_descriptor_t>(bouquet_linkage_t& l, const descriptor_t& desc) {
		/*information on service which provides additional info (e.g., a replacement
			when not running) for the current service*/
		auto ts_id = get<uint16_t>();
		auto original_network_id = get<uint16_t>();
		auto service_id = get<uint16_t>();

		auto linkage_type = get<uint8_t>();
		switch (linkage_type) {
		case 0x02: // EPG service

			dtdebugx("LINK to epg service nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			l.network_id = original_network_id;
			l.ts_id = ts_id;
			break;

		case 0x04: // TS containing complete Network/Bouquet SI

			dtdebugx("LINK to network/bouquet info nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			l.network_id = original_network_id;
			l.ts_id = ts_id;
			break;
		case 0x09: // System Software Update Service (TS 102 006 [11])
		default:
#if 0
			dtdebugx("NIT: unknown linkage: nid=%d, tid=%d, sid=%d link_type=0x%x]\n",
							 original_network_id, ts_id, service_id, linkage_type);
			//not interested...
#endif
			break;
		}

		skip(desc.len - 7);
		// TODO: do something more useful here
		return 0;
	}

	template <> int stored_section_t::get_fields<linkage_descriptor_t>(service_t& ret, const descriptor_t& desc) {
		/*information on service which provides additional info (e.g., a replacement
			when not running) for the current service*/
		auto ts_id = get<uint16_t>();
		auto original_network_id = get<uint16_t>();
		auto service_id = get<uint16_t>();
		auto linkage_type = get<uint8_t>();
		switch (linkage_type) {
		case 0x01: // information service
			dtdebugx("LINK to info service nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			break;

		case 0x02: // epg service
			// dtdebugx("LINK to epg service nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			break;

		case 0x04: // TS containing complete Network/Bouquet SI
			// dtdebugx("LINK to network/bouquet nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			break;

		case 0x05: // service replacement service
			// dtdebugx("LINK to replacement service nid=%d, tid=%d, sid=%d]\n", original_network_id, ts_id, service_id);
			break;
		case 0x09: // System Software Update Service (TS 102 006 [11])
			break;
		default:
			// not interested...
			break;
		}

		skip(desc.len - 7);
		// TODO: do something more useful here
		return 0;
	}

	template <> int stored_section_t::get_fields<short_event_descriptor_t>(epgdb::epg_record_t& rec) {
		auto* p = current_pointer(3);
		int langcode = lang_iso639(p[0], p[1], p[2]);
		get_fields<dvb_text_t>(rec.event_name);
		get_fields<dvb_text_t>(rec.story);
		return 0;
	}

	template <>
	int stored_section_t::get_fields<opentv_title_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc,
																															uint16_t mjd_time) {
		auto end = available();
		int time_offset = get<uint16_t>(); // bytes 16,17
		time_offset <<= 1;
		int duration = get<uint16_t>(); // bytes 18,19
		duration <<= 1;
		auto category = get<uint8_t>(); // bytes 20
		auto x2 = get<uint16_t>();			// bytes 21...22

		int content_code = 0;
		int parental_rating = 0;
		rec.k.start_time = ((mjd_time - 40587) * 86400) + time_offset;
		rec.end_time = rec.k.start_time + duration;

		auto len = desc.len - (end - available());
		auto* p = (uint8_t*)this->current_pointer(len);
		if (!p)
			return -1;
		opentv_decode_string(rec.event_name, p, len, opentv_table_type_t::SKY_UK);
		return 0;
	}

	template <>
	int stored_section_t::get_fields<opentv_summary_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc) {
		auto len = desc.len;
		auto* p = (uint8_t*)this->current_pointer(len);
		if (!p)
			return -1;
		opentv_decode_string(rec.story, p, len, opentv_table_type_t::SKY_UK);
		return 0;
	}

	template <>
	int stored_section_t::get_fields<opentv_description_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc) {
		ss::string<64> description;
		auto len = desc.len;
		auto* p = (uint8_t*)this->current_pointer(len);
		if (!p)
			return -1;
		opentv_decode_string(description, p, len, opentv_table_type_t::SKY_UK);
		return 0;
	}

	template <>
	int stored_section_t::get_fields<opentv_serieslink_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc) {

		rec.series_link = get<uint16_t>();
		return 0;
	}

	template <> int stored_section_t::get_fields<extended_event_descriptor_t>(epgdb::epg_record_t& rec) {
		auto descriptor_number = get<uint8_t>();
		auto last_descriptor_number = descriptor_number & 0xf;
		descriptor_number >>= 4;
		auto* p = current_pointer(3);
		int langcode = lang_iso639(p[0], p[1], p[2]);
		auto len_of_items = get<uint8_t>();
		auto end = available() - len_of_items;

		while (available() > end) {
			ss::string<16> item_desc;
			ss::string<64> item;
			get_fields<dvb_text_t>(item_desc);
			get_fields<dvb_text_t>(item);
		}
		assert(available() == end);
		ss::string<128> text;
		get_fields<dvb_text_t>(rec.story); // should be appended to story
		return 0;
	}

	template <> int stored_section_t::get_fields<time_shifted_event_descriptor_t>(epgdb::epg_record_t& rec) {
		/*The time shifted event descriptoris used in place of the short_event_descriptor to indicate an event
			which is a time shifted copy of another event.*/

		auto reference_service_id = get<uint16_t>();
		auto reference_event_id = get<uint16_t>();
		return 0;
	}
/*can be used to find out the available languages for a record; e.g. 5.0W*/
	template <>
	int stored_section_t::get_fields<component_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc) {
		auto end = available() - desc.len;
		auto streacom_content = get<uint8_t>();
		auto streacom_content_ext = (streacom_content >> 4);
		streacom_content = (streacom_content & 0xf);
		auto component_type = get<uint8_t>();
		auto component_tag = get<uint8_t>();
		auto* p = current_pointer(3);
		int langcode = lang_iso639(p[0], p[1], p[2]);
		ss::string<16> text;
		auto len = available() - end;
		auto* p1 = current_pointer(len);
		if (!p1)
			return -1;
		decodeText(text, p1, len);
		return 0;
	}

	template <>
	int stored_section_t::get_fields<multilingual_component_descriptor_t>(epgdb::epg_record_t& rec,
																																				const descriptor_t& desc) {
		auto end = available() - desc.len;
		auto component_tag = get<uint8_t>();
		while (available() > end) {
			auto* p = current_pointer(3);
			int langcode = lang_iso639(p[0], p[1], p[2]);
			ss::string<32> text;
			get_fields<dvb_text_t>(text);
		}

		return 0;
	}

	template <> int stored_section_t::get_fields<content_descriptor_t>(epgdb::epg_record_t& rec, const descriptor_t& desc) {
		auto end = available() - desc.len;
		bool stored = false;
		while (available() > end) {
			auto content_code = get<uint8_t>();
			rec.content_codes.push_back(content_code);
			auto user UNUSED = get<uint8_t>();
		}
		return 0;
	}

	template <> subtitle_info_t stored_section_t::get<subtitle_info_t>(const descriptor_t& desc, uint16_t stream_pid) {
		subtitle_info_t ret;
		ret.lang_code[0] = get<char>();
		ret.lang_code[1] = get<char>();
		ret.lang_code[2] = get<char>();
		ret.lang_code[3] = 0;
		ret.subtitle_type = get<uint8_t>();
		ret.composition_page_id = get<uint16_t>();
		ret.ancillary_page_id = get<uint16_t>();
		return ret;
	}

	template <>
	audio_language_info_t stored_section_t::get<audio_language_info_t>(const descriptor_t& desc, uint16_t stream_pid) {
		audio_language_info_t ret;
		ret.lang_code[0] = get<char>();
		ret.lang_code[1] = get<char>();
		ret.lang_code[2] = get<char>();
		ret.lang_code[3] = 0;
		ret.audio_type = get<char>();
		return ret;
	}

/*! extract and save relevant data from a pmt descriptor
	in_es_loop indicates if we are parsing descriptors for the overall program
	or for elementary streams in it
*/
	void pmt_info_t::parse_descriptors(stored_section_t& s, pid_info_t& info, bool in_es_loop) {
		uint16_t program_info_len = s.get<uint16_t>();
		program_info_len &= 0x0fff;

		auto end = program_info_len + s.bytes_read;
		while (s.bytes_read < end) {

			switch (auto _desc = s.get<descriptor_t>(); _desc.tag) {
			case SI::CaDescriptorTag: {
				auto desc = get_ca(s, _desc, info.stream_pid);

				ca_descriptors.push_back(desc);
				dtdebug("sid=" << service_id << ": ECM PID=" << desc.ca_pid << " system_id=" << desc.ca_system_id
								<< " stream_pid=" << desc.stream_pid);
			} break;
			case SI::ServiceMoveDescriptorTag: {
				auto desc = s.get<service_move_info_t>(_desc, info.stream_pid);
				service_move_descriptors.push_back(desc);
				dtdebug("sid=" << service_id << ": service move descriptor "
								<< " new: network_id" << desc.new_original_network_id << " tsid=" << desc.new_transport_stream_id
								<< " sid=" << desc.new_service_id);
			} break;
			case SI::MHP_PrefetchDescriptorTag: {
				static int called = 0;
				if (!called) {
					dtdebug_nice("MHP_PrefetchDescriptor " << (int)_desc.tag << "=" << name_of_descriptor_tag(_desc.tag));
					called = 1;
				}
				s.skip(_desc.len);
				assert(s.bytes_read <= end);
			} break;
			case SI::StreamIdentifierDescriptorTag: {
				static int called = 0;
				if (_desc.len != 1) {
					s.throw_bad_data();
					return;
				}
				auto component_tag = s.get<uint8_t>();
				if (!called) {
					dtdebug_nice("Stream id: " << (int)component_tag);
					called = 1;
				}
			} break;
			case SI::AC3DescriptorTag: { // see en_300468v011101p-1.pdf p. 119
				info.audio_lang.ac3 = true;
				info.audio_lang.ac3_descriptor_data.push_back(_desc.tag);
				info.audio_lang.ac3_descriptor_data.push_back(_desc.len);
				info.audio_lang.ac3_descriptor_data.append_raw(s.current_pointer(0), _desc.len);
				s.skip(_desc.len);
			} break;
			case SI::EnhancedAC3DescriptorTag: { // see en_300468v011101p-1.pdf p. 121
				info.audio_lang.ac3 = true;
				info.audio_lang.ac3_descriptor_data.push_back(_desc.tag);
				info.audio_lang.ac3_descriptor_data.push_back(_desc.len);
				info.audio_lang.ac3_descriptor_data.append_raw(s.current_pointer(0), _desc.len);
				s.skip(_desc.len);
			} break;

			case SI::PrivateDataSpecifierDescriptorTag: {
				// See https://github.com/tsduck/tsduck/blob/master/src/libtsduck/tsduck.dvb.names
				// 4 bytes indicating the owner of the following private descriptor
				if (_desc.len != 4) {
					s.throw_bad_data();
					return;
				}
				auto private_data_specifier = s.get<uint32_t>();
				auto private_desc = s.get<descriptor_t>();
				if (private_desc.len == 1) {
					auto x = s.get<uint8_t>();
					if (private_data_specifier == 0x2													// sky
							&& info.stream_pid >= 0x30 && info.stream_pid <= 0x37 // sky title pid
							// && x == info.stream_pid - 0x30
						)
						num_sky_title_pids++;
					else if (private_data_specifier == 0x2												// sky
									 && info.stream_pid >= 040 && info.stream_pid <= 0x47 // sky title pid
									 //&& x == info.stream_pid - 0xe
						)
						num_sky_summary_pids++;
				} else
					s.skip(private_desc.len); // 0x40 1 byte for IEPG service 4398
				// dtdebugx("private data descriptor 0x%x len=%d", private_data_specifier, private_desc.len);
				if (private_data_specifier == 0x46534154 && info.stream_pid >= 3840 && info.stream_pid <= 3844) {
					has_freesat_epg = true; // has a local freesat stream
					num_freesat_pids++;
				}
			} break;
			case SI::SubtitlingDescriptorTag: { // see en_3000468v011101p.pdf p 73
				auto end = s.bytes_read + _desc.len;
				while (s.bytes_read < end) {
					auto sub = s.get<subtitle_info_t>(_desc, info.stream_pid);
					info.subtitle_descriptors.push_back(sub);
				}
				if (s.bytes_read != end) {
					s.throw_bad_data();
					return;
				}
			} break;
			case SI::ISO639LanguageDescriptorTag: {
				info.audio_lang = s.get<audio_language_info_t>(_desc, info.stream_pid);
			} break;
			case SI::ExtensionDescriptorTag: {
				auto end = s.available() - _desc.len;
				auto desc_tag_extension = s.get<uint8_t>();
				switch (desc_tag_extension) {
				case SI::T2MIDescriptorTag: {
					auto t2mi_stream_id = s.get<uint8_t>();
					t2mi_stream_id &= 7;
					auto num_t2mi_streams_minus_one = s.get<uint8_t>();
					num_t2mi_streams_minus_one &= 7;
					auto pcr_iscr_common_clock_flag = s.get<uint8_t>();
					pcr_iscr_common_clock_flag &= 1;
					assert(s.available() >= end);
					if (s.available() > end)
						s.skip(s.available() - end);
					info.t2mi_stream_id = t2mi_stream_id;
					info.num_t2mi_streams_minus_one = num_t2mi_streams_minus_one;
				} break;
				default:
					dtdebug_nice("PMT: unhandled extension descriptor " << (int)desc_tag_extension << "="
											 << name_of_descriptor_tag(desc_tag_extension)
											 << " size=" << (int)_desc.len);

					s.skip(_desc.len - 1); // we already read 1 byte
				}
			} break;
			case SI::DataBroadcastDescriptorTag:
			case SI::ApplicationSignallingDescriptorTag:
			case SI::AncillaryDataDescriptorTag:
			case SI::VBITeletextDescriptorTag:
			case SI::VBIDataDescriptorTag:
			case SI::TeletextDescriptorTag:
			default:
				dtdebug_nice("PMT: unhandled descriptor " << (int)_desc.tag << "=" << name_of_descriptor_tag(_desc.tag)
										 << " size=" << (int)_desc.len);

				s.skip(_desc.len);
				break;
			}
		}

		if (s.bytes_read != end) {
			dtdebug_nicex("Unexpected: s.bytes_read=%d != end=%d", s.bytes_read, end);
			if(s.bytes_read > end)
				s.throw_bad_data();
			s.bytes_read = end;
		}
	}

#if 0
	template<> pmt_info_t ts_substream_t::get<pmt_info_t>() {
		pmt_info_t ret;
		auto stream_type = get<uint8_t>();
		ret.pid = get<uint16_t>() & 0x1fff;
		ret.info_len = get<uint16_t>() & 0x0fff;
		auto bytes_remaining = (int)ret.info_len;
		int saved = bytes_read;
		skip(bytes_remaining);
		assert(bytes_read==saved+ (int)ret.info_len);
		return ret;
	}
#endif

/*
	returns 1 on success, -1 on error
*/

	using namespace dtdemux;
#if 0
	void psi_parser_t::write_section_header(const section_header_t& hdr, ss::bytebuffer_& out) {
		out.clear();
		out.append_raw((uint8_t)native_to_net(hdr.table_id));
		out.append_raw((uint16_t)native_to_net(hdr.len_field));
	}
#endif

	bool crc_is_correct(const ss::bytebuffer_& payload) {
		auto crc = crc32(payload.buffer(), payload.size());
		return crc == 0;
	}

	void stored_section_t::parse_section_header(section_header_t& ret) {
		ret.pid = pid;

		ret.table_id = this->get<uint8_t>();
		bytes_read = 1;
		// table length
		auto len = this->get<uint16_t>();
		ret.len = len & 0xfff;
		ret.section_syntax_indicator = (len >> 15);
		ret.private_bit = ((len >> 14) & 1);
/*
	Section syntax indicator: A flag that indicates if the syntax section follows the section length.
	The PAT, PMT, and CAT all set this to 1.
	Private bit:            	The PAT, PMT, and CAT all set this to 0. Other tables set this to 1.
	So: PAT, PMT, CAT: 10
	Other tables:      x1
	if x==1 length field has the usual meaning
*/
#if 0
		if ((len & 0x3000) != 0x3000) {

			dtdebug("Bad reserved bits"); //tests reserve flags
			THROW_BAD_DATA;
		}
#endif
	}

	void stored_section_t::parse_table_header(section_header_t& ret) {
		parse_section_header(ret);
		ret.table_id_extension = this->get<uint16_t>();

		ret.version_number = this->get<uint8_t>();

		ret.current_next = ret.version_number & 0x1;					 // 1=current 0=next
		ret.version_number = (ret.version_number & 0x3f) >> 1; // skip 2 reserve bits and version_number

		ret.section_number = this->get<uint8_t>();
		ret.last_section_number = this->get<uint8_t>();

		if (ret.is_sdt()) {
			ret.table_id_extension1 = this->get<uint16_t>();
		} else if (ret.is_eit()) {
			ret.table_id_extension1 = this->get<uint16_t>();
			ret.table_id_extension2 = this->get<uint16_t>();
		}

		ret.header_len = bytes_read;
	}

	bool stored_section_t::parse_pat_section(pat_services_t& pat_services, section_header_t& hdr) {
		auto ts_id = hdr.table_id_extension;
		pat_services.version_number = hdr.version_number;
		pat_services.ts_id = ts_id;
		if (hdr.table_id != 0x0) {
			LOG4CXX_ERROR(logger, "PAT with bad table id " << (int)hdr.table_id);
			return false;
		}

		int pid = hdr.pid;
		if (pid != 0)
			return false;

		int remaining = hdr.len - 5 /* bytes already read*/ - 4 /* checksum*/;
		if (remaining <= 0) {
			error = true;
			RETURN_ON_ERROR false;
		}
		if ((remaining % 4) != 0)
			return false;
		if (hdr.current_next) {
		} else {
			dtdebug("Received `next' PAT (unhandled)");
		}

		auto num_entries = remaining / 4;
		if (num_entries == 0)
			dtdebugx("empty pat");
#if 0
		LOG4CXX_DEBUG(logger, "PAT=" << (int)hdr.table_id << " len=" << hdr.len << " ts_id=" << ts_id
									<< " vers=" << (int)hdr.version_number << " current=" << (int)hdr.current_next
									<< " entries=" << num_entries);
#endif
		for (int i = 0; i < num_entries; ++i) {
			uint16_t program_number = this->get<uint16_t>();
			uint16_t pid = this->get<uint16_t>() & 0x1fff;
			// LOG4CXX_DEBUG(logger, "program: no=" << program_number << " pid=" << pid);
			pat_entry_t e(program_number, pid);
			pat_services.entries.push_back(e);
		}
		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		// dtdebugx("PAT CRC=0x%x", crc);
		assert(this->available() == 0);
		return true;
	}

	bool stored_section_t::parse_pat_section(pat_services_t& pat_services) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_pat_section(pat_services, hdr);
	}

	bool stored_section_t::parse_pmt_section(pmt_info_t& pmt,
																					 section_header_t& hdr) {
		// current_version_number = hdr.version_number;
		int pid = hdr.pid;
		if (hdr.table_id != 0x02) {
			dterror("PMT PID=" << pid << ": with bad table id " << (int)hdr.table_id);
			return false;
		}

		if (hdr.section_number != 0 || hdr.last_section_number != 0) {
			dterror("Bad PMT: secno=" << (int)hdr.section_number << " last_sec_no=" << (int)hdr.last_section_number);
			return false;
		}

		/*This provides some basic pmt parsing
			We should really use libsi for more detailed parsing:
			PMT(const unsigned char *data, bool doCopy=true)
			with doCopy==false, we can avoid calling "new"

			We still will need to parse the section header in order to find out how much data
			to copy.

			Based on PMT analysis we may also have to decide which CA_PIDs to save in the stream
			or to read from the stream ***but only in case we ever support post-encryption!**
		*/
		pmt.version_number = hdr.version_number;
		pmt.current_next = hdr.current_next;
		pmt.service_id = hdr.table_id_extension;
		pmt.pcr_pid = this->get<uint16_t>() & 0x1fff;
		pmt.pmt_pid = pid;

#if 0
		LOG4CXX_INFO(logger, "PMT service " << service_id << " PCR pid " << pcr_pid);
#endif

		pid_info_t info;
		const bool in_es_loop = false;
		pmt.parse_descriptors(*this, info,
													in_es_loop);
		pmt.estimated_media_mode = (info.t2mi_stream_id >=0) ? media_mode_t::T2MI : media_mode_t::UNKNOWN;
		// elementary stream loop
		while (this->available() > 4) { // 4 is the crc

			auto stream_type = this->get<uint8_t>();
			pmt.capmt_data.push_back((uint8_t)0); // special tag indicating that this is not a ca descriptor
			pmt.capmt_data.push_back((uint8_t)3); // length
			pmt.capmt_data.push_back((uint8_t)stream_type);

			auto stream_pid = this->get<uint16_t>();
			pmt.capmt_data.append_raw((uint16_t)native_to_net(stream_pid));
			stream_pid &= 0x1fff;

			pid_info_t info(stream_pid, stream_type);
			const bool in_es_loop = true;
			pmt.parse_descriptors(*this, info, in_es_loop);

			if (stream_type::is_video(stream_type::stream_type_t(stream_type))) {
				pmt.video_pid = stream_pid;
				pmt.estimated_media_mode = media_mode_t::TV;
			} else if (is_audio(info)) {
				if(pmt.estimated_media_mode != media_mode_t::TV)
					pmt.estimated_media_mode = media_mode_t::RADIO;
			}



			if(info.t2mi_stream_id >=0)
				pmt.estimated_media_mode = media_mode_t::T2MI;


			pmt.pid_descriptors.push_back(info);
		}
		uint32_t crc UNUSED = this->get<uint32_t>();
		if (pmt.num_sky_summary_pids >= 4 && pmt.num_sky_title_pids >= 4)
			pmt.has_skyuk_epg = true;

		assert(this->available() == 0);
#if 0
		if(!error)
			current_version_number = hdr.version_number;
#endif
		assert(	(int)pmt.estimated_media_mode <=4);
		return true;
	}

	bool stored_section_t::parse_pmt_section(pmt_info_t& pmt) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_pmt_section(pmt, hdr);
	}

	bool stored_section_t::parse_nit_section(nit_network_t& network, section_header_t& hdr) {
		auto network_id = hdr.table_id_extension;
		bool is_actual = (hdr.table_id == 0x40);

		if (!hdr.section_syntax_indicator) {
			LOG4CXX_ERROR(logger, "SDT with bad section_syntax_indicator");
			return false;
		}

		if (hdr.current_next) {
		} else {
			dtdebug("Received 'next' in NIT");
			// this happens on 52E 11246V (only next)
		}
		auto section_number = hdr.section_number;
		auto last_section_number = hdr.last_section_number;

		// auto& network = is_actual ? actual_networks[network_id] : other_networks[network_id];
		network.network_id = network_id;
		network.is_actual = is_actual;
		// network stream loop
		auto desc_loop_len = this->get<uint16_t>();
		desc_loop_len &= 0xfff;
		auto end = this->available() - desc_loop_len;
		auto tst = this->available();
		while (this->available() > end) {
			auto desc = this->get<descriptor_t>();
			switch (desc.tag) {
			case SI::NetworkNameDescriptorTag:
				if (this->get_fields<dvb_text_t>(network.network_name, desc) < 0) {
					return false;
				}
				break;

			case SI::LinkageDescriptorTag: {
				// if (network.bouquet_linkage.size() > 0)
				// dterrorx("Unexpected: more than one bouque linkage: num=%d\n", network.bouquet_linkage.size() + 1);
				bouquet_linkage_t bouquet_linkage;
				this->get_fields<linkage_descriptor_t>(bouquet_linkage, desc);
				network.bouquet_linkage.push_back(bouquet_linkage);
			} break;
			default:
				dtdebug_nice("NIT"
										 << ": unknown descriptor " << (int)desc.tag << "=" << name_of_descriptor_tag(desc.tag));
			case SI::PrivateDataSpecifierDescriptorTag:
				this->skip(desc.len);
				break;
			}
			if (this->has_error())
				return false;
			tst -= (desc.len + 2);
			assert(tst == this->available());
		}
		assert(this->available() == end);

		// transport stream loop
		desc_loop_len = this->get<uint16_t>();
		desc_loop_len &= 0xfff;
		end = this->available() - desc_loop_len;
		assert(end >= 0);
		while (this->available() > end) {
			bool is_dvbs{false};
			bool is_dvbt{false};
			bool is_dvbc{false};
			auto tune_src = is_actual ? tune_src_t::NIT_ACTUAL : tune_src_t::NIT_OTHER;
			auto key_src = is_actual ? key_src_t::NIT_ACTUAL : key_src_t::NIT_OTHER;
			dvbs_mux_t dvbs_mux;
			dvbt_mux_t dvbt_mux;
			dvbc_mux_t dvbc_mux;
			dvbs_mux.c.tune_src = tune_src;
			dvbs_mux.c.key_src = key_src;
			dvbc_mux.c.tune_src = tune_src;
			dvbc_mux.c.key_src = key_src;
			dvbt_mux.c.tune_src = tune_src;
			dvbt_mux.c.key_src = key_src;

			auto ts_id = this->get<uint16_t>();
			auto original_network_id = this->get<uint16_t>();
#if 0
			LOG4CXX_DEBUG(logger, "NIT=" << (int)hdr.table_id << " len=" << hdr.len << " ts_id=" <<
										ts_id << " vers=" << (int)hdr.version_number << " current=" <<
										(int)hdr.current_next << " onid=" << original_network_id);
#endif
			auto desc_loop_len1 = this->get<uint16_t>() & 0xfff;
			auto end1 = this->available() - desc_loop_len1;
			auto tst1 = this->available();
			while (this->available() > end1) {
				auto desc1 = this->get<descriptor_t>();
				switch (desc1.tag) {
				case SI::FrequencyListDescriptorTag: {
					frequency_list_t frequencies UNUSED;
					this->get_fields<frequency_list_descriptor_t>(frequencies, desc1);
				} break;

				case SI::S2SatelliteDeliverySystemDescriptorTag: {
					dterror("S2SatelliteDeliverySystemDescriptor");
					if (desc1.len > 0) { // solves problem on 5.0W 12340H
						this->get_fields<s2_satellite_delivery_system_descriptor_t>(dvbs_mux);
						if (!is_dvbs) {
							dvbs_mux.c.nit_network_id = original_network_id;
							dvbs_mux.c.nit_ts_id = ts_id;
							is_dvbs = true;
						}
					}
				} break;
				case SI::SatelliteDeliverySystemDescriptorTag: {
					this->get_fields<satellite_delivery_system_descriptor_t>(dvbs_mux);
					dvbs_mux.c.nit_network_id = original_network_id;
					dvbs_mux.c.nit_ts_id = ts_id;
					is_dvbs = true;
					network.sat_set = true;
				} break;
				case SI::CableDeliverySystemDescriptorTag: {
					auto& mux = dvbc_mux;
					mux.c.nit_network_id = original_network_id;
					mux.c.nit_ts_id = ts_id;
					is_dvbc = true;

					this->get_fields<cable_delivery_system_descriptor_t>(dvbc_mux);
				} break;
				case SI::TerrestrialDeliverySystemDescriptorTag: {
					auto& mux = dvbt_mux;
					mux.c.nit_network_id = original_network_id;
					mux.c.nit_ts_id = ts_id;
					is_dvbt = true;
					if (this->get_fields<terrestrial_delivery_system_descriptor_t>(dvbt_mux) < 0) {
						dterror("Bad mux found: " << dvbt_mux);
					}
				} break;
				case SI::ServiceListDescriptorTag: {
					bouquet_t bouquet; // todo
					service_list_t service_list{network_id, ts_id, bouquet};
					if (this->available() - desc1.len < end1) {
						dterrorx("Incorrect section available=%d desc.len=%d end=%d",
										 this->available(), desc1.len, end1);
						return false;
					}
					this->get_fields<service_list_descriptor_t>(service_list, desc1);
				} break;
				case SI::LinkageDescriptorTag: {
					bouquet_linkage_t bouquet_linkage; // todo: this should not be a service
					this->get_fields<linkage_descriptor_t>(bouquet_linkage, desc1);
				} break;
				default:
				case SI::PrivateDataSpecifierDescriptorTag:
				case SI::LogicalChannelDescriptorTag: // could be useful to parse
					this->skip(desc1.len);
					break;
				}
				tst1 -= (desc1.len + 2);
				assert(tst1 == this->available());
				if (has_error())
					return false;
			}
			assert(end1 == this->available());
#ifndef NDEBUG
#if 0
			int x = network.is_dvbs + network.is_dvbc + network.is_dvbt;
			if(x!=1) {
				dterrorx("NIT ts section without descriptors on %s network", is_actual ? "ACTUAL" : "OTHER");
			}
#endif
#endif
#if 0
			if(is_current_tp && (network.is_dvbs? network.sat_set: true))
				network.tuned_mux_idx = network.muxes.size();
#endif

			if (is_dvbs) {
				network.muxes.push_back(chdb::any_mux_t(dvbs_mux));
			} else if (is_dvbc)
				network.muxes.push_back(chdb::any_mux_t(dvbc_mux));
			else if (is_dvbt)
				network.muxes.push_back(chdb::any_mux_t(dvbt_mux));
			if (has_error())
				return false;
		}
		// assert(this->available() == end); //this assertion fails on 52E 11246V because desc_loop_len is 17 instead of 19

		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		if (this->available() != 0) {
			return false;
		}
		return true;
	}

	bool stored_section_t::parse_nit_section(nit_network_t& network) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_nit_section(network, hdr);
	}

	bool stored_section_t::parse_sdt_section(sdt_services_t& ret, section_header_t& hdr) {
		ret.has_opentv_epg = false;
		ret.has_freesat_home_epg = false;
		bool is_current_tp = false;

		ret.ts_id = hdr.table_id_extension;
		ret.is_actual = (hdr.table_id == 0x42);

		if (!hdr.section_syntax_indicator) {
			LOG4CXX_ERROR(logger, "SDT with bad section_syntax_indicator");
			return false;
		}
		if (has_error())
			return false;

		if (hdr.current_next) {
		} else {
			dtdebug("Received 'next' SDT unhandled");
		}
		auto section_number = hdr.section_number;
		auto last_section_number = hdr.last_section_number;
		auto original_network_id = hdr.table_id_extension1;

		ret.original_network_id = original_network_id;
		auto reserved = this->get<uint8_t>();
		while (true) {
			if (this->available() < 6)
				break;
			ret.services.resize(ret.services.size() + 1);
			service_t& service = ret.services[ret.services.size() - 1];
			service.k.service_id = this->get<uint16_t>();
			service.k.network_id = original_network_id;
			service.k.ts_id = ret.ts_id;

			auto eit_ = this->get<uint8_t>();
			bool eit_schedule_flag = eit_ & 0x2;
			bool eit_pf_flag = eit_ & 0x1;

			auto desc_loop_len = this->get<uint16_t>();
			uint8_t running_status = (desc_loop_len >> 13) & 0x7;
			bool free_ca_mode = (desc_loop_len >> 12) & 0x1; // encrypted or not
			service.encrypted = free_ca_mode;

			desc_loop_len &= 0xfff;
			if (this->available() < desc_loop_len)
				return false;
			auto end = this->available() - desc_loop_len;
			int tst = this->available();
			while (this->available() > end) {
				auto desc = this->get<descriptor_t>();
				assert(desc.len <= this->available());
				switch (desc.tag) {
				case SI::ServiceDescriptorTag: {
					this->get_fields<service_descriptor_t>(service);
#if 0
					//test for skyit; not working
					if(ret.is_actual && service.service_type== 131) //todo: we also need to be sure that service.k.network_id
						//matches currently tuned mux?
						ret.has_opentv_epg = true;
#endif

				} break;
				case SI::DataBroadcastDescriptorTag: {
					this->get_fields<data_broadcast_descriptor_t>(service);
				} break;
				case SI::CaIdentifierDescriptorTag: {
					// Using the CaIdentifierDescriptor is no good, because some tv stations
					// just don't use it. pmt contains perhaps better data
					this->get_fields<ca_identifier_descriptor_t>(service, desc);
				} break;
				case SI::LinkageDescriptorTag:
					this->get_fields<linkage_descriptor_t>(service, desc);
					break;
				case SI::CountryAvailabilityDescriptorTag:
				case SI::PrivateDataSpecifierDescriptorTag: // xxx
				case SI::ReturnTransmissionModeTag:					// Return_Transmission_Modes_descriptor? en_301790v010501o.pdf
				case SI::DefaultAuthorityDescriptorTag:
					this->skip(desc.len);
					// ignored
					break;

				default:
					if (desc.tag >= 0x80 && desc.tag <= 0xfe) {
						// user defined descriptor
					} else {
						dtdebug_nice(service.name << ": unknown descriptor " << (int)desc.tag << "="
												 << name_of_descriptor_tag(desc.tag));
					}
					this->skip(desc.len);
					break;
				}
				if (has_error())
					return false;
				tst -= (desc.len + 2);
				assert(tst == this->available());
			}
			assert(end >= this->available());
			if (end > this->available()) {
				dterrorx("Extra bytes after descriptor loop: %d/%d", end, this->available());
				this->skip(end - this->available());
			}
			if (service.service_type == 12 && strcmp(service.name.c_str(), "FreesatHome") == 0)
				ret.has_freesat_home_epg = true;
			if (service.name.size() == 0) {
				service.name.sprintf("Service %d", service.k.service_id);
			}
		}
		if (this->available() < 4) {
			dterrorx("Too few bytes left at end of sdt");
			return false;
		}
		if (this->available() > 4) {
			dterrorx("bytes left at end of sdt");
			this->skip(this->available() - 4);
		}
		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		if (this->available() != 0) {
			return false;
		}
		return true;
	}

	bool stored_section_t::parse_sdt_section(sdt_services_t& ret) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_sdt_section(ret, hdr);
	}

	bool stored_section_t::parse_bat_section(bouquet_t& bouquet, section_header_t& hdr, int fst_preferred_region_id) {
		bouquet.bouquet_id = hdr.table_id_extension;
		if (!hdr.section_syntax_indicator) {
			LOG4CXX_ERROR(logger, "SDT with bad section_syntax_indicator");
			return false;
		}
		if (has_error())
			return false;

		if (hdr.current_next) {
		} else {
			dtdebug("Received 'next' BAT unhandled");
		}
		auto section_number = hdr.section_number;
		auto last_section_number = hdr.last_section_number;

#if 0
		dtdebug("BAT=" << (int)hdr.table_id << " len=" << hdr.len << " bouquet_id=" <<
						bouquet.bouquet_id << " vers=" << (int)hdr.version_number << " current=" <<
						(int)hdr.current_next);
#endif
		auto bouquet_desc_len = this->get<uint16_t>() & 0xfff;
		if (this->available() < bouquet_desc_len)
			return false;

		auto end = this->available() - bouquet_desc_len;
		int tst = this->available();
		while (this->available() > end) {
			auto desc = this->get<descriptor_t>();
			assert(desc.len <= this->available());
			switch (desc.tag) {
			case SI::LinkageDescriptorTag: {
				chdb::service_t service;
				this->get_fields<linkage_descriptor_t>(service, desc);
			} break;
			case SI::BouquetNameDescriptorTag: {
				if (this->get_fields<dvb_text_t>(bouquet.name, desc) < 0) {
					return false;
				}
			} break;
			case SI::CountryAvailabilityDescriptorTag:
			case SI::DataBroadcastDescriptorTag:
				this->skip(desc.len);
				break;
			case SI::PrivateDataSpecifierDescriptorTag: {
				if (desc.len == 4) {
					auto data = this->get<uint32_t>();
					if (data == 0x02)
						bouquet.is_sky = true; // 0x2= BskyB 1
				} else if (desc.len > 0)	 // desc.len==0 happens on 45.0E 12520V
					this->skip(desc.len);
			} break;
			case SI::FSTRegionListDescriptorTag: {
				fst_region_list_t region_list{fst_preferred_region_id, bouquet.name};
				this->get_fields<fst_region_list_descriptor_t>(region_list, desc);
			} break;
			case SI::FSTChannelCategoryDescriptorTag: {
#if 0
				fst_channel_category_list_t channel_category_list;
				this->get_fields<fst_channel_category_list_descriptor_t>(channel_category_list, desc);
#else
				this->skip(desc.len);
#endif
			} break;
			case SI::FSTCategoryDescriptorTag: {
#if 0
				fst_channel_category_t channel_category;
				this->get_fields<fst_category_description_list_descriptor_t>(channel_category, desc);
#else
				this->skip(desc.len);
#endif
			} break;
			case SI::LocalTimeOffsetDescriptorTag:

				dtdebug("BAT: unknown descriptor " << (int)desc.tag << "=" << name_of_descriptor_tag(desc.tag)); // 129 (0x81 User defined/ATSC reserved)
				this->skip(desc.len);
				break;
			default:
				if (desc.tag >= 0x80 && desc.tag <= 0xfe) {
					// user defined descriptor
				} else {
					dtdebug("BAT: unknown descriptor " << (int)desc.tag << "=" << name_of_descriptor_tag(desc.tag));
				}
				this->skip(desc.len);
				break;
			}
			if (has_error())
				return false;
			tst -= (desc.len + 2);
			if (tst != this->available()) {
				dterrorx("Error while parsing bat section: %d != %d", tst, this->available());
				return false;
			}
		}
		if(end > this->available()) {
			dterrorx("Read more bytes than we were supposed to: end=%d available=%d", end, this->available());
			//happens on 7.0E 10804V
			return false;
		}

		if (this->available() > end)
			this->skip(this->available() - end);

		auto ts_loop_len = this->get<uint16_t>() & 0xfff;
		if (this->available() < ts_loop_len)
			return false;
		end = this->available() - ts_loop_len;
		tst = this->available();

		while (this->available() > end) {
			if (this->available() < 6)
				return false;
			auto ts_id = this->get<uint16_t>();
			auto network_id = this->get<uint16_t>();
			auto ts_desc_len = this->get<uint16_t>() & 0xfff;
			if (this->available() < ts_desc_len)
				return false;
			auto end1 = this->available() - ts_desc_len;
			auto tst1 = this->available();
			while (this->available() > end1) {
				auto desc = this->get<descriptor_t>();
				assert(desc.len <= this->available());
				switch (desc.tag) {
					// This is used by skyuk epg!
				case SI::OtvServiceListDescriptorTag: {
					service_list_t service_list{network_id, ts_id, bouquet};
					this->get_fields<otv_service_list_descriptor_t>(service_list, desc);
				}

					break;

				case SI::ServiceListDescriptorTag: {
					service_list_t service_list{network_id, ts_id, bouquet};
					this->get_fields<service_list_descriptor_t>(service_list, desc);
				} break;
				case SI::FSTServiceListDescriptorTag: {
					fst_service_list_t service_list{network_id, ts_id, fst_preferred_region_id, bouquet};
					this->get_fields<fst_service_list_descriptor_t>(service_list, desc);
				} break;
				case SI::LogicalChannelDescriptorTag: // could be useful to parse
					this->skip(desc.len);
					break;

				default:
					dtdebug("BAT: unknown descriptor " << (int)desc.tag << "=" << name_of_descriptor_tag(desc.tag)); // 129 (0x81 User defined/ATSC reserved)
				case 0x80 ... 0x82: //user defined
				case 0x84 ... 0x85: //user defined
				case 0x87 ... 0xb0: //user defined
				case 0xb2 ... 0xd2: //user defined
				case 0xd4 ... 0xfe: //user defined
				case SI::EacemStreamIdentifierDescriptorTag:
				case SI::PrivateDataSpecifierDescriptorTag: // 0x5f value=2 (0x00000002)  [= BskyB 1]
					this->skip(desc.len);
					break;
				}
				if (has_error())
					return false;
				tst1 -= (desc.len + 2);
				assert(tst1 == this->available());
			}
			if(end1 > this->available())
				return false;
		}
		assert(end == this->available());
		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		if (this->available() != 0) {
			return false;
		}
		return true;
	}

	bool stored_section_t::parse_bat_section(bouquet_t& bouquet, int fst_preferred_region_id) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_bat_section(bouquet, hdr, fst_preferred_region_id);
	}

	bool stored_section_t::parse_eit_section(epg_t& ret, section_header_t& hdr) {
		ret.epg_service.service_id = hdr.table_id_extension;
		ret.is_actual = (hdr.table_id == 0x4e) || ((hdr.table_id >= 0x50) && (hdr.table_id <= 0x5f));

		if (!hdr.section_syntax_indicator) {
			LOG4CXX_ERROR(logger, "EIT with bad section_syntax_indicator");
			return false;
		}

		if (hdr.current_next) {
		} else {
			dtdebug("Received 'next' EIT unhandled");
		}
		auto section_number = hdr.section_number;
		auto last_section_number = hdr.last_section_number;

		ret.epg_service.ts_id = hdr.table_id_extension1;
		ret.epg_service.network_id = hdr.table_id_extension2;
		auto segment_last_section_number = this->get<uint8_t>();
		auto last_table_id = this->get<uint8_t>();
		while (this->available() > 4) {
			ret.epg_records.resize(ret.epg_records.size() + 1);
			epgdb::epg_record_t& rec = ret.epg_records[ret.epg_records.size() - 1];
			rec.k.service = ret.epg_service;
			rec.k.event_id = this->get<uint16_t>();

			this->get_fields<start_time_duration_t>(rec);

			auto desc_loop_len = this->get<uint16_t>();
			auto running_status = desc_loop_len >> 13;
			bool free_ca_mode = (desc_loop_len >> 12) & 0x1;
			desc_loop_len &= 0xfff;

			auto end = this->available() - desc_loop_len;
			auto tst = this->available();
			while (this->available() > end) {
				auto desc = this->get<descriptor_t>();
				switch (desc.tag) {
				case SI::ShortEventDescriptorTag:
					this->get_fields<short_event_descriptor_t>(rec);
					break;
				case SI::ExtendedEventDescriptorTag:
					this->get_fields<extended_event_descriptor_t>(rec);
					break;

				case SI::TimeShiftedEventDescriptorTag:
					this->get_fields<time_shifted_event_descriptor_t>(rec);
					break;

				case SI::ComponentDescriptorTag:
					this->get_fields<component_descriptor_t>(rec, desc);
					break;
				case SI::ContentDescriptorTag:
					this->get_fields<content_descriptor_t>(rec, desc);
					break;
				case SI::MultilingualComponentDescriptorTag:
					this->get_fields<multilingual_component_descriptor_t>(rec, desc);
					break;
				default:
					dtdebugx("Unknown EIT descriptor 0x%x", desc.tag);
				case 0x80 ... 0xfe: // user defined
				case SI::LinkageDescriptorTag:
				case SI::PDCDescriptorTag:
				case SI::CaIdentifierDescriptorTag:
				case SI::ParentalRatingDescriptorTag:
				case SI::PrivateDataSpecifierDescriptorTag:
				case SI::ContentIdentifierDescriptorTag:
					this->skip(desc.len);
					break;
				}
				tst -= (desc.len + 2);
				assert(tst == this->available());
			}
		}

		assert(this->available() == 4);

		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		if (this->available() != 0) {
			return false;
		}
		return true;
	}

	bool stored_section_t::parse_eit_section(epg_t& ret) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_eit_section(ret, hdr);
	}

	bool stored_section_t::parse_sky_section(epg_t& ret, section_header_t& hdr) {
		ret.channel_id = hdr.table_id_extension;

		if (!hdr.section_syntax_indicator) { // TOTEST
			LOG4CXX_ERROR(logger, "SKY-TITLE with bad section_syntax_indicator");
			return false;
		}

		if (hdr.current_next) {
#if 0
			if(hdr.version_number == current_version_number) {
				return;
			}
			current_version_number = hdr.version_number;
#endif
		} else {
			dtdebug("Received 'next' SKY unhandled");
		}
		auto section_number = hdr.section_number;
		auto last_section_number = hdr.last_section_number;

		auto mjd_time = this->get<uint16_t>();
		auto mjd_date_mod_8 = (pid - 0x30) % 8;
		assert(mjd_date_mod_8 == mjd_time % 8);

		while (this->available() > 4) {
			ret.epg_records.resize(ret.epg_records.size() + 1);
			epgdb::epg_record_t& rec = ret.epg_records[ret.epg_records.size() - 1];

			rec.k.event_id = this->get<uint16_t>(); // byte 10 and 11
			auto desc_loop_len = this->get<uint16_t>();
			desc_loop_len &= 0xfff;
			auto end = this->available() - desc_loop_len;
			auto tst = this->available();

			while (this->available() > end) {
				auto desc = this->get<descriptor_t>();
				switch (desc.tag) {
				case 0xb5: // title
					this->get_fields<opentv_title_descriptor_t>(rec, desc, mjd_time);
					break;

				case 0xb9: // summary
					this->get_fields<opentv_summary_descriptor_t>(rec, desc);
					break;
				case 0xbb: // desc
					this->get_fields<opentv_description_descriptor_t>(rec, desc);
					break;
				case 0xc1: // series link
					this->get_fields<opentv_serieslink_descriptor_t>(rec, desc);
					break;
				default:
					this->skip(desc.len);
					break;
				}
				tst -= (desc.len + 2);
			}
			assert(tst == this->available());
		}

		assert(this->available() == 4);

		uint32_t crc UNUSED = this->get<uint32_t>(); // avoid compiler warning
		if (this->available() != 0) {
			return false;
		}
		return true;
	}

	bool stored_section_t::parse_sky_section(epg_t& ret) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_sky_section(ret, hdr);
	}

	bool stored_section_t::parse_mhw2_channel_section(bouquet_t& bouquet, section_header_t& hdr) {
		this->skip(120 - this->bytes_read);
		RETURN_ON_ERROR false;
		auto num_channels = this->get<uint8_t>();
		auto offset = (int)this->bytes_read + (int)(8 * num_channels);
		auto len = (int)this->payload.size() - offset;
		if (len <= 0) {
			dterrorx("Invalid channel section");
			return -1;
		}
		// hdr.table_id_extension: 0 for sd and 2 for hd and 3 for dtt
		bouquet.is_mhw2 = true;
		bouquet.bouquet_id = hdr.table_id_extension;
		bouquet.name = "Movistar+";
		data_range_t names((uint8_t*)this->payload.buffer() + offset, len);
		for (int i = 0; i < num_channels; ++i) {
			bouquet_t::chgm_t chgm{};
			auto network_id = this->get<uint16_t>();

			auto ts_id = this->get<uint16_t>();
			auto service_id = this->get<uint16_t>();
			auto unknown UNUSED = this->get<uint16_t>();

			chgm.lcn = i + 1;
			chgm.service_key.mux.sat_pos = sat_pos_none; // todo
			chgm.service_type = 0;											 //@todo
			chgm.service_key.network_id = network_id;
			chgm.service_key.ts_id = ts_id;
			chgm.service_key.service_id = service_id;
			chgm.is_opentv_or_mhw2 = true;
			bouquet.channels[chgm.lcn] = chgm;
			ss::string<128> name;
			auto tlen = names.get<uint8_t>() & 0x3f;
			auto* p = names.current_pointer(tlen);
			if (!p)
				return -1;
			decodeText(name, p, tlen);
			RETURN_ON_ERROR false;
		}
		return true;
	}

	bool stored_section_t::parse_mhw2_channel_section(bouquet_t& bouquet) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_mhw2_channel_section(bouquet, hdr);
	}

	bool stored_section_t::parse_mhw2_title_section(epg_t& epg, section_header_t& hdr) {

		auto channel_id = hdr.last_section_number + 1; // or: 2 bytes?
		assert(this->payload.buffer()[7] + 1 == channel_id);
		assert(this->bytes_read == 8);
		epg.channel_id = channel_id;
#if 1
		auto x0 = this->get<uint8_t>();
#endif
		while (this->available() >= 20) {
			epg.epg_records.resize(epg.epg_records.size() + 1);
			auto& rec = epg.epg_records[epg.epg_records.size() - 1];
			// bytes 4-6
			auto summary_id1 = this->get<uint16_t>(); // byte 1 and 2
			auto x1 = this->get<uint8_t>();						// byte 3 values: 128 and 255, could be part of summary_id1

			auto summary_id = (uint32_t)this->get<uint8_t>();			 // byte 4
			summary_id = (summary_id << 8) | this->get<uint8_t>(); // byte 5
			summary_id = (summary_id << 8) | this->get<uint8_t>(); // byte 6
			// bytes 7,8,9,10
			auto title_id = this->get<uint32_t>();

			// bytes 11,12,13,14,15,16,17
			this->get_fields<mhw2_start_time_duration_t>(rec); // 7 bytes
			assert(rec.k.start_time > 0);
			// byte 18 and following
			this->get_fields<dvb_text_t>(rec.event_name); // 1 byte

			auto x2 UNUSED = this->get<uint16_t>(); // 2 bytes series link?
			rec.k.event_id = summary_id;

			if (has_error())
				epg.epg_records.resize_no_init(epg.epg_records.size() - 1);
			RETURN_ON_ERROR false;
		}

		return true;
	}

	bool stored_section_t::parse_mhw2_title_section(epg_t& epg) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_mhw2_title_section(epg, hdr);
	}

	bool stored_section_t::parse_mhw2_short_summary_section(epg_t& epg, section_header_t& hdr) {

		assert(this->bytes_read == 8);
		epg.channel_id = 0;
		bool long_descriptions = (hdr.section_syntax_indicator == 0 && pid == 642);
		bool short_descriptions = (hdr.section_syntax_indicator == 1 && pid == 644);

		epg.epg_records.resize(epg.epg_records.size() + 1);
		auto& rec = epg.epg_records[epg.epg_records.size() - 1];

		auto x0 = this->get<uint8_t>(); // byte 8
		// byte 9, 10,11

		// Note a summary id?
		uint32_t summary_id = this->get<uint8_t>();
		summary_id = (summary_id << 8) | this->get<uint8_t>();
		summary_id = (summary_id << 8) | this->get<uint8_t>();

		auto x1 = this->get<uint16_t>(); // byte 12,13
		RETURN_ON_ERROR false;
		this->get_fields<dvb_text_t>(rec.story); // 1 byte
		rec.story.sprintf("\n");
		auto x2 = this->get<uint8_t>(); // 1 byte
		RETURN_ON_ERROR false;
		this->get_fields<dvb_text_t>(rec.story);
		this->skip(9);
		RETURN_ON_ERROR false;

		// title id indeed matches
		uint32_t title_id = this->get<uint32_t>();
		rec.k.event_id = summary_id;

		RETURN_ON_ERROR false;
		return true;
	}

	bool stored_section_t::parse_mhw2_short_summary_section(epg_t& epg) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_mhw2_short_summary_section(epg, hdr);
	}

	bool stored_section_t::parse_mhw2_long_summary_section(epg_t& epg, section_header_t& hdr) {
		assert(this->bytes_read == 8);
		char hex[128] = "";

		epg.channel_id = 0;
		bool long_descriptions = (hdr.section_syntax_indicator == 0 && pid == 642);
		bool short_descriptions = (hdr.section_syntax_indicator == 1 && pid == 644);

		epg.epg_records.resize(epg.epg_records.size() + 1);
		auto& rec = epg.epg_records[epg.epg_records.size() - 1];

		this->bytes_read = 6;

		// pos+6, pos+7, pos+8
		uint32_t summary_id = this->get<uint8_t>();
		summary_id = (summary_id << 8) | this->get<uint8_t>();
		summary_id = (summary_id << 8) | this->get<uint8_t>();

		this->skip(10);

		this->get_fields<dvb_text_t>(rec.story); // 1 byte byte pos=19 contains length
		rec.story.sprintf("\n");
		RETURN_ON_ERROR false;												// pos now points here
		this->get_fields<mhw2_dvb_text_t>(rec.story); // 2 bytes length field

		/*
			4 bytes program_id
			6 bytes ????
			4 bytes "next_date"
			6 bytes ????
			repeats until end
		*/

		this->skip(10);
		RETURN_ON_ERROR false;
		// 12 extra

		// title id indeed matches
		uint32_t title_id = this->get<uint32_t>();
		rec.k.event_id = summary_id;
		RETURN_ON_ERROR false;

		RETURN_ON_ERROR false;
		return true;
	}

	bool stored_section_t::parse_mhw2_long_summary_section(epg_t& epg) {
		section_header_t hdr;
		parse_table_header(hdr);
		return parse_mhw2_long_summary_section(epg, hdr);
	}

	inline bool operator==(const ca_info_t& a, const ca_info_t& b) {
		return a.stream_pid == b.stream_pid && a.ca_system_id == b.ca_system_id && a.ca_pid == b.ca_pid;
	}

	inline bool operator!=(const ca_info_t& a, const ca_info_t& b) {
		return !(a == b);
	}

	bool pmt_ca_changed(const pmt_info_t& a, const pmt_info_t& b) {
		if (a.ca_descriptors.size() != b.ca_descriptors.size())
			return true;
		auto it = a.ca_descriptors.begin();
		for (auto& adesc : a.ca_descriptors) {
			if (adesc != *it)
				return true;
		}

		return false;
	}

}; // namespace dtdemux

extern const char* lang_name(const char* code);

std::ostream& dtdemux::operator<<(std::ostream& os, const dtdemux::pid_info_t& info) {
	stdex::printf(os, "PID[%0x] type=0x%x ", info.stream_pid, (int)info.stream_type);
	if (info.audio_lang.lang_code[0] != 0)
		stdex::printf(os, " lang=%s %s", &info.audio_lang.lang_code[0], lang_name(&info.audio_lang.lang_code[0]));
	if (info.subtitle_descriptors.size() > 0)
		stdex::printf(os, " subs={");
	for (const auto& s : info.subtitle_descriptors) {
		stdex::printf(os, " type=0x%x ", (int)s.subtitle_type);
		if (s.lang_code[0] != 0)
			stdex::printf(os, " lang=%s", &s.lang_code[0]);
		stdex::printf(os, "page=%03d-%03d  ", s.composition_page_id, s.ancillary_page_id);
	}
	if (info.subtitle_descriptors.size() > 0)
		stdex::printf(os, " }");
	return os;
}

std::ostream& dtdemux::operator<<(std::ostream& os, const dtdemux::pmt_info_t& pmt) {
	for (auto pid : pmt.pid_descriptors) {
		os << pid << "\n";
	}
	return os;
}

ss::vector<language_code_t, 8> pmt_info_t::audio_languages() const {
	using namespace chdb;
	ss::vector<language_code_t, 8> ret;
	for (const auto& pid_desc : pid_descriptors) {
		if (pid_desc.audio_lang.lang_code[0] != 0) {
			auto* l = &pid_desc.audio_lang.lang_code[0];
			int count = 0;
			language_code_t y(0, l[0], l[1], l[2]);
			for (const auto& x : ret) {
				count += (is_same_language(x, y));
			}
			y.position = count;
			ret.push_back(y);
		}
	}
	return ret;
}

ss::vector<language_code_t, 8> pmt_info_t::subtitle_languages() const {
	using namespace chdb;
	ss::vector<language_code_t, 8> ret;
	for (const auto& pid_desc : pid_descriptors) {
		for (const auto& desc : pid_desc.subtitle_descriptors) {
			auto* l = &desc.lang_code[0];
			int count = 0;
			language_code_t y(0, l[0], l[1], l[2]);
			for (const auto& x : ret) {
				count += (is_same_language(x, y));
			}
			y.position = count;
			ret.push_back(y);
		}
	}
	return ret;
}

// uint32_t to_int(char lang[4]) { return lang[0] | lang[1] << 8 | lang[2] << 16; }

/*
	returns a language code_t containing the index of the pmt language entry corresponding to pref,
	or an invalid language_code_t if the pmt does not contain pref
*/
static
std::tuple<const dtdemux::pid_info_t*, chdb::language_code_t>
find_audio_pref_in_pmt(const pmt_info_t& pmtinfo, const language_code_t& pref) {
	using namespace chdb;
	int order = 0; /* for duplicate entries in pmt, order will be 0,1,2,...*/
	for (const auto& pid_desc : pmtinfo.pid_descriptors) {
		if(!is_audio(pid_desc))
			continue;
		language_code_t lang_code(order, pid_desc.audio_lang.lang_code[0], pid_desc.audio_lang.lang_code[1],
															pid_desc.audio_lang.lang_code[2]);
		if (is_same_language(lang_code, pref)) {
			// order is the order of preference
			if (order == pref.position) { // in pref, position 1 means the second language of this type
				return {&pid_desc, lang_code};
			}
			order++;
		}
	}

	return {nullptr, language_code_t{}};
}

static std::tuple<const dtdemux::pid_info_t*, chdb::language_code_t>
first_audio(const pmt_info_t& pmtinfo) {
	using namespace chdb;
	for (const auto& pid_desc : pmtinfo.pid_descriptors) {
		if(!is_audio(pid_desc))
			continue;
		auto&c  = pid_desc.audio_lang.lang_code;
		chdb::language_code_t lang_code(0, c[0], c[1], c[2]);
		return {&pid_desc, lang_code};
	}
	return {nullptr, chdb::language_code_t()};
}

/*
	returns the best audio language based on user preferences.
	Prefs is an array of languages 3 chars indicating the audio
	languge; the fourth byte is used to distinghuish between duplicates;
*/
std::tuple<const dtdemux::pid_info_t*, chdb::language_code_t>
pmt_info_t::best_audio_language(const ss::vector_<language_code_t>& prefs) const {
	using namespace chdb;
	/* loop over preferences in descending order of preference
		 and return the first match, which is the one with the highest user priority
	*/
	for (const auto& p : prefs) {
		auto [ret, lang_code] = find_audio_pref_in_pmt(*this, p);
		if (ret) {
			return {ret, lang_code};
		}
	}
	return  first_audio(*this);
}


/*
	returns a language code)t containing the index of the pmt language entry corresponding to pref,
	or an invalid language_code_t if the pmt does not contain pref
*/
static std::tuple<const pid_info_t*, const subtitle_info_t*, chdb::language_code_t>
	find_subtitle_pref_in_pmt(const pmt_info_t& pmtinfo, const language_code_t& pref) {
	using namespace chdb;
	int order = 0; /* for duplicate entries in pmt, order will be 0,1,2,...*/
	for (const auto& pid_desc : pmtinfo.pid_descriptors) {
		if(!pid_desc.has_subtitles())
			continue;
		for (const auto& desc : pid_desc.subtitle_descriptors) {
			language_code_t lang_code(order, desc.lang_code[0], desc.lang_code[1], desc.lang_code[2]);
			if (is_same_language(lang_code, pref)) {
				// order is the order of preference
				if (order == pref.position) // in pref, position 1 means the second language of this type
					return {&pid_desc, &desc, lang_code};
				order++;
			}
		}
	}
	return {nullptr, nullptr, language_code_t{}}; // not found
}

static std::tuple<const pid_info_t*, const subtitle_info_t*, chdb::language_code_t>
	first_subtitle(const pmt_info_t& pmtinfo) {
	using namespace chdb;
	for (const auto& pid_desc : pmtinfo.pid_descriptors) {
		if(!pid_desc.has_subtitles())
			continue;
		for (const auto& desc : pid_desc.subtitle_descriptors) {
			auto&c  = desc.lang_code;
			chdb::language_code_t lang_code(0, c[0], c[1], c[2]);
			return {&pid_desc, &desc, lang_code};
		}
	}
	return {nullptr, nullptr, language_code_t{}}; // not found
}


/*
	returns the best subtitle language based on user preferences.
	Prefs is an array of languages 3 chars indicating the subtitle
	languge; the fourth byte is used to distinghuish between duplicates;
*/
std::tuple<const pid_info_t*, const subtitle_info_t*, chdb::language_code_t>
pmt_info_t::best_subtitle_language(const ss::vector_<language_code_t>& prefs) const {
	using namespace chdb;
	/* loop over preferences in descending order of preference
		 and return the first match, which is the one with the highest user priority
	*/
	for (const auto& p : prefs) {
		auto [pid_desc, subtit_desc, subt_lang]  = find_subtitle_pref_in_pmt(*this, p);
		if (pid_desc)
			return {pid_desc, subtit_desc, subt_lang};
	}
	return  first_subtitle(*this);
}

bool pmt_info_t::is_ecm_pid(uint16_t pid) {
	return std::find_if(ca_descriptors.begin(), ca_descriptors.end(),
											[&pid](auto& ca_info) { return ca_info.ca_pid == pid; }) != ca_descriptors.end();
}



/*
	create a pat/pmt with only the preferred audio and subtitle stream
 */
std::tuple<chdb::language_code_t, chdb::language_code_t>
pmt_info_t::make_preferred_pmt_ts(ss::bytebuffer_& output,
																	const ss::vector_<language_code_t>& audio_prefs,
																	const ss::vector_<language_code_t>& subtitle_prefs) {
	pat_writer_t pat_writer;
	pmt_writer_t pmt_writer;
	//make a new pmt with only the selected audio/subtitle language
	pat_writer.start_section(service_id, pmt_pid);
	pat_writer.end_section();

	auto [selected_audio_lang, selected_subtitle_lang] =
		pmt_writer.make(*this, audio_prefs, subtitle_prefs);
	//convert to transport stream
	ts_writer_t w1(pat_writer.section, 0x0);
	w1.output(output);
	ts_writer_t w2(pmt_writer.section, pmt_pid);
	w2.output(output);
	return {selected_audio_lang, selected_subtitle_lang};
}


pmt_info_t dtdemux::parse_pmt_section(const ss::bytebuffer_& pmt_section_data, uint16_t pmt_pid) {
	stored_section_t section(pmt_section_data, pmt_pid); //@todo performs needless copy
	pmt_info_t pmt;
	//read the original full pmt
	section.parse_pmt_section(pmt);
	pmt.pmt_pid = pmt_pid;
	return pmt;
}


/*

	stream type 5
	pid=0x30-0x37: titles
	pid=0x40-0x47: summary
	19/3/2021
	first entry: 0x34  private_data_specifier = 0x2 1 byte 0xc ?
	0x44  private_data_specifier = 0x2 1 byte 0x2c
	0x35  private_data_specifier = 0x2 1 byte 0xd
	0x45  private_data_specifier = 0x2 1 byte 02d
	0x36  private_data_specifier = 0x2 1 byte 0xe
	0x46  private_data_specifier = 0x2 1 byte 0x2e
	0x37  private_data_specifier = 0x2 1 byte 0xf
	0x47  private_data_specifier = 0x2 1 byte 0x2f
	0x30  private_data_specifier = 0x2 1 byte 0x0
	0x40  private_data_specifier = 0x2 1 byte 0x20
	0x31  private_data_specifier = 0x2 1 byte 0x01
	0x41  private_data_specifier = 0x2 1 byte 0x21
	0x32  private_data_specifier = 0x2 1 byte 0x2
	0x42  private_data_specifier = 0x2 1 byte 0x22
	0x33  private_data_specifier = 0x2 1 byte 0x3
	0x43  private_data_specifier = 0x2 1 byte 0x23

	0x52  private_data_specifier = 0x2 1 byte 0x40
	0x53  private_data_specifier = 0x2 1 byte 0x41
	0x54  private_data_specifier = 0x2 1 byte
	0x50  private_data_specifier = 0x2 1 byte 0x10
	0x51  private_data_specifier = 0x2 1 byte 0x30
	0x55  private_data_specifier = 0x2 1 byte 0xc0
	stream_type 4 0x288 audio


*/
/*
	sky it iepg service=4398 pid=48 private_byte=0x0
	service=4398 pid=82 private_byte=0x64
	service=4398 pid=83 private_byte=0x65
	service=4398 pid=84 private_byte=0x66
	service=4398 pid=80 private_byte=0x16
	service=4398 pid=85 private_byte=0x192

	skyuk: iepg1 service service=4189

	service=4189 pid=48 private_byte=0x0 title
	service=4189 pid=64 private_byte=0x32 summ
	service=4189 pid=49 private_byte=0x1 title
	service=4189 pid=65 private_byte=0x33 summ
	service=4189 pid=50 private_byte=0x2 title
	service=4189 pid=66 private_byte=0x34 summ
	service=4189 pid=51 private_byte=0x3 title
	service=4189 pid=67 private_byte=0x35 summ
	service=4189 pid=52 private_byte=0x4 title
	service=4189 pid=68 private_byte=0x36 summ
	service=4189 pid=53 private_byte=0x5 title
	service=4189 pid=69 private_byte=0x37 summ
	service=4189 pid=54 private_byte=0x6 title
	service=4189 pid=70 private_byte=0x38 summ
	service=4189 pid=55 private_byte=0x7 title
	service=4189 pid=71 private_byte=0x39 summ
	service=4189 pid=82 private_byte=0x64
	service=4189 pid=83 private_byte=0x65
	service=4189 pid=84 private_byte=0x66
	service=4189 pid=80 private_byte=0x16
	service=4189 pid=81 private_byte=0x48
	service=4189 pid=85 private_byte=0x192

	skyuk iepg 12206.98H 64511 7550 sid=4195
	service=4195 pid=48 private_byte=0x0 tables=163 and 167
	service=4195 pid=49 private_byte=0x1
	service=4195 pid=82 private_byte=0x64
	service=4195 pid=83 private_byte=0x65
	service=4195 pid=84 private_byte=0x66
	service=4195 pid=80 private_byte=0x16
	service=4195 pid=85 private_byte=0x192


*/

/*
	EN 300 468 v1.13.1
	sub_table: collection of sections with the same value of table_id and:
	for a NIT:
	the same table_id_extension (network_id) and version_number;
	for a BAT:
	the same table_id_extension (bouquet_id) and version_number;
	for a SDT:
	the same table_id_extension (transport_stream_id), the same original_network_id and
	version_number;
	for a EIT:
	the same table_id_extension (service_id), the same transport_stream_id, the same original_network_id
	and version_number.
	NOTE:
	The table_id_extension field is equivalent to the fourth and fifth byte of a section when the
	section_syntax_indicator is set to a value of "1".



*/

/*
	https://tvheadend.org/issues/4263
	mhwepg2  	10847.00	V 19.2E
	programme names on 563 and descriptions on 566. The other PIDs have similar on them (I think series link, channel
	names/SIDs etc).

	Channel details for the bouquet, including NID, TSID and SID for each service are carried on PID 561 in the table with
	TID 200 (0xC8) -

	To get the data above, you find tables with TID 200 (0xC8) on PID 561, then you look for byte 117 of the table data
	(after the header) which gives the number of channels (N) in the bouquet. After this byte, there are 8 bytes for each
	channel - 2 bytes network ID (e.g. 0x00 0x01 for Astra 1 with NID 1), 2 bytes TSID, 2 bytes SID, and 2 bytes unknown.

	At the end of this data, so byte 117+(8*N) there are the channel names, with the 1 byte before each name containing
	the length of the name in the last four bits (byte & 0xF).

	Next to investigate is the programme names table (PID 644 TID 220 for 7 day EPG) and p
	rogramme descriptions table (PID 642 TID 150).

	Update - the channel names table is also present on PID 644, as are some of the programme descriptions (short ones).

	So you can get partial data from just that single PID. I've been investigating so far with a Python script and got
	information like this just from parsing the tables (150, 200 and 220) on PID 644 -


*/
