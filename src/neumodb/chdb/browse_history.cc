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

#include "receiver/neumofrontend.h"
#include "neumodb/chdb/chdb_extra.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace chdb;

chdb::history_mgr_t::history_mgr_t(neumodb_t& db_, int32_t user_id)
	: db(db_) {
	h.user_id = user_id;
}

void chdb::history_mgr_t::init() {
	assert(!inited);
	auto txn = db.rtxn();
	auto c = browse_history_t::find_by_key(txn, h.user_id);
	if (c.is_valid()) {
		h = c.current();
	}
	txn.abort();
	inited = true;
}

void chdb::history_mgr_t::save() {
	using namespace chdb::browse_history;
	auto txn = db.wtxn();
	put_record(txn, h);
	txn.commit();
}

void chdb::history_mgr_t::save(const service_t& service) {
	if (h.services.size() >= hist_size) {
		std::rotate(h.services.begin(), h.services.begin() + 1, h.services.end());
		h.services[hist_size - 1] = service;
	} else {
		h.services.push_back(service);
	}

	save();
}

void chdb::history_mgr_t::save(const chgm_t& channel) {
	if (h.chgms.size() >= hist_size) {
		std::rotate(h.chgms.begin(), h.chgms.begin() + 1, h.chgms.end());
		h.chgms[hist_size - 1] = channel;
	} else {
		h.chgms.push_back(channel);
	}

	save();
}

std::optional<chdb::service_t> chdb::history_mgr_t::last_service() {
	assert(inited);
	if (h.services.size() > 0)
		return h.services[h.services.size() - 1];
	else
		return {};
}

std::optional<chdb::chgm_t> chdb::history_mgr_t::last_chgm() {
	assert(inited);
	if (h.chgms.size() > 0)
		return h.chgms[h.chgms.size() - 1];
	else
		return {};
}

std::optional<chdb::service_t> chdb::history_mgr_t::prev_service() {
	assert(inited);
	auto s = h.services.size() - 1;
	if (s > 0) {
		std::rotate(h.services.begin(), h.services.begin() + s - 1, h.services.end());
		save();
		return h.services[h.services.size() - 1];
	} else
		return {};
}

std::optional<chdb::service_t> chdb::history_mgr_t::next_service() {
	assert(inited);
	auto s = h.services.size() - 1;
	if (s > 0) {
		std::rotate(h.services.begin(), h.services.begin() + 1, h.services.end());
		save();
		return h.services[h.services.size() - 1];
	} else
		return {};
}

std::optional<chdb::chgm_t> chdb::history_mgr_t::prev_chgm() {
	assert(inited);
	auto s = h.chgms.size() - 1;
	if (s > 0) {
		std::rotate(h.chgms.begin(), h.chgms.begin() + s - 1, h.chgms.end());
		save();
		return h.chgms[h.chgms.size() - 1];
	} else
		return {};
}

std::optional<chdb::chgm_t> chdb::history_mgr_t::next_chgm() {
	assert(inited);
	auto s = h.chgms.size() - 1;
	if (s > 0) {
		std::rotate(h.chgms.begin(), h.chgms.begin() + 1, h.chgms.end());
		save();
		return h.chgms[h.chgms.size() - 1];
	} else
		return {};
}

void chdb::history_mgr_t::clear() {
	h.services.clear();
	h.chgms.clear();
	save();
}

std::optional<chdb::service_t> chdb::history_mgr_t::recall_service() {
	assert(inited);
	auto s = h.services.size();
	if (s > 1) {
		std::swap(h.services[s - 1], h.services[s - 2]);
		save();
		return h.services[s - 1];
	} else
		return {};
}

std::optional<chdb::chgm_t> chdb::history_mgr_t::recall_chgm() {
	assert(inited);
	auto s = h.chgms.size();
	if (s > 1) {
		std::swap(h.chgms[s - 1], h.chgms[s - 2]);
		save();
		return h.chgms[s - 1];
	} else
		return {};
}
