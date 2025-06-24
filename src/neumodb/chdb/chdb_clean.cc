/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "receiver/neumofrontend.h"
#include "util/template_util.h"
#include <signal.h>
#include <iomanip>
#include <iostream>
#include "../util/neumovariant.h"

using namespace chdb;


namespace chdb {
	static void clean_sats(db_txn& wtxn);
	template<typename mux_t> static void clean_muxes(db_txn& wtxn);
	template<typename record_t> static void clean_expired(db_txn& wtxn, std::chrono::seconds age, const char* label);
	static void clean_overlapping_muxes_on_sat(db_txn& wtn, const chdb::dvbs_mux_t& mux, int sat_pos);
	static void clean_overlapping_muxes(db_txn& wtn, const chdb::dvbs_mux_t& mux);
	template<typename mux_t>
	static void clean_overlapping_muxes(db_txn& wtn, const mux_t& mux);
};

static void chdb::clean_sats(db_txn& wtxn) {
	using namespace chdb;
	int count{0};

	auto c = find_first<sat_t>(wtxn);

	auto clean1 = [&](scan_status_t& scan_status, scan_id_t& scan_id) {
		switch(scan_status) {
		case scan_status_t::PENDING:
		case scan_status_t::ACTIVE:
		case scan_status_t::RETRY:
			scan_status = scan_status_t::IDLE;
			scan_id = {};
			return true;
			break;
		default:
			return false;
		}
	};

	auto clean = [&](band_scan_t& band_scan) {
		if(chdb::scan_in_progress(band_scan.scan_id)) {
			auto owner_pid = band_scan.scan_id.pid;
			if(kill((pid_t)owner_pid, 0) == 0) {
				dtdebugf("process pid={:d} is still active; skip deleting scan status", owner_pid);
				return false;
				}
			band_scan.scan_id = {};
		}

		bool changed{false};
		changed |= clean1(band_scan.scan_status, band_scan.scan_id);
		return changed;
	};

	for(auto sat: c.range())  {
		//we no longer store dvbc and dvbt as fake sats
		if(sat.sat_pos == sat_pos_dvbc || sat.sat_pos == sat_pos_dvbt) {
			delete_record_at_cursor(c);
			continue;
		}
		bool changed{false};
		for(auto & band_scan: sat.band_scans) {
			changed  |= clean(band_scan);
		}
		if(changed) {
			sat.mtime = system_clock_t::to_time_t(now);
			put_record(wtxn, sat);
			count++;
		}
	}
	dtdebugf("Cleaned {:d} sats with PENDING/ACTIVE/RETRY status", count);
}

template<typename mux_t> static void chdb::clean_muxes(db_txn& wtxn) {
	using namespace chdb;
	int count{0};
	auto c = chdb::find_first<mux_t>(wtxn);
	for(auto mux: c.range())  {
		bool need_clean = (mux.c.scan_status == scan_status_t::PENDING ||
											 mux.c.scan_status == scan_status_t::ACTIVE ||
											 mux.c.scan_status == scan_status_t::RETRY);
		if(!need_clean) {
			if (mux.c.scan_id != scan_id_t{}) {
				need_clean = true; //should not happen, except due to bugs
			}
		}

		if(mux.c.scan_id != scan_id_t{}) {
			auto owner_pid = mux.c.scan_id.pid;
				if(kill((pid_t)owner_pid, 0) == 0) {
					dtdebugf("process pid={:d} is still active; skip deleting scan status", owner_pid);
				continue;
				}
			}
			mux.c.scan_status = chdb::scan_status_t::IDLE;
			mux.c.scan_id = {};
			mux.c.scan_rf_path = {};
			put_record(wtxn, mux);
			count++;
	}

	dtdebugf("Cleaned {:d} muxes with PENDING/ACTIVE/RETRY status", count);
}

template<typename record_t> static void chdb::clean_expired(db_txn& wtxn, std::chrono::seconds age, const char* label)
{
	using namespace chdb;
	int count{0};
	int skipped{0};
	//auto age = std::chrono::duration_cast<std::chrono::seconds>(age.count);
	auto t = system_clock_t::to_time_t(now + age);
	auto c = find_first<record_t>(wtxn);

	for(auto record: c.range())  {
		if(!record.expired)
			continue;
		if(record.mtime >= t) {
			skipped++;
			continue;
		}
		delete_record(wtxn, record);
		count++;
	}
	dtdebugf("removed {:d} expired {:s}; {:d} skipped", count, label, skipped);
}

void chdb::clean_scan_status(db_txn& wtxn)
{
	clean_sats(wtxn);
	clean_muxes<chdb::dvbs_mux_t>(wtxn);
	clean_muxes<chdb::dvbc_mux_t>(wtxn);
	clean_muxes<chdb::dvbt_mux_t>(wtxn);
}

void chdb::clean_expired_services(db_txn& wtxn, std::chrono::seconds age)
{
	clean_expired<chdb::service_t>(wtxn, age, "services");
	clean_expired<chdb::chgm_t>(wtxn, age, "channels");
	clean_chgms_without_services(wtxn);
}

void chdb::clean_chgms_without_services(db_txn& wtxn)
{
	using namespace chdb;
	int count{0};
	dttime_init();
	//auto age = std::chrono::duration_cast<std::chrono::seconds>(age.count);
	auto c = find_first<chgm_t>(wtxn);
	for(auto record: c.range())  {
		auto& service_key =  record.service;
		auto c1 = service_t::find_by_key(wtxn, service_key.mux, service_key.service_id, find_type_t::find_eq);
		if(!c1.is_valid()) {
			delete_record(wtxn, record);
			count++;
		}
	}
	dttime(10);
	dtdebugf("{:d} chgm records deleted", count);
}

/*
	Clear PENDING scan status  for all muxes with overlapping frequency with ref_mux,
	except ref_mux itself.
	This is used in two cases
	1. a mux has been successfully tuned, but it is not a multistream. In this case any multistream
	on the same mux is incorrect (e.g., left over from old mux status) and scanning it will be pointless
	2. a stream has failed to lock. In this case, any other stream with the same tuning parameters and
	different stream_id will fail to scan as well, and scanning them is a pointless waste of time

	We must be careful to not clear the PENDING status on muxes with different symbol rate, e.g., in
	case a bad broadband mux overlaps with a good narrow band mux.

	Also, we do not clear muxes which differ only in mux_key
 */
template <typename mux_t> void chdb::clear_all_streams_pending_status(
	db_txn& chdb_wtxn, system_time_t now_, const mux_t& ref_mux) {
	using namespace chdb;

	auto c = [&]() {
		// find tps with matching frequency, but probably incorrect network_id/ts_id
		if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
			// approx. match in sat_pos, frequency, exact match in  polarisation, t2mi_pid and stream_id
			return chdb::find_by_mux_fuzzy(chdb_wtxn, ref_mux, true/*ignore_stream_id*/, true /*ignore_t2mi_pid*/);
		} else {
			return chdb::find_by_freq_fuzzy<mux_t>(chdb_wtxn, ref_mux.frequency);
		}
	}();


	int tolerance = get_member(ref_mux, symbol_rate, 500000)/1000; //in kHze

	for(auto mux: c.range()) {
		if(mux.k.sat_pos != ref_mux.k.sat_pos)
			break; //we have reached the end
		if ((int)mux.frequency >  (int) ref_mux.frequency + tolerance)
			break; //we have reached the end
		if(mux.k == ref_mux.k || mux.k.stream_id == ref_mux.k.stream_id)
			continue;
		if(!matches_physical(mux, ref_mux, true /*check_sat_pos*/, true /*ignore_stream_id*/))
				continue;
		if(mux.c.scan_status == scan_status_t::PENDING && mux.c.scan_id == ref_mux.c.scan_id && mux.k.stream_id>=0) {
			mux.c.scan_status = scan_status_t::IDLE;
			mux.c.scan_id = {};
			mux.c.scan_result = ref_mux.c.scan_result;
			put_record(chdb_wtxn, mux);
		}
	}
}


static void chdb::clean_overlapping_muxes_on_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux, int sat_pos) {
	auto tolerance = (mux.symbol_rate*1.35) / 2000;
	auto min_freq = mux.frequency - tolerance;
	auto max_freq = mux.frequency + tolerance;

	//find first mux below the bandwith of mux
	auto c = dvbs_mux_t::find_by_sat_pol_freq(wtxn, sat_pos, mux.pol, min_freq, find_leq,
																						dvbs_mux_t::partial_keys_t::sat_pos_pol);
	for(const auto& db_mux: c.range()) {
		auto tolerance = (db_mux.symbol_rate*1.35) / 2000;
		/*
			We check overlap as follows:
			min_freq and max_freq are an estimate of the frequency range covered by a mux. The exact value depends
			on the roll off factor, which corresponds to a factor of 1.05 to 1.35.

			if a mux's central frequency falls in [min_freq, max_freq] then there is overlap.

			We also apply a similar test with teh roles of mux and db_mux reversed.

			Note that the test is on the one hand too loose (possible overestimation of bandwith by a factor 1.35/1.05)
			and too strict (muxes which only just satisfy the criteria may still overlap half of their bandwith

		 */

		if (db_mux.frequency < min_freq  && mux.frequency > db_mux.frequency +tolerance)
			continue; //not overlapping
		if(db_mux.frequency > max_freq && mux.frequency < db_mux.frequency - tolerance)
			continue; //not overlapping and no more overlap possible
		if(db_mux.k == mux.k)
			continue; //do not erase the master mux
		if((db_mux.pol == mux.pol) && (
				 mux.k.mux_id != db_mux.k.mux_id || // duplicate in database
					 (mux.k.stream_id == -1 && db_mux.k.stream_id >=0) || //mux must be single or multistream, not both
					 (mux.k.stream_id >=0 && db_mux.k.stream_id == -1) || //mux must be single or multistream, not both
					 (db_mux.k.stream_id ==  mux.k.stream_id && db_mux.k.t2mi_pid == mux.k.t2mi_pid)
				 )) {
			//overlapping mux
			auto cs = chdb::service::find_by_mux_key(wtxn, db_mux.k);
			for(auto service: cs.range()) {
				service.expired = true;
				put_record(wtxn, service);
			}
			delete_record(wtxn, db_mux);
		}
	}
}

static void chdb::clean_overlapping_muxes(db_txn& wtxn, const chdb::dvbs_mux_t& mux) {
	assert(!chdb::is_template(mux));
	int sat_tolerance = sat_pos_tolerance;
	auto cs = sat_t::find_by_key(wtxn, mux.k.sat_pos-sat_tolerance, find_type_t::find_geq);
	for(const auto& sat:  cs.range()) {
		if (sat.sat_pos > mux.k.sat_pos + sat_tolerance)
			break;
		clean_overlapping_muxes_on_sat(wtxn, mux, sat.sat_pos);
	}
}

template<typename mux_t>
static void chdb::clean_overlapping_muxes(db_txn& wtxn, const mux_t& mux) {
	//@todo: not implemented
}


void chdb::clean_overlapping_muxes(db_txn& wtxn, const chdb::any_mux_t& mux) {
	assert(!chdb::is_template(mux));
	visit_variant(
		mux,
		[&](const chdb::dvbs_mux_t& mux) {
			clean_overlapping_muxes(wtxn, mux);
		},
		[&](const chdb::dvbc_mux_t& mux) {
			clean_overlapping_muxes(wtxn, mux);
		},
		[&](const chdb::dvbt_mux_t& mux) {
			clean_overlapping_muxes(wtxn, mux);
		}
		);
}

//template instantiations
template void chdb::clear_all_streams_pending_status(db_txn& chdb_wtxn, system_time_t now_,
																										 const chdb::dvbs_mux_t& ref_mux);
template void chdb::clear_all_streams_pending_status(db_txn& chdb_wtxn, system_time_t now_,
																										 const chdb::dvbc_mux_t& ref_mux);
template void chdb::clear_all_streams_pending_status(db_txn& chdb_wtxn, system_time_t now_,
																							const chdb::dvbt_mux_t& ref_mux);
