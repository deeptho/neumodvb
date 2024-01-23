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
#include "util/dtassert.h"
#include <iomanip>
#include <iostream>

#include "packetstream.h"
#include "streamtime.h"
#include "fmt/core.h"

using namespace dtdemux;

/*If time between two PCRs is larger than this number, we assume a
	pcr discontinuity. Officially the value is 100ms, but we allow double that
*/
const pcr_t pcr_t::max_pcr_interval = pcr_t(pts_ticks_t(2 * 9000));

namespace dtdemux {
	std::istream& operator>>(std::istream& ss, pts_dts_t& r) {
		std::tm t = {};
		auto pos = ss.tellg();
		ss >> std::get_time(&t, "%H:%M:%S");
		int seconds = 0;
		int ms = 0;
		if (ss.fail()) {
			// restore ss to point where parsing failed
			ss.clear();
			ss.seekg(pos, ss.beg);
		} else {
			seconds = t.tm_sec + 60 * t.tm_min + 3600 * t.tm_hour;
		}
		char c = 0;
		ss >> c;
		if (!ss.fail()) {
			if (c != '.') {
				dterrorf("Bad time format");
				assert(0);
			}
			// throw "bad time format";
			ss >> ms;
			if (ss.fail()) {
				dterrorf("Bad time format");
				assert(0);
			}
		}
		int64_t val = 90 * ms + 90000 * seconds;
		r = pts_dts_t(pts_ticks_t(val));
		ss.clear();
		return ss;
	}

}; // namespace dtdemux

fmt::format_context::iterator
fmt::formatter<dtdemux::pts_dts_t>::format(const dtdemux::pts_dts_t& a, fmt::format_context& ctx) const
{
		uint64_t ull = a.time >> 31;
		int h, m, s, u;
		uint64_t p = ull / 9;
		h = (p / (10000L * 60 * 60));
		m = (p / (10000L * 60)) - (h * 60);
		s = (p / 10000L) - (h * 3600) - (m * 60);
		u = p - (h * 10000L * 60 * 60) - (m * 10000L * 60) - (s * 10000L);
		return fmt::format_to(ctx.out(), "[{:02d}:{:02d}:{:02d}.{:04d}]", h, m, s, u);
}
