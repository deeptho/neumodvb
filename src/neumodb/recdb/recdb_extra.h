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
#include "neumodb/recdb/recdb_db.h"
#include "neumodb/epgdb/epgdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumotime.h"
#include "fmt/format.h"

namespace recdb {
	using namespace recdb;


class rec_history_mgr_t {
	bool inited=false;
	constexpr static int hist_size = 8;
	neumodb_t& db;
public:
	recdb::browse_history_t h;

	rec_history_mgr_t(neumodb_t& db_, int32_t user_id=0);
	void init();
	void clear();
	void save();
	void save(const rec_t& recording);
	std::optional<recdb::rec_t> last_recording();
	std::optional<recdb::rec_t> prev_recording();
	std::optional<recdb::rec_t> next_recording();
	std::optional<recdb::rec_t> recall_recording();
};

	int32_t make_unique_id(db_txn& txn, autorec_t& autorec);

	recdb::rec_t new_recording(db_txn& rec_wtxn, const chdb::service_t& service,
														 epgdb::epg_record_t& epgrec, int pre_record_time, int post_record_time);
	recdb::rec_t new_recording(db_txn& rec_wtxn, db_txn& epg_wtxn,
														 const chdb::service_t& service, epgdb::epg_record_t& epgrec,
														 int pre_record_time, int post_record_time);

};

namespace recdb::rec {
	void make_filename(ss::string_& ret, const chdb::service_t& s, const epgdb::epg_record_t& epg);

	std::optional<rec_t> best_matching(db_txn& txn, const epgdb::epg_record_t& epg, bool anonymous);
};


#define declfmt(t)																											\
	template <> struct fmt::formatter<t> {																\
	inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) { \
		return ctx.begin();																									\
	}																																			\
																																				\
	format_context::iterator format(const t&, format_context& ctx) const ;\
}

declfmt(recdb::marker_key_t);
declfmt(recdb::marker_t);
declfmt(recdb::file_t);
declfmt(recdb::rec_fragment_t);
declfmt(recdb::rec_t);

#if 0 // not implemented
declfmt(recdb::live_service_t);
declfmt(recdb::stream_descriptor_t);
declfmt(recdb::autorec_t);
declfmt(recdb::file_key_t);
declfmt(recdb::browse_history_t);
#endif

#undef declfmt
