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

#include "stackstring.h"

#include "packetstream.h"
#include "section.h"
#include "si.h"

struct section_writer_t {
	uint16_t pid{0x1fff};
	ss::bytebuffer<1024> data;

	dtdemux::data_range_t section;
	int output_byte_no{0};

	section_writer_t(uint16_t pid)
		: pid(pid)
		, section(data.buffer(), data.capacity())
		{}


	//read data into a buffer
	int get_buffer(uint8_t* output_buffer, int size) {
		auto * p = output_buffer;

		while(size>0) {
			int i = output_byte_no % 188;
			if(i < 4 ) {
				uint8_t continuity_counter = output_byte_no/188;
				uint8_t pusi = output_byte_no==0 ? 0x40 : 0;
				//PUSI set in first packet, no adaptation field
				uint8_t val[4]={0x47, (uint8_t)(pusi | (pid&0x1fff)>>8), (uint8_t)(pid&0x1fff),
					(uint8_t)(0x010|(continuity_counter%16))};
				auto s = std::min(size, (int)sizeof(val) - i);
				memcpy(p + i, &val[i], s);
				output_byte_no += s;
				p +=  s;
				size -= s;
				assert(size>=0);
			}
			if(size == 0)
				break;

			if(output_byte_no == 4) { //first packet
				uint8_t pointer_field{0};
				*p++ = pointer_field;
				output_byte_no++;
				size--;
				assert(size>=0);
			}

			if(size==0)
				break;

			//remainder of packet
			auto s = std::min((int)section.available(), size);
			if (s>0) {
				s = std::min(188- (output_byte_no%188), s);
				if(s==0)
					continue; //make next packet
				section.get_buffer(p, s);
				p += s;
				output_byte_no += s;
				size -= s;
				assert(size>=0);
			}
			if(section.available() == 0) {
				//write trailing stuffing bytes
				auto s = 188- (output_byte_no%188);
				if(s==0)
					break;  //done
				memset(p, 0xff, s);
				size -= s;
				output_byte_no += s;
				p += s;
				break;
			}
		if (size==0)
			break;
		}
		return p - output_buffer;
	}

	void write(FILE*fp) {
		uint8_t buffer[1024];
		int written = sizeof(buffer);
		while(written >= (int)sizeof(buffer)) {
			written = get_buffer(buffer, sizeof(buffer));
			fwrite(buffer, written, 1,  fp);
		}
	}
};

struct pat_writer_t : public section_writer_t  {

	pat_writer_t(uint16_t service_id = 231, uint16_t pmt_pid = 817,
							 uint16_t ts_id = 48)
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
		printf("len=%ld\n", section.processed());

		crc32 = dtdemux::crc32(p_section_start, section.processed() - (p_section_start - data.buffer()));
		printf("CRC=0x%x\n",  crc32);
		section.put<uint32_t>(crc32);

		for(int i = section.processed();  i<188;++i)
			section.put<uint8_t>(0xff);
		section = dtdemux::data_range_t(data.buffer(), section.processed());


	}

};



struct pmt_writer_t : public section_writer_t  {

	pmt_writer_t(	uint16_t service_id = 231, uint16_t pmt_pid = 817,
								uint16_t pcr_pid = 6400)
		: section_writer_t(pmt_pid) {
		section.is_writer = true;

		uint8_t table_id = TABLE_ID_PMT;
		uint16_t length{0};
		uint16_t program_info_length{0};
		uint8_t section_number{0};
		uint8_t last_section_number{0};
		uint8_t version_number{4};

		auto* p_section_start = section.current_pointer(0);
		section.put<uint8_t>(table_id);

		auto* p_length = section.current_pointer(0);
		//glength=29 | ((0x80 | 0x30)<<8); //080= section syntax indicator; 0x30 = reserved;
		section.put<uint16_t>(length);

		section.put<uint16_t>(service_id); //program number
		section.put<uint8_t>((0x03<<6) | (version_number<<1) |1);
		section.put<uint8_t>(section_number);
		section.put<uint8_t>(last_section_number);
		section.put<uint16_t>(pcr_pid|0xe000);

		//auto* p_program_info_length = section.current_pointer(0);
		section.put<uint16_t>(program_info_length | (0xf<<12)); //will be zero


	//stream type loop

		{
			uint8_t stream_type{27}; //video
			uint16_t elementary_pid{6400};
			uint8_t es_info_length{0};
			section.put<uint8_t>(stream_type);
			section.put<uint16_t>(elementary_pid|0xe000);
			section.put<uint16_t>(es_info_length | 0xf000);
		}

		for(int  i=0; i<64;++i)
		{
			uint8_t stream_type{4};
			uint16_t elementary_pid{6401};
			elementary_pid +=i;
			uint8_t es_info_length{0};
			uint8_t tag{SI::ISO639LanguageDescriptorTag};
			uint8_t desc_len{4};
			uint8_t audio_type{0};
			section.put<uint8_t>(stream_type);
			section.put<uint16_t>(elementary_pid|0xe000);
			auto* p_es_info_length = section.current_pointer(0);
			section.put<uint16_t>(es_info_length | 0xf000);
			//ISO_639_language_descriptor()
			section.put<uint8_t>(tag);
			section.put<uint8_t>(desc_len);
			section.put<uint8_t>('p');
			section.put<uint8_t>('o');
			section.put<uint8_t>('r');
			section.put<uint8_t>(audio_type);
			es_info_length = section.current_pointer(0) - p_es_info_length -2;
			printf("l=%d\n", es_info_length);
			assert(es_info_length == 6);
			p_es_info_length [0] = ((es_info_length | 0xf000)) >>8;
			p_es_info_length [1] = ((es_info_length | 0xf000)) &0xff;
		}

	uint32_t crc32{0};
#if 1
	length = section.current_pointer(0) - p_length - sizeof(length) + sizeof(crc32);
	p_length[0] = (length>>8) | 0x80 | 0x30; //080= section syntax indicator; 0x30 = reserved
	p_length[1] = (length&0xff);
	printf("len=%ld\n", section.processed());
#endif
	crc32 = dtdemux::crc32(p_section_start, section.processed() - (p_section_start - data.buffer()));
#if 1
	printf("CRC=0x%x\n",  crc32);
	section.put<uint32_t>(crc32);
	//assert (section.processed() <= 188);
#endif
	for(int i = section.processed();  i<188;++i)
		section.put<uint8_t>(0xff);
	section = dtdemux::data_range_t(data.buffer(), section.processed());
	}
};
