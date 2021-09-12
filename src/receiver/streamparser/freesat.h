/*
 * freesat_huffman.h
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

namespace ss {
struct string_;
};

extern int freesat_huffman_decode(ss::string_& dst, const uint8_t *src, int srcsize);
