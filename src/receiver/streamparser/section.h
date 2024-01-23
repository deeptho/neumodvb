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


#pragma once
#include <cstdlib>
#include "mpeg.h"
#include "substream.h"
#include "neumodb/chdb/chdb_extra.h"

namespace dtdemux {
	struct pat_services_t;
	struct pmt_info_t;
	struct sdt_services_t;
	struct bouquet_t;
	struct pmt_writer_t;
	struct nit_network_t;
	struct epg_t;

	struct section_header_t{
		uint16_t pid{0x1fff};
		uint16_t len{0};
		uint16_t header_len{0};
		uint16_t table_id_extension{0};
		uint16_t table_id_extension1{0};
		uint16_t table_id_extension2{0};
		int16_t segment_last_section_number{0};
		uint8_t table_id{0};
		uint8_t last_table_id{0};
		uint8_t version_number{0};
		uint8_t section_number{0};
		uint8_t last_section_number{0};
		bool current_next = 1;
		bool section_syntax_indicator{0};
		bool private_bit{0};

		inline bool is_sdt() const  {
			return section_syntax_indicator && (table_id == 0x42  || table_id== 0x46);
		}
		inline bool is_freesat_eit() const  {
			return section_syntax_indicator &&
				(pid==3004 || pid==3003 || pid==3842 || pid==3843);
		}

		inline bool is_eit() const  {
			return (section_syntax_indicator && (table_id >= 0x4e  && table_id<= 0x6f))
				|| is_freesat_eit();
		}
		inline bool is_sky_summary() const  {
			return section_syntax_indicator && (pid >= 0x40 &&  pid < 0x48);
		}
		inline bool is_mhw2() const  {
			return section_syntax_indicator && ((pid >= 561 &&  pid < 567) || pid ==644);
		}
		inline bool is_sky_title() const  {
			return section_syntax_indicator && (pid >= 0x30 &&  pid < 0x38);
		}
	};

	struct stored_section_t {
		uint16_t pid{0x1fff};
		ss::bytebuffer_& payload;
		int bytes_read{0};
		bool error{false};
		bool throw_on_error{false};
		bool has_error() const {
			return error;
		}

		void throw_bad_data() {
			error = true;
			if (throw_on_error)
				throw bad_data_exception();
		}

		stored_section_t(ss::bytebuffer_& payload_, uint16_t pid_)
			: pid(pid_)
			, payload(payload_) {}

		int available() const {
			return payload.size() - bytes_read;
		}

		uint8_t* current_pointer(int size)  {
			if(available() < size) {
				throw_bad_data();
				return nullptr;
			}
			auto ret = payload.buffer() + bytes_read;
			bytes_read += size;
			return ret;
		}

		int get_buffer(uint8_t* buffer, int toread) {
			if(toread > available()) {
				throw_bad_data();
				return -1;
			}
			memcpy(buffer, payload.buffer() + bytes_read, toread);
			bytes_read += toread;
			return toread;
		}

		template<typename T> T get() {
			typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
			get_buffer((uint8_t*)::std::addressof(storage), sizeof(T));
			return net_to_native((const T&) storage);
		}

		template<typename T> T get(const descriptor_t& desc, uint16_t stream_pid);

		//T is the structure to fill, U defines which substructure to fill
		//returns -1 on error
		template<typename U, typename T> int get_fields(T& ret);
		template<typename U, typename T> int get_fields(T& ret, const descriptor_t&desc);
		template<typename U, typename T, typename E> int get_fields(T& ret, const descriptor_t&desc, E extra);
		template<typename U, int size> int get_fields(ss::string<size>& ret) {
			return get_fields<U, ss::string_>(ret);
		}
		template<typename U, int size> int get_fields(ss::string<size>& ret, const descriptor_t&desc) {
			return get_fields<U, ss::string_>(ret, desc);
		}

		template<typename U, typename T, int size> int get_vector_fields(ss::vector<T,size>& ret, const descriptor_t&desc);


		int skip(int toskip) {
			if(toskip > available()) {
				throw_bad_data();
				return -1;
			}
			bytes_read += toskip;
				return 1;
		}
		void parse_section_header(section_header_t& ret);
		void parse_table_header(section_header_t& ret);
		bool parse_pat_section(pat_services_t& pat_services, section_header_t& hdr);
		bool parse_pat_section(pat_services_t& pat_services);
		bool parse_pmt_section(pmt_info_t& pmt, section_header_t& hdr);
		bool parse_pmt_section(pmt_info_t& pmt);
		bool parse_sdt_section(sdt_services_t& ret, section_header_t& hdr);
		bool parse_sdt_section(sdt_services_t& ret);
		bool parse_bat_section(bouquet_t& bouquet, section_header_t& hdr, int fst_preferred_region_id);
		bool parse_bat_section(bouquet_t& bouquet, int fst_preferred_region_id);
		bool parse_nit_section(nit_network_t& network);
		bool parse_nit_section(nit_network_t& network, section_header_t& hdr);
		bool parse_eit_section(epg_t& ret, section_header_t& hdr);
		bool parse_eit_section(epg_t& ret);
		bool parse_sky_section(epg_t& ret, section_header_t& hdr);
		bool parse_sky_section(epg_t& ret);
		bool parse_mhw2_channel_section(bouquet_t& bouquet, section_header_t& hdr);
		bool parse_mhw2_channel_section(bouquet_t& bouquet);
		bool parse_mhw2_title_section(epg_t& epg, section_header_t& hdr);
		bool parse_mhw2_title_section(epg_t& epg);
		bool parse_mhw2_short_summary_section(epg_t& epg, section_header_t& hdr);
		bool parse_mhw2_short_summary_section(epg_t& epg);
		bool parse_mhw2_long_summary_section(epg_t& epg, section_header_t& hdr);
		bool parse_mhw2_long_summary_section(epg_t& epg);
	};
	pmt_info_t parse_pmt_section(ss::bytebuffer_& pmt_section_data, uint16_t pmt_pid);
};
