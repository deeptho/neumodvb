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
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <variant>


#include "neumodb/statdb/statdb_db.h"
#include "stackstring/stackstring.h"
#include "neumodb/chdb/chdb_extra.h"

struct spectrum_scan_t;

namespace statdb {
	using namespace statdb;
	void make_spectrum_scan_filename(ss::string_& ret, const statdb::spectrum_t& spectrum);

	std::optional<statdb::spectrum_t>
	make_spectrum(const ss::string_& spectrum_path, const spectrum_scan_t& scan, bool append, int min_freq);

	std::optional<statdb::spectrum_t>
	save_spectrum_scan(const ss::string_& spectrum_path, const spectrum_scan_t& scan, bool append, int min_freq);

	void clean_live(db_txn& wtxn);

}

namespace statdb::signal_stat {
	ss::vector_<signal_stat_t> get_by_mux_fuzzy(
		db_txn& devdb_rtxn, int16_t sat_pos, chdb::fe_polarisation_t pol, int frequency, time_t start_time,
		int tolerance);
}

#ifdef declfmt
#undef declfmt
#endif
#define declfmt(t)																											\
	template <> struct fmt::formatter<t> {																\
	inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) { \
		return ctx.begin();																									\
	}																																			\
																																				\
	format_context::iterator format(const t&, format_context& ctx) const ;\
}

declfmt(statdb::signal_stat_key_t);
declfmt(statdb::signal_stat_entry_t);
declfmt(statdb::signal_stat_t);
declfmt(statdb::spectrum_key_t);
declfmt(statdb::spectrum_t);
