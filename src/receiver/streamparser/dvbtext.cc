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
#include "dvbtext.h"
#include "freesat.h"
#include "stackstring/stackstring.h"

static const char* CharacterTables1[] = {
	nullptr,			 // 0x00
	"ISO-8859-5",	 // 0x01
	"ISO-8859-6",	 // 0x02
	"ISO-8859-7",	 // 0x03
	"ISO-8859-8",	 // 0x04
	"ISO-8859-9",	 // 0x05
	"ISO-8859-10", // 0x06
	"ISO-8859-11", // 0x07
	"ISO-8859-12", // 0x08
	"ISO-8859-13", // 0x09
	"ISO-8859-14", // 0x0A
	"ISO-8859-15", // 0x0B
	nullptr,			 // 0x0C
	nullptr,			 // 0x0D
	nullptr,			 // 0x0E
	nullptr,			 // 0x0F
	nullptr,			 // 0x10
	"UTF-16",			 // 0x11
	"EUC-KR",			 // 0x12
	"GB2312",			 // 0x13
	"GBK",				 // 0x14
	"UTF-8",			 // 0x15
	nullptr,			 // 0x16
	nullptr,			 // 0x17
	nullptr,			 // 0x18
	nullptr,			 // 0x19
	nullptr,			 // 0x1A
	nullptr,			 // 0x1B
	nullptr,			 // 0x1C
	nullptr,			 // 0x1D
	nullptr,			 // 0x1E
	nullptr,			 // 0x1F
};

#define SingleByteLimit 0x0B

static const char* CharacterTables2[] = {
	nullptr,			 // 0x00
	"ISO-8859-1",	 // 0x01
	"ISO-8859-2",	 // 0x02
	"ISO-8859-3",	 // 0x03
	"ISO-8859-4",	 // 0x04
	"ISO-8859-5",	 // 0x05
	"ISO-8859-6",	 // 0x06
	"ISO-8859-7",	 // 0x07
	"ISO-8859-8",	 // 0x08
	"ISO-8859-9",	 // 0x09
	"ISO-8859-10", // 0x0A
	"ISO-8859-11", // 0x0B
	nullptr,			 // 0x0C
	"ISO-8859-13", // 0x0D
	"ISO-8859-14", // 0x0E
	"ISO-8859-15", // 0x0F
};

#define NumEntries(Table) (sizeof(Table) / sizeof(char*))

/*
	returns character table string, and bool which is true if single byte system
*/
static std::tuple<const char*, bool>
get_char_table_and_size(uint8_t*& buffer, int& length) {
	const char* cs = "ISO6937";
	// Workaround for broadcaster stupidity: according to
	// "ETSI EN 300 468" the default character set is ISO6937. But unfortunately some
	// broadcasters actually use ISO-8859-9, but fail to correctly announce that.
	if (length <= 0)
		return {cs , true};
	unsigned int tag = buffer[0];
	if (tag >= 0x20)
		return {cs, true};			 // DeepThought: default table (table 0) Latin alphabet see en_300468v011101p.pdf
	if (tag == 0x10) { // DeepThought: buffer[1..2] point to a table specified in iso8859 parts 1-9
		// see p. 103
		if (length >= 3) {
			tag = (buffer[1] << 8) | buffer[2];
			if (tag < NumEntries(CharacterTables2) && CharacterTables2[tag]) {
				buffer += 3;
				length -= 3;
				return {CharacterTables2[tag], true};
			}
		}
	} else if (tag < NumEntries(CharacterTables1) && CharacterTables1[tag]) {
		buffer += 1;
		length -= 1;
		return {CharacterTables1[tag], tag <= SingleByteLimit};
	}
	return {cs, false};
}



static 	int translate_dvb_control_characters(uint8_t* to, int size_, bool single_byte_char, bool clean) {
	auto* from = to;
	auto* start = to;
	auto* end = to + size_;
	int len = size_;

	auto convert_char = [clean](uint8_t c) -> int {
		// Handle control codes:
		switch (c) {
		case 0x8A:
			return '\n';
		case 0xA0:
			return ' ';
			break;
		case 0x86:
		case 0x87:
			if (clean) {
				// skip the code
				return -1;
			} else {
				return '*';
			}
			break;
		default:
			break;
		}
		return c;
	};

	if(single_byte_char)
		while (from < end) {
			auto c = *from++;
			if(c=='_') {
				//replace " _" with ","
				if (from-2 >= start && from[-2]==' ') {
					assert(to[-1] == ' ');
					to[-1] = '\''; //overwrite the space
				}
			} else {
				auto ret = convert_char(c);
				if (ret >= 0)
					*to++ = ret;
			}
		}
	else //!single_byte_char
		while (from < end) {
			auto last = *from++;
			if (last == 0xe0) {
				*to++ = 0; //may not be correct!
				if(from <end) {
					auto ret = convert_char(*from++);
					if (ret >= 0)
						*to++ = ret;
				}
			} else {
				*to++ = *from++;
				if(from <end)
					*to++ = *from++;
			}
		}


	int delta = from - to;
	assert(delta >= 0);
	auto newlen = size_ - delta;
	return newlen;
}

//#define PRINTTIME
#ifdef PRINTTIME
static int64_t processing_count;
static int64_t processing_delay;
#include "util/time_util.h"
#include "util/logger.h"
using namespace std::chrono;
#endif


// originally from libdtv, Copyright Rolf Hakenes <hakenes@hippomi.de>
/* @brief decodes an SI string into a ss::string dataStructure
	 Returns the c-length of the number of characters written
	 String may still contain the emphasis on/off codes 0x86, 0x87
*/
int decode_text(ss::string_& out, uint8_t* from, int len) {
	if (len <= 0) {
		return 0;
	}

	// DeepThought:
	// hack for  Al Jamahiriy, which stores extra zero bytes at the end
	// of strings
	while (len >= 1 && from[len - 1] == 0)
		len--;
	if (len < 1) {
		return len;
	}

	if (from[0] == 0x1f) {
#ifdef PRINTTIME
		auto xxx_start = system_clock_t::now();
#endif
		auto ret= freesat_huffman_decode(out, from, len); // TODO? perhaps this one should also use charset conversion?
#ifdef PRINTTIME
		auto now = system_clock_t::now();
		processing_delay +=  std::chrono::duration_cast<std::chrono::microseconds>(now -xxx_start).count();
		processing_count++;
		if(processing_count>0) {
			dtdebug_nicex("PERF: {:f}us per call ({:d}/{:d})",
										processing_delay/(double)processing_count, processing_delay, processing_count);
		}
#endif
		return ret;
	}

	auto [cs, single_byte_char]  = get_char_table_and_size(from, len);
	auto new_len = translate_dvb_control_characters(from, len, single_byte_char, true /*clean*/);
	int ret = out.append_as_utf8((char*)from, new_len, cs);
	return ret;
}
