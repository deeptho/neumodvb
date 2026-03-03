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

struct modcod_desc_t {
	int plsCode;             // Unified 8-bit code (0-127 = S2, 128-255 = S2X)
	const char* standard="??";    // "S2" or "S2X"
	const char*  modulation="??";  // e.g. "QPSK", "16APSK-L"
	const char* code_rate="??";    // e.g. "1/2", "13/45"
	const char* frame_size="??";   // "Normal", "Medium", "Short"
};


EXPORT const modcod_desc_t* get_modcod_desc(int modcod);
