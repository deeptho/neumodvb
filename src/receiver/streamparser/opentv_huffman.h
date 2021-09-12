/*
 * freesat_huffman.c
 *
 * Decode a Freesat huffman encoded buffer.
 * Once decoded the buffer can be used like a "standard" DVB buffer.
 *
 * Code originally authored for tv_grab_dvb_plus and subsequently modified
 * to integrate into tvheadend by Adam Sutton <dev@adamsutton.me.uk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <stdint.h>
#include <limits>
#include <algorithm>


class huff_entry_t {
public:
	uint32_t code;
	uint8_t numbits;
	uint8_t data_len;
	const char* data;

	constexpr huff_entry_t() : code(0), numbits(0), data_len(0), data("") {}


	constexpr huff_entry_t(uint32_t code, uint8_t numbits, uint8_t data_len, const char* data) :
		code(code), numbits(numbits), data_len(data_len), data(data) {}


	bool operator<(const huff_entry_t&other) const {
		unsigned int mask=std::numeric_limits<uint32_t>::max();
		mask >>= std::min(numbits,other.numbits);
		mask = ~mask;
		return (code&mask) < (other.code&mask);
	}


};
