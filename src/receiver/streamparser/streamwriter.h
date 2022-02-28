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
	int get_buffer(uint8_t* output_buffer, int size);

	void write(FILE*fp);
};

struct pat_writer_t : public section_writer_t  {

	pat_writer_t(uint16_t service_id = 231, uint16_t pmt_pid = 817, uint16_t ts_id = 48);
};



struct pmt_writer_t : public section_writer_t  {

	pmt_writer_t(	uint16_t service_id = 231, uint16_t pmt_pid = 817,
								uint16_t pcr_pid = 6400);
};
