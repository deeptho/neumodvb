/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 *
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
#include "util/dtassert.h"
#include "neumodb/recdb/recdb_extra.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace chdb;



recdb::rec_history_mgr_t::rec_history_mgr_t(neumodb_t& db_, int32_t user_id)
	: db(db_) {
	h.user_id = user_id;
}

void recdb::rec_history_mgr_t::init() {
	assert(!inited);
	auto txn = db.rtxn();
	auto c = browse_history_t::find_by_key(txn, h.user_id);
	if (c.is_valid()) {
		h = c.current();
	}
	txn.abort();
	inited = true;
}

void recdb::rec_history_mgr_t::save() {
	using namespace recdb::browse_history;
	auto txn = db.wtxn();
	put_record(txn, h);
	txn.commit();
}

void recdb::rec_history_mgr_t::save(const rec_t& rec) {
	if (h.recordings.size() >= hist_size) {
		std::rotate(h.recordings.begin(), h.recordings.begin() + 1, h.recordings.end());
		h.recordings[hist_size - 1] = rec;
	} else {
		h.recordings.push_back(rec);
	}

	save();
}

std::optional<recdb::rec_t> recdb::rec_history_mgr_t::last_recording() {
	assert(inited);
	if (h.recordings.size() > 0)
		return h.recordings[h.recordings.size() - 1];
	else
		return {};
}

std::optional<recdb::rec_t> recdb::rec_history_mgr_t::prev_recording() {
	assert(inited);
	auto s = h.recordings.size() - 1;
	if (s > 0) {
		std::rotate(h.recordings.begin(), h.recordings.begin() + s - 1, h.recordings.end());
		save();
		return h.recordings[h.recordings.size() - 1];
	} else
		return {};
}

std::optional<recdb::rec_t> recdb::rec_history_mgr_t::next_recording() {
	assert(inited);
	auto s = h.recordings.size() - 1;
	if (s > 0) {
		std::rotate(h.recordings.begin(), h.recordings.begin() + 1, h.recordings.end());
		save();
		return h.recordings[h.recordings.size() - 1];
	} else
		return {};
}

void recdb::rec_history_mgr_t::clear() {
	h.recordings.clear();
	save();
}

std::optional<recdb::rec_t> recdb::rec_history_mgr_t::recall_recording() {
	assert(inited);
	auto s = h.recordings.size();
	if (s > 1) {
		std::swap(h.recordings[s - 1], h.recordings[s - 2]);
		save();
		return h.recordings[s - 1];
	} else
		return {};
}
