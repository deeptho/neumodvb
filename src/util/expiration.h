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
#include <chrono>
#include "stackstring.h"

class expiration_t {
	system_time_t expiration_time;
	bool armed{false};
public:
	inline void start(std::chrono::milliseconds duration = 5s) {
		auto now = system_clock_t::now();
		expiration_time = now + duration;
		armed = true;
	}

	inline bool has_expired_now() {
		auto now = system_clock_t::now();
		bool ret = armed && now >= expiration_time;
		if(ret)
			armed=false;
		return ret;
	}

	inline bool is_armed() {
		return armed && ! has_expired_now();
	}
};
