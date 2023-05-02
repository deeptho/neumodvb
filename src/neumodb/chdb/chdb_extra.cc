/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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

#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "receiver/neumofrontend.h"
#include "stackstring/ssaccu.h"
#include "xformat/ioformat.h"
#include "util/template_util.h"
#include <signal.h>
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
			/*we reached the first matching mux; assign next available lower value to mux.k.extra_id
				this can only fail if about 65535 muxes with the same sat_pos, network_id and ts_id exist;
				In that case the loop continues, just in case some of these 65535 muxes have been deleted in the mean time,
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
	auto c = mux_t::find_by_key(txn, key, find_geq);
	return ::make_unique_id(txn, key, c);
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
		dterror("Overflow for bouquet_id");
		assert(0);
	}
	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.bouquet_id)>::max(); // highest possible value
}

int32_t chdb::make_unique_id(db_txn& txn, chgm_key_t key) {
	auto c = chgm_t::find_by_key(txn, key, find_geq);
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
void chdb::on_mux_key_change(db_txn& wtxn, const mux_key_t& old_mux_key, mux_t& new_mux, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace chdb;
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

void chdb::on_mux_key_change(db_txn& wtxn, const chdb::mux_key_t& old_mux_key, chdb::dvbs_mux_t& new_mux,
															system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace chdb;
	auto& new_mux_key = *mux_key_ptr(new_mux);
	{
		auto c = chdb::sat_t::find_by_key(wtxn, new_mux_key.sat_pos);
		if (!c.is_valid()) {
			chdb::sat_t sat;
			sat.sat_pos = new_mux_key.sat_pos;
			sat.name = chdb::sat_pos_str(new_mux_key.sat_pos);
			put_record(wtxn, sat);
		}
	}
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
		auto c = sat_t::find_by_key(wtxn, old_mux_key.sat_pos, find_type_t::find_geq,
																		sat_t::partial_keys_t::sat_pos);
		for(auto sat: c.range()) {
			if (sat.reference_tp == old_mux_key) {
				sat.reference_tp = new_mux_key;
				put_record(wtxn, sat);
			}
		}
	}
}

void chdb::on_mux_key_change(db_txn& wtxn, const chdb::mux_key_t& old_mux_key, chdb::any_mux_t& new_mux,
															system_time_t now_) {
	using namespace chdb;

	std::visit([&](auto& new_mux) { on_mux_key_change(wtxn,
																										old_mux_key,
																										new_mux,
																										now_);
	}, new_mux);
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
	mux.c.tune_src = db_mux.c.tune_src;
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
	mux.c.tune_src = db_mux.c.tune_src;
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
		auto c1 = chdb::service_t::find_by_key(wtxn, service.k);
		if (c1.is_valid()) {
			auto dst_service = c1.current();
			if(dst_service.ch_order ==0 && service.ch_order !=0) {
				dst_service.ch_order = service.ch_order;
				put_record(wtxn, dst_service);
			}
		} else {
			chdb::to_str(service.mux_desc, dst);
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
void merge_muxes(mux_t& mux, mux_t& db_mux, update_mux_preserve_t::flags preserve) {
	namespace m = update_mux_preserve_t;
	dtdebug("db_mux=" << db_mux << "-> mux=" << mux);
	assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
					mux.c.scan_status != chdb::scan_status_t::PENDING) ||
				 mux.c.scan_id >0);

	if( (preserve & m::MUX_KEY) || mux_key_ptr(mux)->extra_id == 0) {
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

	if( (preserve & m::MUX_KEY) ) { //actual copying handled by caller
		mux.c.key_src = db_mux.c.key_src;
	}

	if (preserve & m::TUNE_DATA) {
		copy_tuning(mux, db_mux); //preserve what is in the database
	}
	if (preserve & m::SCAN_DATA) {
		mux.c.scan_time = db_mux.c.scan_time;
		mux.c.scan_result = db_mux.c.scan_result;
		mux.c.scan_duration = db_mux.c.scan_duration;
		mux.c.epg_scan = db_mux.c.epg_scan;
	}

	if (preserve & m::SCAN_STATUS) {
		mux.c.scan_status = db_mux.c.scan_status;
		mux.c.scan_id = db_mux.c.scan_id;

		assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux.c.scan_status != chdb::scan_status_t::PENDING) ||
					 mux.c.scan_id >0);

	}
	dtdebug("after merge db_mux=" << db_mux << " mux=" << mux << "->"
					<< " preserve&SCAN_STATUS=" << (preserve & m::SCAN_STATUS));


	if (preserve & m::NUM_SERVICES)
		mux.c.num_services = db_mux.c.num_services;

	if (preserve & m::EPG_TYPES)
		mux.c.epg_types = db_mux.c.epg_types;

	if (preserve & m::NIT_SI_DATA) {
		mux.c.nit_network_id = db_mux.c.nit_network_id;
		mux.c.nit_ts_id = db_mux.c.nit_ts_id;
	}
}

/*! Put a mux record, taking into account that its key may have changed
	returns: true if this is a new mux (first time scanned); false otherwise
	Also, updates the mux, specifically k.extra_id

	First find a mux which matches approximately in sat_pos, and frequency
	and exactly in polarisation,
 and exactly in key except extra_id (unless ignore_key==True)
	and exactly in t2mi_pid and stream_id

	In case of multiple matches prefer the one with matching extra_id (unless ignore_key==True)
	and with closest frequency

	Then either change the found mux, but preserve some of its data (depending on preserver(
	or insert a new one.

	In case MUX_KEY preservation is requested: do not change the mux_key (including extra_id)

	for must_exist: return without saving if existing mux cannot be found and update would create a new mux

*/
template <typename mux_t>
update_mux_ret_t chdb::update_mux(db_txn& wtxn, mux_t& mux, system_time_t now_, update_mux_preserve_t::flags preserve,
																	std::function<bool(chdb::mux_common_t*, const chdb::mux_key_t*)> cb,
																	bool ignore_key, bool ignore_t2mi_pid, bool must_exist)
{
	auto now = system_clock_t::to_time_t(now_);
	//assert(mux.c.tune_src != tune_src_t::TEMPLATE);
	assert(!ignore_t2mi_pid || ignore_key);
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
	auto c = chdb::find_by_mux_physical(wtxn, mux, false /*ignore_stream_id*/, ignore_key, ignore_t2mi_pid);
	bool delete_db_mux{false};

	bool is_new = true; // do we modify an existing record or create a new one?
	if (c.is_valid()) {
		db_mux = c.current();
		assert(db_mux.stream_id == mux.stream_id);
		if(db_mux.k.extra_id == 0 ) {
			dterror("Database mux " << db_mux << " has zero extra_id; fixing it");
			mux.k.extra_id = make_unique_id<mux_t>(wtxn, mux.k);
		}
		auto tmp_key = mux.k;
		tmp_key.extra_id = db_mux.k.extra_id;
		auto key_matches = tmp_key == db_mux.k;
		if(key_matches)
			mux.k.extra_id = db_mux.k.extra_id; //avoid creating two muxes with same extra_id
		/*
			if !key_matches: We are going to overwrite a mux with similar frequency but different ts_id, network_id.
			If one of the muxes is a template and the other not, the non-template data
			gets priority.
		*/
		if(!key_matches) {
			if ((preserve & m::MUX_KEY) && must_exist)
				return update_mux_ret_t::NO_MATCHING_KEY;
			delete_db_mux = true;

			if(mux.k.extra_id ==0)
				mux.k.extra_id = make_unique_id<mux_t>(wtxn, mux.k);
			if(is_template(db_mux)) {
				db_mux.k = mux.k;
			}
		}
		assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux.c.scan_status != chdb::scan_status_t::PENDING) ||
					 mux.c.scan_id >0);

		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);
		ret = key_matches ? update_mux_ret_t::MATCHING_SI_AND_FREQ: update_mux_ret_t::MATCHING_FREQ;

		is_new = false;
		if(!cb(&db_mux.c, &db_mux.k))
			return update_mux_ret_t::NO_MATCHING_KEY;
		assert((mux.c.scan_status != chdb::scan_status_t::ACTIVE &&
						mux.c.scan_status != chdb::scan_status_t::PENDING) ||
					 mux.c.scan_id >0);
		key_matches = (mux.k == db_mux.k); //key can be changed by cb()
		delete_db_mux = !key_matches;
		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);
		if(!is_new) {
			merge_muxes<mux_t>(mux, db_mux, preserve);
			if(!key_matches) {
				int matype = get_member(mux, matype, 0x3 << 14);
				int db_matype = get_member(db_mux, matype, 0x3 << 14);
				auto is_dvb = ((matype >> 14) & 0x3) == 0x3;
				auto db_mux_is_dvb = ((db_matype >> 14) & 0x3) == 0x3;
				if(is_dvb && db_mux_is_dvb)
					merge_services(wtxn, db_mux.k, mux);
				else if (!is_dvb && db_mux_is_dvb)
					remove_services(wtxn, db_mux.k);
			}
		}
		//dtdebug("db_mux=" << db_mux << " mux=" << mux << " status=" << (int)db_mux.c.scan_status << "/" << (int)mux.c.scan_status);

		dtdebugx("Transponder %s: sat_pos=%d => %d nid=%d => %d ts_id=%d => %d", to_str(mux).c_str(),
						 db_mux.k.sat_pos, mux.k.sat_pos, db_mux.k.network_id,
						 mux.k.network_id, db_mux.k.ts_id, mux.k.ts_id);
		//assert( mux.c.tune_src != tune_src_t::TEMPLATE );

		ret = is_new ? update_mux_ret_t::NO_MATCHING_KEY : update_mux_ret_t::MATCHING_SI_AND_FREQ;
	} else { //no mux in db
		if(must_exist)
			return update_mux_ret_t::NO_MATCHING_KEY;
		ret = update_mux_ret_t::NEW;
		dterror("Database mux " << db_mux << " setting extra_id on new mux");
		mux.k.extra_id = make_unique_id<mux_t>(wtxn, mux.k);

		if(!cb(nullptr, nullptr))
			return update_mux_ret_t::NO_MATCHING_KEY;
	}

	if(is_template(mux)) {
		dterror("Unexpected: saving template mux=" << mux);
		return update_mux_ret_t::EQUAL;
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

		dtdebugx("NIT %s: old=%s new=%s #s=%d", is_new ? "NEW" : "CHANGED", to_str(db_mux).c_str(), to_str(mux).c_str(),
						 mux.c.num_services);
		assert(mux.frequency > 0);

		dtdebug("mux=" << mux);
		assert(mux.k.extra_id>0);
		assert(!is_template(mux));
		if(delete_db_mux) {
			delete_record(c, db_mux);
		}
		put_record(wtxn, mux);
	}
	return ret;
}

update_mux_ret_t chdb::update_mux(db_txn& wtxn, chdb::any_mux_t& mux, system_time_t now,
																	update_mux_preserve_t::flags preserve,
																	std::function<bool(chdb::mux_common_t*, const chdb::mux_key_t*)> cb,
																	bool ignore_key, bool ignore_t2mi_pid, bool must_exist) {
	using namespace chdb;
	update_mux_ret_t ret;
	visit_variant(
		mux,
		[&](chdb::dvbs_mux_t& mux) {
			if(mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_DVBS)
				mux.matype = 256;
			ret = chdb::update_mux(wtxn, mux, now, preserve, cb, ignore_key, ignore_t2mi_pid, must_exist); },
		[&](chdb::dvbc_mux_t& mux) { ret = chdb::update_mux(wtxn, mux, now, preserve, cb,
																												ignore_key, ignore_t2mi_pid, must_exist); },
		[&](chdb::dvbt_mux_t& mux) { ret = chdb::update_mux(wtxn, mux, now, preserve, cb,
																												ignore_key, ignore_t2mi_pid, must_exist); });
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

void chdb::matype_str(ss::string_& s, int16_t matype, int rolloff) {
	// See en 302 307 v1.1.2; stid135 manual seems wrong in places
	// en_302307v010201p_DVBS2.pdf
	//s.sprintf("0x%x ", matype);
	bool is_ts{false};
	if( matype == 256 ) { //dvbs
		s.sprintf("DVBS");
		return;
	}
	if(matype <0) {
		s.sprintf("");
		return;
	}
	s.sprintf("0x%x: ", matype);
	if( matype <0 ) { //not tuned yet; matype unknown
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
		is_ts=true;
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
		s.sprintf(is_ts? "NPD ": "Lite ");
	if(rolloff>=0) {
		switch((fe_rolloff) rolloff) {
		case 	ROLLOFF_35: s.sprintf("35%%"); break;
		case 	ROLLOFF_20: s.sprintf("20%%"); break;
		case 	ROLLOFF_25: s.sprintf("25%%"); break;
		case 	ROLLOFF_LOW: s.sprintf("low%%"); break;
		case 	ROLLOFF_15: s.sprintf("15%%"); break;
		case 	ROLLOFF_10: s.sprintf("10%%"); break;
		case 	ROLLOFF_5: s.sprintf("5%%"); break;
		default:
			s.sprintf("??"); break;
		}
	} else {
		switch (matype & 3) {
		case 0:
			s.sprintf("35%%");
			break;
		case 1:
			s.sprintf("25%%");
			break;
		case 2:
			s.sprintf("20%%");
			break;
		case 3:
			s.sprintf("LO"); //low rollof (requires inspection of multiple bbframes)
			break;
		}
	}
}

std::ostream& chdb::operator<<(std::ostream& os, tune_src_t tune_src) {
	switch(tune_src) {
	case tune_src_t::TEMPLATE:  os << "tmpl"; break;
	case tune_src_t::NIT_TUNED:  os << "nit"; break;
	case tune_src_t::NIT_ACTUAL: os << "nita"; break;
	case tune_src_t::NIT_OTHER:  os << "nito"; break;
	case tune_src_t::DRIVER:     os << "drv"; break;
	case tune_src_t::USER:       os << "user"; break;
	case tune_src_t::AUTO:       os << "auto"; break;
	case tune_src_t::UNKNOWN:    os << "unk"; break;
	}
	return os;
}


std::ostream& chdb::operator<<(std::ostream& os, key_src_t key_src) {
	switch(key_src) {
	case  key_src_t::NONE:  os << "none"; break;
	case key_src_t::NIT_TUNED:  os << "nit"; break;
	case key_src_t::SDT_TUNED: os << "sdt"; break;
	case key_src_t::PAT_TUNED: os << "pat"; break;
	case key_src_t::NIT_ACTUAL:  os << "nita"; break;
	case key_src_t::NIT_OTHER:  os << "nito"; break;
	case key_src_t::SDT_OTHER:  os << "sdto"; break;
	case key_src_t::USER:       os << "user"; break;
	case key_src_t::AUTO:       os << "auto"; break;
	}
	return os;
}


std::ostream& chdb::operator<<(std::ostream& os, const language_code_t& code) {
	os << lang_name(code);
	return os;
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

std::ostream& chdb::operator<<(std::ostream& os, const scan_status_t& scan_status) {
	os << scan_status_name(scan_status);
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const scan_result_t& scan_result) {
	os << scan_result_name(scan_result);
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
		auto fpos = std::abs(sat.sat_pos) / (double)100.;
		stdex::printf(os, "%3.1f%c", fpos, sat.sat_pos < 0 ? 'W' : 'E');
	}
	return os;
}

std::ostream& chdb::operator<<(std::ostream& os, const dvbs_mux_t& mux) {
	auto sat = sat_pos_str(mux.k.sat_pos);
	stdex::printf(os, "%d.%03d%s", mux.frequency / 1000, mux.frequency%1000, enum_to_str(mux.pol));
	if (mux.stream_id >= 0)
		stdex::printf(os, "-%d", mux.stream_id);
	if (mux.k.t2mi_pid != 0)
		stdex::printf(os, "-T%d", mux.k.t2mi_pid);
	stdex::printf(os, " ");
	os << mux.k;
#if 0
	stdex::printf(os, " %s/%s", scan_status_name(mux.c.scan_status), scan_result_name(mux.c.scan_result));
#endif
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
	stdex::printf(os, " - %d/%d/%d", k.network_id, k.ts_id, k.extra_id);
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


std::ostream& chdb::operator<<(std::ostream& os, const fe_polarisation_t& pol) {
		stdex::printf(os, "%s",
									pol == fe_polarisation_t::H	 ? "H"
									: pol == fe_polarisation_t::V ? "V"
									: pol == fe_polarisation_t::L ? "L"
									: "R");
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

void chdb::to_str(ss::string_& ret, const sat_t& sat) {
	ret.clear();
	sat_pos_str(ret, sat.sat_pos);
}

void chdb::to_str(ss::string_& ret, const scan_status_t& scan_status) {
	ret.clear();
	ret << scan_status;
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

void chdb::to_str(ss::string_& ret, tune_src_t tune_src) {
	ret.clear();
	ret << tune_src;
}

void chdb::to_str(ss::string_& ret, key_src_t key_src) {
	ret.clear();
	ret << key_src;
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
				lnb, devdb::fe_band_t::LOW);
			mux.frequency = freq_low;
			return {mux, sat};
		};

	auto return_mux = [&chdb_rtxn, &lnb, &return_some_mux](const devdb::lnb_network_t& network)
		-> std::tuple<std::optional<chdb::dvbs_mux_t>, std::optional<chdb::sat_t>>
		{
			auto cs = chdb::sat_t::find_by_key(chdb_rtxn, network.sat_pos);
			std::optional<chdb::sat_t> sat = cs.is_valid() ? cs.current() : chdb::sat_t();
			auto c = chdb::dvbs_mux_t::find_by_key(chdb_rtxn, network.ref_mux);
			if (c.is_valid()) {
				auto mux = c.current();
				if (devdb::lnb_can_tune_to_mux(lnb, mux, true /*disregard networks*/, nullptr /*error*/))
					return {mux, sat};
			}
			//pick the first mux on the sat as the ref mux, taking into account lnb type and polarisation
			c = chdb::dvbs_mux_t::find_by_key(chdb_rtxn, network.sat_pos, 0, 0, 0, find_type_t::find_geq,
																				chdb::dvbs_mux_t::partial_keys_t::sat_pos);
			for(auto mux: c.range()) {
				if (devdb::lnb_can_tune_to_mux(lnb, mux, true /*disregard networks*/, nullptr /*error*/))
					return {mux, sat};
			}
			return return_some_mux(sat);
		};

	using namespace chdb;
	const bool disregard_networks{false};
	if (proposed_mux && lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks)) {
		auto cs = chdb::sat_t::find_by_key(chdb_rtxn, proposed_mux->k.sat_pos);
		return {*proposed_mux, cs.is_valid() ? cs.current() : chdb::sat_t{}};
	}
	{
		auto usals_pos = lnb.usals_pos;

		auto best = std::numeric_limits<int>::max();
		const devdb::lnb_network_t* bestp{nullptr};
		for (auto& network : lnb.networks) {
			if(usals_pos == sat_pos_none)
				usals_pos = network.usals_pos;
			auto delta =  std::abs(network.usals_pos - usals_pos);
			if (delta < best) {
				best = delta;
				bestp = &network;
			}
		}

		if (bestp) {
			return return_mux(*bestp);
		} else if( bestp && !devdb::lnb::on_positioner(lnb)) {
			return return_mux(*bestp);
		}
		auto cs = chdb::sat_t::find_by_key(chdb_rtxn, lnb.cur_sat_pos);
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
