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

#pragma once
#include "neumodb/recdb/recdb_db.h"
#include "neumodb/epgdb/epgdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumotime.h"
#include "ssaccu.h"

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

	template<typename T>
	inline void to_str(ss::string_& ret, T& t) {
		ret.sprintf("%p", &t);
	}

	template<typename T> auto to_str(T&& t)
	{
		ss::string<128> s;
		to_str(s, (const T&) t);
		return s;
	}

	std::ostream& operator<<(std::ostream& os, const marker_key_t& k);
	std::ostream& operator<<(std::ostream& os, const marker_t& m);
	std::ostream& operator<<(std::ostream& os, const file_t& f);
	std::ostream& operator<<(std::ostream& os, const rec_fragment_t& f);
	std::ostream& operator<<(std::ostream& os, const rec_t& r);

	void to_str(ss::string_& ret, const marker_key_t& k);

	void to_str(ss::string_& ret, const marker_t& m);

	void to_str(ss::string_& ret, const file_t& f);

	void to_str(ss::string_& ret, const rec_fragment_t& f);

	void to_str(ss::string_& ret, const rec_t& r);

	int32_t make_unique_id(db_txn& txn, autorec_t& autorec);

};

namespace recdb::rec {
	void make_filename(ss::string_& ret, const chdb::service_t& s, const epgdb::epg_record_t& epg);

	std::optional<rec_t> best_matching(db_txn& txn, const epgdb::epg_record_t& epg, bool anonymous);
};
