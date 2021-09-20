/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include <bitset>
#include "mpeg.h"
#include "substream.h"

namespace dtdemux {
	struct stored_section_t;

	struct table_timeout_t {
		steady_time_t start_time;
		bool timedout_{false};
		std::chrono::milliseconds timeoutms{11000ms};
		int16_t table_id{-1};

		table_timeout_t(int table_id);

		inline bool timedout_now();

		void reset() {
			*this = table_timeout_t(table_id);
			start_time = steady_clock_t::now();
		}

		inline bool timedout(steady_time_t last_reset_time);

	};

	enum class section_type_t {
		DUPLICATE,
		COMPLETE, //and duplicate
		NEW,
		LAST, //last section (and new)
	};

	struct section_header_t {
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



/*
	assumption: different parser, and therefore different subtable_status per pid
*/
	struct subtable_key_t {
		uint16_t table_id_extension{0}; //network_id (NIT), bouquet_id(BAT), ts_id (SDT), service_id (EPG)
		uint16_t table_id_extension1{0}; //0 (NIT, BAT), onid(SDT), ts_id(EIT)
		uint16_t table_id_extension2{0}; //0  (NIT, BAT< SDT) onid(EIT)
		/*For EIT schedule there multiple tables for schedule/actual and schedule/other
			are mapped to the same table_id below
		*/
		int16_t table_id{-1};

	private:
		operator uint64_t() const {
			return (uint64_t(table_id_extension) << 48) |
				(uint64_t(table_id_extension1) << 32) |
				(uint64_t(table_id_extension2) << 16) |
				(table_id);
		}
	public:

		bool operator<(const subtable_key_t& other) const {
			return uint64_t(*this) < uint64_t(other);
		}
		subtable_key_t(const section_header_t hdr)
			: table_id_extension(hdr.table_id_extension)
			, table_id_extension1(hdr.table_id_extension1)
			, table_id_extension2(hdr.table_id_extension2)
			, table_id(hdr.table_id) {
		}

		subtable_key_t(const section_header_t& hdr, int table_id)
			: subtable_key_t(hdr) {
			this->table_id = table_id;
		}

	};


	struct completion_status_t {

		int16_t version_number{-1};
		int16_t last_section_number{-1}; //number of sections in this subtable
    /*only useful for EIT which has multiple tables per "subtable" (confusing, yes)
			otherwise, set equal to a single (unique) table_id for this section
		*/
		int16_t count{0};
		int16_t maxcount{0};
		bool completed = false;
		uint32_t section_flags[16]{0};
		inline bool set_flag(int idx);
		inline section_type_t set_flag(const section_header_t& hdr);
		completion_status_t() = default;

		inline completion_status_t(const section_header_t& hdr);
		inline void reset(const section_header_t& hdr);

	};



	class parser_status_t {
		steady_time_t last_new_section;
		steady_time_t last_section;

		int count_completed{0};
		bool completed {false};

		int last_cc_error_count{0};
		steady_time_t last_cc_error_time{};
		//steady_time_t start_time{0};

		std::map<subtable_key_t, completion_status_t> cstates;
		std::map<uint8_t, table_timeout_t> table_timeouts; //indexed by table id

		inline completion_status_t& completion_status_for_section(const section_header_t& hdr);


	public:
		//bool timed_out() const;
		parser_status_t() = default;

		void reset();
		void reset(const section_header_t& hdr);
		bool timedout_now(uint8_t table_id);
#if 0
		void stats();
#endif

/*
	check if a section was already processed.
	Also check if the table has timedout, which is the case if no cc_errors have appeared for
	a certain amount of time
	returns: timedout, new_subtable_version, section_type
 */
		std::tuple<bool, bool, section_type_t> check(const section_header_t& hdr, int cc_error_counter);

		std::tuple<int, int> get_counts() const {
			return std::make_tuple(count_completed, cstates.size());
		}
	};
}