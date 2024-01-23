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

#include <string.h>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "fmt/core.h"

#define DTPACKED __attribute__((packed))

struct milliseconds_t {
	int64_t ms = 0;

	milliseconds_t() = default;

	explicit milliseconds_t(int64_t ms_) : ms(ms_) {}

	explicit operator int64_t() const {
		return ms;
	}


	milliseconds_t& operator+=(const milliseconds_t& other) {
		ms += other.ms;
		return *this;
	}

	milliseconds_t& operator-=(const milliseconds_t& other) {
		ms -= other.ms;
		return *this;
	}

	milliseconds_t operator+(const milliseconds_t& other) const {
		auto ret = *this;
		ret += other;
		return ret;
	}
	milliseconds_t operator-(const milliseconds_t& other) {
		auto ret = *this;
		ret -= other;
		return ret;
	}


	bool operator==(const milliseconds_t& other) const {
		return other.ms == this->ms;
	}
	bool operator!=(const milliseconds_t& other) const {
		return other.ms != this->ms;
	}
	bool operator<(const milliseconds_t& other) const {
		return this->ms < other.ms;
	}
	bool operator<=(const milliseconds_t& other) const {
		return this->ms <= other.ms;
	}
	bool operator>(const milliseconds_t& other) const {
		return this->ms > other.ms;
	}
	bool operator>=(const milliseconds_t& other) const {
		return this->ms >= other.ms;
	}
};

namespace std {
	template<>
	class numeric_limits<milliseconds_t> {
	public:
		static milliseconds_t max() {
			return milliseconds_t(std::numeric_limits<int64_t>::max());
		}
	};
}

template <> struct fmt::formatter<milliseconds_t> {
	inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) {
		return ctx.begin();
	}
	format_context::iterator format(const milliseconds_t&, format_context& ctx) const ;
};
