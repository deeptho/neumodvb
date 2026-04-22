/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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
/*
	class for creating a single-pid transport stream containing sections,
	The class contains some state: the continunity counter

	Usage: call one of the methods in the derived class, to create a SINSGLE sections.
	The section will be created and stored into the internal buffer section_data
	and then converted to a transport stream into the buffer specified in the call

	This process can be repeated multiple times, with a consistent continuity counter
	over all output sections

 */

	struct ts_writer_t {
		int cc_counter{0};
		ss::bytebuffer<1024> section_data; /* internal buffer for section data, not ts format
																					(ts data is created using "output" method)
																			 */
		dtdemux::data_range_t data_range;
		pid_t pid{0x1fff};
		//read data into a buffer
		int get_buffer(uint8_t* output_buffer, int size);
		ts_writer_t(uint16_t pid)
			: data_range(section_data.buffer(), section_data.capacity())
			, pid(pid)
			{}

		void output(ss::bytebuffer_& out);
	};


	class section_writer_t : public ts_writer_t {
	protected:
		int version_number{0};
		uint16_t pid{0x1fff};
		uint8_t * p_section_start{nullptr};
		uint8_t * p_length{nullptr}; //where section length is stored
		uint8_t* p_es_info_length{nullptr}; //where started last es_info_descripor_loop starts;
		void end_section();
	public:
		section_writer_t(pid_t pid);

		inline void clear() {
			section_data.clear();
			data_range = dtdemux::data_range_t(section_data.buffer(), section_data.capacity());
		}
	};

	class pat_writer_t : public section_writer_t  {
		void start_section(uint16_t service_id, uint16_t pmt_pid);
	public:
		pat_writer_t() :
			section_writer_t(0)
			{}
		inline void add_single_service_pat(ss::bytebuffer_& output, uint16_t service_id, uint16_t pmt_pid) {
			this->clear();
			this->start_section(service_id, pmt_pid);
			this->end_section();
			this->output(output);
		}
	};


	class pmt_writer_t : public section_writer_t  {
		std::tuple<chdb::language_code_t, pid_t, chdb::language_code_t, pid_t>
		make(const pmt_info_t& pmt,
				 chdb::language_code_t selected_audio_lang,
				 chdb::language_code_t selected_subtitle_lang,
				 const ss::vector_<chdb::language_code_t>& audio_prefs,
				 const ss::vector_<chdb::language_code_t>& subtitle_prefs);
	public:
		void start_section(const pmt_info_t& pmt_info);
		void start_es(uint8_t stream_type, uint16_t pid);
		void add_desc(const descriptor_t& desc, const uint8_t* data);
		void add_audio_desc(const pmt_info_t& pmt, const audio_language_info_t& ai);
		void add_subtitle_desc(const pmt_info_t& pmt, const subtitle_info_t& si);

		std::tuple<chdb::language_code_t, pid_t, chdb::language_code_t, pid_t>
		add_preferred_pmt_ts(ss::bytebuffer_& output, pmt_info_t& pmt_info,
												 chdb::language_code_t selected_audio_lang_,
												 chdb::language_code_t selected_subtitle_lang_,
												 const ss::vector_<chdb::language_code_t>& audio_prefs,
												 const ss::vector_<chdb::language_code_t>& subtitle_prefs);

		void end_es();

		pmt_writer_t(pid_t pmt_pid)
			:section_writer_t(pmt_pid)
			{}

	};


};
