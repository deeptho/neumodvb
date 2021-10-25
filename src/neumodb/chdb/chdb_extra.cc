/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

#include "neumodb/chdb/chdb_extra.h"
#include "linux/dvb/frontend.h"
#include "stackstring/ssaccu.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace chdb;

extern const char* lang_name(int8_t lang1, int8_t lang2, int8_t lang3);
const char* chdb::lang_name(const chdb::language_code_t& code) {
	return ::lang_name(code.lang1, code.lang2, code.lang3);
}

bool chdb::tuning_parameters_match(const chdb::dvbs_mux_t& a, const chdb::dvbs_mux_t& b) {
	if (a.pol != b.pol || a.stream_id != b.stream_id)
		return false;
	auto tolerance = ((int)std::min(a.symbol_rate, a.symbol_rate)) / (2 * 1350);
	return std::abs((int)a.frequency - (int)a.frequency) < tolerance;
}

bool chdb::tuning_parameters_match(const chdb::dvbc_mux_t& a, const chdb::dvbc_mux_t& b) {
	if (a.stream_id != b.stream_id)
		return false;
	auto tolerance = 2500; // kHz
	return std::abs((int)a.frequency - (int)a.frequency) < tolerance;
}

bool chdb::tuning_parameters_match(const chdb::dvbt_mux_t& a, const chdb::dvbt_mux_t& b) {
	if (a.stream_id != b.stream_id)
		return false;
	auto tolerance = 2500; // kHz
	return std::abs((int)a.frequency - (int)a.frequency) < tolerance;
}

bool chdb::tuning_parameters_match(const chdb::any_mux_t& a, const chdb::any_mux_t& b) {
	using namespace chdb;
	{
		auto* a_ = std::get_if<dvbs_mux_t>(&a);
		auto* b_ = std::get_if<dvbs_mux_t>(&b);
		if (a_ && b_)
			return chdb::tuning_parameters_match(*a_, *b_);
	}
	{
		auto* a_ = std::get_if<dvbc_mux_t>(&a);
		auto* b_ = std::get_if<dvbc_mux_t>(&b);
		if (a_ && b_)
			return chdb::tuning_parameters_match(*a_, *b_);
	}
	{
		auto* a_ = std::get_if<dvbt_mux_t>(&a);
		auto* b_ = std::get_if<dvbt_mux_t>(&b);
		if (a_ && b_)
			return chdb::tuning_parameters_match(*a_, *b_);
	}
	assert(0);
	return false;
}

const mux_key_t* chdb::mux_key_ptr(const chdb::any_mux_t& key) {
	{
		auto* p = std::get_if<chdb::dvbs_mux_t>(&key);
		if (p)
			return &p->k;
	}
	{
		auto* p = std::get_if<chdb::dvbt_mux_t>(&key);
		if (p)
			return &p->k;
	}
	{
		auto* p = std::get_if<chdb::dvbc_mux_t>(&key);
		if (p)
			return &p->k;
	}
	return nullptr;
}

mux_key_t* chdb::mux_key_ptr(chdb::any_mux_t& key) {
	return const_cast<mux_key_t*>(mux_key_ptr(static_cast<const chdb::any_mux_t&>(key)));
}

const mux_common_t* chdb::mux_common_ptr(const chdb::any_mux_t& key) {
	{
		auto* p = std::get_if<chdb::dvbs_mux_t>(&key);
		if (p)
			return &p->c;
	}
	{
		auto* p = std::get_if<chdb::dvbt_mux_t>(&key);
		if (p)
			return &p->c;
	}
	{
		auto* p = std::get_if<chdb::dvbc_mux_t>(&key);
		if (p)
			return &p->c;
	}
	return nullptr;
}

mux_common_t* chdb::mux_common_ptr(chdb::any_mux_t& key) {
	return const_cast<mux_common_t*>(mux_common_ptr(static_cast<const chdb::any_mux_t&>(key)));
}

/*!
	return all sats in use by muxes
*/
ss::vector_<int16_t> chdb::dvbs_mux::list_distinct_sats(db_txn& txn) {
	ss::vector_<int16_t> data;
	auto c = find_first_sorted_by_sat_freq_pol(txn);
	if (c.is_valid()) {
		auto last_sat_pos = c.current().k.sat_pos;
		data.push_back(last_sat_pos);
		for (auto const& x : c.range()) {
			if (x.k.sat_pos != last_sat_pos) {
				last_sat_pos = x.k.sat_pos;
				data.push_back(last_sat_pos);
			}
		}
	}
	return data;
}

/*find a matching mux, based on sat_pos, ts_id, network_id, ignoring  extra_id
	This is called by the SDT_other parsing code and will not work if multiple
	muxes exist on the same sat with the same (network_id, ts_id)
*/
template <typename mux_t>
static find_by_mux_key_fuzzy_ret_t find_by_mux_key_fuzzy_(db_txn& txn, const mux_key_t& mux_key) {
	using namespace chdb;
	auto c = mux_t::find_by_key(txn, mux_key.sat_pos, mux_key.network_id, mux_key.ts_id, mux_key.t2mi_pid, find_geq,
															mux_t::partial_keys_t::sat_pos_network_id_ts_id_t2mi_pid);
	if (!c.is_valid()) {
		c.close();
		return {};
	}
	find_by_mux_key_fuzzy_ret_t ret;
	for (auto const& cmux : c.range()) {
		/*There could be multiple muxes with the same sat_pos, network_id and ts_id.
			Therefore we count them
		*/
		if (!ret.num_matches)
			ret.mux = cmux;
		ret.num_matches++;
	}

	c.close();
	return ret;
}

find_by_mux_key_fuzzy_ret_t chdb::find_by_mux_key_fuzzy(db_txn& txn, const mux_key_t& mux_key) {
	switch (mux_key.sat_pos) {
	case sat_pos_dvbt:
		return find_by_mux_key_fuzzy_<dvbt_mux_t>(txn, mux_key);
	case sat_pos_dvbc:
		return find_by_mux_key_fuzzy_<dvbc_mux_t>(txn, mux_key);
	default:
		return find_by_mux_key_fuzzy_<dvbs_mux_t>(txn, mux_key);
	}
}

/*find a matching mux, disregarding extra_id,
	pick the one with the closest frequency, and correct polarisation and stream_id
*/
db_tcursor<chdb::dvbs_mux_t> chdb::find_by_mux_fuzzy(db_txn& txn, const dvbs_mux_t& mux) {
	using namespace chdb;
	auto c = dvbs_mux_t::find_by_key(txn, mux.k.sat_pos, mux.k.network_id, mux.k.ts_id, mux.k.t2mi_pid, find_geq,
																	 dvbs_mux_t::partial_keys_t::sat_pos_network_id_ts_id_t2mi_pid);
	if (!c.is_valid()) {
		c.close();
		return c;
	}
	/*There could be multiple muxes with the same sat_pos, network_id and ts_id.
		Therefore also check the frequency

		@todo: return the mux with the closest frequency instead of returning the first one which
		is close enough. The problem is how to remember a cursor for the best matching position.
		This requires lmdb::cursor to be made copyable
	*/
	for (auto const& cmux : c.range()) {
		//@todo double check that searching starts at a closeby frequency
		assert(cmux.k.sat_pos == mux.k.sat_pos && cmux.k.t2mi_pid == mux.k.t2mi_pid &&
					 cmux.k.network_id == mux.k.network_id && cmux.k.ts_id == mux.k.ts_id);
		if (tuning_parameters_match(mux, cmux))
			return c;
	}
	c.close();
	return c;
}

/*
	find a mux with matching sat_pos, network_id, ts_id and the closest frequency
	extra_id is ignored
*/
template <typename mux_t> db_tcursor<mux_t> chdb::find_by_mux_fuzzy(db_txn& txn, const mux_t& mux) {
	using namespace chdb;
	auto c = mux_t::find_by_key(txn, mux.k.sat_pos, mux.k.network_id, mux.k.ts_id, mux.k.t2mi_pid, find_geq,
															mux_t::partial_keys_t::sat_pos_network_id_ts_id_t2mi_pid);
	if (!c.is_valid()) {
		c.close();
		return c;
	}
	/*@todo
		Return the mux with the closest frequency instead of returning the first one which
		is close enough. The problem is how to remember a cursor for the best matching position.
		This requires lmdb::cursor to be made copyable
	*/
	for (auto const& cmux : c.range()) {
		//@todo double check that searching starts at a closeby frequency
		assert(cmux.k.sat_pos == mux.k.sat_pos && cmux.k.t2mi_pid == mux.k.t2mi_pid &&
					 cmux.k.network_id == mux.k.network_id && cmux.k.ts_id == mux.k.ts_id);
		if (tuning_parameters_match(mux, cmux))
			return c;
	}
	c.close();
	return c;
}

db_tcursor<chdb::dvbs_mux_t> chdb::dvbs_mux::find_by_sat_t2mi_pid_nid_tid(db_txn& txn, int16_t sat_pos,
																																					uint16_t t2mi_pid, uint16_t network_id,
																																					uint16_t ts_id) {
	chdb::mux_key_t k;
	k.sat_pos = sat_pos;
	k.network_id = network_id;
	k.t2mi_pid = t2mi_pid;
	k.ts_id = ts_id;
	k.extra_id = 0;																					//
	auto c = chdb::dvbs_mux_t::find_by_k(txn, k, find_geq); // find_geq ensures that any nonzero extra_id is also found
	if (c.is_valid()) {
		const auto& mux = c.current();
		if (k.sat_pos != mux.k.sat_pos || k.t2mi_pid != mux.k.t2mi_pid || k.network_id != network_id || k.ts_id != ts_id)
			c.close();
	}
	return c;
}

template <typename mux_t> inline static auto find_fuzzy_(db_txn& txn, const mux_t& mux) {
	return find_by_freq_fuzzy<mux_t>(txn, mux.frequency);
}

inline static auto find_fuzzy_(db_txn& txn, const chdb::dvbs_mux_t& mux) {
	return chdb::find_by_sat_freq_pol_fuzzy(txn, mux.k.sat_pos, mux.frequency, mux.pol, mux.k.t2mi_pid, mux.stream_id);
}

template <typename cursor_t> static int16_t make_unique_id(db_txn& txn, lnb_key_t key, cursor_t& c) {
	key.lnb_id = 0;
	int gap_start = 1; // start of a potential gap of unused extra_ids
	for (const auto& lnb : c.range()) {
		if (lnb.k.lnb_id > gap_start) {
			/*we reached the first matching mux; assign next available lower  value to lnb.k.lnb_id
				this can only fail if about 65535 lnbs
				In that case the loop continues, just in case some of these  65535 muxes have been deleted in the mean time,
				which has left gaps in numbering */
			// easy case: just assign the next lower id
			return lnb.k.lnb_id - 1;
		} else {
			// check for a gap in the numbers
			gap_start = lnb.k.lnb_id + 1;
			assert(gap_start > 0);
		}
	}

	if (gap_start >= std::numeric_limits<decltype(key.lnb_id)>::max()) {
		// all ids exhausted
		// The following is very unlikely. We prefer to cause a result on a
		// single mux rather than throwing an error
		dterror("Overflow for extra_id");
		assert(0);
	}

	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.lnb_id)>::max(); // highest possible value
}

int16_t chdb::make_unique_id(db_txn& txn, lnb_key_t key) {
	key.lnb_id = 0;
	auto c = chdb::lnb_t::find_by_k(txn, key, find_geq);
	return ::make_unique_id(txn, key, c);
}

template <typename cursor_t> static uint16_t make_unique_id(db_txn& txn, mux_key_t key, cursor_t& c) {
	key.extra_id = 0;
	int gap_start = 1; // start of a potential gap of unused extra_ids
	for (const auto& mux : c.range()) {
		if (mux.k.sat_pos != key.sat_pos || mux.k.t2mi_pid != key.t2mi_pid || mux.k.network_id != key.network_id ||
				mux.k.ts_id != key.ts_id) {
			break; // no more matching muxes
		}
		if (is_template(mux) && mux.k.extra_id == 0) {
			// templates with unset extra_id
			assert(0);
			continue;
		} else if (mux.k.extra_id > gap_start) {
			/*we reached the first matching mux; assign next available lower  value to mux.k.extra_id
				this can only fail if about 65535 muxes with the same sat_pos, network_id and ts_id exist;
				In that case the loop continues, just in case some of these  65535 muxes have been deleted in the mean time,
				which has left gaps in numbering */
			// easy case: just assign the next lower id
			return mux.k.extra_id - 1;
		} else {
			// check for a gap in the numbers
			gap_start = mux.k.extra_id + 1;
			assert(gap_start > 0);
		}
	}

	if (gap_start >= std::numeric_limits<decltype(key.extra_id)>::max()) {
		// all ids exhausted
		// The following is very unlikely. We prefer to cause a result on a
		// single mux rather than throwing an error
		dterror("Overflow for extra_id");
		assert(0);
	}

	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.extra_id)>::max(); // highest possible value
}

template <typename mux_t> uint16_t chdb::make_unique_id(db_txn& txn, mux_key_t key) {
	key.extra_id = 0;
	auto c = mux_t::find_by_k(txn, key, find_geq);
	return ::make_unique_id(txn, key, c);
}

int32_t chdb::make_unique_id(db_txn& txn, chg_key_t key) {
	auto c = chg_t::find_by_k(txn, key, find_geq);
	int gap_start = 1; // start of a potential gap of unused extra_ids
	for (const auto& chg : c.range()) {
		if (chg.k.bouquet_id > gap_start) {
			return chg.k.bouquet_id - 1;
		} else {
			// check for a gap in the numbers
			gap_start = chg.k.bouquet_id + 1;
			assert(gap_start > 0);
		}
	}

	if (gap_start >= std::numeric_limits<decltype(key.bouquet_id)>::max()) {
		// all ids exhausted
		// The following is very unlikely. We prefer to cause a result on a
		// single mux rather than throwing an error
		dterror("Overflow for bouquet_id");
		assert(0);
	}
	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.bouquet_id)>::max(); // highest possible value
}

int32_t chdb::make_unique_id(db_txn& txn, chgm_key_t key) {
	auto c = chgm_t::find_by_k(txn, key, find_geq);
	int gap_start = 1; // start of a potential gap of unused extra_ids
	for (const auto& chgm : c.range()) {
		if (chgm.k.channel_id > gap_start) {
			return chgm.k.channel_id - 1;
		} else {
			// check for a gap in the numbers
			gap_start = chgm.k.channel_id + 1;
			assert(gap_start > 0);
		}
	}

	if (gap_start >= std::numeric_limits<decltype(key.channel_id)>::max()) {
		// all ids exhausted
		// The following is very unlikely. We prefer to cause a result on a
		// single mux rather than throwing an error
		dterror("Overflow for chgm_id");
		assert(0);
	}
	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.channel_id)>::max(); // highest possible value
}

/*! Check for duplicate sat_pos, but matching freq/pol/ts_id/network_id
	Then replaces any matching mux with the new one

	We only ever update sat_pos, even though frequency may have slightly changed
	and other fields (num_services, tuning parameters...) could also have changed

	The reasoning is that our caller (active_si_stream.cc) will probably only call us if a changed sat_pos was detected.
	IN this case the caller will have already inserted a record with the correct position; in that case,
	the code below will delete other records wit closely matching frequency (possibly leaving
	orphaned services! @todo: implement some cleaning strategy, perhaps as housekeeping)

	If caller wishes to overwrite other data, it should call update_mux afterwards

*/
bool chdb::update_mux_sat_pos(db_txn& txn, chdb::dvbs_mux_t& mux) {
	using namespace chdb;
	auto c = dvbs_mux_t::find_by_network_id_ts_id(txn, mux.k.network_id, mux.k.ts_id, find_type_t::find_eq,
																								dvbs_mux_t::partial_keys_t::network_id_ts_id);
	auto c1 = c.clone();
	int best_match = sat_pos_none;
	auto best_delta = std::numeric_limits<int>::max();
	const bool check_sat_pos = true;

	for (const auto& m : c.range()) {
		if (!matches_physical_fuzzy(mux, m, check_sat_pos))
			continue;
		auto delta = std::abs(m.k.sat_pos - mux.k.sat_pos);
		if (delta < best_delta) {
			best_delta = delta;
			best_match = m.k.sat_pos;
		}
	}
	c.close();

	bool updated = false;
	if (best_match != sat_pos_none)
		for (auto m : c1.range()) {
			if (!matches_physical_fuzzy(mux, m, check_sat_pos))
				continue;
			if (m.k.sat_pos == best_match) {
				if (mux.k.sat_pos != m.k.sat_pos) {
					m.k.sat_pos = mux.k.sat_pos;
					put_record(txn, mux);
					updated = true;
				}
			} else {
				delete_record(txn, m);
			}
		}
	c1.close();
	return updated;
}

/*! Put a mux record, taking into account that its key may have changed
	returns: true if this is a new mux (first time scanned); false otherwise
	Also, updates the mux, specifically k.extra_id
*/
template <typename mux_t>
update_mux_ret_t chdb::update_mux(db_txn& txn, mux_t& mux, time_t now, update_mux_preserve_t::flags preserve) {
	assert(!mux.c.is_template);
	update_mux_ret_t ret = update_mux_ret_t::UNKNOWN;
	mux_t db_mux;
	/* The following cases need to be handled
		 a) the tp does not yet exist for this network_id, ts_id, and there is also no tp with matching freq/pol
		 => insert new mux in the db; ensure unique extra_id
		 b) tp with (more or less) correct frequency and polarisation exists but has a different network_id or ts_id
		 with network_id and ts_id !=0
		 => delete the old mux, which is clearly invalid now;  and insert the new mux with unique extra_id  (as in case a)
		 c) a tp with (more or less) correct frequency and polarisation exists but has a network_id=0 and ts_id =0
		 ts_id (0, 0). The latter occurs when seeding the database.
		 => delete the old template mux and set extra_id=0 (as in case a)
		 d) tp with (more or less) correct frequency and polarisation exists with the correct network_id, ts_id
		 => update the mux, and preserve extra_id
		 e) a tp exists with a very different frequency or a different polarisation and with the correct network_id, ts_id
		 This could be a moved tp, but it also happens that some sats have transponders with duplicate
		 network_id, ts_id. This happens because providers have no official network_id (e.g., muxes not for
		 the general public). It also happens sometimes during transponder switchovers.
		 It is difficult to decide if we update the old tp with the new frequency, or rather treat this
		 as a new tp (e.g., two active but different transponders, two active transponders carrying the same mux,
		 or: the old tp is no longer active and was replaced by a new tp)
		 => the current compromise is to treat it as a new mux, so we have to select an extra_id different
		 such that (sat_pos, network_id, ts_id, extra_id) differs from the existing old mux
	*/
	namespace m = update_mux_preserve_t;
	auto cp_scan = [&](mux_t& mux, mux_t& db_mux) {
		if (preserve & m::SCAN_DATA) {
			mux.c.scan_status = db_mux.c.scan_status;
			mux.c.scan_result = db_mux.c.scan_result;
			mux.c.scan_duration = db_mux.c.scan_duration;
			mux.c.scan_time = db_mux.c.scan_time;
			// mux.c.num_services = db_mux.c.num_services;
			mux.c.epg_scan = db_mux.c.epg_scan;
			// mux.c.is_template = false;
		}
		if (preserve & m::NUM_SERVICES)
			mux.c.num_services = db_mux.c.num_services;
		if (preserve & m::EPG_TYPES)
			mux.c.epg_types = db_mux.c.epg_types;
	};

	/*Look for a tp with correct network_id, ts_id, polarisation and stream_id and the closest matching frequency
		but without requiring a matching extra_id*/
	auto c = chdb::find_by_mux_fuzzy(txn, mux);
	bool is_new = true; // do we modify an existing record or create a new one?
	if (c.is_valid()) {
		// case d)
		/*
			The database contains a mux with similar frequency, correct pol and stream_id and the same ts_id, network_id
		*/
		db_mux = c.current();
		mux.k.extra_id = db_mux.k.extra_id; // preserve existing extra_id in the database
		mux.c.is_template = false;
		assert(mux.k.network_id == db_mux.k.network_id);
		assert(mux.k.ts_id == db_mux.k.ts_id);
		assert(mux.k.t2mi_pid == db_mux.k.t2mi_pid);
		cp_scan(mux, db_mux);
		is_new = false;
		ret = update_mux_ret_t::MATCHING_SI_AND_FREQ;
	} else {
		/*find tps with matching frequency, polarisation and stream_id but probably incorrect network_id/ts_id
		 */
		auto c = find_fuzzy_(txn, mux);
		if (c.is_valid()) {
			/*
				We are going to overwrite a mux with similar frequency but different ts_id, network_id.
				If one of the muxes is a template and the other not, the non-template data
				gets priority.
			*/

			// we need to delete the old tp, which has a different ts_id. This tp can no longer
			// exist (because it would be at the same frequency);
			// The old tp can also be a template, but then this template is no longer needed
			// In both cases we need to remove the old tp
			db_mux = c.current();
			ret = update_mux_ret_t::MATCHING_FREQ;
			if (is_template(db_mux)) {
				is_new = true; // replace template with new record
				chdb::delete_record(c.maincursor, db_mux);
			} else {
				assert(db_mux.k.t2mi_pid == mux.k.t2mi_pid);
				is_new = false;
				if (is_template(mux) && (preserve & m::MUX_KEY)) {
					mux.k = db_mux.k; // dbmux key is more likely to be correct
				} else {

					if (preserve & m::MUX_KEY)
						return update_mux_ret_t::NO_MATCHING_KEY;
					else {
						dtdebug("deleting db_mux " << db_mux);
						chdb::delete_record(c.maincursor, db_mux);
					}
					// mux and dbmux are not templates. Keep the newest data
				}
			}

			dtdebugx("Transponder %s: nid=%d => %d ts_id=%d => %d", to_str(mux).c_str(), db_mux.k.network_id,
							 mux.k.network_id, db_mux.k.ts_id, mux.k.ts_id);
		} else {
			ret = update_mux_ret_t::NEW;
		}
		// It is possible that another tp exists with the same ts_id at an other very different frequency.
		// Therefore we need to generate a unique extra_id
		mux.k.extra_id = make_unique_id<mux_t>(txn, mux.k);
	}

	assert(ret != update_mux_ret_t::UNKNOWN);
	// the database has a mux, but we may need to update it

	if (!is_new && chdb::is_same(db_mux, mux))
		ret = update_mux_ret_t::EQUAL;

	if (is_new || ret != update_mux_ret_t::EQUAL) {
		/*We found match, either based on key or on frequency
			if the match was based on frequency, an existing mux may be overwritten!
		*/
		mux.c.mtime = now;
		assert(mux.k.extra_id != 0);
		dtdebugx("NIT %s: old=%s  new=%s #s=%d", is_new ? "NEW" : "CHANGED", to_str(db_mux).c_str(), to_str(mux).c_str(),
						 mux.c.num_services);
		assert(mux.frequency > 0);
		put_record(txn, mux);
	}
	return ret;
}

update_mux_ret_t chdb::update_mux(db_txn& txn, chdb::any_mux_t& mux, time_t now,
																	update_mux_preserve_t::flags preserve) {
	using namespace chdb;
	update_mux_ret_t ret;
	visit_variant(
		mux, [&](chdb::dvbs_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve); },
		[&](chdb::dvbc_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve); },
		[&](chdb::dvbt_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve); });
	return ret;
}

std::optional<chdb::any_mux_t> chdb::find_mux_by_key(db_txn& txn, const chdb::mux_key_t& mux_key) {
	{
		auto c = chdb::dvbs_mux_t::find_by_key(txn, mux_key);
		if (c.is_valid())
			return chdb::any_mux_t(c.current());
	}
	{
		auto c = chdb::dvbt_mux_t::find_by_key(txn, mux_key);
		if (c.is_valid())
			return chdb::any_mux_t(c.current());
	}
	{
		auto c = chdb::dvbc_mux_t::find_by_key(txn, mux_key);
		if (c.is_valid())
			return chdb::any_mux_t(c.current());
	}
	return {};
}

/*
	find a mux based on network_id, ts_id; returns the first match and the number of matches
*/
find_by_mux_key_fuzzy_ret_t chdb::find_by_network_id_ts_id(db_txn& txn, uint16_t network_id, uint16_t ts_id) {
	find_by_mux_key_fuzzy_ret_t ret;
	{
		auto c = chdb::dvbs_mux_t::find_by_network_id_ts_id(txn, network_id, ts_id, find_type_t::find_eq,
																												dvbs_mux_t::partial_keys_t::network_id_ts_id);
		for (auto const& cmux : c.range()) {
			/*There could be multiple muxes with the same sat_pos, network_id and ts_id.
				Therefore we count them
			*/
			if (!ret.num_matches)
				ret.mux = cmux;
			ret.num_matches++;
		}
		c.close();
		return ret;
	}
	{
		chdb::mux_key_t mux_key(sat_pos_dvbt, network_id, ts_id, 0, 0);
		auto c = chdb::dvbt_mux_t::find_by_key(txn, mux_key);
		if (c.is_valid()) {
			ret.mux = c.current();
			ret.num_matches = 1;
		}
		c.close();
		return ret;
	}
	{
		chdb::mux_key_t mux_key(sat_pos_dvbc, 0, network_id, ts_id, 0);
		auto c = chdb::dvbc_mux_t::find_by_key(txn, mux_key);
		if (c.is_valid()) {
			ret.mux = c.current();
			ret.num_matches = 1;
		}
		c.close();
		return ret;
	}
	return ret;
}


/*
	find a mux based on network_id, ts_id and approximate sat_pos; returns the best match
*/
std::optional<chdb::dvbs_mux_t> chdb::find_by_fuzzy_sat_pos_network_id_ts_id(db_txn& txn, int16_t sat_pos, uint16_t network_id, uint16_t ts_id) {
	auto c = chdb::dvbs_mux_t::find_by_network_id_ts_id(txn, network_id, ts_id, find_type_t::find_eq,
																											dvbs_mux_t::partial_keys_t::network_id_ts_id);
	std::optional<chdb::dvbs_mux_t> ret;
	int best_delta = std::numeric_limits<int>::max();
	for (auto const& cmux : c.range()) {
		/*There could be multiple muxes with the same sat_pos, network_id and ts_id.
			Therefore we count them
		*/
		auto delta = std::abs(cmux.k.sat_pos - sat_pos);
		if (delta>150) // 1.5 degree -> no match
			continue;
		if (delta<best_delta)  {
			best_delta = delta;
			ret = cmux;
		}
	}
	c.close();
	return ret;
}

db_tcursor_index<chdb::dvbs_mux_t> chdb::find_by_sat_freq_pol_fuzzy(db_txn& txn, int16_t sat_pos, uint32_t frequency,
																																		chdb::fe_polarisation_t polarisation,
																																		uint16_t t2mi_pid, int stream_id) {
	using namespace chdb;

	// look up the first record with matching sat_pos and closeby frequency
	// and create a range which iterates over all with the same sat_freq_pol
	// find_leq is essential to find the first frequency below the wanted one if the wanted one does not exist
	auto c = dvbs_mux_t::find_by_sat_pos_frequency_pol(txn, sat_pos, frequency, polarisation, find_leq,
																										 dvbs_mux_t::partial_keys_t::sat_pos);
	auto temp = c.clone();
	while (c.is_valid()) {
		/*
			handle the case of muxes with very similar frequency but different stream_id or t2mi_pid
			The desired mux might have a slightly lower frequency than the found one
		*/
		const auto& mux = c.current();
		assert(mux.k.sat_pos == sat_pos);
		auto tolerance = ((int)mux.symbol_rate) / (2 * 1350);
		if (frequency >= mux.frequency + tolerance) {
			c.next();
			break;
		}
		c.prev();
	}
	if (!c.is_valid() && temp.is_valid())
		c = std::move(temp);
	temp.close();
	if (!c.is_valid()) {
		// no frequencies lower than the wanted one on this sat
		c.close();
		// perhaps there are closeby higher frequencies on this sat
		c = dvbs_mux_t::find_by_sat_pos_frequency_pol(txn, sat_pos, frequency, polarisation, find_geq,
																									dvbs_mux_t::partial_keys_t::sat_pos);
		if (!c.is_valid()) {
			// no frequencies higher than the wanted one on this sat
			c.close();
			return c;
		}
	}
	/*@todo
		There could be multiple matching muxes with very similar frequencies in the database
		(although we try to prevent this)
	*/
	int best = std::numeric_limits<int>::max();
	auto bestc = c.clone();
	for (auto const& mux : c.range()) {
		assert(mux.k.sat_pos == sat_pos); // see above: iterator would be invalid in this case
		//@todo  double check that searching starts at a closeby cursor position
		if (mux.frequency == frequency && mux.pol == polarisation && mux.stream_id == stream_id &&
				mux.k.t2mi_pid == t2mi_pid) {
			return c;
		}
		auto tolerance = ((int)mux.symbol_rate) / (2 * 1350);
		if ((int)frequency - (int)mux.frequency > tolerance)
			continue;
		if ((int)mux.frequency - (int)frequency > tolerance)
			break;
		if (mux.pol != polarisation || mux.stream_id != stream_id || mux.k.t2mi_pid != t2mi_pid)
			continue;
		// delta will drop in each iteration and will start to rise after the minimum
		auto delta = std::abs((int)mux.frequency - (int)frequency);
		if (delta > best) {
			c.prev();
			return c;
		}
		best = delta;
		bestc = std::move(c);
	}
	if (best == std::numeric_limits<int>::max()) {
		bestc.close();
		c.close();
		return c;
	}
	c.close();
	return bestc;
}

template <typename mux_t>
db_tcursor_index<mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance) {
	using namespace chdb;

	// look up the first record with matching sat_pos and closeby frequency
	// and create a range which iterates over all with the same sat_freq_pol
	// find_leq is essential to find the first frequency below the wanted one if the wanted one does not exist
	auto c = mux_t::find_by_frequency(txn, frequency, find_leq);

	if (!c.is_valid()) {
		// no frequencies lower than the wanted one
		c.close();
		// perhaps there are closeby higher frequencies
		c = mux_t::find_by_frequency(txn, frequency, find_geq);
		if (!c.is_valid()) {
			// no frequencies higher than the wanted one on this sat
			c.close();
			return c;
		}
	}
	/*@todo
		There could be multiple matching muxes with very similar frequencies in the database
		(although we try to prevent this)
	*/
	int best = std::numeric_limits<int>::max();
	for (auto const& mux : c.range()) {
		//@todo  double check that searching starts at a closeby cursor position
		if (mux.frequency == frequency) {
			return c;
		}
		if ((int)frequency - (int)mux.frequency > tolerance)
			continue;
		if ((int)mux.frequency - (int)frequency > tolerance)
			break;
		// delta will drop in each iteration and will start to rise after the minium
		auto delta = std::abs((int)mux.frequency - (int)frequency);
		if (delta > best) {
			c.prev();
			return c;
		}
		best = delta;
	}
	if (best == std::numeric_limits<int>::max())
		c.close();
	else
		c.prev(); // to handle the case where only one mux is present and within tolerance
	return c;
}

bool chdb::matches_physical_fuzzy(const any_mux_t& a, const any_mux_t& b, bool check_sat_pos) {
	{
		auto* pa = std::get_if<chdb::dvbs_mux_t>(&a);
		auto* pb = std::get_if<chdb::dvbs_mux_t>(&b);
		if (pa && pb) {
			if (pa->pol != pb->pol)
				return false;
			if (check_sat_pos && (std::abs(pa->k.sat_pos - pb->k.sat_pos) > 30)) // 0.3 degree
				return false;
			auto tolerance = ((int)std::min(pa->symbol_rate, pb->symbol_rate)) / (2 * 1350);
			return (std::abs((int)pa->frequency - (int)pb->frequency) < tolerance);
		}
	}

	{
		auto* pa = std::get_if<chdb::dvbc_mux_t>(&a);
		auto* pb = std::get_if<chdb::dvbc_mux_t>(&b);
		if (pa && pb) {
			auto tolerance = 1000;
			return (std::abs((int)pa->frequency - (int)pb->frequency) < tolerance);
		}
	}
	{
		auto* pa = std::get_if<chdb::dvbt_mux_t>(&a);
		auto* pb = std::get_if<chdb::dvbt_mux_t>(&b);
		if (pa && pb) {
			auto tolerance = 1000;
			return (std::abs((int)pa->frequency - (int)pb->frequency) < tolerance);
		}
	}
	return false;
}

template db_tcursor_index<chdb::dvbt_mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance);
template db_tcursor_index<chdb::dvbc_mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance);

void chdb::sat_pos_str(ss::string_& s, int position) {
	if (position == sat_pos_dvbc) {
		s.sprintf("DVBC");
	} else if (position == sat_pos_dvbt) {
		s.sprintf("DVBT");
	} else if (position == sat_pos_none) {
		s.sprintf("----");
	} else {
		auto fpos = std::abs(position) / (double)100.;
		s.sprintf("%3.1f%c", fpos, position < 0 ? 'W' : 'E');
	}
}

void chdb::matype_str(ss::string_& s, uint8_t matype) {
	// See en 302 307 v1.1.2; stid135 manual seems wrong in places
	switch (matype >> 6) {
	case 0:
		s.sprintf("GFP "); ///generic packetised stream
		break;
	case 1:
		s.sprintf("GCS "); //generic continuous stream
		break;
	case 2:
		s.sprintf("GSE "); //eneric encapsulated stream
		break;
	case 3:
		s.sprintf("TS "); //transport stream
		break;
#if 0
	case 4:
		s.sprintf("GSEL "); //GSE Lite
		break;
#endif
	}

	if ((matype >> 5) & 1)
		s.sprintf("SIS ");
	else
		s.sprintf("MIS ");

	if ((matype >> 4) & 1)
		s.sprintf("ACM/VCM ");
	else
		s.sprintf("CCM ");
	if ((matype >> 3) & 1)
		s.sprintf("ISSYI ");

	if ((matype >> 2) & 1)
		s.sprintf("NPD ");

	switch (matype & 3) {
	case 0:
		s.sprintf("35%");
		break;
	case 1:
		s.sprintf("25%");
		break;
	case 2:
		s.sprintf("20%");
		break;
	case 3:
		s.sprintf("??%");
		break;
	}
}

std::ostream& chdb::operator<<(std::ostream& os, const language_code_t& code) {
	os << lang_name(code);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const sat_t& sat) {
	if (sat.sat_pos == sat_pos_dvbc) {
		stdex::printf(os, "DVBC");
	} else if (sat.sat_pos == sat_pos_dvbt) {
		stdex::printf(os, "DVBS");
	} else if (sat.sat_pos == sat_pos_none) {
		stdex::printf(os, "----");
	} else {
		float fpos = std::abs(sat.sat_pos) / 10.;
		stdex::printf(os, "%3.1f%c", fpos, sat.sat_pos < 0 ? 'W' : 'E');
	}
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const dvbs_mux_t& mux) {
	auto sat = sat_pos_str(mux.k.sat_pos);
	stdex::printf(os, "%s %d%s", sat.c_str(), mux.frequency / 1000, enum_to_str(mux.pol));
	if (mux.stream_id >= 0)
		stdex::printf(os, "-%d", mux.stream_id);
	if (mux.k.t2mi_pid != 0)
		stdex::printf(os, "-T%d", mux.k.t2mi_pid);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const dvbc_mux_t& mux) {
	if (mux.frequency % 1000 == 0)
		stdex::printf(os, " DVB-C %dMHz", mux.frequency / 1000);
	else
		stdex::printf(os, " DVB-C %.3fMHz", mux.frequency / (float)1000);
	if (mux.stream_id >= 0)
		stdex::printf(os, " stream %d", mux.stream_id);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const dvbt_mux_t& mux) {
	if (mux.frequency % 1000 == 0)
		stdex::printf(os, " DVB-T %dMHz", mux.frequency / 1000);
	else
		stdex::printf(os, " DVB-T %.3fMHz", mux.frequency / (float)1000);
	if (mux.stream_id >= 0)
		stdex::printf(os, " stream %d", mux.stream_id);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const any_mux_t& mux) {
	using namespace chdb;
	visit_variant(mux,
								[&](const chdb::dvbs_mux_t& mux) { os << mux;},
								[&](const chdb::dvbc_mux_t& mux) { os << mux;},
								[&](const chdb::dvbt_mux_t& mux) { os << mux;});
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const mux_key_t& k) {
	auto sat = sat_pos_str(k.sat_pos);
	os << sat;
	stdex::printf(os, " - nid=%d tid=%d extra=%d", k.network_id, k.ts_id, k.extra_id);
	if (k.t2mi_pid != 0)
		stdex::printf(os, "-T%d", k.t2mi_pid);

	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const service_t& service) {
	auto s = sat_pos_str(service.k.mux.sat_pos);
#if 0
	stdex::printf(os, "[%5.5d] %s ts=%d sid=%d - %s", service.ch_order, s.c_str(),
								service.k.mux.ts_id, service.k.service_id, service.name.c_str());
#else
	stdex::printf(os, "[%5.5d] %s - %s", service.ch_order, service.mux_desc.c_str(), service.name.c_str());
#endif
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const service_key_t& k) {
	auto s = sat_pos_str(k.mux.sat_pos);
	stdex::printf(os, "%s ts=%d sid=%d", s.c_str(), k.mux.ts_id, k.service_id);
	if (k.mux.t2mi_pid >= 0)
		stdex::printf(os, "-T%d", k.mux.t2mi_pid);

	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const lnb_key_t& lnb_key) {
	stdex::printf(os, "D%d T%d %d", (int)lnb_key.dish_id, (int)lnb_key.adapter_no, (int)lnb_key.lnb_id);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const lnb_t& lnb) {
	using namespace chdb;
	switch (lnb.rotor_control) {
	case rotor_control_t::FIXED_DISH: {
		auto sat = sat_pos_str(lnb.usals_pos); // in this case usals pos equals one of the network sat_pos
		stdex::printf(os, sat.c_str());
	} break;
	case rotor_control_t::ROTOR_MASTER_USALS:
	case rotor_control_t::ROTOR_MASTER_DISEQC12:
		stdex::printf(os, "rotor");
		break;
	case rotor_control_t::ROTOR_SLAVE:
		stdex::printf(os, "slave");
	}

	stdex::printf(os, " dish%d T%d", (int)lnb.k.dish_id, (int)lnb.k.adapter_no);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const lnb_network_t& lnb_network) {
	auto s = sat_pos_str(lnb_network.sat_pos);
	// the int casts are needed (bug in std::printf?
	stdex::printf(os, "[%p] pos=%s enabled=%d", &lnb_network, s.c_str(), lnb_network.enabled);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const fe_band_pol_t& band_pol) {
	// the int casts are needed (bug in std::printf?
	stdex::printf(os, "%s-%s",
								band_pol.pol == fe_polarisation_t::H	 ? "H"
								: band_pol.pol == fe_polarisation_t::V ? "V"
								: band_pol.pol == fe_polarisation_t::L ? "L"
								: "R",
								band_pol.band == fe_band_t::HIGH ? "High" : "Low");
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const chg_t& chg) {
	stdex::printf(os, "[%d-%04d] %s", int(chg.k.group_type), chg.k.bouquet_id, chg.name.c_str());

	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const chgm_t& chgm) {
	stdex::printf(os, "[%04d:%04d] ", chgm.k.chg.bouquet_id, chgm.chgm_order);
	os << chgm.service;
	stdex::printf(os, " %s", chgm.name.c_str());
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const fe_key_t& fe_key) {
	stdex::printf(os, "A%d F%d", (int)fe_key.adapter_no, (int)fe_key.frontend_no);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const fe_t& fe) {
	using namespace chdb;
	os << fe.k;
	stdex::printf(os, " %s;%s;%s", fe.card_name, fe.adapter_name, fe.card_address);
	stdex::printf(os, " master=%d enabled=%d available=%d", fe.master_adapter, fe.enabled, fe.can_be_used);
	return os;
}

void chdb::to_str(ss::string_& ret, const sat_t& sat) {
	ret.clear();
	sat_pos_str(ret, sat.sat_pos);
}

void chdb::to_str(ss::string_& ret, const language_code_t& code) {
	ret.clear();
	ret << code;
}

void chdb::to_str(ss::string_& ret, const dvbs_mux_t& mux) {
	ret.clear();
	ret << mux;
}

void chdb::to_str(ss::string_& ret, const dvbc_mux_t& mux) {
	ret.clear();
	ret << mux;
}

void chdb::to_str(ss::string_& ret, const dvbt_mux_t& mux) {
	ret.clear();
	ret << mux;
}

void chdb::to_str(ss::string_& ret, const any_mux_t& mux) {
	ret.clear();
	ret << mux;
}

void chdb::to_str(ss::string_& ret, const mux_key_t& k) {
	ret.clear();
	ret << k;
}

void chdb::to_str(ss::string_& ret, const service_t& service) {
	ret.clear();
	ret << service;
}

void chdb::to_str(ss::string_& ret, const chg_t& chg) {
	ret.clear();
	ret << chg;
}

void chdb::to_str(ss::string_& ret, const chgm_t& chgm) {
	ret.clear();
	ret << chgm;
}

void chdb::to_str(ss::string_& ret, const lnb_key_t& lnb_key) {
	ret.clear();
	ret << lnb_key;
}

void chdb::to_str(ss::string_& ret, const lnb_t& lnb) {
	ret.clear();
	ret << lnb;
}

void chdb::to_str(ss::string_& ret, const lnb_network_t& lnb_network) {
	ret.clear();
	ret << lnb_network;
}

void chdb::to_str(ss::string_& ret, const fe_band_pol_t& band_pol) {
	ret.clear();
	ret << band_pol;
}

void chdb::to_str(ss::string_& ret, const fe_key_t& fe_key) {
	ret.clear();
	ret << fe_key;
}

void chdb::to_str(ss::string_& ret, const fe_t& fe) {
	ret.clear();
	ret << fe;
}

namespace chdb {
	bool is_same(const mux_common_t& a, const mux_common_t& b);
};

bool chdb::is_same(const mux_common_t& a, const mux_common_t& b) {

	if (!(a.scan_time == b.scan_time))
		return false;

	if (!(a.scan_duration == b.scan_duration))
		return false;

	if (!(a.scan_status == b.scan_status))
		return false;

	if (!(a.scan_result == b.scan_result))
		return false;

	if (!(a.num_services == b.num_services))
		return false;

	if (!(a.epg_scan == b.epg_scan))
		return false;

	if (!(a.is_template == b.is_template))
		return false;
#if 0
	if (!(a.mtime == b.mtime))
		return false;
#endif
	return true;
}

bool chdb::is_same(const dvbs_mux_t& a, const dvbs_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if (!(a.delivery_system == b.delivery_system))
		return false;
	if (!(a.frequency == b.frequency))
		return false;
	if (!(a.pol == b.pol))
		return false;
	if (!(a.symbol_rate == b.symbol_rate))
		return false;
	if (!(a.modulation == b.modulation))
		return false;
	if (!(a.fec == b.fec))
		return false;
	if (!(a.inversion == b.inversion))
		return false;
	if (!(a.rolloff == b.rolloff))
		return false;
	if (!(a.pilot == b.pilot))
		return false;
	if (!(a.stream_id == b.stream_id))
		return false;
	if (!(a.pls_mode == b.pls_mode))
		return false;
	if (!(a.pls_code == b.pls_code))
		return false;

	if (!(a.c == b.c))
		return false;
	return true;
}

bool chdb::is_same(const dvbt_mux_t& a, const dvbt_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if (!(a.delivery_system == b.delivery_system))
		return false;
	if (!(a.frequency == b.frequency))
		return false;
	if (!(a.bandwidth == b.bandwidth))
		return false;
	if (!(a.modulation == b.modulation))
		return false;
	if (!(a.transmission_mode == b.transmission_mode))
		return false;
	if (!(a.guard_interval == b.guard_interval))
		return false;
	if (!(a.hierarchy == b.hierarchy))
		return false;
	if (!(a.HP_code_rate == b.HP_code_rate))
		return false;
	if (!(a.LP_code_rate == b.LP_code_rate))
		return false;
	if (!(a.stream_id == b.stream_id))
		return false;
	if (!(a.c == b.c))
		return false;
	return true;
}

bool chdb::is_same(const dvbc_mux_t& a, const dvbc_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if (!(a.delivery_system == b.delivery_system))
		return false;
	if (!(a.frequency == b.frequency))
		return false;
	if (!(a.symbol_rate == b.symbol_rate))
		return false;
	if (!(a.modulation == b.modulation))
		return false;
	if (!(a.fec_inner == b.fec_inner))
		return false;
	if (!(a.fec_outer == b.fec_outer))
		return false;
	if (!(a.stream_id == b.stream_id))
		return false;
	if (!(a.c == b.c))
		return false;
	return true;
}

chdb::delsys_type_t chdb::delsys_to_type(chdb::fe_delsys_t delsys_) {
	auto delsys = (fe_delivery_system)delsys_;
	switch (delsys) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
	case SYS_DVBC_ANNEX_B:
	case SYS_ISDBC:
		return chdb::delsys_type_t::DVB_C;
	case SYS_DVBT:
	case SYS_DVBT2:
	case SYS_TURBO:
	case SYS_ATSC:
	case SYS_ATSCMH:
	case SYS_ISDBT:
		return chdb::delsys_type_t::DVB_T;
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_ISDBS:
		return chdb::delsys_type_t::DVB_S;

#if 0
		//@todo
	case SYS_DTMB:
		return chdb::fe_delsys_t::DVB_TYPE_DTMB;
	case SYS_DAB:
		return chdb::fe_delsys_t::DAB;
#endif
	default:
		return chdb::delsys_type_t::NONE;
	}
}


/*
	returns true, priority if network exists, otherwise false
*/
std::tuple<bool, int> chdb::lnb::has_network(const lnb_t& lnb, int16_t sat_pos) {
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](const chdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	if (it != lnb.networks.end())
		return std::make_tuple(true, it->priority);
	else
		return std::make_tuple(false, -1);
}

/*
	returns the network if it exists
*/
const chdb::lnb_network_t* chdb::lnb::get_network(const lnb_t& lnb, int16_t sat_pos) {
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](const chdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	if (it != lnb.networks.end())
		return &*it;
	else
		return nullptr;
}

chdb::lnb_t chdb::lnb::new_lnb(int adapter_no, int16_t sat_pos, int dish_id, chdb::lnb_type_t type) {
	auto c = chdb::lnb_t();
	bool on_rotor = false;
	c.k.adapter_no = adapter_no;
	c.k.dish_id = dish_id;
	c.k.lnb_id = on_rotor ? 0 : sat_pos;
	c.k.lnb_type = chdb::lnb_type_t::UNIV;

	c.polarisations = (1 << (int)chdb::fe_polarisation_t::H) | (1 << (int)chdb::fe_polarisation_t::V);
	c.enabled = true;
	c.mtime = system_clock_t::to_time_t(now);

	c.usals_pos = sat_pos;

	c.tune_string = "UC";
	c.diseqc_10 = 0;
	c.networks.push_back(
		lnb_network_t(sat_pos, {} /*priority*/, sat_pos /*usals_pos*/, {} /*diseqc12*/, {} /*enabled*/, {} /*ref_mux*/));

	return c;
}

void chdb::service::update_audio_pref(db_txn& txn, const chdb::service_t& from_service) {
	auto c = chdb::service_t::find_by_key(txn, from_service.k);
	/*
		handle the case where some other thread has updated other things
		like the service name
	*/
	if (c.is_valid()) {
		auto service_ = c.current();
		service_.audio_pref = from_service.audio_pref;
		put_record(txn, service_);
	} else
		put_record(txn, from_service);
}

void chdb::service::update_subtitle_pref(db_txn& txn, const chdb::service_t& from_service) {
	auto c = chdb::service_t::find_by_key(txn, from_service.k);
	/*
		handle the case where some other thread has updated other things
		like the service name
	*/
	if (c.is_valid()) {
		auto service_ = c.current();
		service_.subtitle_pref = from_service.subtitle_pref;
		put_record(txn, service_);
	} else
		put_record(txn, from_service);
}

bool chdb::put_record(db_txn& txn, const chdb::any_mux_t& mux, unsigned int put_flags) {
	using namespace chdb;
	bool ret;
	visit_variant(
		mux, [&](const chdb::dvbs_mux_t& mux) { ret = ::put_record(txn, mux, put_flags); },
		[&](const chdb::dvbc_mux_t& mux) { ret = ::put_record(txn, mux, put_flags); },
		[&](const chdb::dvbt_mux_t& mux) { ret = ::put_record(txn, mux, put_flags); });
	return ret;
}

void chdb::delete_record(db_txn& txn, const chdb::any_mux_t& mux) {

	visit_variant(
		mux, [&txn](const chdb::dvbs_mux_t& mux) { delete_record(txn, mux); },
		[&txn](const chdb::dvbc_mux_t& mux) { delete_record(txn, mux); },
		[&txn](const chdb::dvbt_mux_t& mux) { delete_record(txn, mux); });
}

static std::tuple<uint32_t, uint32_t, uint32_t> lnb_band_helper(const chdb::lnb_t& lnb) {
	auto freq_low = std::numeric_limits<uint32_t>::min();
	auto freq_high = std::numeric_limits<uint32_t>::min();
	auto freq_mid = freq_low;

	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		freq_low = lnb.freq_low < 0 ? 3400000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 4200000 : lnb.freq_high;
		freq_mid = freq_low;
	} break;
	case lnb_type_t::UNIV: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = (lnb.freq_mid < 0) ? 11700000 : lnb.freq_mid;
	} break;
	case lnb_type_t::KU: {
		freq_low = lnb.freq_low < 0 ? 11700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12200000 : lnb.freq_high;
		freq_mid = freq_low;
	} break;
	default:
		assert(0);
	}
	return {freq_low, freq_mid, freq_high};
}

bool chdb::lnb_can_tune_to_mux(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool disregard_networks) {
	auto [freq_low, freq_mid, freq_high] = lnb_band_helper(lnb);
	if (mux.frequency < freq_low || mux.frequency >= freq_high)
		return false;
	if (!((1 << (uint8_t)mux.pol) & lnb.polarisations))
		return false;
	if (disregard_networks)
		return true;
	for (auto& network : lnb.networks) {
		if (network.sat_pos == mux.k.sat_pos)
			return true;
	}
	return false;
}

/*
	band = 0 or 1 for low or high (22Khz off/on)
	voltage = 0 (H,L, 13V) or 1 (V, R, 18V) or 2 (off)
	freq: frequency after LNB local oscilllator compensation

	Reasons why lnb cannot tune mux: c_band lnb cannot tune ku-band mux; lnb has wrong polarisation
*/
std::tuple<int, int, int> chdb::lnb::band_pol_freq_for_mux(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
	using namespace chdb;
	int band = -1;
	int voltage = -1;
	int frequency = -1;
	const bool disregard_networks{true};
	if (!lnb_can_tune_to_mux(lnb, mux, disregard_networks))
		return std::make_tuple(band, voltage, frequency);

	frequency = driver_freq_for_freq(lnb, mux.frequency);
	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		band = 0;
		voltage = (mux.pol == fe_polarisation_t::H || mux.pol == fe_polarisation_t::L);
	} break;
	case lnb_type_t::UNIV: {
		auto [freq_low, freq_mid, freq_high] = lnb_band_helper(lnb);
		band = (mux.frequency >= freq_mid);
		voltage = (mux.pol == fe_polarisation_t::H || mux.pol == fe_polarisation_t::L);
	} break;
	case lnb_type_t::KU: {
		band = 0;
		voltage = (mux.pol == fe_polarisation_t::V || mux.pol == fe_polarisation_t::R);
	} break;
	default:
		assert(0);
	}

	return std::make_tuple(band, voltage, frequency);
}

chdb::fe_band_t chdb::lnb::band_for_freq(const chdb::lnb_t& lnb, uint32_t frequency) {
	using namespace chdb;

	auto [freq_low, freq_mid, freq_high] = lnb_band_helper(lnb);

	if (frequency < freq_low || frequency > freq_high)
		return chdb::fe_band_t::UNKNOWN;
	return (signed)(frequency >= freq_mid) ? chdb::fe_band_t::HIGH : chdb::fe_band_t::LOW;
}

int chdb::lnb::driver_freq_for_freq(const chdb::lnb_t& lnb, int frequency) {
	using namespace chdb;
	int band = 0;

	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		band = 0;
		auto lof_low = lnb.lof_low < 0 ? 5150000 : lnb.lof_low;
		frequency = frequency - lof_low;
		break;
	}
	case lnb_type_t::UNIV: {
		auto lof_low = (lnb.lof_low < 0) ? 9750000 : lnb.lof_low;
		auto lof_high = (lnb.lof_high < 0) ? 10600000 : lnb.lof_high;

		auto freq_mid = (lnb.freq_mid < 0) ? 11700000 : lnb.freq_mid;
		auto band = (signed)frequency >= freq_mid;

		frequency = band ? frequency - lof_high : frequency - lof_low;
	} break;
	case lnb_type_t::KU: {
		auto lof_low = lnb.lof_low < 0 ? 5150000 : lnb.lof_low;
		band = 0;
		frequency = frequency - lof_low;
	} break;
	default:
		assert(0);
	}
	if (band < lnb.lof_offsets.size()) {
		if (std::abs(lnb.lof_offsets[band]) < 5000)
			frequency += lnb.lof_offsets[band];
	}
	return std::abs(frequency);
}

std::tuple<int32_t, int32_t, int32_t> chdb::lnb::band_frequencies(const chdb::lnb_t& lnb, chdb::fe_band_t band) {
	return lnb_band_helper(lnb);
}

/*
	translate driver frequency to real frequency
	voltage_high = true if high
	@todo: see linuxdvb_lnb.c for more configs to support
	@todo: uniqcable
*/
int chdb::lnb::freq_for_driver_freq(const chdb::lnb_t& lnb, int frequency, bool high_band) {
	using namespace chdb;
	auto correct = [&lnb](int band, int frequency) {
		if (band >= lnb.lof_offsets.size()) {
			dterror("lnb_loffsets too small for lnb: " << lnb);
			return frequency;
		}
		if (std::abs(lnb.lof_offsets[band]) < 5000)
			frequency -= lnb.lof_offsets[band];
		return frequency;
	};

	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		auto lof_low = lnb.lof_low < 0 ? 5150000 : lnb.lof_low;
		return correct(0, -frequency + lof_low); // - to cope with inversion
	} break;
	case lnb_type_t::UNIV: {
		auto lof_low = lnb.lof_low < 0 ? 9750000 : lnb.lof_low;
		auto lof_high = lnb.lof_high < 0 ? 10600000 : lnb.lof_high;
		return high_band ? correct(1, frequency + lof_high) : correct(0, frequency + lof_low);
	} break;
	case lnb_type_t::KU: {
		auto lof_low = lnb.lof_low < 0 ? 5150000 : lnb.lof_low;
		return correct(0, frequency + lof_low);
	} break;
	default:
		assert(0);
	}
	return -1;
}

// used by add_mux in scan.cc
template <typename mux_t> db_tcursor<mux_t> chdb::find_mux_by_key_or_frequency(db_txn& txn, const mux_t& mux) {
	auto c = chdb::find_by_mux_fuzzy(txn, mux);
	if (c.is_valid())
		return c;
	else {
		// find tps with matching frequency, but probably incorrect network_id/ts_id
		auto c = chdb::find_by_freq_fuzzy<mux_t>(txn, mux.frequency);
		return std::move(c.maincursor);
	}
}

template db_tcursor<chdb::dvbt_mux_t> chdb::find_mux_by_key_or_frequency(db_txn& txn, const chdb::dvbt_mux_t& mux);
template db_tcursor<chdb::dvbc_mux_t> chdb::find_mux_by_key_or_frequency(db_txn& txn, const chdb::dvbc_mux_t& mux);

// used by add_mux in scan.cc
db_tcursor<chdb::dvbs_mux_t> chdb::find_mux_by_key_or_frequency(db_txn& txn, chdb::dvbs_mux_t& mux) {

	auto c = find_by_mux_fuzzy<chdb::dvbs_mux_t>(txn, mux);
	if (c.is_valid())
		return c;
	else {
		// find tps with matching frequency, but probably incorrect network_id/ts_id
		auto c =
			chdb::find_by_sat_freq_pol_fuzzy(txn, mux.k.sat_pos, mux.frequency, mux.pol, mux.k.t2mi_pid, mux.stream_id);
		return std::move(c.maincursor);
	}
}

chdb::dvbs_mux_t chdb::lnb::select_reference_mux(db_txn& rtxn, const chdb::lnb_t& lnb,
																								 const chdb::dvbs_mux_t* proposed_mux) {
	auto return_mux = [&rtxn](const chdb::lnb_network_t& network) {
		auto c = dvbs_mux_t::find_by_key(rtxn, network.ref_mux);
		if (c.is_valid())
			return c.current();
		c = dvbs_mux_t::find_by_key(rtxn, network.sat_pos, 0, 0, 0, find_type_t::find_geq,
																dvbs_mux_t::partial_keys_t::sat_pos);
		if (c.is_valid())
			return c.current();
		return dvbs_mux_t();
	};

	using namespace chdb;
	const bool disregard_networks{false};
	if (proposed_mux && lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
		return *proposed_mux;

	if (!on_rotor(lnb)) {
		for (auto& network : lnb.networks) {
			if (usals_is_close(lnb.usals_pos, network.usals_pos)) { // dish is tuned to the right sat
				return return_mux(network);
			}
		}
		if (lnb.networks.size() > 0)
			return return_mux(lnb.networks[0]);

	} else {
		auto best = std::numeric_limits<int>::max();
		const chdb::lnb_network_t* bestp{nullptr};
		for (auto& network : lnb.networks) {
			auto delta = std::abs(network.usals_pos - lnb.usals_pos);
			if (delta < best) {
				best = delta;
				bestp = &network;
			}
		}
		if (bestp && usals_is_close(bestp->usals_pos, lnb.usals_pos)) {
			return return_mux(*bestp);
		}
		return dvbs_mux_t();
	}
	return dvbs_mux_t(); // has sat_pos == sat_pos_none;
}

chdb::lnb_t chdb::lnb::select_lnb(db_txn& rtxn, const chdb::sat_t* sat_, const chdb::dvbs_mux_t* proposed_mux) {
	using namespace chdb;
	if (!sat_ && !proposed_mux)
		return lnb_t();
	chdb::sat_t sat;
	if (sat_) {
		sat = *sat_;
	} else {
		auto c = sat_t::find_by_key(rtxn, proposed_mux->k.sat_pos);
		if (!c.is_valid())
			return lnb_t();
		sat = c.current();
	}
	/*
		Loop over all lnbs to find a suitable one.
		First give preference to rotor
	*/
	auto c = find_first<chdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		auto [has_network, network_priority] = chdb::lnb::has_network(lnb, sat.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if (!has_network || !lnb.enabled)
			continue;
		if (on_rotor(lnb)) {
			const bool disregard_networks{false};
			if (!proposed_mux || lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
				// we prefer a rotor, which is most useful for user
				return lnb;
		}
	}

	/*
		Try without rotor
	*/
	c = find_first<chdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		auto [has_network, network_priority] = chdb::lnb::has_network(lnb, sat.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if (!has_network || !lnb.enabled)
			continue;
		const bool disregard_networks{false};
		if (!proposed_mux || lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
			return lnb;
	}
	// give up
	return lnb_t();
}

bool chdb::lnb::add_network(chdb::lnb_t& lnb, chdb::lnb_network_t& network) {
	using namespace chdb;
	for (auto& n : lnb.networks) {
		if (n.sat_pos == network.sat_pos)
			return false; // cannot add duplicate network
	}
	if (network.usals_pos == sat_pos_none)
		network.usals_pos = network.sat_pos;
	lnb.networks.push_back(network);
	if (lnb.usals_pos == sat_pos_none)
		lnb.usals_pos = (network.usals_pos == sat_pos_none) ? network.sat_pos : network.usals_pos;
	std::sort(lnb.networks.begin(), lnb.networks.end(),
						[](const lnb_network_t& a, const lnb_network_t& b) { return a.sat_pos < b.sat_pos; });
	return true;
}

bool chdb::has_epg_type(const chdb::any_mux_t& mux, chdb::epg_type_t epg_type) {
	auto* c = mux_common_ptr(mux);
	for (const auto& t : c->epg_types) {
		if (t == epg_type)
			return true;
	}
	return false;
}

chdb::media_mode_t chdb::media_mode_for_service_type(uint8_t service_type) {
	bool is_hd = false;
	switch (service_type) {
	case 0x01: // digital television service; encoding not specified
	case 0x04: // NVOD reference service
	case 0x05: // NVOD time-shifted service
	case 0x06: // Mosaic service
	case 0x1F: // HEVC digital television service; encoding unspecified
		return chdb::media_mode_t::TV;
		break;
	case 0x0b: // Advanced codec Mosaic service
		break;
	case 0x0c: // data service
		return chdb::media_mode_t::DATA;
		break;
	case 0x11: // MPEG2 HD television service
	case 0x16: // advanced codec  SD digital television service
	case 0x17: // advanced codec  SD NVOD time-shifted service
	case 0x18: // advanced codec  SD NVOD reference service
	case 0x19: // advanced codec  HD igital television service
	case 0x1A: // advanced codec  HD NVOD time-shifted service
	case 0x1B: // advanced codec HD NVOD reference service
	case 0x82: // used for bbci and such
		is_hd = true;
		return chdb::media_mode_t::TV;
		break;
	case 0x02: // digital radio sound service
	case 0x07: // FM radio service
	case 0x0a: // Advanced codec digital radio; encoding not specified
		return chdb::media_mode_t::RADIO;
		break;
	default:
		break;
	}
	return chdb::media_mode_t::UNKNOWN;
}

/*
	returns true if epg type was actually updated
*/
bool chdb::add_epg_type(chdb::any_mux_t& mux, chdb::epg_type_t tnew) {
	auto* c = mux_common_ptr(mux);
	for (const auto& t : c->epg_types) {
		if (t == tnew)
			return false; // already present
	}
	c->epg_types.push_back(tnew);
	return true;
}

/*
	returns true if epg type was actually updated
*/
bool chdb::remove_epg_type(chdb::any_mux_t& mux, chdb::epg_type_t tnew) {
	int idx = 0;
	auto* c = mux_common_ptr(mux);
	for (const auto& t : c->epg_types) {
		if (t == tnew)
			break; // already present
		idx++;
	}
	if (idx == c->epg_types.size())
		return false;
	c->epg_types.erase(idx);
	return true;
}

void chdb::lnb_update_usals_pos(db_txn& wtxn, const chdb::lnb_t& lnb) {
	switch (lnb.rotor_control) {
	case chdb::rotor_control_t::ROTOR_MASTER_USALS:
	case chdb::rotor_control_t::ROTOR_MASTER_DISEQC12:
		break;
	default:
		return;
	}
	auto c = chdb::lnb_t::find_by_key(wtxn, lnb.k, find_type_t::find_eq, lnb_t::partial_keys_t::all);
	if (c.is_valid()) {
		auto db_lnb = c.current();
		if (db_lnb.usals_pos != lnb.usals_pos) {
			db_lnb.usals_pos = lnb.usals_pos;
			put_record(wtxn, db_lnb);
		}
	}
}

static inline auto bouquet_find_service(db_txn& rtxn, const chdb::chg_t& chg, const chdb::service_key_t& service_key) {
	using namespace chdb;
	chgm_key_t k(chg.k, 0);
	auto c = chdb::chgm_t::find_by_chg_service(rtxn, chg.k, service_key);
	return c;
}

bool chdb::bouquet_contains_service(db_txn& rtxn, const chdb::chg_t& chg, const chdb::service_key_t& service_key) {
	auto c = bouquet_find_service(rtxn, chg, service_key);
	return c.is_valid();
}

bool chdb::toggle_service_in_bouquet(db_txn& wtxn, const chg_t& chg, const service_t& service) {
	auto c = bouquet_find_service(wtxn, chg, service.k);
	if (c.is_valid()) {
		delete_record_at_cursor<chgm_t>(c.maincursor);
		return false;
	}
	auto k = chgm_key_t(chg.k, channel_id_template);
	k.channel_id = make_unique_id(wtxn, k);
	chgm_t chgm;
	chgm.k = k;
	chgm.service = service.k;
	chgm.service_type = service.service_type;
	chgm.encrypted = service.encrypted;
	chgm.media_mode = service.media_mode;
	chgm.name = service.name;
	chgm.mtime = system_clock_t::to_time_t(now);
	put_record(wtxn, chgm);
	return true;
}

bool chdb::toggle_channel_in_bouquet(db_txn& wtxn, const chg_t& chg, const chgm_t& chgm) {
	auto c = bouquet_find_service(wtxn, chg, chgm.service);
	if (c.is_valid()) {
		delete_record_at_cursor(c.maincursor);
		return false;
	}
	auto k = chgm_key_t(chg.k, channel_id_template);
	k.channel_id = make_unique_id(wtxn, k);
	chgm_t newchgm;
	newchgm.k = k;
	newchgm.service = chgm.service;
	newchgm.service_type = chgm.service_type;
	newchgm.encrypted = chgm.encrypted;
	newchgm.media_mode = chgm.media_mode;
	newchgm.name = chgm.name;
	newchgm.mtime = system_clock_t::to_time_t(now);
	put_record(wtxn, newchgm);
	return true;
}
