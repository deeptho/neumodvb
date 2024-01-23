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

#include "stackstring.h"

#include "packetstream.h"
#include "section.h"


namespace dtdemux {

	struct ts_writer_t {
		dtdemux::data_range_t& section;
		int output_byte_no{0};
		uint16_t pid{0x1fff};
		//read data into a buffer
		int get_buffer(uint8_t* output_buffer, int size);
		ts_writer_t(dtdemux::data_range_t& section, uint16_t pid)
			: section(section)
			, pid(pid)
			{}

		void output(ss::bytebuffer_& out);
	};

	struct section_writer_t {
		uint16_t pid{0x1fff};
		ss::bytebuffer<1024> data;
		uint8_t * p_section_start{nullptr};
		uint8_t * p_length{nullptr}; //where section length is stored
		uint8_t* p_es_info_length{nullptr}; //where started last es_info_descripor_loop starts;
		dtdemux::data_range_t section;

		section_writer_t();
		void end_section();

		void save(ss::bytebuffer_& output, uint16_t pid);
	};

	struct pat_writer_t : public section_writer_t  {
		pat_writer_t() =default;
		void start_section(uint16_t service_id, uint16_t pmt_pid);
	};



	struct pmt_writer_t : public section_writer_t  {
		void start_section(const pmt_info_t& pmt_info);
		void start_es(uint8_t stream_type, uint16_t pid);
		void add_desc(const descriptor_t& desc, const uint8_t* data);
		void add_audio_desc(const pmt_info_t& pmt, const audio_language_info_t& ai);
		void add_subtitle_desc(const pmt_info_t& pmt, const subtitle_info_t& si);
		std::tuple<chdb::language_code_t, chdb::language_code_t>
		make(const pmt_info_t& pmt, const ss::vector_<chdb::language_code_t>& audio_prefs,
				 const ss::vector_<chdb::language_code_t>& subtitle_prefs);
		void end_es();

		pmt_writer_t() = default;

	};


};
