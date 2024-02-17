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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "util/dtassert.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "receiver/neumofrontend.h"
#include "util/template_util.h"
#include <signal.h>
#include <limits>

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

/*
	make a unique id for a dvbs_mux; together with sat_pos, stream_id and k.t2mi_pid
	this forms a unique key that is used to (re)identify the mux even when ts_id, network_id
	or frequency change.
	The code uses the current (initial) frequency to create the id,
	but in general no reltion should be assumed and it should be treated as an opaque integer

possible values:
	0 : invalid data (e.g. for use with db upgrade code
	positive integer: valid and will never change, even if other data changes
 */
static void make_mux_id_helper(db_txn& rtxn, int frequency, mux_key_t& mux_key) {
	assert(mux_key.mux_id ==0); //never change mux_id on existing mux
	uint16_t mux_id = 1+(frequency % (0xffff-1));
	assert(mux_id>0);
	int count=0;
	mux_key.mux_id = mux_id;
	auto key_prefix =  dvbs_mux_t::partial_keys_t::sat_pos_stream_id_t2mi_pid;
	auto find_prefix =  dvbs_mux_t::partial_keys_t::all;
	auto c = chdb::dvbs_mux_t::find_by_key(rtxn, mux_key, find_geq, key_prefix, find_prefix);
	mux_key.mux_id = 0 ;//restore
	for(const auto & mux: c.range()) {
		if(mux.k.mux_id > mux_id) {
			assert(mux_id !=0);
			mux_key.mux_id = mux_id;
			return;
		}
		assert(mux.k.mux_id == mux_id);
		mux_id++; //may overflow
		if(mux_id==0)
			mux_id++;
		if(count>=10000) {
			dterrorf("Unexpected: ran out of ids\n");
			return;
		}
	}
	assert(mux_id>0);
	mux_key.mux_id = mux_id; //if we reach this point, we have examined all muxes
}


template<typename mux_t>
	requires (is_same_type_v<mux_t, chdb::dvbs_mux_t> || is_same_type_v<mux_t, chdb::dvbc_mux_t>
						|| is_same_type_v<mux_t, chdb::dvbt_mux_t>)
void chdb::make_mux_id(db_txn& rtxn, mux_t& mux) {
		make_mux_id_helper(rtxn, mux.frequency, mux.k);
}

void chdb::make_mux_id(db_txn& rtxn, chdb::any_mux_t& mux) {
	std::visit([&rtxn](auto& mux) {
		make_mux_id_helper(rtxn, mux.frequency, mux.k);}, mux);
}

int32_t chdb::make_unique_id(db_txn& txn, chg_key_t key) {
	auto c = chg_t::find_by_key(txn, key, find_geq);
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
		dterrorf("Overflow for bouquet_id");
		assert(0);
	}
	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.bouquet_id)>::max(); // highest possible value
}

int32_t chdb::make_unique_id(db_txn& txn, chgm_key_t key) {
	auto c = chgm_t::find_by_key(txn, key, find_geq);
	int gap_start = 1; // start of a potential gap of unused ids
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
		dterrorf("Overflow for chgm_id");
		assert(0);
	}
	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.channel_id)>::max(); // highest possible value
}

static inline void copy_tuning(dvbc_mux_t& mux, const dvbc_mux_t& db_mux) {
	mux.delivery_system = db_mux.delivery_system;
	mux.frequency = db_mux.frequency;
	mux.inversion = db_mux.inversion;
	mux.symbol_rate = db_mux.symbol_rate;
	mux.modulation = db_mux.modulation;
	mux.fec_inner = db_mux.fec_inner;
	mux.fec_outer = db_mux.fec_outer;
	mux.k.stream_id = db_mux.k.stream_id;
	mux.c.tune_src = db_mux.c.tune_src;
}

static inline void copy_tuning(dvbt_mux_t& mux, const dvbt_mux_t& db_mux) {
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
	mux.k.stream_id = db_mux.k.stream_id;
	mux.c.tune_src = db_mux.c.tune_src;
}

static inline void copy_tuning(dvbs_mux_t& mux, const dvbs_mux_t& db_mux) {
	mux.delivery_system = db_mux.delivery_system;
	mux.frequency = db_mux.frequency;
	mux.inversion = db_mux.inversion;
	mux.pol = db_mux.pol;
	mux.symbol_rate = db_mux.symbol_rate;
	mux.modulation = db_mux.modulation;
	mux.fec = db_mux.fec;
	mux.rolloff = db_mux.rolloff;
	mux.pilot = db_mux.pilot;
#if 0
	mux.k.stream_id = db_mux.k.stream_id;
#endif
	mux.pls_mode = db_mux.pls_mode;
	mux.matype = db_mux.matype;
	mux.pls_code = db_mux.pls_code;
	mux.c.tune_src = db_mux.c.tune_src;
}

/*
	move services from a source mux to a destination mux if they
	do not exist for the destination mux. Else copy ch_order

	todo: this needs to be cascaded to channels
 */

void chdb::merge_services(db_txn& wtxn, const mux_key_t& src_key, const chdb::any_mux_t& dst) {
	auto c = chdb::service::find_by_mux_key(wtxn, src_key);
	for(auto service: c.range()) {
		service.k.mux = * mux_key_ptr(dst);
		auto c1 = chdb::service_t::find_by_key(wtxn, service.k.mux, service.k.service_id);
		if (c1.is_valid()) {
			auto dst_service = c1.current();
			if(dst_service.ch_order ==0 && service.ch_order !=0) {
				dst_service.ch_order = service.ch_order;
				put_record(wtxn, dst_service);
			}
		} else {
			std::visit([&](auto &mux) {
			service.frequency = mux.frequency;
			service.pol = get_member(mux, pol, chdb::fe_polarisation_t::NONE);}, dst);
			put_record(wtxn, service);
		}
	}
}

void chdb::remove_services(db_txn& wtxn, const mux_key_t& mux_key) {
	auto c = chdb::service::find_by_mux_key(wtxn, mux_key);
	for(const auto& service: c.range()) {
		assert(service.k.mux == mux_key);
		delete_record(c, service);
	}
}

/*
	selectively replace some data in mux by data in db_mux, while preserving the data specified in
	preserve and taking into account the tune_src fields, which specify where the data comes from
 */

template <typename mux_t>
void merge_muxes(mux_t& mux, const mux_t& db_mux, update_mux_preserve_t::flags preserve) {
	namespace m = update_mux_preserve_t;
	dtdebugf("db_mux={}-> mux={}", db_mux, mux);
	assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
					mux.c.scan_status != chdb::scan_status_t::PENDING) ||
				 scan_in_progress(mux.c.scan_id));

	if( (preserve & m::MUX_KEY) || mux_key_ptr(mux)->mux_id == 0) {
		//template mux key entered by user is never considered valid, so use database value
		mux.k = db_mux.k;
	}
	if(db_mux.c.tune_src == tune_src_t::USER && mux.c.tune_src != tune_src_t::AUTO) {
		/*preserve all tuning data, unless the user wants to turn this off, which is indicated by
			mux.c.tune_src == tune_src_t::MANUAL
		*/
		mux.c.tune_src = tune_src_t::USER;
		copy_tuning(mux, db_mux); //preserve what is in the database
	}
#if 0
	if( (preserve & m::MUX_KEY) ) { //actual copying handled by caller
		mux.c.key_src = db_mux.c.key_src;
	}
#endif
	if (preserve & m::TUNE_DATA) {
		copy_tuning(mux, db_mux); //preserve what is in the database
	}
	if (preserve & m::SCAN_DATA) {
		mux.c.scan_time = db_mux.c.scan_time;
		mux.c.scan_result = db_mux.c.scan_result;
		mux.c.scan_lock_result = db_mux.c.scan_lock_result;
		mux.c.epg_scan_completeness = db_mux.c.epg_scan_completeness;
		mux.c.scan_duration = db_mux.c.scan_duration;
		mux.c.epg_scan = db_mux.c.epg_scan;
	}

	if (preserve & m::SCAN_STATUS) {
		mux.c.scan_status = db_mux.c.scan_status;
		mux.c.scan_id = db_mux.c.scan_id;

		assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux.c.scan_status != chdb::scan_status_t::PENDING) ||
					 scan_in_progress(mux.c.scan_id));

	}
	dtdebugf("after merge db_mux={} mux={} preserve&SCAN_STATUS={}", db_mux, mux,
					 (preserve & m::SCAN_STATUS));


	if (preserve & m::NUM_SERVICES)
		mux.c.num_services = db_mux.c.num_services;

	if (preserve & m::EPG_TYPES)
		mux.c.epg_types = db_mux.c.epg_types;

	if (preserve & m::NIT_SI_DATA) {
		mux.c.nit_network_id = db_mux.c.nit_network_id;
		mux.c.nit_ts_id = db_mux.c.nit_ts_id;
	}
	if (preserve & m::SDT_SI_DATA) {
		mux.c.network_id = db_mux.c.network_id;
		mux.c.ts_id = db_mux.c.ts_id;
		mux.c.key_src = db_mux.c.key_src;
	}
}

/*! Put a mux record, taking into account that its key may have changed
	returns: true if this is a new mux (first time scanned); false otherwise
	Also, updates the mux, specifically k.extra_id


	Either change the found mux, but preserve some of its data (depending on preserver(
	or insert a new one.

	In case MUX_KEY preservation is requested: do not change the mux_key (including extra_id)

	for must_exist: return without saving if existing mux cannot be found and update would create a new mux

*/
template <typename mux_t>
update_mux_ret_t chdb::update_mux(db_txn& wtxn, mux_t& mux_to_save, system_time_t now_,
																	update_mux_preserve_t::flags preserve,
																	update_mux_cb_t cb,
																	/*bool ignore_key,*/ bool ignore_t2mi_pid, bool must_exist)
{
	auto now = system_clock_t::to_time_t(now_);
	//assert(mux.c.tune_src != tune_src_t::TEMPLATE);
	update_mux_ret_t ret = update_mux_ret_t::UNKNOWN;
	mux_t db_mux;
	/* The following cases need to be handled
		 a) the tp does not yet exist for this network_id, ts_id, and there is also no tp with matching freq/pol
		 => insert new mux in the db; ensure unique extra_id
		 b) tp with (more or less) correct frequency and polarisation exists but has a different network_id or ts_id
		 with network_id and ts_id !=0
		 => delete the old mux, which is clearly invalid now; and insert the new mux with unique extra_id (as in case a)
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
	/*find mux with matching frequency, stream_id and t2mi_pid*/
#ifndef NDEBUG
	auto& mux_common = *mux_common_ptr(mux_to_save);
	if(!((mux_to_save.k.mux_id > 0) ||
				 (is_template(mux_to_save) || mux_common.tune_src== tune_src_t::NIT_TUNED
					|| mux_common.tune_src== tune_src_t::NIT_CORRECTED
					|| mux_common.tune_src == tune_src_t::NIT_ACTUAL
					|| mux_common.tune_src == tune_src_t::NIT_OTHER
					|| mux_common.tune_src == tune_src_t::AUTO
					|| mux_common.tune_src == tune_src_t::DRIVER)
			 )) {
		dterrorf("detected incorrect tune_src={:d}\n", (int) mux_common.tune_src);
	}
#endif
	/*a template mux with mux.k.stream_id==-1 can mean two things: 1) user wants to tune to a SIS stream
		2) User wants to tune to a MIS stream with unspecified stream_id, and specifies mux.k.stream_id=-1
		(default when clicking in spectrum dialog)
		In case 2 we set ignore_stream_id true.
	 */
	auto ignore_stream_id = (is_template(mux_to_save) && mux_to_save.k.stream_id==-1);
	db_tcursor<mux_t> c = mux_to_save.k.mux_id > 0 ? chdb::find_by_mux(wtxn, mux_to_save) :
		find_by_mux_physical(wtxn, mux_to_save, ignore_stream_id /*ignore_stream_id*/, ignore_t2mi_pid);
#if 0
	bool delete_db_mux{false};
#endif
	bool is_new = true; // do we modify an existing record or create a new one?
	auto merged_mux = mux_to_save;
	if (c.is_valid()) { //mux exists
		db_mux = c.current();
#ifndef NDEBUG
		assert(db_mux.k.stream_id == mux_to_save.k.stream_id);
		assert(db_mux.k.t2mi_pid == mux_to_save.k.t2mi_pid);
		if(db_mux.k.mux_id == 0) {
			make_mux_id(wtxn, db_mux);
			dterrorf("Illegal mux found in db and fixed: {}", db_mux);
		}
		assert(db_mux.k.mux_id > 0);
		auto tmp_key = mux_to_save.k;
		tmp_key.mux_id = db_mux.k.mux_id;
		auto key_matches = tmp_key == db_mux.k;
		assert(key_matches);
#else
#endif
		assert((mux_to_save.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux_to_save.c.scan_status != chdb::scan_status_t::PENDING) ||
					 scan_in_progress(mux_to_save.c.scan_id));
		mux_to_save.k = db_mux.k;

		ret = update_mux_ret_t::MATCHING_KEY_AND_FREQ;

		is_new = false;
		assert((mux_to_save.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux_to_save.c.scan_status != chdb::scan_status_t::PENDING) ||
					 scan_in_progress(mux_to_save.c.scan_id));
#ifndef NDEBUG
		key_matches = (mux_to_save.k == db_mux.k); //key can NOT be changed by cb()
#if 0
		delete_db_mux = !key_matches;
#endif
#endif

		if(!is_new) {
			merge_muxes<mux_t>(merged_mux, db_mux, preserve);
			if(!cb(&merged_mux.c, &merged_mux.k, &db_mux.c, &db_mux.k))
				return update_mux_ret_t::NO_MATCHING_KEY;

			auto& c = merged_mux.c;
			auto& dbc = *mux_common_ptr(db_mux);
			auto sdt_key_matches =  (c.network_id == dbc.network_id) && (c.ts_id == dbc.ts_id);
			if(!sdt_key_matches) {
				int matype = get_member(merged_mux, matype, 0x3 << 14);
				int db_matype = get_member(db_mux, matype, 0x3 << 14);
				auto is_dvb = ((matype >> 14) & 0x3) == 0x3;
				auto db_mux_is_dvb = ((db_matype >> 14) & 0x3) == 0x3;
				if(is_dvb && db_mux_is_dvb)
					merge_services(wtxn, db_mux.k, merged_mux);
				else if (!is_dvb && db_mux_is_dvb)
					remove_services(wtxn, db_mux.k);
			}
		}

		dtdebugf("Transponder {}: sat_pos={:d} => {:d}; mux_id={:d} => {:d}; stream_id={:d} => {:d}; t2mi_pid={:d} =< {:d}",
						 merged_mux,
						 db_mux.k.sat_pos, merged_mux.k.sat_pos,
						 db_mux.k.mux_id, merged_mux.k.mux_id,
						 db_mux.k.stream_id, merged_mux.k.stream_id,
						 db_mux.k.t2mi_pid, merged_mux.k.t2mi_pid);
		//assert( mux.c.tune_src != tune_src_t::TEMPLATE );

		ret = is_new ? update_mux_ret_t::NO_MATCHING_KEY : update_mux_ret_t::MATCHING_KEY_AND_FREQ;
	} else { //no mux in db
		if(must_exist)
			return update_mux_ret_t::NO_MATCHING_KEY;
		ret = update_mux_ret_t::NEW;
		/*
			It is possible that mux.k.mux_id > 0 for a new mux, e.g., because all muxes differing only in stream_id
			are deliberately given the same mux_id
		 */
		if(merged_mux.k.mux_id == 0)
			make_mux_id(wtxn, merged_mux);
		if(!cb(&merged_mux.c, &merged_mux.k, nullptr, nullptr))
			return update_mux_ret_t::NO_MATCHING_KEY;
	}

	assert(!is_template(mux_to_save));
	assert(ret != update_mux_ret_t::UNKNOWN);
	// the database has a mux, but we may need to update it
	mux_to_save = merged_mux;
	if (!is_new && chdb::is_same(db_mux, merged_mux))
		ret = update_mux_ret_t::EQUAL;

	if (is_new || ret != update_mux_ret_t::EQUAL) {
		/*We found match, either based on key or on frequency
			if the match was based on frequency, an existing mux may be overwritten!
		*/
		mux_to_save.c.mtime = now;

		dtdebugf("NIT {}: old={} new={} #s={:d}", is_new ? "NEW" : "CHANGED", db_mux, mux_to_save,
						 mux_to_save.c.num_services);
		assert(mux_to_save.frequency > 0);

		dtdebugf("mux={}", mux_to_save);
		assert(mux_to_save.k.mux_id > 0);
		assert(!is_template(mux_to_save));
#if 0
		if(delete_db_mux) {
			delete_record(c, db_mux);
		}
#endif
		put_record(wtxn, mux_to_save);
	}
	return ret;
}

update_mux_ret_t chdb::update_mux(db_txn& wtxn, chdb::any_mux_t& mux, system_time_t now,
																	update_mux_preserve_t::flags preserve,
																	update_mux_cb_t cb,
																	/*bool ignore_key, */ bool ignore_t2mi_pid, bool must_exist) {
	using namespace chdb;
	update_mux_ret_t ret;
	visit_variant(
		mux,
		[&](chdb::dvbs_mux_t& mux) {
			if(mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_DVBS)
				mux.matype = 256;
			ret = chdb::update_mux(wtxn, mux, now, preserve, cb, /*ignore_key,*/ ignore_t2mi_pid, must_exist); },
		[&](chdb::dvbc_mux_t& mux) { ret = chdb::update_mux(wtxn, mux, now, preserve, cb,
																												/*ignore_key,*/ ignore_t2mi_pid, must_exist); },
		[&](chdb::dvbt_mux_t& mux) { ret = chdb::update_mux(wtxn, mux, now, preserve, cb,
																												/*ignore_key,*/ ignore_t2mi_pid, must_exist); });
	return ret;
}


void chdb::sat_pos_str(ss::string_& s, int position) {
	if (position == sat_pos_dvbc) {
		s.format("DVBC");
	} else if (position == sat_pos_dvbt) {
		s.format("DVBT");
	} else if (position == sat_pos_none) {
		s.format("----");
	} else {
		auto fpos = std::abs(position) / (double)100.;
		s.format("{:3.1f}{:c}", fpos, position < 0 ? 'W' : 'E');
	}
}

void chdb::matype_str(ss::string_& s, int16_t matype, int rolloff) {
	// See en 302 307 v1.1.2; stid135 manual seems wrong in places
	// en_302307v010201p_DVBS2.pdf
	bool is_ts{false};
	if( matype == 256 ) { //dvbs
		s.format("DVBS");
		return;
	}
	if(matype <0) {
		s.format("");
		return;
	}
	if( matype <0 ) { //not tuned yet; matype unknown
		s.format("");
		return;
	}
	switch (matype >> 6) {
	case 0:
		s.format("GFP "); ///generic packetised stream
		break;
	case 1:
		s.format("GCS "); //generic continuous stream
		break;
	case 2:
		s.format("GSE "); //generic encapsulated stream
		break;
	case 3:
		s.format("TS "); //transport stream
		is_ts=true;
		break;
#if 0
	case 4:
		s.format("GSEL "); //GSE Lite
		break;
#endif
	}

	if ((matype >> 5) & 1)
		s.format("SIS ");
	else
		s.format("MIS ");

	if ((matype >> 4) & 1)
		s.format("CCM ");
	else
		s.format("VCM ");
	if ((matype >> 3) & 1)
		s.format("ISSYI ");

	if ((matype >> 2) & 1)
		is_ts ? s.format("NPD ") : s.format("Lite ");
	if(rolloff>=0) {
		switch((fe_rolloff) rolloff) {
		case 	ROLLOFF_35: s.format("35%"); break;
		case 	ROLLOFF_20: s.format("20%"); break;
		case 	ROLLOFF_25: s.format("25%"); break;
		case 	ROLLOFF_LOW: s.format("low%"); break;
		case 	ROLLOFF_15: s.format("15%"); break;
		case 	ROLLOFF_10: s.format("10%"); break;
		case 	ROLLOFF_5: s.format("5%"); break;
		default:
			s.format("??"); break;
		}
	} else {
		switch (matype & 3) {
		case 0:
			s.format("35%");
			break;
		case 1:
			s.format("25%");
			break;
		case 2:
			s.format("20%");
			break;
		case 3:
			s.format("LO"); //low rollof (requires inspection of multiple bbframes)
			break;
		}
	}
}

inline static const char* scan_status_name(const chdb::scan_status_t& scan_status) {
	switch(scan_status) {
	case scan_status_t::PENDING:
		return "PENDING";
	case scan_status_t::IDLE:
		return "IDLE";
	case scan_status_t::ACTIVE:
		return "ACTIVE";
	case scan_status_t::NONE:
		return "NONE";
	case scan_status_t::RETRY:
		return "RETRY";
	}
	return "??";
}

inline static const char* scan_result_name(const chdb::scan_result_t& scan_result) {
	switch(scan_result) {
	case scan_result_t::NONE:
		return "NONE";
	case scan_result_t::NOLOCK:
		return "NOLOCK";
	case scan_result_t::ABORTED:
		return "ABORTED";
	case scan_result_t::PARTIAL:
		return "PARTIAL";
	case scan_result_t::OK:
		return "OK";
	case scan_result_t::NODATA:
		return "NODATA";
	case scan_result_t::NOTS:
		return "NODVB";
	case scan_result_t::BAD:
		return "BAD";
	case scan_result_t::TEMPFAIL:
		return "TMPFAIL";
	case scan_result_t::DISABLED:
		return "DISABLED";
	}
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

	if (!(a.epg_scan_completeness == b.epg_scan_completeness))
		return false;

	if (!(a.scan_lock_result == b.scan_lock_result))
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
	if (!(a.k.stream_id == b.k.stream_id))
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
	if (!(a.k.stream_id == b.k.stream_id))
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
	if (!(a.k.stream_id == b.k.stream_id))
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


void chdb::service::update_audio_pref(db_txn& txn, const chdb::service_t& from_service) {
	auto c = chdb::service_t::find_by_key(txn, from_service.k.mux, from_service.k.service_id);
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
	auto c = chdb::service_t::find_by_key(txn, from_service.k.mux, from_service.k.service_id);
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

bool chdb::has_epg_type(const chdb::any_mux_t& mux, chdb::epg_type_t epg_type) {
	auto* c = mux_common_ptr(mux);
	for (const auto& t : c->epg_types) {
		if (t == epg_type)
			return true;
	}
	return false;
}

chdb::media_mode_t chdb::media_mode_for_service_type(uint8_t service_type) {
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


/*
	Selects the network on the proposed lnb which best matches
	the lnb's usals position, i.e., the satellite position to which the dish points.

	In case the dish would need to be moved, return None, None
	In case the dish is not on a positioner, return the mux for the main network
 */
std::tuple<std::optional<chdb::dvbs_mux_t>, std::optional<chdb::sat_t>>
chdb::select_sat_and_reference_mux(db_txn& chdb_rtxn, const devdb::lnb_t& lnb,
																	 const chdb::dvbs_mux_t* proposed_mux) {
	auto return_some_mux = [&lnb](std::optional<chdb::sat_t>& sat)
		-> std::tuple<std::optional<chdb::dvbs_mux_t>, std::optional<chdb::sat_t>>
		{
			chdb::dvbs_mux_t mux;
			mux.k.sat_pos = sat->sat_pos;
			switch (lnb.pol_type) {
			case devdb::lnb_pol_type_t::HV:
			case devdb::lnb_pol_type_t::VH:
			case devdb::lnb_pol_type_t::H:
				mux.pol = chdb::fe_polarisation_t::H;
				break;
			case devdb::lnb_pol_type_t::V:
				mux.pol = chdb::fe_polarisation_t::V;
				break;
			case devdb::lnb_pol_type_t::LR:
			case devdb::lnb_pol_type_t::RL:
			case devdb::lnb_pol_type_t::L:
				mux.pol = chdb::fe_polarisation_t::L;
				break;
			case devdb::lnb_pol_type_t::R:
				mux.pol = chdb::fe_polarisation_t::R;
				break;
			default:
				break;
			}
			auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = devdb::lnb::band_frequencies(
				lnb, chdb::sat_sub_band_t::LOW);
			mux.frequency = freq_low;
			return {mux, sat};
		};

	auto return_mux = [&chdb_rtxn, &lnb, &return_some_mux](const devdb::lnb_network_t& network)
		-> std::tuple<std::optional<chdb::dvbs_mux_t>, std::optional<chdb::sat_t>>
		{
			auto cs = chdb::sat_t::find_by_key(chdb_rtxn, network.sat_pos, devdb::lnb::sat_band(lnb));
			std::optional<chdb::sat_t> sat;
			if (cs.is_valid())
				sat= cs.current();
#if 0
			else {
				for(auto& n: lnb.networks) {
					cs = chdb::sat_t::find_by_key(chdb_rtxn, n.sat_pos, devdb::lnb::sat_band(lnb));
					if(cs.is_valid()) {
						sat = cs.current();
						if(n.ref_mux.sat_pos == sat->sat_pos) {
							auto c = chdb::dvbs_mux_t::find_by_key(chdb_rtxn, network.ref_mux);
							if (c.is_valid()) {
								auto mux = c.current();
								if (devdb::lnb_can_tune_to_mux(lnb, mux, true /*disregard networks*/, nullptr /*error*/))
									return {mux, sat};
							}
						}
					}
				}
			}
#endif
			if(network.ref_mux.sat_pos != sat_pos_none) {
				auto c = chdb::dvbs_mux_t::find_by_key(chdb_rtxn, network.ref_mux);
				if (c.is_valid()) {
					auto mux = c.current();
					if (devdb::lnb_can_tune_to_mux(lnb, mux, true /*disregard networks*/, nullptr /*error*/))
						return {mux, sat};
				}
			}
			//pick the first mux on the sat as the ref mux, taking into account lnb type and polarisation
			auto c = chdb::dvbs_mux_t::find_by_key(chdb_rtxn, network.sat_pos, find_type_t::find_geq,
																				chdb::dvbs_mux_t::partial_keys_t::sat_pos);
			for(auto mux: c.range()) {
				if (devdb::lnb_can_tune_to_mux(lnb, mux, true /*disregard networks*/, nullptr /*error*/))
					return {mux, sat};
			}
			if(sat)
				return return_some_mux(sat);
			else
				return {{},{}};
		};

	using namespace chdb;
	const bool disregard_networks{false};
	if (proposed_mux && lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks)) {
		auto cs = chdb::sat_t::find_by_key(chdb_rtxn, proposed_mux->k.sat_pos, devdb::lnb::sat_band(lnb));
		return {*proposed_mux, cs.is_valid() ? cs.current() : chdb::sat_t{}};
	}
	{
		auto usals_pos = lnb.usals_pos;
		auto lnb_usals_pos = lnb.lnb_usals_pos;
		auto cur_sat_pos = lnb.cur_sat_pos;
		if(cur_sat_pos == sat_pos_none)
			cur_sat_pos = usals_pos;

		auto best = std::numeric_limits<int>::max();
		const devdb::lnb_network_t* bestp{nullptr};
		for (auto& network : lnb.networks) {
			if(usals_pos == sat_pos_none)
				usals_pos = network.usals_pos;
			if(lnb_usals_pos == sat_pos_none)
				lnb_usals_pos = network.sat_pos;
			if(cur_sat_pos == sat_pos_none)
				cur_sat_pos = network.sat_pos;
			auto delta =  std::abs(network.sat_pos - cur_sat_pos);
			if (delta < best) {
				best = delta;
				bestp = &network;
			}
		}

		if (bestp) {
			return return_mux(*bestp);
		} else if( bestp && !lnb.on_positioner) {
			return return_mux(*bestp);
		}
		auto cs = chdb::sat_t::find_by_key(chdb_rtxn, lnb.lnb_usals_pos, devdb::lnb::sat_band(lnb));
		std::optional<chdb::sat_t> sat = cs.is_valid() ? cs.current() : chdb::sat_t();
		if(!sat)
			return {{}, sat};
		return return_some_mux(sat);
	}
}


/*
	Selects and sets initial reference mux for  the current lnb and network
	returns False if input parameters invalid, else true.
	mux is selected from the database or a non-existing mux with some good defaults is created
 */
chdb::dvbs_mux_t
chdb::select_reference_mux(db_txn& chdb_rtxn, const devdb::lnb_t& lnb,
																	 int16_t sat_pos) {
	using namespace chdb;
	using namespace devdb;
	fe_polarisation_t pol;
	switch(lnb.pol_type) {
	case lnb_pol_type_t::HV:
	case lnb_pol_type_t::VH:
	case lnb_pol_type_t::H:
		pol = fe_polarisation_t::H;
		break;
	case lnb_pol_type_t::V:
		pol = fe_polarisation_t::V;
		break;
	case lnb_pol_type_t::LR:
	case lnb_pol_type_t::RL:
	case lnb_pol_type_t::L:
		pol = fe_polarisation_t::L;
		break;
	case lnb_pol_type_t::R:
		pol = fe_polarisation_t::R;
		break;
	default:
		pol = fe_polarisation_t::R;
		break;
	}
	auto [low_freq, high_freq] = devdb::lnb::lnb_frequency_range(lnb);

	assert(sat_pos != sat_pos_none);

	auto c = dvbs_mux_t::find_by_sat_pol_freq
		(chdb_rtxn, sat_pos, pol, low_freq,
		 find_type_t::find_geq, dvbs_mux_t::partial_keys_t::sat_pos_pol);
	if(c.is_valid()) {
		return c.current();
	} else {
		dvbs_mux_t ret;
		ret.k.sat_pos = sat_pos;
		ret.frequency = low_freq;
		ret.pol = pol;
		return ret;
	}
}

std::tuple<chdb::sat_band_t, chdb::sat_sub_band_t> chdb::sat_band_for_freq(int frequency) {
	using namespace chdb;
	using namespace devdb;
	if(frequency >= 3400000  && frequency <= 4200000)
		return {sat_band_t::C, sat_sub_band_t::LOW};
	if(frequency >= 10700000  && frequency < 11700000)
		return {sat_band_t::Ku, sat_sub_band_t::LOW};
	if(frequency >= 11700000  && frequency < 12750000)
		return {sat_band_t::Ku, sat_sub_band_t::HIGH};
	if(frequency >= 18200000 && frequency < 19200000)
		return {sat_band_t::KaA, sat_sub_band_t::LOW};
	if(frequency >=  19200000 && frequency < 20200000)
		return {sat_band_t::KaB, sat_sub_band_t::LOW};
	if(frequency >= 20200000 && frequency < 21200000)
		return {sat_band_t::KaC, sat_sub_band_t::LOW};
	if(frequency >= 21200000 && frequency < 22200000)
		return {sat_band_t::KaD, sat_sub_band_t::LOW};
	if(frequency >= 17200000 && frequency < 18200000)
		return {sat_band_t::KaE, sat_sub_band_t::LOW};
	return {sat_band_t::UNKNOWN, sat_sub_band_t::LOW};
}

std::tuple<int32_t, int32_t> chdb::sat_band_freq_bounds(chdb::sat_band_t sat_band, chdb::sat_sub_band_t sub_band) {
	using namespace chdb;
	switch(sat_band) {
	case sat_band_t::C:
		return {3400000, 4200000};
	case sat_band_t::Ku:
		switch(sub_band) {
		default:
		case sat_sub_band_t::NONE:
			return {10700000, 12750000};
		case sat_sub_band_t::LOW:
			return {10700000, 11700000};
		case sat_sub_band_t::HIGH:
			return {11700000, 12750000};
		}
		break;
	case sat_band_t::KaA:
		return {18200000, 19200000};
	case sat_band_t::KaB:
		return {19200000, 20200000};
	case sat_band_t::KaC:
		return {20200000, 21200000};
	case sat_band_t::KaD:
		return {21200000, 22200000};
	case sat_band_t::KaE:
		return {17200000, 18200000};
	default:
		return {0, std::numeric_limits<int32_t>::max()};
	}
}

chdb::band_scan_t& chdb::sat::band_scan_for_pol_sub_band(chdb::sat_t& sat,
																												 chdb::fe_polarisation_t pol,
																												 chdb::sat_sub_band_t sub_band) {

	using namespace chdb;
	for(auto & band_scan: sat.band_scans) {
		if (band_scan.pol == pol && band_scan.sat_sub_band == sub_band)
			return band_scan;
	}
	band_scan_t band_scan;
	band_scan.pol=pol;
	band_scan.sat_band = sat.sat_band;
	band_scan.sat_sub_band=sub_band;
	sat.band_scans.push_back(band_scan);
	return sat.band_scans[-1];
}


std::optional<chdb::sat_t>
chdb::select_sat_for_sat_band(db_txn& chdb_rtxn, const chdb::sat_band_t& sat_band, int sat_pos) {
	using namespace chdb;
	using namespace devdb;
	int last = std::numeric_limits<int>::max();
	sat_t last_sat;
	auto c = find_first<chdb::sat_t>(chdb_rtxn);
	for(const auto& sat: c.range()) {
		if(sat.sat_band != sat_band)
			continue;
		if(sat.sat_pos == sat_pos || sat_pos == sat_pos_none)
			return sat;
		auto diff = std::abs(sat.sat_pos - sat_pos);
		if(diff > last)
			return last_sat;
		last = diff;
		last_sat = sat;
	}
	return {};
}

void chdb::sat::clean_band_scan_pols(chdb::sat_t& sat, devdb::lnb_pol_type_t lnb_pol_type)
{
	using namespace chdb;
	bool have_hl{false};
	bool have_rv{false};
	for(int i=0; i < sat.band_scans.size(); ++i) {
		auto& band_scan = sat.band_scans[i];
		switch(band_scan.pol) {
		case fe_polarisation_t::H:
			if (lnb_pol_type != devdb::lnb_pol_type_t::HV || have_hl) {
				sat.band_scans.erase(i);
				have_hl = true;
				--i;
			}
			continue;
			break;
		case fe_polarisation_t::V:
			if (lnb_pol_type != devdb::lnb_pol_type_t::HV || have_rv) {
				sat.band_scans.erase(i);
				have_rv = true;
				--i;
			}
			continue;
			break;
		case fe_polarisation_t::L:
			if (lnb_pol_type != devdb::lnb_pol_type_t::LR || have_hl) {
				sat.band_scans.erase(i);
				have_hl = true;
				--i;
			}
			continue;
			break;
		case fe_polarisation_t::R:
			if (lnb_pol_type != devdb::lnb_pol_type_t::LR || have_rv) {
				sat.band_scans.erase(i);
				have_rv = true;
				--i;
			}
			continue;
			break;
		default:
			sat.band_scans.erase(i);
			--i;
			continue;
			break;
		}
	}
}



//template instantiations
template void chdb::make_mux_id<chdb::dvbs_mux_t>(db_txn& rtxn, chdb::dvbs_mux_t& mux);
template void chdb::make_mux_id<chdb::dvbc_mux_t>(db_txn& rtxn, chdb::dvbc_mux_t& mux);
template void chdb::make_mux_id<chdb::dvbt_mux_t>(db_txn& rtxn, chdb::dvbt_mux_t& mux);


fmt::format_context::iterator
fmt::formatter<chdb::scan_status_t>::format(const chdb::scan_status_t& scan_status, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}", scan_status_name(scan_status));
}

fmt::format_context::iterator
fmt::formatter<chdb::scan_result_t>::format(const chdb::scan_result_t& scan_result, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}", scan_result_name(scan_result));
}

fmt::format_context::iterator
fmt::formatter<chdb::language_code_t>::format(const chdb::language_code_t& code, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}", lang_name(code));
}

fmt::format_context::iterator
fmt::formatter<chdb::sat_t>::format(const chdb::sat_t& sat, format_context& ctx) const {
	if (sat.sat_pos == sat_pos_dvbc) {
		return fmt::format_to(ctx.out(), "DVBC");
	} else if (sat.sat_pos == sat_pos_dvbt) {
		return fmt::format_to(ctx.out(), "DVBT");
	} else if (sat.sat_pos == sat_pos_dvbs) {
		return fmt::format_to(ctx.out(), "DVBS");
	} else if (sat.sat_pos == sat_pos_none) {
		return fmt::format_to(ctx.out(), "----");
	} else {
		auto fpos = std::abs(sat.sat_pos) / (double)100.;
		return fmt::format_to(ctx.out(), "{:3.1f}{:c} {}", fpos, sat.sat_pos < 0 ? 'W' : 'E', to_str(sat.sat_band));
	}
}

fmt::format_context::iterator
fmt::formatter<chdb::dvbs_mux_t>::format(const chdb::dvbs_mux_t& mux, format_context& ctx) const {
	auto it = fmt::format_to(ctx.out(), "{:d}.{:03d}{:s}", mux.frequency / 1000,
													 mux.frequency%1000, to_str(mux.pol));
	if (mux.k.stream_id >= 0)
		it = fmt::format_to(ctx.out(), "-{:d}", mux.k.stream_id);
	if (mux.k.t2mi_pid >= 0)
		it = fmt::format_to(ctx.out(), "-T{:d}", mux.k.t2mi_pid);
#if 0
	it = fmt::format_to(ctx.out(), " {} {}", mux.k, mux.c.tune_src);
#endif
#if 0
	it = fmt::format_to(ctx.out(), " STA={:s}", scan_status_name(mux.c.scan_status));
#endif
	return it;
}

fmt::format_context::iterator
fmt::formatter<chdb::dvbc_mux_t>::format(const chdb::dvbc_mux_t& mux, format_context& ctx) const {
	auto it=ctx.out();
	if (mux.frequency % 1000 == 0)
		it = fmt::format_to(ctx.out(), " DVB-C {:d}MHz", mux.frequency / 1000);
	else
		it = fmt::format_to(ctx.out(), " DVB-C {:.3f}MHz", mux.frequency / (float)1000);
	if (mux.k.stream_id >= 0)
		it = fmt::format_to(ctx.out(), " stream {:d}", mux.k.stream_id);
	return it;
}


fmt::format_context::iterator
fmt::formatter<chdb::dvbt_mux_t>::format(const chdb::dvbt_mux_t& mux, format_context& ctx) const {
	auto it = ctx.out();
	if (mux.frequency % 1000 == 0)
			it = fmt::format_to(ctx.out(), " DVB-T {:d}MHz", mux.frequency / 1000);
	else
		it = fmt::format_to(ctx.out(), " DVB-T {:.3f}MHz", mux.frequency / (float)1000);
	if (mux.k.stream_id >= 0)
		it = fmt::format_to(ctx.out(), " stream {:d}", mux.k.stream_id);
	return it;
}

fmt::format_context::iterator
fmt::formatter<chdb::any_mux_t>::format(const chdb::any_mux_t& mux, format_context& ctx) const {
	using namespace chdb;
	auto ret = ctx.out();
	std::visit([&ret,&ctx](auto&&mux) {
		ret = fmt::format_to(ctx.out(), "{}", mux);
	}, mux);
	return ret;
}

fmt::format_context::iterator
fmt::formatter<chdb::mux_key_t>::format(const chdb::mux_key_t& k, format_context& ctx) const {
	auto sat = sat_pos_str(k.sat_pos);
	auto it = fmt::format_to(ctx.out(), "{} - mux {:d}", sat, k.mux_id);
	if(k.stream_id >=0)
		it = fmt::format_to(ctx.out(), "-{:d}", k.stream_id);
	if (k.t2mi_pid >= 0)
		it = fmt::format_to(ctx.out(), "-T{:d}", k.t2mi_pid);
	return it;
}

fmt::format_context::iterator
fmt::formatter<chdb::service_key_t>::format(const chdb::service_key_t& k, format_context& ctx) const {
	auto s = sat_pos_str(k.mux.sat_pos);
	auto it = fmt::format_to(ctx.out(), "{:s} ts={:d} sid={:d}", s.c_str(), k.ts_id, k.service_id);
	if (k.mux.t2mi_pid >= 0)
		it =fmt::format_to(ctx.out(), "-T{:d}", k.mux.t2mi_pid);
	return it;
}

fmt::format_context::iterator
fmt::formatter<chdb::service_t>::format(const chdb::service_t& service, format_context& ctx) const {
	auto s = sat_pos_str(service.k.mux.sat_pos);
	return fmt::format_to(ctx.out(),  "[{:>5d}] {:d}.{:03d}{:s} - {:s}", service.ch_order,
												service.frequency / 1000, service.frequency%1000,
												(service.pol == chdb::fe_polarisation_t::NONE) ? "" : to_str(service.pol),
												service.name.c_str());
}

fmt::format_context::iterator
fmt::formatter<chdb::fe_polarisation_t>::format(const chdb::fe_polarisation_t& pol, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}",  "{:s}", pol_str(pol));
}

fmt::format_context::iterator
fmt::formatter<chdb::chg_t>::format(const chdb::chg_t& chg, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "[{:d}-{:04d}] {:s}", int(chg.k.group_type), chg.k.bouquet_id, chg.name.c_str());
}

fmt::format_context::iterator
fmt::formatter<chdb::chgm_t>::format(const chdb::chgm_t& chgm, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "[{:04d}:{:04d}] {} {:s}", chgm.k.chg.bouquet_id, chgm.chgm_order, chgm.service,
												chgm.name.c_str());
}

fmt::format_context::iterator
fmt::formatter<chdb::tune_src_t>::format(const chdb::tune_src_t& tune_src, format_context& ctx) const {
	const char* p{nullptr};
	switch(tune_src) {
	case tune_src_t::TEMPLATE:  p="tmpl"; break;
	case tune_src_t::NIT_TUNED:  p="nit"; break;
	case tune_src_t::NIT_CORRECTED:  p="nitc"; break;
	case tune_src_t::NIT_ACTUAL: p="nita"; break;
	case tune_src_t::NIT_OTHER:  p="nito"; break;
	case tune_src_t::DRIVER:     p="drv"; break;
	case tune_src_t::USER:       p="user"; break;
	case tune_src_t::AUTO:       p="auto"; break;
	case tune_src_t::UNKNOWN:    p="unk"; break;
	default:
		return fmt::format_to(ctx.out(), "{}", (int) tune_src);
	}
	return fmt::format_to(ctx.out(), "{}", p);
}

fmt::format_context::iterator
fmt::formatter<chdb::key_src_t>::format(const chdb::key_src_t& key_src, format_context& ctx) const {
	const char* p{nullptr};
	switch(key_src) {
	case  key_src_t::NONE:  p="none"; break;
	case key_src_t::NIT_TUNED:  p="nit"; break;
	case key_src_t::SDT_TUNED: p="sdt"; break;
	case key_src_t::PAT_TUNED: p="pat"; break;
	case key_src_t::NIT_ACTUAL:  p="nita"; break;
	case key_src_t::NIT_OTHER:  p="nito"; break;
	case key_src_t::SDT_OTHER:  p="sdto"; break;
	case key_src_t::USER:       p="user"; break;
	case key_src_t::AUTO:       p="auto"; break;
		default:
			assert(0);
	}
	return fmt::format_to(ctx.out(), "{}", p);
}

fmt::format_context::iterator
fmt::formatter<chdb::sat_sub_band_pol_t>::format(const chdb::sat_sub_band_pol_t& band_pol, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{:s}-{:s}",
												band_pol.pol == chdb::fe_polarisation_t::H	 ? "H"
												: band_pol.pol == chdb::fe_polarisation_t::V ? "V"
												: band_pol.pol == chdb::fe_polarisation_t::L ? "L"
												: "R",
												to_str(band_pol.band));
}

fmt::format_context::iterator
fmt::formatter<chdb::band_scan_t>::format(const chdb::band_scan_t& band_scan, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{:s}-{:s}-{:s}",
												to_str(band_scan.pol),
												to_str(band_scan.sat_band),
												to_str(band_scan.sat_sub_band));
}
