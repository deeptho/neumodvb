/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

#include "scan.h"
#include "active_adapter.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/db_keys_helper.h"
#include "receiver.h"

/*
	Processes events and continues scanning.
	found_mux_keys = muxes found in nit_actual and nit_other
	Return 0 when scanning is done; otherwise number of muxes left

*/
int scanner_t::housekeeping() {
	if (must_end)
		return 0;

	auto pending = scan_loop({});

	dtdebugx("%d muxes left to scan; %d active", pending, (int)subscriptions.size());
	return must_end ? 0 : pending + subscriptions.size();
}

static void report(const char* msg, subscription_id_t finished_subscription_id,
									 subscription_id_t subscription_id, const chdb::any_mux_t& mux,
									 const std::set<subscription_id_t>& subscription_ids) {
	if ((int) finished_subscription_id < 0 && (int) subscription_id < 0)
		return;
	ss::string<128> s;
	//to_str(s, mux);
	s.sprintf(" Scan  %s %s finished_subscription_id=%d subscription_id=%d todo=[",
						msg, to_str(mux).c_str(), finished_subscription_id, subscription_id);
	for (auto& id : subscription_ids)
		s.sprintf("%2d ", id);
	s.sprintf("] \n");
	dtdebug(s.c_str());
}


template<typename mux_t>
std::tuple<subscription_id_t, int> scanner_t::scan_next(db_txn& rtxn, subscription_id_t finished_subscription_id)
{
	std::vector<task_queue_t::future_t> futures;

	// start as many subscriptions as possible
	using namespace chdb;
	auto c = mux_t::find_by_scan_status(rtxn, scan_status_t::PENDING, find_type_t::find_geq,
																			mux_t::partial_keys_t::scan_status);
	int num_pending{0};
	auto c1 = c.clone();
#if 0
	dtdebug("-------------------------------------");
	for(auto mux_to_scan: c1.range())  {
		dtdebug(" MUX=" << mux_to_scan);
	}
	dtdebug("++++++++++++++++++++++++++++++++++");
#endif
	std::optional<db_txn> devdb_wtxn;
	subscription_id_t subscription_id{-1};
	for(auto mux_to_scan: c.range())  {
		assert(mux_to_scan.c.scan_status == scan_status_t::PENDING);
		if ((int)subscriptions.size() >=
				((int)finished_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1))
			continue; // to have accurate num_pending count
		{
			auto devdb_wtxn = receiver.devdb.wtxn();
			subscription_id =
				receiver_thread.subscribe_mux(futures, devdb_wtxn, mux_to_scan, finished_subscription_id, tune_options, nullptr);
			devdb_wtxn.commit();
		}
		report("SUBSCRIBED", finished_subscription_id, subscription_id, mux_to_scan, subscriptions);
		dtdebug("Asked to subscribe " << mux_to_scan << " subscription_id="  << (int) subscription_id);
		wait_for_all(futures); // must be done before continuing this loop

		if ((int)subscription_id < 0) {
			// we cannot subscribe the mux right now
			num_pending++;
			continue;
		}
		last_subscribed_mux = mux_to_scan;
		dtdebug("subscribed to " << mux_to_scan << " subscription_id="  << (int) subscription_id <<
						" finished_subscription_id=" << (int) finished_subscription_id);
		if ((int)finished_subscription_id >= 0) {
			/*all other available subscriptions are still in use, and there is no point
				in continuing to check
				*/
			subscribed_muxes[subscription_id] = mux_to_scan;
			assert(subscription_id == finished_subscription_id);
			finished_subscription_id = subscription_id_t{-1}; // we have reused finished_subscription_id, but can only do that once
			continue;
		} else {
			// this is a second or third or ... subscription
			assert((int)finished_subscription_id == -1);

			subscriptions.insert(subscription_id);
			subscribed_muxes[subscription_id] = mux_to_scan;
		}
	}
	return {finished_subscription_id, num_pending};
}

/*
	Returns number of muxes left to scan
*/
int scanner_t::scan_loop(const chdb::any_mux_t& finished_mux) {
	std::vector<task_queue_t::future_t> futures;
	int error{};
	int num_pending{0};
	auto& finished_mux_key = *chdb::mux_key_ptr(finished_mux);
	subscription_id_t subscription_id{-1};
	try {
		subscription_id_t finished_subscription_id{-1};
		if (finished_mux_key.sat_pos != sat_pos_none) {
			auto [it, found] = find_in_map_if(subscribed_muxes, [&finished_mux](const auto& x) {
				auto& [subscription_id_, rec] = x;
				bool match = chdb::matches_physical_fuzzy(rec, finished_mux, true /*check_sat_pos*/);
				return match && (mux_key_ptr(finished_mux)->t2mi_pid == mux_key_ptr(rec)->t2mi_pid);
			});
			if (!found) {
				dterror("Cannot find subscription for active_adapter");
			}
			finished_subscription_id = it->first;
		}

		// start as many subscriptions as possible
		auto rtxn = receiver.chdb.rtxn();
		using namespace chdb;
		int num_pending1{0};
		auto old_finished_subscription_id = finished_subscription_id;
		std::tie(finished_subscription_id, num_pending1) =
			scan_next<chdb::dvbs_mux_t>(rtxn, finished_subscription_id);
		num_pending += num_pending1;
		dtdebugx("old_finished_subscription_id=%d finished_subscription_id=%d num_pending=%d",
						 (int) old_finished_subscription_id, (int) finished_subscription_id, num_pending);
		old_finished_subscription_id = finished_subscription_id;
		std::tie(finished_subscription_id, num_pending1) =
			scan_next<chdb::dvbc_mux_t>(rtxn, finished_subscription_id);
		num_pending += num_pending1;
		old_finished_subscription_id = finished_subscription_id;
		dtdebugx("old_finished_subscription_id=%d finished_subscription_id=%d num_pending=%d",
						 (int) old_finished_subscription_id, (int) finished_subscription_id, num_pending);
		std::tie(finished_subscription_id, num_pending1) =
			scan_next<chdb::dvbt_mux_t>(rtxn, finished_subscription_id);
		num_pending += num_pending1;
		old_finished_subscription_id = finished_subscription_id;
		dtdebugx("old_finished_subscription_id=%d finished_subscription_id=%d pending=%d",
						 (int) old_finished_subscription_id, (int)finished_subscription_id, num_pending);
		dtdebug("status: subscription_id="  << (int) subscription_id <<
						" finished_subscription_id=" << (int) finished_subscription_id << " pending="
						<< num_pending);

		if ((int)finished_subscription_id >= 0) {
			/*
				We arrive here only when finished_subscription_id was not reused at all
				finished_subscription is currently not of any use; terminate it
			*/
			if (subscriptions.erase(finished_subscription_id)>0) {
				dtdebugx("Abandoning subscription %d", (int) finished_subscription_id);
				cb(receiver_thread).unsubscribe(finished_subscription_id);
				subscribed_muxes.erase(finished_subscription_id);
				report("ERASED", finished_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
			} else {
				dtdebugx("Unexpected: %d", (int) finished_subscription_id);
			}
		}

		if (finished_mux_key.sat_pos != sat_pos_none) {
			add_completed_mux(finished_mux, num_pending);
		} else {
			auto w = scan_stats.writeAccess();
			w->scheduled_muxes = num_pending;
			w->active_muxes = subscriptions.size();
		}

		rtxn.commit();


		// error = wait_for_all(futures);

		if (error) {
			dterror("Error encountered during scan");
		}
	} catch (std::runtime_error) {
		dterror("run time exception encoutered");
		assert(0);
	}
	return num_pending;
}


scanner_t::scanner_t(receiver_thread_t& receiver_thread_,
										 //ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<devdb::lnb_t>* lnbs_,
										 bool scan_found_muxes_, int max_num_subscriptions_,
										 subscription_id_t subscription_id_)
	: receiver_thread(receiver_thread_)
	, receiver(receiver_thread_.receiver)
	,	scan_subscription_id(subscription_id_)
	, scan_start_time(system_clock_t::to_time_t(system_clock_t::now()))
	,	max_num_subscriptions(max_num_subscriptions_)
	,	scan_found_muxes(scan_found_muxes_)
{
	tune_options.scan_target =  scan_target_t::SCAN_FULL;
	tune_options.may_move_dish = false; //could become an option
	tune_options.use_blind_tune = false; //could become an option
	init();
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
	dtdebugx("Cleaned %d muxes with PENDING status", count);
	//assert(count==0); //should not occur, except at startup but clean_dbs should take care of that
}

void scanner_t::init()
{
	auto wtxn = receiver.chdb.wtxn();
	clean<chdb::dvbs_mux_t>(wtxn);
	clean<chdb::dvbc_mux_t>(wtxn);
	clean<chdb::dvbt_mux_t>(wtxn);
	wtxn.commit();
}


/*
	Returns true if a the  mux was new, or false if it was already planned
*/
void scanner_t::add_completed_mux(const chdb::any_mux_t& mux, int num_pending) {
	bool failed = false;
	failed = chdb::mux_common_ptr(mux)->scan_result == chdb::scan_result_t::FAILED;

	{
		auto w = scan_stats.writeAccess();
		w->last_subscribed_mux = last_subscribed_mux;
		w->last_scanned_mux = mux;
		w->finished_muxes++;
		w->failed_muxes += failed;
		w->scheduled_muxes = num_pending;
		w->active_muxes = subscriptions.size();
	}
}


template <typename mux_t>
int scanner_t::add_muxes(const ss::vector_<mux_t>& muxes, bool init) {
	auto wtxn = receiver.chdb.wtxn();
	for(const auto& mux_: muxes) {
		auto mux = mux_;
		mux.c.scan_status = chdb::scan_status_t::PENDING;
		put_record(wtxn, mux);
	}

	wtxn.commit();
	{
		auto w = scan_stats.writeAccess();
		if (init) {
			w->last_subscribed_mux = last_subscribed_mux;
			w->failed_muxes = 0;
			w->finished_muxes = 0;
			w->scheduled_muxes = muxes.size();
		} else{
			w->scheduled_muxes += muxes.size(); //might be wrong when same mux is added, but scan_loop will fix this later
		}
		w->active_muxes = subscriptions.size();
	}
	return 0;
}




void scanner_t::set_allowed_lnbs(const ss::vector_<devdb::lnb_t>& lnbs) { allowed_lnbs = lnbs; }

void scanner_t::set_allowed_lnbs() { allowed_lnbs.clear(); }

scanner_t::~scanner_t() {
	for (auto subscription_id : subscriptions) {
		cb(receiver_thread).unsubscribe(subscription_id);
	}
}


/*
	called from tuner thread when scanning a mux has ended
*/
int scanner_t::on_scan_mux_end(const chdb::any_mux_t& finished_mux)
{
	if (must_end) {
		return 0 ;
	}

	auto pending = scan_loop(finished_mux);

	dtdebug("finished_mux=" << finished_mux << " " << pending << " muxes left to scan; "
					<< ((int) subscriptions.size()) << " active");
	return must_end ? 0 : pending + subscriptions.size();
}


template int scanner_t::add_muxes<chdb::dvbs_mux_t>(const ss::vector_<chdb::dvbs_mux_t>& muxes, bool init);
template int scanner_t::add_muxes<chdb::dvbc_mux_t>(const ss::vector_<chdb::dvbc_mux_t>& muxes, bool init);
template int scanner_t::add_muxes<chdb::dvbt_mux_t>(const ss::vector_<chdb::dvbt_mux_t>& muxes, bool init);


template std::tuple<subscription_id_t, int>
scanner_t::scan_next<chdb::dvbs_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);

template std::tuple<subscription_id_t, int>
scanner_t::scan_next<chdb::dvbc_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);

template std::tuple<subscription_id_t, int>
scanner_t::scan_next<chdb::dvbt_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);
