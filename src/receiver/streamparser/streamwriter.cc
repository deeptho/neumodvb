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

#include "si.h"
#include "psi.h"
#include "section.h"
#include "streamwriter.h"

using namespace dtdemux;

section_writer_t::section_writer_t()
	: section(data.buffer(), data.capacity())
{}


void section_writer_t::end_section() {
	uint16_t length{0};
	uint32_t crc32{0};

	length = section.current_pointer(0) - p_length - sizeof(length) + sizeof(crc32);
	p_length[0] = (length>>8) | 0x80 | 0x30; //080= section syntax indicator; 0x30 = reserved
	p_length[1] = (length&0xff);

	crc32 = dtdemux::crc32(p_section_start, section.processed() - (p_section_start - data.buffer()));

	section.put<uint32_t>(crc32);
	data.resize_no_init( section.processed());
	section = dtdemux::data_range_t(data.buffer(), section.processed());
}


//read data into a buffer
void ts_writer_t::output(ss::bytebuffer_& output)  {

	while(section.available()>0) {
		int i = output_byte_no % ts_packet_t::size;
		output.reserve(output.size() + ts_packet_t::size);
		if(i < 4 ) {
			uint8_t continuity_counter = output_byte_no/ts_packet_t::size;
			uint8_t pusi = output_byte_no==0 ? 0x40 : 0;
			//PUSI set in first packet, no adaptation field
			uint8_t val[4]={0x47, (uint8_t)(pusi | (pid&0x1fff)>>8), (uint8_t)(pid&0x1fff),
				(uint8_t)(0x010|(continuity_counter%16))};
			auto s = (int)sizeof(val) - i;
			output.append_raw(&val[i], s);
			output_byte_no += s;
		}

		if(output_byte_no == 4) { //first packet
			uint8_t pointer_field{0};
			output.push_back(pointer_field);
			output_byte_no++;
		}

		//remainder of packet
		auto s = std::min((int)section.available(), output.capacity() - output.size());
		if (s>0) {
			s = std::min(ts_packet_t::size- (output_byte_no%ts_packet_t::size), s);
			if(s==0)
				continue; //make next packet
			output.resize_no_init(output.size() + s);
			auto* p = output.buffer() + output.size() - s;
			section.get_buffer(p, s);
			output_byte_no += s;
		}
		if(section.available() == 0) {
			//write trailing stuffing bytes
			auto s = ts_packet_t::size- (output_byte_no%ts_packet_t::size);
			if(s==0)
				break;  //done
			output.resize_no_init(output.size() + s);
			auto* p = output.buffer() + output.size() - s;
			memset(p, 0xff, s);
			output_byte_no += s;
			break;
		}
	}
}

void section_writer_t::save(ss::bytebuffer_& output, uint16_t pid) {
	ts_writer_t w(section, pid);
	w.output(output);
}

void pmt_writer_t::add_audio_desc(const pmt_info_t& pmt, const audio_language_info_t& ai) {
	section.put<uint8_t>(SI::ISO639LanguageDescriptorTag);
	section.put<uint8_t>(4); //descriptor len
	section.put_buffer((const uint8_t*) &ai.lang_code[0], 3);
	section.put<uint8_t>(ai.audio_type);
	if(ai.ac3)
		section.put_buffer(ai.ac3_descriptor_data.buffer(), ai.ac3_descriptor_data.size());
}

void pmt_writer_t::add_subtitle_desc(const pmt_info_t& pmt, const subtitle_info_t& si) {
	section.put<uint8_t>(SI::SubtitlingDescriptorTag);
	section.put<uint8_t>(4); //descriptor len
	section.put_buffer((const uint8_t*) &si.lang_code[0], 3);
	section.put<uint8_t>(si.subtitle_type);
	section.put<uint16_t>(si.composition_page_id);
	section.put<uint16_t>(si.ancillary_page_id);
}


std::tuple<chdb::language_code_t, chdb::language_code_t>
pmt_writer_t::make(const pmt_info_t& pmt, const ss::vector_<chdb::language_code_t>& audio_prefs,
									 const ss::vector_<chdb::language_code_t>& subtitle_prefs) {
	using namespace chdb;
	language_code_t selected_audio_lang;
	language_code_t selected_subtitle_lang;
	start_section(pmt);
	//first add video
	for (const auto& pid_desc : pmt.pid_descriptors) {
		if(stream_type::is_video(stream_type::stream_type_t(pid_desc.stream_type))) {
			start_es((uint8_t)pid_desc.stream_type, pid_desc.stream_pid);
			end_es();
			break;
		}
	}

	{
		auto[pid_desc, alang]  = pmt.best_audio_language(audio_prefs);
		if (pid_desc) {
			selected_audio_lang = alang;
			start_es((uint8_t)pid_desc->stream_type, pid_desc->stream_pid);
			add_audio_desc(pmt, pid_desc->audio_lang); //also adds ac3 if present
			end_es();
		}
	}

	auto [pid_desc, subtit_desc, slang] =pmt.best_subtitle_language(subtitle_prefs);
	if (pid_desc) {
		selected_subtitle_lang = slang;
		assert(subtit_desc);
		start_es((uint8_t)pid_desc->stream_type, pid_desc->stream_pid);
		add_subtitle_desc(pmt, *subtit_desc);
		end_es();
	}
	end_section();
	return {selected_audio_lang, selected_subtitle_lang};
}

void pmt_writer_t::start_section(const pmt_info_t& pmt) {
	section.is_writer = true;
	uint8_t table_id = TABLE_ID_PMT;
	uint16_t length{0};
	uint16_t program_info_length{0};
	uint8_t section_number{0};
	uint8_t last_section_number{0};
	pid = pmt.pmt_pid;
	p_section_start = section.current_pointer(0);
	section.put<uint8_t>(table_id);

	p_length = section.current_pointer(0);
	//glength=29 | ((0x80 | 0x30)<<8); //080= section syntax indicator; 0x30 = reserved;
	section.put<uint16_t>(length);

	section.put<uint16_t>(pmt.service_id); //program number
	section.put<uint8_t>((0x03<<6) | (pmt.version_number<<1) |1);
	section.put<uint8_t>(section_number);
	section.put<uint8_t>(last_section_number);
	section.put<uint16_t>(pmt.pcr_pid|0xe000);
	section.put<uint16_t>(program_info_length | (0xf<<12)); //will be zero

}

void pmt_writer_t::start_es(uint8_t stream_type, uint16_t pid) {
		uint8_t es_info_length{0};
		section.put<uint8_t>((uint8_t)stream_type);
		section.put<uint16_t>(pid|0xe000);
		p_es_info_length = section.current_pointer(0);
		section.put<uint16_t>(es_info_length | 0xf000);
}

void pmt_writer_t::add_desc(const descriptor_t& desc, const uint8_t* data) {
	section.put(desc.tag);
	section.put(desc.len);
	section.put_buffer(data, desc.len);
}

void pmt_writer_t::end_es() {
	uint16_t es_info_length = section.current_pointer(0) - p_es_info_length -2;
	if(! es_info_length)
		return;
	p_es_info_length [0] = ((es_info_length | 0xf000)) >>8;
	p_es_info_length [1] = ((es_info_length | 0xf000)) &0xff;
}


void pat_writer_t::start_section(uint16_t service_id, uint16_t pmt_pid) {
	section.is_writer = true;
	uint8_t table_id = TABLE_ID_PAT;
	uint16_t length{0};
	uint16_t program_info_length{0};
	uint8_t section_number{0};
	uint8_t last_section_number{0};
	uint16_t ts_id{1};
	uint16_t version_number{1};
	p_section_start = section.current_pointer(0);
	section.put<uint8_t>(table_id);

	p_length = section.current_pointer(0);
	//glength=29 | ((0x80 | 0x30)<<8); //080= section syntax indicator; 0x30 = reserved;
	section.put<uint16_t>(length);

	section.put<uint16_t>(ts_id); //program number
	section.put<uint8_t>((0x03<<6) | (version_number<<1) |1);
	section.put<uint8_t>(section_number);
	section.put<uint8_t>(last_section_number);
	section.put<uint16_t>(service_id);
	section.put<uint16_t>(pmt_pid|0xe000);
}


#if 0
pat_writer_t::pat_writer_t(uint16_t service_id, uint16_t pmt_pid, uint16_t ts_id)
	: section_writer_t(0) {
	section.is_writer = true;
	uint8_t table_id = TABLE_ID_PAT;
	uint16_t length{0};  //TODO
	uint8_t version_number{8};
	uint8_t section_number{0};
	uint8_t last_section_number{0};

	auto* p_section_start = section.current_pointer(0);
	section.put<uint8_t>(table_id);

	auto* p_length = section.current_pointer(0);
	section.put<uint16_t>(length);

	section.put<uint16_t>(ts_id); //table_id_extension
	section.put<uint8_t>((0x03<<6) | (version_number<<1) |1);
	section.put<uint8_t>(section_number);
	section.put<uint8_t>(last_section_number);

	section.put<uint16_t>(service_id);
	section.put<uint16_t>(pmt_pid|0xe000);

	uint32_t crc32{0};
	length = section.current_pointer(0) - p_length - sizeof(length) + sizeof(crc32);
	p_length[0] = (length>>8) | 0x80 | 0x30; //080= section syntax indicator; 0x30 = reserved
	p_length[1] = (length&0xff);

	crc32 = dtdemux::crc32(p_section_start, section.processed() - (p_section_start - data.buffer()));
	section.put<uint32_t>(crc32);

	for(int i = section.processed();  i<ts_packet_t::size;++i)
		section.put<uint8_t>(0xff);
	section = dtdemux::data_range_t(data.buffer(), section.processed());


}
#endif
