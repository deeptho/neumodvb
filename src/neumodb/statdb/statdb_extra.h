/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <variant>


#include "neumodb/statdb/statdb_db.h"
#include "stackstring/ssaccu.h"
#include "neumodb/chdb/chdb_extra.h"

struct spectrum_scan_t;

namespace statdb {
	using namespace statdb;

	std::ostream& operator<<(std::ostream& os, const signal_stat_t& stat);
	std::ostream& operator<<(std::ostream& os, const spectrum_key_t& spectrum_key);
	std::ostream& operator<<(std::ostream& os, const spectrum_t& spectrum);

	inline void to_str(ss::string_& ret, const signal_stat_t& stat) {
		ret << stat;
	}

	inline void to_str(ss::string_& ret, const spectrum_key_t& spectrum_key) {
		ret << spectrum_key;
	}

	inline void to_str(ss::string_& ret, const spectrum_t& spectrum) {
		ret << spectrum;
	}

	template<typename T>
	inline auto to_str(T&& t)
	{
		ss::string<128> s;
		to_str((ss::string_&)s, (const T&) t);
		return s;
	}

	void make_spectrum_scan_filename(ss::string_& ret, const statdb::spectrum_t& spectrum);

	std::optional<statdb::spectrum_t>
	save_spectrum_scan(const ss::string_& spectrum_path, const spectrum_scan_t& scan, bool append);
}
