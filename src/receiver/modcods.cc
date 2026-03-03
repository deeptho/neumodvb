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
#include "modcods.h"

struct modcod_desc_t unknown{-1, "??", "??", "??", "??"};

static const std::array<modcod_desc_t,38> dvbs2_table_0_37{{
    // --- DVB-S2 Legacy (PLS 0-127) ---
		{0,   "S2",  "Dummy",     "N/A",   "Normal"},
		{1,   "S2",  "QPSK",      "1/4",   "Normal"},
		{2,   "S2",  "QPSK",      "1/3",   "Normal"},
    {3,   "S2",  "QPSK",      "2/5",   "Normal"},
    {4,   "S2",  "QPSK",      "1/2",   "Normal"},
    {5,   "S2",  "QPSK",      "3/5",   "Normal"},
    {6,   "S2",  "QPSK",      "2/3",   "Normal"},
    {7,   "S2",  "QPSK",      "3/4",   "Normal"},
    {8,   "S2",  "QPSK",      "4/5",   "Normal"},
    {9,   "S2",  "QPSK",      "5/6",   "Normal"},
    {10,  "S2",  "QPSK",      "8/9",   "Normal"},
    {11,  "S2",  "QPSK",      "9/10",  "Normal"},
    {12,  "S2",  "8PSK",      "3/5",   "Normal"},
    {13,  "S2",  "8PSK",      "2/3",   "Normal"},
    {14,  "S2",  "8PSK",      "3/4",   "Normal"},
    {15,  "S2",  "8PSK",      "5/6",   "Normal"},
    {16,  "S2",  "8PSK",      "8/9",   "Normal"},
    {17,  "S2",  "8PSK",      "9/10",  "Normal"},
    {18,  "S2",  "16APSK",    "2/3",   "Normal"},
    {19,  "S2",  "16APSK",    "3/4",   "Normal"},
    {20,  "S2",  "16APSK",    "4/5",   "Normal"},
    {21,  "S2",  "16APSK",    "5/6",   "Normal"},
    {22,  "S2",  "16APSK",    "8/9",   "Normal"},
    {23,  "S2",  "16APSK",    "9/10",  "Normal"},
    {24,  "S2",  "32APSK",    "3/4",   "Normal"},
    {25,  "S2",  "32APSK",    "4/5",   "Normal"},
    {26,  "S2",  "32APSK",    "5/6",   "Normal"},
    {27,  "S2",  "32APSK",    "8/9",   "Normal"},
    {28,  "S2",  "32APSK",    "9/10",  "Normal"},
		{29,  "S2",  "[29]",    "",  "Normal"},
		{30,  "S2",  "[30]",    "",  "Normal"},
		{31,  "S2",  "[31]",    "",  "Normal"},
		{32,  "S1",  "[QPSK]",    "1/2",  "Normal"},
		{33,  "S1",  "QPSK",   "2/3", "Normal"},
		{34,  "S1",  "QPSK", "3/4", "Normal"},
		{35,  "S1",  "QPSK", "5/6", "Normal"},
		{36,  "S1",  "QPSK", "6/7", "Normal"},
		{37,  "S1",  "QPSK", "7/8", "Normal"}
	}};

static const std::array<modcod_desc_t, (250-132)/2> dvbs2x_table_132_248{{
		// --- DVB-S2X Extensions (PLS 128-255) ---
		// Mapping: S2X MODCOD + 128
		{132, "S2X", "QPSK",      "13/45", "Normal"},
		{134, "S2X", "QPSK",      "9/20", "Normal"},
		{136, "S2X", "QPSK",      "11/20", "Normal"},
		{138, "S2X", "8APSK", "5/9-L", "Normal"},
		{140, "S2X", "8AP  SK", "26/45-L", "Normal"},
		{142, "S2X", "8PSK"     , "23/36", "Normal"},
		{144, "S2X", "8PSK", "25  /36", "Normal"},
		{146, "S2X", "8PSK", "13/18", "Normal"},
		{148, "S2X", "16APSK", "1/2-L", "Normal"},
		{150, "S2X", "16APSK", "8/15-L", "Normal"},
		{152, "S2X", "16APSK", "5/9-L", "Normal"},
		{154, "S2X", "16APSK", "26/45", "Normal"},
		{156, "S2X", "16APSK", "3/5", "Normal"},
		{158, "S2X", "16APSK", "3/5-L", "Normal"},
		{160, "S2X", "16APSK", "28/45", "Normal"},
		{162, "S2X", "16APSK", "23/36", "Normal"},
		{164, "S2X", "16APSK", "2/3-L", "Normal"},
		{166, "S2X", "16APSK", "25/36", "Normal"},
		{168, "S2X", "16APSK", "13/18", "Normal"},
		{170, "S2X", "16APSK", "7/9", "Normal"},
		{172, "S2X", "16APSK", "77/90", "Normal"},
		{174, "S2X", "32APSK", "2/3-L", "Normal"},
		{178, "S2X", "32APSK", "32/45", "Normal"},
		{180, "S2X", "32APSK", "11/15", "Normal"},
		{182, "S2X", "32APSK", "7/9", "Normal"},
		{184, "S2X", "64APSK", "32/45-L", "Normal"},
		{186, "S2X", "64APSK", "11/15", "Normal"},
		{188, "S2X", "[188]", "", "Normal"},
		{190, "S2X", "64APSK", "7/9", "Normal"},
		{192, "S2X", "pi/2 BPSK", "1/4",   "Normal"},
		{194, "S2X", "64APSK", "4/5", "Normal"},
		{196, "S2X", "[196]", "", "Normal"},
		{198, "S2X", "64APSK", "5/6", "Normal"},
		{200, "S2X", 	"128APSK", "3/4", "Normal"},
		{202, "S2X", 	"128APSK", "7/9", "Normal"},
		{204, "S2X", 	"256APSK", "29/45-L", "Normal"},
		{206, "S2X", 	"256APSK", "2/3-L", "Normal"},
		{208, "S2X", 	"256APSK", "31/45-L", "Normal"},
		{210, "S2X", 	"256APSK", "32/45", "Normal"},
		{212, "S2X", 	"256APSK", "11/15-L", "Normal"},
		{214, "S2X", 	"256APSK", "3/4", "Normal"},
		{216, "S2X",  "QPSK", "11/45", "Normal"},
		{218, "S2X",  "QPSK", "4/15", "Normal"},
		{220, "S2X",  "QPSK", "14/45", "Normal"},
		{222, "S2X",  "QPSK", "7/15", "Normal"},
		{224, "S2X",  "QPSK", "8/15", "Normal"},
		{226, "S2X",  "QPSK", "32/45", "Normal"},
		{228, "S2X",  "8PSK", "7/15", "Normal"},
		{230, "S2X",  "8PSK", "8/15", "Normal"},
		{232, "S2X",  "8PSK", "26/45", "Normal"},
		{234, "S2X",  "8PSK", "32/45", "Normal"},
		{236, "S2X",  "16APSK", "7/15", "Normal"},
		{238, "S2X",  "16APSK", "8/15", "Normal"},
		{240, "S2X",  "16APSK", "26/45", "Normal"},
		{242, "S2X",  "16APSK", "3/5", "Normal"},
		{244, "S2X",  "16APSK", "32/45", "Normal"},
		{246, "S2X",  "32APSK", "2/3", "Normal"},
		{248, "S2X",  "32APSK", "32/45", "Normal"}
	}};

const modcod_desc_t* get_modcod_desc(int modcod) {
	switch(modcod) {
	case 0 ... 37:
		return &dvbs2_table_0_37[modcod];
		break;
	case 132 ... 248:
		if((modcod&0x1)==0)
			return &dvbs2x_table_132_248[(modcod-132)>>1];
	default:
		return &unknown;
	}
}
