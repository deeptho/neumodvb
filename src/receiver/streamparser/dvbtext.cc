/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

static const char* getCharacterTable(const uint8_t*& buffer, int& length, bool* isSingleByte) {
	const char* cs = "ISO6937";
	// Workaround for broadcaster stupidity: according to
	// "ETSI EN 300 468" the default character set is ISO6937. But unfortunately some
	// broadcasters actually use ISO-8859-9, but fail to correctly announce that.
	if (isSingleByte)
		*isSingleByte = false;
	if (length <= 0)
		return cs;
	unsigned int tag = buffer[0];
	if (tag >= 0x20)
		return cs;			 // DeepThought: default table (table 0) Latin alphabet see en_300468v011101p.pdf
	if (tag == 0x10) { // DeepThought: buffer[1..2] point to a table specified in iso8859 parts 1-9
		// see p. 103
		if (length >= 3) {
			tag = (buffer[1] << 8) | buffer[2];
			if (tag < NumEntries(CharacterTables2) && CharacterTables2[tag]) {
				buffer += 3;
				length -= 3;
				if (isSingleByte)
					*isSingleByte = true;
				return CharacterTables2[tag];
			}
		}
	} else if (tag < NumEntries(CharacterTables1) && CharacterTables1[tag]) {
		buffer += 1;
		length -= 1;
		if (isSingleByte)
			*isSingleByte = tag <= SingleByteLimit;
		return CharacterTables1[tag];
	}
	return cs;
}

// originally from libdtv, Copyright Rolf Hakenes <hakenes@hippomi.de>
/* @brief decodes an SI string into a ss::string dataStructure
	 Returns the c-length of the number of characters written
	 String may still contain the emphasis on/off codes 0x86, 0x87
*/
int decodeText(ss::string_& out, const uint8_t* from, int len) {
	if (len <= 0) {
		return 0;
	}

	// DeepThought:
	// hack for  Al Jamahiriy 	 which stores extra zero bytes at the end
	// of strings
	while (len >= 1 && from[len - 1] == 0)
		len--;
	if (len < 1) {
		return len;
	}

	if (from[0] == 0x1f)
		return freesat_huffman_decode(out, from, len); // TODO? perhaps this one should also use charset conversion?

	bool singleByte;
	const char* cs = getCharacterTable(from, len, &singleByte);
	int ret = out.append_as_utf8((char*)from, len, cs);
	return ret;
}
