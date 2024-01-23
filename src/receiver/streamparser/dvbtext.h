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
#include <stdint.h>

// originally from libdtv, Copyright Rolf Hakenes <hakenes@hippomi.de>
/* @brief decodes an SI string into a ss::string dataStructure
	 Returns the c-length of the number of characters written
	 String may still contain the emphasis on/off codes ox86, 0x87
 */
namespace ss {
	struct string_;
};

int decode_text(ss::string_& out, uint8_t* from, int len);
