/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
#include "psi.h"
#include "packetstream.h"
#include "section.h"
#include "si.h"
#include "sidebug.h"
#include "streamwriter.h"

#include "dvbtext.h"
#include "opentv_string_decoder.h"
#include "psi_impl.h"
#include <iomanip>
#include <iostream>


using namespace dtdemux;
using namespace dtypes;

#define BCDCHARTOINT(x) (10 * ((x & 0xF0) >> 4) + (x & 0xF))
#define lang_iso639(a, b, c) ((a) << 16 | (b) << 8 | (c))


inline bool crc_is_correct(const ss::bytebuffer_& payload) {
	auto crc = crc32(payload.buffer(), payload.size());
	return crc == 0;
}

namespace dtdemux {

	section_header_t* section_parser_t::header() {
		if (!section_complete)
			return nullptr;
		return &parsed_header;
	}

/*
	returns 1 on success, -1 on error
*/

	using namespace dtdemux;

	void section_parser_t::parse_section_header(section_header_t& ret) {
		payload.clear();
		assert(bytes_read == payload.size());
		RETURN_ON_ERROR;
		ret.pid = pid;
		assert(current_ts_packet->get_pid() == pid);
		/*
			The following call can trigger loading a new packet. Only after reading the
			packet we will know of the existence of a pointer field. If one exists,
			we must restart reading the table_id at the location of the pointer field
		*/

		if (pointer_field > 0) {
			// assert(ret.table_id == 0xff);
			skip_to_pointer();
			bytes_read = 0;
			if (ret.table_id == 0xff)
				THROW_BAD_DATA;
		}
		ret.table_id = this->get<uint8_t>();

		if (ret.table_id == 0xff) {
			// rest of packet is stuffing.
#if 0
			skip(current_ts_packet->range.available());
#else
			if(pointer_field)
				skip_to_pointer();
			else
				skip(current_ts_packet->range.available());
#endif
			ret.table_id = this->get<uint8_t>();
			if (ret.table_id == 0xff)
				THROW_BAD_DATA;
		}

		assert(!wait_for_unit_start);
		bytes_read = 1;
		payload.append_raw((uint8_t)native_to_net(ret.table_id)); // save what we read
		assert(bytes_read == payload.size());
		// table length
		auto len = this->get<uint16_t>();
		payload.append_raw((uint16_t)native_to_net(len)); // save what we read
		assert(bytes_read == payload.size());
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

			dtdebugf("Bad reserved bits"); //tests reserve flags
			THROW_BAD_DATA;
		}
#endif
	}

	void section_parser_t::parse_table_header(section_header_t& ret) {
		parse_section_header(ret);
		RETURN_ON_ERROR;
		assert(bytes_read == payload.size());
		// in this case, the test would be: len  4093
		if (ret.len > 4096 - 3) {
			dtdebugf("Bad section length: pid=0x{:x} table_id=0x{:x} len{:d}", ret.pid, ret.table_id, ret.len); // See p. 44
			THROW_BAD_DATA;
			RETURN_ON_ERROR;
		}
		ret.table_id_extension = this->get<uint16_t>();
		payload.append_raw((uint16_t)native_to_net(ret.table_id_extension)); // save what we read

		ret.version_number = this->get<uint8_t>();
		payload.append_raw((uint8_t)native_to_net(ret.version_number)); // save what we read

		ret.current_next = ret.version_number & 0x1;					 // 1=current 0=next
		ret.version_number = (ret.version_number & 0x3f) >> 1; // skip 2 reserve bits and version_number

		ret.section_number = this->get<uint8_t>();
		payload.append_raw((uint8_t)native_to_net(ret.section_number)); // save what we read

		ret.last_section_number = this->get<uint8_t>();
		payload.append_raw((uint8_t)native_to_net(ret.last_section_number)); // save what we read
		RETURN_ON_ERROR;
		assert(bytes_read == payload.size());

		if (ret.is_sdt()) {
			ret.table_id_extension1 = this->get<uint16_t>();
			payload.append_raw((uint16_t)native_to_net(ret.table_id_extension1)); // save what we read
		} else if (ret.is_eit()) {
			ret.table_id_extension1 = this->get<uint16_t>();
			payload.append_raw((uint16_t)native_to_net(ret.table_id_extension1)); // save what we read

			ret.table_id_extension2 = this->get<uint16_t>();
			payload.append_raw((uint16_t)native_to_net(ret.table_id_extension2)); // save what we read
		}

		ret.header_len = bytes_read;
	}

/*
	generic parser which saves a complete table section
	This is equivalent to getting a section
*/
	void section_parser_t::parse_payload_unit_(bool parse_only_section_header) {
		// auto current_frame = pes_packet_desc_t(*current_ts_packet);
		section_complete = false;
		section_header_t hdr{};
		if (parse_only_section_header)
			parse_section_header(hdr);
		else
			parse_table_header(hdr);
		if (error) {
			parsed_header = section_header_t{};
			RETURN_ON_ERROR;
		}
		hdr.pid = current_ts_packet->get_pid();

		int toread = hdr.len - (bytes_read - 3); // we may already read 5 bytes of data following the length field
		if (toread <= 0 || toread >= 4096) {
			error = true;
			RETURN_ON_ERROR;
		}

		bool is_stuffing = (hdr.table_id == 0x72);
		if (this->get_buffer((uint8_t*)payload.buffer() + payload.size(), toread) < 0) {
			THROW_BAD_DATA;
			RETURN_ON_ERROR;
		}
		payload.resize_no_init(toread + payload.size());

		if (!is_stuffing) {
			if (!parse_only_section_header && hdr.section_syntax_indicator &&
					!crc_is_correct(payload)) { // CA sections may not have a crc!
				dtdebugf("Skipping bad section (CRC error) section_number={} pid=0x{:x}", hdr.section_number, pid);
				THROW_BAD_DATA;
				RETURN_ON_ERROR;
			}
			if (hdr.is_eit()) {
				assert(hdr.len > 13);
				hdr.segment_last_section_number = payload[12];
				hdr.last_table_id = payload[13];
			} else {
				hdr.last_table_id = hdr.table_id;
				hdr.segment_last_section_number = -1;
			}
		}
		// current_frame.set_end(current_ts_packet->range.end_bytepos());
		parsed_header = hdr;
		section_complete = true;
		return;
	}

/*
	generic parser which saves a complete table section
	This is equivalent to getting a section
*/
	void section_parser_t::parse_payload_unit() {
		bool parse_only_section_header = true;
		parse_payload_unit_(parse_only_section_header);
		if (!has_error())
			parent.psi_cb(pid, payload);
		return;
	}

/*!
	update  byte position markets when a current pes packet or a current section has been completed
*/
	void section_parser_t::unit_completed_cb() {
#if 0
		dtdebugf("INDEX: %c: [{:d}, {:d}[", 	(char)current_unit_type, current_unit_start_bytepos,
						 current_unit_end_bytepos);
#endif
		if (current_unit_type == stream_type::marker_t::pat) {
			parent.event_handler.last_pat_start_bytepos = this->current_unit_start_bytepos;
			parent.event_handler.last_pat_end_bytepos = this->current_unit_end_bytepos;
		} else if (current_unit_type == stream_type::marker_t::pmt) {
			parent.event_handler.last_pmt_start_bytepos = this->current_unit_start_bytepos;
			parent.event_handler.last_pmt_end_bytepos = this->current_unit_end_bytepos;
		}
#if 0
		parent.event_handler.index_event(current_unit_type,
																		 /* this->pid, this->stream_type, */
																		 this->last_play_time,
																		 1, //illegal
																		 1, //illegal
																		 this->current_unit_start_bytepos,
																		 this->current_unit_end_bytepos, this->name);
#endif

		current_unit_start_bytepos = current_ts_packet->range.start_bytepos();
	}

	void psi_parser_t::parse_payload_unit() {

		auto new_play_time = parent.event_handler.pcr_play_time();
		assert(new_play_time >= this->last_play_time);
		this->last_play_time = new_play_time;
		bool parse_only_section_header = false;
		section_parser_t::parse_payload_unit_(parse_only_section_header);
		RETURN_ON_ERROR;
	}

	bool pat_parser_t::parse_pat_section(stored_section_t& section, pat_services_t& pat_services) {
		auto& hdr = *header();
		return section.parse_pat_section(pat_services, hdr);
	}

	bool pmt_parser_t::parse_pmt_section(stored_section_t& section, pmt_info_t& pmt) {
		section_header_t& hdr = *header();
		return section.parse_pmt_section(pmt, hdr);
	}

	bool nit_parser_t::parse_nit_section(stored_section_t& section, nit_network_t& network) {
		auto& hdr = *header();
		return section.parse_nit_section(network, hdr);
	}

	bool sdt_bat_parser_t::parse_sdt_section(stored_section_t& section, sdt_services_t& ret) {
		auto& hdr = *header();
		return section.parse_sdt_section(ret, hdr);
	}

	bool sdt_bat_parser_t::parse_bat_section(stored_section_t& section, bouquet_t& bouquet) {
		auto& hdr = *header();
		return section.parse_bat_section(bouquet, hdr, fst_preferred_region_id);
	}

	bool eit_parser_t::parse_eit_section(stored_section_t& section, epg_t& ret) {
		auto& hdr = *header();
		return section.parse_eit_section(ret, hdr);
	}

	bool eit_parser_t::parse_sky_section(stored_section_t& section, epg_t& ret) {
		auto& hdr = *header();
		return section.parse_sky_section(ret, hdr);
	}

	bool mhw2_parser_t::parse_mhw2_channel_section(stored_section_t& section, bouquet_t& bouquet) {
		auto& hdr = *header();
		return section.parse_mhw2_channel_section(bouquet, hdr);
	}

	bool mhw2_parser_t::parse_mhw2_title_section(stored_section_t& section, epg_t& epg) {
		auto& hdr = *header();
		return section.parse_mhw2_title_section(epg, hdr);
	}

	bool mhw2_parser_t::parse_mhw2_short_summary_section(stored_section_t& section, epg_t& epg) {
		auto& hdr = *header();
		return section.parse_mhw2_short_summary_section(epg, hdr);
	}

	bool mhw2_parser_t::parse_mhw2_long_summary_section(stored_section_t& section, epg_t& epg) {
		auto& hdr = *header();
		return section.parse_mhw2_long_summary_section(epg, hdr);
	}

}; // namespace dtdemux

void psi_parser_t::parse_payload_unit_init() {
	auto new_play_time = parent.event_handler.pcr_play_time();
	assert(new_play_time >= this->last_play_time);
	this->last_play_time = new_play_time;
	bool parse_only_section_header = false;
	section_parser_t::parse_payload_unit_(parse_only_section_header);
}

void pmt_parser_t::parse_payload_unit() {
	parse_payload_unit_init();
	RETURN_ON_ERROR;

	auto& hdr = *header();

	if (hdr.table_id != 0x2) {
		error = true;
		RETURN_ON_ERROR;
	}

	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);
	assert(!(must_process && badversion));

	log4cxx::NDC::push(" PMT");
	pmt_info_t pmt;
	bool success = false;

	if (must_process || timedout)  {
		stored_section_t section(payload, hdr.pid);
		section.skip(hdr.header_len); // already parsed
		success = timedout ? true : parse_pmt_section(section, pmt);
		if (success) {
			pmt.stream_packetno_end = parent.event_handler.last_pmt_end_bytepos / ts_packet_t::size;
			auto must_reset = this->section_cb(this, pmt, !hdr.current_next, section.payload);
			if (must_reset == reset_type_t::ABORT)
				parent.return_early();
			else if (must_reset == reset_type_t::RESET)
				parser_status.reset(hdr);
		}
	}
	log4cxx::NDC::pop();
}

void pat_parser_t::parse_payload_unit() {
	parse_payload_unit_init();
	RETURN_ON_ERROR;

	auto& hdr = *header();
	if (hdr.table_id != 0x0 || !hdr.section_syntax_indicator) { // not a pat section
		error = true;
		RETURN_ON_ERROR;
	}

	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);

	log4cxx::NDC::push(" PAT");
#if 0
	if (completed_now)
		dtdebugf("Parser completed");
#endif
	pat_services_t pat_services;
	bool success = false;
	if (must_process || timedout) {
		stored_section_t section(payload, hdr.pid);
		section.skip(hdr.header_len); // already parsed
		success = parse_pat_section(section, pat_services);
		if (success && pat_services.entries.size() == 0) {
			dtdebugf("empty pat (2)\n");
			stored_section_t section(payload, hdr.pid);
			section.skip(hdr.header_len); // already parsed
			pat_services_t pat_services;
			parse_pat_section(section, pat_services);
		}
	}
	if (success || timedout) {
		subtable_info_t info{pid, true, hdr.table_id, hdr.version_number, hdr.last_section_number + 1, done, timedout};
		auto must_reset = section_cb(pat_services, info);
		if (must_reset == reset_type_t::ABORT)
			parent.return_early();
		else if (must_reset == reset_type_t::RESET)
			parser_status.reset(hdr);
	}
	log4cxx::NDC::pop();
}

void nit_parser_t::parse_payload_unit() {
	parse_payload_unit_init();
	RETURN_ON_ERROR;

	auto& hdr = *header();
	bool is_nit = ((hdr.table_id & ~0x1) == 0x40);
	bool is_stuffing = (hdr.table_id == 0x72);
	if (is_stuffing)
		return;
	if (!is_nit) { // not a nit section; could be a stuffing section
		error = true;
		RETURN_ON_ERROR;
	}
	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);
	assert(!(must_process && badversion));
	log4cxx::NDC::push(" NIT");
	if (completed_now)
		dtdebugf("Parser completed");
	nit_network_t network;
	bool success{false};
	if (must_process || timedout) {
		stored_section_t section(payload, hdr.pid);
		section.skip(hdr.header_len); // already parsed
		success = parse_nit_section(section, network);
	} else
		network.is_actual = (hdr.table_id == 0x40);

	if (success || timedout) {
		subtable_info_t info{pid,	 network.is_actual, hdr.table_id, hdr.version_number, hdr.last_section_number + 1,
			done, timedout};
		auto must_reset = section_cb(network, info);
		if (must_reset == reset_type_t::ABORT)
			parent.return_early();
		else if (must_reset == reset_type_t::RESET)
			parser_status.reset(hdr);
	}
	log4cxx::NDC::pop();
}

void sdt_bat_parser_t::parse_payload_unit() {
	dttime_init();
	parse_payload_unit_init();
	RETURN_ON_ERROR;
	auto& hdr = *header();

	bool is_sdt = (hdr.table_id == 0x42 || hdr.table_id == 0x46);
	bool is_bat = (hdr.table_id == 0x4A);
	bool is_stuffing = (hdr.table_id == 0x72);
	if (is_stuffing)
		return;
	if (!is_sdt && !is_bat) { // not a sdt/bat section
		error = true;
		RETURN_ON_ERROR;
	}

	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);
	assert(!(must_process && badversion));
	if (is_sdt) {
		log4cxx::NDC::push(" SDT");
#if 0
		if (completed_now)
			dtdebugf("Parser completed now");
#endif
		sdt_services_t services;
		bool success{false};
		if (must_process || timedout) {
			stored_section_t section(payload, hdr.pid);
			section.skip(hdr.header_len); // already parsed
			success = parse_sdt_section(section, services);
		} else
			services.is_actual = (hdr.table_id == 0x42);
		if (success || timedout) {
			subtable_info_t info{
				pid, services.is_actual, hdr.table_id, hdr.version_number, hdr.last_section_number + 1, done, timedout};
			auto must_reset = sdt_section_cb(services, info);
			if (must_reset == reset_type_t::ABORT)
				parent.return_early();
			else if (must_reset == reset_type_t::RESET)
				parser_status.reset(hdr);
		}
	} else {
		log4cxx::NDC::push(" BAT");
		if (completed_now)
			dtdebugf("Parser completed now");
		bouquet_t bouquet;
		bool success{false};
		if (must_process || timedout) {
			timedout = false;
			stored_section_t section(payload, hdr.pid);
			section.skip(hdr.header_len); // already parsed
			success = parse_bat_section(section, bouquet);
		}
		if (success || timedout) {
			dtdebug_nicef("bat_cb bouquet={:d} vers={:d} sec={:d}/{:d} done={:d} timedout={:d}\n", bouquet.bouquet_id,
						 hdr.version_number, hdr.section_number, hdr.last_section_number + 1, done, timedout);
			subtable_info_t info{pid, true, hdr.table_id, hdr.version_number, hdr.last_section_number + 1, done, timedout};
			auto must_reset = bat_section_cb(bouquet, info);
			if (must_reset == reset_type_t::ABORT)
				parent.return_early();
			else if (must_reset == reset_type_t::RESET) {
				parser_status.reset(hdr);
			}
		}
		if (completed_now)
			dtdebugf("Parser completed");
	}
	log4cxx::NDC::pop();
}

void eit_parser_t::parse_payload_unit() {
	parse_payload_unit_init();
	RETURN_ON_ERROR;

	auto& hdr = *header();
	bool is_stuffing = (hdr.table_id == 0x72);
	if (is_stuffing)
		return;

	if(hdr.table_id > hdr.last_table_id)
		hdr.last_table_id = hdr.table_id;
	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);
	assert(!(must_process && badversion));
	if (completed_now)
		dtdebugf("Parser: table completed");
	epg_t epg;
	std::tie(epg.num_subtables_completed, epg.num_subtables_known) = parser_status.get_counts();

	epg.epg_type = epg_type;
	epg.is_sky_title = pid >= 0x30 && pid < 0x38;
	epg.is_sky_summary = pid >= 0x40 && pid < 0x48;
	epg.is_sky = epg.is_sky_summary || epg.is_sky_title;
	epg.is_freesat = (pid == dtdemux::ts_stream_t::FREESAT_EIT_PID);
	bool success{false};
	if (must_process || timedout) {
		stored_section_t section(payload, hdr.pid);
#ifdef PRINTTIME
		auto xxx_start = system_clock_t::now();
#endif
		section.skip(hdr.header_len); // already parsed
		log4cxx::NDC::push(" EIT");
		if (epg.is_sky)  {
			success = parse_sky_section(section, epg);
#ifdef PRINTTIME
			auto now = system_clock_t::now();
			processing_delay +=  std::chrono::duration_cast<std::chrono::microseconds>(now -xxx_start).count();
			processing_count ++;
#endif
		}
		else
			success = parse_eit_section(section, epg);
		log4cxx::NDC::pop();
	} else {
		if (!epg.is_sky) {
			epg.is_actual = (hdr.table_id == 0x4e) || ((hdr.table_id >= 0x50) && (hdr.table_id <= 0x5f));
		} else {
			epg.is_actual = false;
		}
	}
	if (success  || timedout) {
		subtable_info_t info{pid,	 epg.is_actual, hdr.table_id, hdr.version_number, hdr.last_section_number + 1,
			done, timedout};
#ifdef PRINTTIME
		auto xxx_start = system_clock_t::now();
#endif
		auto must_reset = section_cb(epg, info);
#ifdef PRINTTIME
		if(epg.is_sky) {
			auto now = system_clock_t::now();
			callback_delay +=  std::chrono::duration_cast<std::chrono::microseconds>(now -xxx_start).count();
			callback_count++;
		}
#endif
		if (must_reset == reset_type_t::ABORT)
			parent.return_early();
		else if (must_reset == reset_type_t::RESET)
			parser_status.reset(hdr);
	}
#ifdef PRINTTIME
	if(callback_count>0) {
		dtdebug_nicex("PERF: {:f}us per sec ({:d}/{:d}); {:f}us per cb ({:d}/{:d})",
									processing_delay/(double)processing_count, processing_delay, processing_count,
									callback_delay/(double)callback_count, callback_delay, callback_count);
	}
#endif
}

#ifdef PRINTTIME
int64_t eit_parser_t::processing_count;
int64_t eit_parser_t::callback_count;
int64_t eit_parser_t::processing_delay;
int64_t eit_parser_t::callback_delay;
#endif

void mhw2_parser_t::parse_payload_unit() {
	parse_payload_unit_init();
	RETURN_ON_ERROR;

	auto& hdr = *header();

	bool is_stuffing = (hdr.table_id == 0x72);

	if (is_stuffing)
		return;
	bool is_title{false};
	bool is_channel{false};
	bool is_summary{false};
	bool is_short_summary{false};

	switch (hdr.table_id) {
	case 200: // 0xc8
		switch (hdr.table_id_extension) {
		case 0:
			is_channel = true;
			break;
		case 1:
			// themes
			break;
		case 2: // seems like channels but for network_id==8196
			break;
		default:
			break;
		}
		break;
		// case 220...259:
	case 220: // 0xdc
		is_title = true;
		break;
	case 150: // 0x96
		if (pid == 642)
			is_summary = true;
		break;
	default:
		break;
	}
	if (!is_summary && !is_short_summary && !is_title && !is_channel)
		return;

	auto [timedout, badversion, section_type] = parser_status.check(hdr, cc_error_counter);
	bool must_process = (section_type == section_type_t::NEW || section_type == section_type_t::LAST);
	bool done = (section_type == section_type_t::LAST || section_type == section_type_t::COMPLETE);
	bool completed_now = (section_type == section_type_t::LAST);
	assert(!(must_process && badversion));
	log4cxx::NDC::push(" MHW2C");
	if (completed_now)
		dtdebugf("Parser completed now");

	bool success{false};
	dtdemux::reset_type_t must_reset{dtdemux::reset_type_t::NO_RESET};
	if (must_process || timedout) {
		timedout = false;
		stored_section_t section(payload, hdr.pid);
		section.skip(hdr.header_len); // already parsed
		subtable_info_t info{pid, true, hdr.table_id, hdr.version_number, hdr.last_section_number + 1, done, timedout};

		if (is_channel) {
			bouquet_t bouquet;
			success = parse_mhw2_channel_section(section, bouquet);
			if (success && (must_process || timedout)) {
				must_reset = bat_section_cb(bouquet, info);
			}
		} else if (is_title) {
			epg_t epg;
			epg.is_mhw2 = true;
			epg.is_mhw2_summary = false;
			epg.is_mhw2_title = true;
			epg.epg_type = epg_type;
			success = parse_mhw2_title_section(section, epg);
			if (success && (must_process || timedout)) {
				must_reset = eit_section_cb(epg, info);
			}
		} else {
#if 1
			epg_t epg;
			epg.is_mhw2 = true;
			epg.is_mhw2_summary = true;
			epg.is_mhw2_title = false;
			if (is_short_summary)
				success = parse_mhw2_short_summary_section(section, epg);
			else if (is_summary)
				success = parse_mhw2_long_summary_section(section, epg);
			else {
				assert(0);
			}
			if (success && (must_process || timedout)) {
				must_reset = eit_section_cb(epg, info);
			}
#endif
		}
	}

	if (success || timedout) {
		if (must_reset == reset_type_t::ABORT)
			parent.return_early();
		else if (must_reset == reset_type_t::RESET) {
			dtdebugf("MHW2C: requesting reset for table={:x}-{:d}", hdr.table_id, hdr.table_id_extension);
			parser_status.reset(hdr);
		}
	}

	if (completed_now)
		dtdebugf("Parser completed");

	log4cxx::NDC::pop();
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
