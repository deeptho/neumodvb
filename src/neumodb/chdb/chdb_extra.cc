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

extern const char* lang_name(char lang1, char lang2, char lang3);
const char* chdb::lang_name(const chdb::language_code_t& code) {
	return ::lang_name(code.lang1, code.lang2, code.lang3);
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
	auto c = find_first_sorted_by_sat_pol_freq(txn);
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

template<typename mux_t>
void chdb::on_mux_key_change(db_txn& wtxn, const mux_t& old_mux, mux_t& new_mux, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace chdb;
	auto& old_mux_key = *mux_key_ptr(old_mux);
	auto& new_mux_key = *mux_key_ptr(new_mux);
	{
		auto c = service_t::find_by_key(wtxn, old_mux_key, find_type_t::find_geq,
																		service_t::partial_keys_t::mux);
		for(auto service: c.range()) {
			delete_record(wtxn, service);
			chdb::to_str(service.mux_desc, new_mux);
			service.k.mux = new_mux_key;
			service.mtime = now;
			put_record(wtxn, service);
		}
	} {
		//todo: chgm
	}
}

void chdb::on_mux_key_change(db_txn& wtxn, const chdb::dvbs_mux_t& old_mux, chdb::dvbs_mux_t& new_mux,
															system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace chdb;
	auto& old_mux_key = *mux_key_ptr(old_mux);
	auto& new_mux_key = *mux_key_ptr(new_mux);
	{
		auto c = service_t::find_by_key(wtxn, old_mux_key, find_type_t::find_geq,
																		service_t::partial_keys_t::mux);
		for(auto service: c.range()) {
			delete_record(wtxn, service);
			chdb::to_str(service.mux_desc, new_mux);
			service.k.mux = new_mux_key;
			service.mtime = now;
			put_record(wtxn, service);
		}
	} {
		//todo: chgm
	} {
		auto c = sat_t::find_by_sat_pos(wtxn, old_mux_key.sat_pos, find_type_t::find_geq,
																		sat_t::partial_keys_t::sat_pos);
		for(auto sat: c.range()) {
			if (sat.reference_tp == old_mux_key) {
				sat.reference_tp = new_mux_key;
				put_record(wtxn, sat);
			}
		}
	} {
		auto c = find_first<chdb::lnb_t>(wtxn);
		for(auto lnb: c.range()) {
			auto* n = lnb::get_network(lnb, old_mux_key.sat_pos);
			if(n) {
				lnb.mtime = now;
				if (n->ref_mux == old_mux_key) {
					n->ref_mux = new_mux_key;
					put_record(wtxn, lnb);
				}
			}
			break;
		}
	}
}


void chdb::on_mux_key_change(db_txn& wtxn, const chdb::any_mux_t& old_mux, chdb::any_mux_t& new_mux,
															system_time_t now_) {
	using namespace chdb;

		visit_variant(
		new_mux,
		[&](chdb::dvbs_mux_t& new_mux) { on_mux_key_change(wtxn,
																											 *std::get_if<dvbs_mux_t>(&old_mux),
																											 new_mux,
																											 now_);
		},
		[&](chdb::dvbc_mux_t& new_mux) { on_mux_key_change(wtxn,
																											 *std::get_if<dvbc_mux_t>(&old_mux),
																											 new_mux,
																											 now_);
		},
		[&](chdb::dvbt_mux_t& new_mux) { on_mux_key_change(wtxn,
																											 *std::get_if<dvbt_mux_t>(&old_mux),
																											 new_mux,
																											 now_);
		});
}




static inline void copy_tuning(dvbc_mux_t& mux, dvbc_mux_t& db_mux) {
	mux.delivery_system = db_mux.delivery_system;
	mux.frequency = db_mux.frequency;
	mux.inversion = db_mux.inversion;
	mux.symbol_rate = db_mux.symbol_rate;
	mux.modulation = db_mux.modulation;
	mux.fec_inner = db_mux.fec_inner;
	mux.fec_outer = db_mux.fec_outer;
	mux.stream_id = db_mux.stream_id;
}

static inline void copy_tuning(dvbt_mux_t& mux, dvbt_mux_t& db_mux) {
	mux.delivery_system = db_mux.delivery_system;
	mux.frequency = db_mux.frequency;
	mux.inversion = db_mux.inversion;
	mux.bandwidth = db_mux.bandwidth;
	mux.modulation = db_mux.modulation;
	mux.transmission_mode = db_mux.transmission_mode;
	mux.guard_interval = db_mux.guard_interval;
	mux.hierarchy = db_mux.hierarchy;
	mux.HP_code_rate = db_mux.HP_code_rate;
	mux.LP_code_rate = db_mux.LP_code_rate;
	mux.stream_id = db_mux.stream_id;
}

static inline void copy_tuning(dvbs_mux_t& mux, dvbs_mux_t& db_mux) {
	mux.delivery_system = db_mux.delivery_system;
	mux.frequency = db_mux.frequency;
	mux.inversion = db_mux.inversion;
	mux.pol = db_mux.pol;
	mux.symbol_rate = db_mux.symbol_rate;
	mux.modulation = db_mux.modulation;
	mux.fec = db_mux.fec;
	mux.rolloff = db_mux.rolloff;
	mux.pilot = db_mux.pilot;
	mux.stream_id = db_mux.stream_id;
	mux.pls_mode = db_mux.pls_mode;
	mux.matype = db_mux.matype;
	mux.pls_code = db_mux.pls_code;
}



/*
	selectively replace some data in mux by data in db_mux, while preserving the data specified in
	preserve and taking into account the tune_src fields, which specify where the data comes from
 */

template <typename mux_t>
bool merge_muxes(mux_t& mux, mux_t& db_mux,  update_mux_preserve_t::flags preserve) {
	namespace m = update_mux_preserve_t;
	dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);

	bool mux_key_incompatible{false}; // db_mux and mux have different key, even after update
	//assert(!is_template(mux)); //templates should never make it into the database via update_mux
	if( (preserve & m::MUX_KEY) ||  is_template(mux)) {
		//templat mux key entered by user is never considered valid,  so use database value
		mux.k = db_mux.k;
	}
	if(db_mux.c.tune_src == tune_src_t::USER && mux.c.tune_src != tune_src_t::AUTO)  {
		/*preserve all tuning data, unless the user wants to turn this off, which is indicated by
		 mux.c.tune_src == tune_src_t::MANUAL
		*/
		mux.c.tune_src = tune_src_t::USER;
		copy_tuning(mux, db_mux); //preserve what is in the database
	}

	//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);

	switch(mux.c.tune_src) {
	case tune_src_t::TEMPLATE:
		mux.c.tune_src = db_mux.c.tune_src;
		break;

	case tune_src_t::NIT_ACTUAL_TUNED:
		if(db_mux.c.tune_src == tune_src_t::USER) {
			copy_tuning( mux, db_mux );
			mux.c.tune_src = db_mux.c.tune_src;
		}
		break;

	case tune_src_t::NIT_ACTUAL_NON_TUNED:
		if( db_mux.c.tune_src == tune_src_t::NIT_ACTUAL_TUNED && tuning_is_same(mux, db_mux) )
			mux.c.tune_src = db_mux.c.tune_src; //simply preserve where most accurate data comes from
		else if( db_mux.c.tune_src == tune_src_t::USER) { //user wants to preserve
			copy_tuning( mux, db_mux );
			mux.c.tune_src = db_mux.c.tune_src;
		}
		break;


	case tune_src_t::NIT_OTHER_NON_TUNED:
		if( ( db_mux.c.tune_src == tune_src_t::NIT_ACTUAL_TUNED ||
					db_mux.c.tune_src == tune_src_t::NIT_ACTUAL_NON_TUNED )
				 && tuning_is_same(mux, db_mux) )
			mux.c.tune_src = db_mux.c.tune_src; //simply preserve where most accurate data comes from
		else if( db_mux.c.tune_src == tune_src_t::USER) { //user wants to preserve
			copy_tuning( mux, db_mux );
			mux.c.tune_src = db_mux.c.tune_src;
		}
		break;

	case tune_src_t::DRIVER:
		if( ( db_mux.c.tune_src == tune_src_t::NIT_ACTUAL_TUNED ||
					db_mux.c.tune_src == tune_src_t::NIT_ACTUAL_NON_TUNED ||
					db_mux.c.tune_src == tune_src_t::NIT_OTHER_NON_TUNED )
				 && tuning_is_same(mux, db_mux) )
			mux.c.tune_src = db_mux.c.tune_src; //simply preserve where most accurate data comes from
		else if( db_mux.c.tune_src == tune_src_t::USER) { //user wants to preserve
			copy_tuning( mux, db_mux );
			mux.c.tune_src = db_mux.c.tune_src;
		}
		break;

	case tune_src_t::USER:
		break;

	case tune_src_t::AUTO:
		break;

	case tune_src_t::UNKNOWN:
		assert(0);
		break;
	};
	//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);

	if (preserve & m::SCAN_DATA) {
		mux.c.scan_result = db_mux.c.scan_result;
		mux.c.scan_duration = db_mux.c.scan_duration;
		mux.c.scan_time = db_mux.c.scan_time;
		mux.c.epg_scan = db_mux.c.epg_scan;
	}
	if (preserve & m::SCAN_STATUS) {
		mux.c.scan_status = db_mux.c.scan_status;
	}
	dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status << " preserve&SCAN_DATA=" << (preserve & m::SCAN_DATA));


	if (preserve & m::NUM_SERVICES)
		mux.c.num_services = db_mux.c.num_services;

	if (preserve & m::EPG_TYPES)
		mux.c.epg_types = db_mux.c.epg_types;

	return mux_key_incompatible;
}



/*! Put a mux record, taking into account that its key may have changed
	returns: true if this is a new mux (first time scanned); false otherwise
	Also, updates the mux, specifically k.extra_id
*/
template <typename mux_t>
update_mux_ret_t chdb::update_mux(db_txn& txn, mux_t& mux, system_time_t now_, update_mux_preserve_t::flags preserve,
																	std::function<bool(const chdb::mux_common_t*)> cb) {
	auto now = system_clock_t::to_time_t(now_);
	//assert(mux.c.tune_src != tune_src_t::TEMPLATE);
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

	auto c = chdb::find_by_mux_physical(txn, mux);
	bool is_new = true; // do we modify an existing record or create a new one?

	if (c.is_valid()) {
		db_mux = c.current();
		if(db_mux.k.extra_id == 0 ) {
			dterror("Database mux " << db_mux << " has zero extra_id; fixing it");
			mux.k.extra_id = make_unique_id<mux_t>(txn, mux.k);
		}

		mux.k.extra_id = db_mux.k.extra_id;
		auto key_matches = mux.k == db_mux.k;
		/*
			if !key_matches:  We are going to overwrite a mux with similar frequency but different ts_id, network_id.
			If one of the muxes is a template and the other not, the non-template data
			gets priority.
		*/
		if(!key_matches) {
			if (preserve & m::MUX_KEY)
				return update_mux_ret_t::NO_MATCHING_KEY;
			delete_record(c, db_mux);
			if(is_template(db_mux))
				db_mux.k = mux.k;
		}

		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);
		ret = key_matches ? update_mux_ret_t::MATCHING_SI_AND_FREQ: update_mux_ret_t::MATCHING_FREQ;

		is_new = false;
		cb(&db_mux.c);
		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);
		merge_muxes<mux_t>(mux, db_mux, preserve);
		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);

		dtdebugx("Transponder %s: sat_pos=%d => %d nid=%d => %d ts_id=%d => %d", to_str(mux).c_str(),
						 db_mux.k.sat_pos, mux.k.sat_pos, db_mux.k.network_id,
						 mux.k.network_id, db_mux.k.ts_id, mux.k.ts_id);
		//assert( mux.c.tune_src != tune_src_t::TEMPLATE );
		is_new = false;
		ret = update_mux_ret_t::MATCHING_SI_AND_FREQ;
	} else { //no mux in db
		ret = update_mux_ret_t::NEW;
		cb(nullptr);
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

		dtdebug("mux=" << mux << " status=" << (int)mux.c.scan_status);
		put_record(txn, mux);
	}
	return ret;
}

update_mux_ret_t chdb::update_mux(db_txn& txn, chdb::any_mux_t& mux, system_time_t now,
																	update_mux_preserve_t::flags preserve,
																	std::function<bool(const chdb::mux_common_t*)> cb) {
	using namespace chdb;
	update_mux_ret_t ret;
	visit_variant(
		mux, [&](chdb::dvbs_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve, cb); },
		[&](chdb::dvbc_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve, cb); },
		[&](chdb::dvbt_mux_t& mux) { ret = chdb::update_mux(txn, mux, now, preserve, cb); });
	return ret;
}




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

void chdb::matype_str(ss::string_& s, int16_t matype) {
	// See en 302 307 v1.1.2; stid135 manual seems wrong in places
	// en_302307v010201p_DVBS2.pdf
	//s.sprintf("0x%x ", matype);
	if( matype == 256 ) { //dvbs
		s.sprintf("DVBS");
		return;
	}

	if( matype <0 ) { //dvbs
		s.sprintf("");
		return;
	}
	switch (matype >> 6) {
	case 0:
		s.sprintf("GFP "); ///generic packetised stream
		break;
	case 1:
		s.sprintf("GCS "); //generic continuous stream
		break;
	case 2:
		s.sprintf("GSE "); //generic encapsulated stream
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
		s.sprintf("CCM ");
	else
		s.sprintf("ACM/VCM ");
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
		stdex::printf(os, "DVBT");
	} else if (sat.sat_pos == sat_pos_dvbs) {
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
	stdex::printf(os, "[%5.5d] %s - %s", service.ch_order, service.mux_desc.c_str(), service.name.c_str());
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
	const char* t = (lnb_key.lnb_type == lnb_type_t::C) ? "C" :
		(lnb_key.lnb_type == lnb_type_t::KU) ? "Ku" : "Ku" ;
	stdex::printf(os, "D%dA%d%s %d", (int)lnb_key.dish_id, (int)lnb_key.adapter_no, t, (int)lnb_key.lnb_id);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const lnb_t& lnb) {
	using namespace chdb;
	const char* t = (lnb.k.lnb_type == lnb_type_t::C) ? "C" :
		(lnb.k.lnb_type == lnb_type_t::KU) ? "Ku" : "Ku" ;
	switch (lnb.rotor_control) {
	case rotor_control_t::FIXED_DISH: {
		auto sat = sat_pos_str(lnb.usals_pos); // in this case usals pos equals one of the network sat_pos
		stdex::printf(os, "D%dA%d%s %s %d", (int)lnb.k.dish_id, (int)lnb.k.adapter_no, t, sat.c_str(),(int)lnb.k.lnb_id);
	} break;
	case rotor_control_t::ROTOR_MASTER_USALS:
	case rotor_control_t::ROTOR_MASTER_DISEQC12:
		stdex::printf(os, "D%dA%d%s rotor %d", (int)lnb.k.dish_id, (int)lnb.k.adapter_no, t, (int)lnb.k.lnb_id);
		break;
	case rotor_control_t::ROTOR_SLAVE:
		stdex::printf(os, "D%dA%d%s slave% d", (int)lnb.k.dish_id, (int)lnb.k.adapter_no, t, (int)lnb.k.lnb_id);
	}
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const lnb_network_t& lnb_network) {
	auto s = sat_pos_str(lnb_network.sat_pos);
	// the int casts are needed (bug in std::printf?
	stdex::printf(os, "[%p] pos=%s enabled=%d", &lnb_network, s.c_str(), lnb_network.enabled);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const fe_polarisation_t& pol) {
		stdex::printf(os, "%s",
									pol == fe_polarisation_t::H	 ? "H"
									: pol == fe_polarisation_t::V ? "V"
									: pol == fe_polarisation_t::L ? "L"
									: "R");
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
	bool tuning_is_same(const dvbs_mux_t& a, const dvbs_mux_t& b);
	bool tuning_is_same(const dvbc_mux_t& a, const dvbc_mux_t& b);
	bool tuning_is_same(const dvbt_mux_t& a, const dvbt_mux_t& b);
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

	if (!(a.tune_src == b.tune_src))
		return false;
	if (!(a.mtime == b.mtime))
		return false;
	return true;
}

bool chdb::tuning_is_same(const dvbs_mux_t& a, const dvbs_mux_t& b) {
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
	if (!(a.matype == b.matype))
		return false;
	return true;
}

bool chdb::tuning_is_same(const dvbt_mux_t& a, const dvbt_mux_t& b) {
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
	return true;
}

bool chdb::tuning_is_same(const dvbc_mux_t& a, const dvbc_mux_t& b) {
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
	return true;
}

bool chdb::is_same(const dvbs_mux_t& a, const dvbs_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if(!tuning_is_same(a, b))
		return false;
	if (!(a.c == b.c))
		return false;
	return true;
}

bool chdb::is_same(const dvbt_mux_t& a, const dvbt_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if(!tuning_is_same(a, b))
		return false;
	if (!(a.c == b.c))
		return false;
	return true;
}

bool chdb::is_same(const dvbc_mux_t& a, const dvbc_mux_t& b) {
	if (!(a.k == b.k))
		return false;
	if(!tuning_is_same(a, b))
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
	returns
	  network_exists
		priority (if network_exists else -1)
		usals_amount: how much does the dish need to be rotated for this network
*/
std::tuple<bool, int, int> chdb::lnb::has_network(const lnb_t& lnb, int16_t sat_pos) {
	int usals_amount{0};
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](const chdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	if (it != lnb.networks.end()) {
		if (chdb::on_rotor(lnb)) {
			auto usals_pos = lnb.usals_pos - lnb.offset_pos;
			usals_amount = std::abs(usals_pos - it->usals_pos);
		}
		return std::make_tuple(true, it->priority, usals_amount);
	}
	else
		return std::make_tuple(false, -1, 0);
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

chdb::lnb_network_t* chdb::lnb::get_network(lnb_t& lnb, int16_t sat_pos) {
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](chdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
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

	c.pol_type = chdb::lnb_pol_type_t::HV;
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


std::tuple<uint32_t, uint32_t> chdb::lnb::lnb_frequency_range(const chdb::lnb_t& lnb)
{
	auto [low, mid, high] = lnb_band_helper(lnb);
	return {low, high};
}

bool chdb::lnb_can_tune_to_mux(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool disregard_networks, ss::string_* error) {
	auto [freq_low, freq_mid, freq_high] = lnb_band_helper(lnb);
	if (mux.frequency < freq_low || mux.frequency >= freq_high) {
		if(error) {
		error->sprintf("Frequency %.3fMhz out for range; must be between %.3fMhz and %.3fMhz",
							 mux.frequency/(float)1000, freq_low/float(1000), freq_high/(float)1000);
		}
		return false;
	}
	if (!chdb::lnb::can_pol(lnb, mux.pol)) {
		if(error) {
			*error << "Polarisation " << mux.pol << " not supported";
		}
		return false;
	}
	if (disregard_networks)
		return true;
	for (auto& network : lnb.networks) {
		if (network.sat_pos == mux.k.sat_pos)
			return true;
	}
	if(error) {
		*error << "No network for  " << sat_pos_str(mux.k.sat_pos);
	}
	return false;
}

/*
	band = 0 or 1 for low or high (22Khz off/on)
	voltage = 0 (H,L, 13V) or 1 (V, R, 18V) or 2 (off)
	freq: frequency after LNB local oscilllator compensation

	Reasons why lnb cannot tune mux: c_band lnb cannot tune ku-band mux; lnb has wrong polarisation
*/
std::tuple<int, int, int> chdb::lnb::band_voltage_freq_for_mux(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
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
	} break;
	case lnb_type_t::UNIV: {
		auto [freq_low, freq_mid, freq_high] = lnb_band_helper(lnb);
		band = (mux.frequency >= freq_mid);
	} break;
	case lnb_type_t::KU: {
		band = 0;
	} break;
	default:
		assert(0);
	}
	voltage = voltage_for_pol(lnb, mux.pol);
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
		band = (signed)frequency >= freq_mid;

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
	frequency = std::abs(frequency);
	if (band < lnb.lof_offsets.size()) {
		if (std::abs(lnb.lof_offsets[band]) < 5000)
			frequency += lnb.lof_offsets[band];
	}
	return frequency;
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
	bool invert{false};
	auto correct = [&lnb, invert](int band, int frequency) {
		if (band >= lnb.lof_offsets.size()) {
			dterror("lnb_loffsets too small for lnb: " << lnb);
			return frequency;
		}
		if (std::abs(lnb.lof_offsets[band]) < 5000) {
			if(invert)
				frequency += lnb.lof_offsets[band];
			else
				frequency -= lnb.lof_offsets[band];
		}
		return frequency;
	};

	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		invert = true;
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



chdb::dvbs_mux_t chdb::lnb::select_reference_mux(db_txn& rtxn, const chdb::lnb_t& lnb,
																								 const chdb::dvbs_mux_t* proposed_mux) {
	auto return_mux = [&rtxn, &lnb](const chdb::lnb_network_t& network) {
		auto c = dvbs_mux_t::find_by_key(rtxn, network.ref_mux);
		if (c.is_valid()) {
			auto mux = c.current();
			if (chdb::lnb::can_pol(lnb, mux.pol))
				return mux;
		}
			c = dvbs_mux_t::find_by_key(rtxn, network.sat_pos, 0, 0, 0, find_type_t::find_geq,
																	dvbs_mux_t::partial_keys_t::sat_pos);
		if (c.is_valid()) {
			auto mux = c.current();
			if (chdb::lnb::can_pol(lnb, mux.pol))
				return mux;
		}
		auto mux = dvbs_mux_t();
		mux.k.sat_pos = network.sat_pos; //handle case where reference mux is absent
		mux.pol = chdb::lnb::pol_for_voltage(lnb, 0); //select default
		return mux;
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
		return dvbs_mux_t(); //  has sat_pos == sat_pos_none; no network present
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
		auto [has_network, network_priority, usals_move_amount] = chdb::lnb::has_network(lnb, sat.sat_pos);
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
		auto [has_network, network_priority, usals_move_amount] = chdb::lnb::has_network(lnb, sat.sat_pos);
		assert (usals_move_amount == 0);
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

int chdb::dish::update_usals_pos(db_txn& wtxn, int dish_id, int usals_pos)
{
	auto c = chdb::find_first<chdb::lnb_t>(wtxn);
	int num_rotors = 0; //for sanity check
	for(auto lnb : c.range()) {
		if(lnb.k.dish_id != dish_id || !chdb::on_rotor(lnb))
			continue;
		num_rotors++;
		lnb.usals_pos = usals_pos;
		put_record(wtxn, lnb);
	}
	if (num_rotors == 0) {
		dterrorx("None of the LNBs for dish %d seems to be on a rotor", dish_id);
		return -1 ;
	}
	return 0;
}



/*
	Find the current usals_posi for the desired sat_pos and compare it with the
	current usals_pos of the dish.

	As all lnbs on the same dish agree on usals_pos, we can stop when we find the first one
 */
bool chdb::dish::dish_needs_to_be_moved(db_txn& rtxn, int dish_id, int16_t sat_pos)
{
	auto c = chdb::find_first<chdb::lnb_t>(rtxn);
	int num_rotors = 0; //for sanity check

	for(auto lnb : c.range()) {
		if(lnb.k.dish_id != dish_id || !chdb::on_rotor(lnb))
			continue;
		num_rotors++;
		auto [h, priority, usals_amount] =  chdb::lnb::has_network(lnb, sat_pos);
		if(h) {
			return usals_amount != 0;
		}
	}
	if (num_rotors == 0) {
		dterrorx("None of the LNBs for dish %d seems to be on a rotor", dish_id);
	}
	return false;
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

int chdb::lnb::voltage_for_pol(const chdb::lnb_t& lnb, const chdb::fe_polarisation_t pol) {
	if(lnb::swapped_pol(lnb))
		return
		(pol == fe_polarisation_t::V || pol == fe_polarisation_t::R)
			? SEC_VOLTAGE_18 : SEC_VOLTAGE_13;
	else
		return
			(pol == fe_polarisation_t::V || pol == fe_polarisation_t::R)
			? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;
}

chdb::fe_polarisation_t chdb::lnb::pol_for_voltage(const chdb::lnb_t& lnb, int voltage_) {
	auto voltage = (fe_sec_voltage_t) voltage_;
	if(voltage != SEC_VOLTAGE_18 && voltage != SEC_VOLTAGE_13)
		return  chdb::fe_polarisation_t::UNKNOWN;
	bool high_voltage = (voltage == SEC_VOLTAGE_18);
	switch(lnb.pol_type) {
	case chdb::lnb_pol_type_t::HV:
		return high_voltage 	? chdb::fe_polarisation_t::H : chdb::fe_polarisation_t::V;
	case chdb::lnb_pol_type_t::VH:
		return (!high_voltage) 	? chdb::fe_polarisation_t::H : chdb::fe_polarisation_t::V;
	case chdb::lnb_pol_type_t::LR:
		return high_voltage 	? chdb::fe_polarisation_t::L : chdb::fe_polarisation_t::R;
	case chdb::lnb_pol_type_t::RL:
		return (!high_voltage) 	? chdb::fe_polarisation_t::L : chdb::fe_polarisation_t::R;
	case chdb::lnb_pol_type_t::H:
		return chdb::fe_polarisation_t::H;
	case chdb::lnb_pol_type_t::V:
		return chdb::fe_polarisation_t::V;
	case chdb::lnb_pol_type_t::L:
		return chdb::fe_polarisation_t::L;
	case chdb::lnb_pol_type_t::R:
		return chdb::fe_polarisation_t::R;
	default:
		return chdb::fe_polarisation_t::H;
	}
}

bool chdb::lnb::can_pol(const chdb::lnb_t &  lnb, chdb::fe_polarisation_t pol)
{
	switch(lnb.pol_type) {
	case chdb::lnb_pol_type_t::HV:
	case chdb::lnb_pol_type_t::VH:
		return pol == chdb::fe_polarisation_t::H || pol == chdb::fe_polarisation_t::V;
		break;
	case chdb::lnb_pol_type_t::H:
		return pol == chdb::fe_polarisation_t::H;
		break;
	case chdb::lnb_pol_type_t::V:
		return pol == chdb::fe_polarisation_t::V;
		break;
	case chdb::lnb_pol_type_t::LR:
	case chdb::lnb_pol_type_t::RL:
		return pol == chdb::fe_polarisation_t::L || pol == chdb::fe_polarisation_t::R;
		break;
	case chdb::lnb_pol_type_t::L:
		return pol == chdb::fe_polarisation_t::L;
		break;
	case chdb::lnb_pol_type_t::R:
		return pol == chdb::fe_polarisation_t::R;
		break;
	default:
		return false;
	}
}

void chdb::lnb::update_lnb(db_txn& wtxn, chdb::lnb_t&  lnb)
{
	bool found=false;
	switch(lnb.rotor_control) {
	case chdb::rotor_control_t::ROTOR_MASTER_USALS:
		//replace all diseqc12 commands with USALS commands
		for(auto& c: lnb.tune_string) {
			if (c=='X') {
				c = 'P';
			} found = true;
		}
		if (!found)
			lnb.tune_string.push_back('P');
		break;
	case chdb::rotor_control_t::ROTOR_MASTER_DISEQC12:
		//replace all usals commands with diseqc12 commands
		for(auto& c: lnb.tune_string) {
			if (c=='P') {
				c = 'X';
			} found = true;
		}
		if (!found)
			lnb.tune_string.push_back('X');
		break;
	default:
		break;
	}
	put_record(wtxn, lnb);
}



template<typename mux_t> static void clean(db_txn& wtxn)
{
	using namespace chdb;
	int count{0};
	auto c = mux_t::find_by_scan_status(wtxn, scan_status_t::PENDING, find_type_t::find_geq,
																			mux_t::partial_keys_t::scan_status);

	for(auto mux: c.range())  {
		assert (mux.c.scan_status == chdb::scan_status_t::PENDING);
		mux.c.scan_status = chdb::scan_status_t::IDLE;
		put_record(wtxn, mux);
		count++;
	}

	c = mux_t::find_by_scan_status(wtxn, scan_status_t::ACTIVE, find_type_t::find_geq,
																 mux_t::partial_keys_t::scan_status);
	for(auto mux: c.range())  {
		assert (mux.c.scan_status == chdb::scan_status_t::ACTIVE);
		mux.c.scan_status = chdb::scan_status_t::IDLE;
		put_record(wtxn, mux);
		count++;
	}

	dtdebugx("Cleaned %d muxes with PENDING/ACTIVE status", count);
}

void chdb::clean_scan_status(db_txn& wtxn)
{
	clean<chdb::dvbs_mux_t>(wtxn);
	clean<chdb::dvbc_mux_t>(wtxn);
	clean<chdb::dvbt_mux_t>(wtxn);
}
