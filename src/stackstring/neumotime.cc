/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "../util/dtassert.h"
#include "../util/dtassert.h"
#include "neumotime.h"
#include "ssaccu.h"
#include "xformat/ioformat.h"
#include <memory.h>
#include <time.h>


std::ostream& operator<<(std::ostream& os, const milliseconds_t& a) {
	int h, m, s, u;
	if (int64_t(a) == -1)
		stdex::printf(os, "end");
	else {
		auto p = (int64_t)a;
		h = (p / (1000L * 60 * 60));
		m = (p / (1000L * 60)) - (h * 60);
		s = (p / (1000L)) - (h * 3600) - (m * 60);
		u = p - (h * 1000L * 60 * 60) - (m * 1000L * 60) - (s * 1000L);
		// str.sprintf("%08lu [%02d:%02d:%02d.%04d]", ull, h, m, s, u);
		stdex::printf(os, "%02d:%02d:%02d.%04d", h, m, s, u);
	}
	return os;
}
