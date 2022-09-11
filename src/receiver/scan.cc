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
#include "neumodb/devdb/devdb_extra.h"
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

	auto pending = scan_loop({}, {});

	dtdebugx("%d muxes left to scan; %d active", pending, (int)subscriptions.size());
	return must_end ? 0 : pending + subscriptions.size();
}

static void report(const char* msg, subscription_id_t finished_subscription_id,
									 subscription_id_t subscription_id, const chdb::any_mux_t& mux,
									 const std::map<devdb::fe_key_t, subscription_id_t>& subscriptions) {
	if ((int) finished_subscription_id < 0 && (int) subscription_id < 0)
		return;
	ss::string<128> s;
	//to_str(s, mux);
	s.sprintf(" Scan  %s %s finished_subscription_id=%d subscription_id=%d todo=[",
						msg, to_str(mux).c_str(), finished_subscription_id, subscription_id);
	for (auto& [fe_key, id] : subscriptions)
		s.sprintf("%2d ", id);
	s.sprintf("] \n");
	dtdebug(s.c_str());
}

template<typename mux_t>
static inline bool& skip_helper(std::map<int,bool>& skip_map, const mux_t& mux)
{
	using namespace chdb;
	int band = 0;
	/*The following handles most lnbs well. Worst case  band will be wrong
		which may lead to a decision to skip scanning a mux now, but the it will be retried
		in a future call of scan_loop
	*/
	if (mux.frequency >= 11700000 && mux.frequency <= 12800000)
		band = 1;
	int pol = 0;
	if constexpr (std::is_same<chdb::dvbs_mux_t, mux_t>::value) {
		pol= (mux.pol == fe_polarisation_t::V) || (mux.pol == fe_polarisation_t::R);
	}
	int key = (mux.k.sat_pos) << 16 | (band &1) << 8 | (pol&1);
	return skip_map[key];
}

/*
	returns the number the new subscription_id and the new value of finished_subscription_id,
	which will be -1 to to indicate that it can no longer be reused
 */
template<typename mux_t>
std::tuple<subscription_id_t, subscription_id_t>
scanner_t::scan_try_mux(subscription_id_t finished_subscription_id ,
												devdb::fe_key_t finished_fe_key,
												const mux_t& mux_to_scan, const devdb::lnb_t* required_lnb)
{
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};
	std::vector<task_queue_t::future_t> futures;
	{
		auto wtxn = receiver.devdb.wtxn();
			std::tie(subscription_id, subscribed_fe_key) =
				receiver_thread.subscribe_mux(futures, wtxn, mux_to_scan, finished_subscription_id, tune_options, required_lnb);
			wtxn.commit();
	}
	report((int)subscription_id <0 ? "NOT SUBSCRIBED" : "SUBSCRIBED", finished_subscription_id, subscription_id, mux_to_scan, subscriptions);
	dtdebug("Asked to subscribe " << mux_to_scan << " subscription_id="  << (int) subscription_id);
	wait_for_all(futures); // must be done before continuing this loop (?)

	if ((int)subscription_id < 0) {
		// we cannot subscribe the mux right now
		return {subscription_id, finished_subscription_id};
	}
	last_subscribed_mux = mux_to_scan;
	dtdebug("subscribed to " << mux_to_scan << " subscription_id="  << (int) subscription_id <<
					" finished_subscription_id=" << (int) finished_subscription_id);

	/*at this point, we know that our newly subscribed fe differs from our old subscribed fe
	 */

	auto [it, inserted] = subscriptions.insert({subscribed_fe_key, subscription_id});
	if(!inserted) {
		//the newly subscribed fe is already used by a subscription
		auto existing_subscription_id = it->second;
		if(existing_subscription_id != subscription_id) {
			/*this indicates that some duplicate muxes exist in the database, which should normally
				never happen. We */
			dtdebug("Cannot insert because fe=" << subscribed_fe_key << " is already used by subscription_id="
							<< (int)existing_subscription_id);
			auto wtxn = receiver.devdb.wtxn();
			receiver_thread.unsubscribe(futures, wtxn, subscription_id);
			wtxn.commit();
			wait_for_all(futures);
			report("ERASED", finished_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
			{
				namespace m = chdb::update_mux_preserve_t;
				auto chdb_wtxn = receiver.chdb.wtxn();
				auto dst_mux = subscribed_muxes[existing_subscription_id];
				assert(mux_key_ptr(dst_mux)->sat_pos != sat_pos_none);
				chdb::merge_services(chdb_wtxn, mux_to_scan.k, dst_mux);
				chdb::delete_record(chdb_wtxn, mux_to_scan);
				chdb_wtxn.commit();
			}
#if 0 //not needed
			subscriptions.erase(subscribed_fe_key); //remove old entry
			subscribed_muxes.erase(subscription_id);
#endif
		} else {
			//this indicates that our new subscription uses the same fe as our old one
			subscribed_muxes[subscription_id] = mux_to_scan;
			finished_subscription_id = subscription_id_t{-1}; // we have reused finished_subscription_id, but can only do that once
		}
		return {subscription_id, finished_subscription_id};
	}

	if ((int)finished_subscription_id >= 0) {
		assert(subscription_id == finished_subscription_id);
		/*
			We have reused an old subscription_id, but are now using a different fe than before
			Find the old fe we are using; not that at this point, subscription_id will be present twice
			as a value in subscriptions; find the old one (by skipping the new one)
		*/
		subscriptions.erase(finished_fe_key); //remove old entry
		subscribed_muxes.erase(subscription_id);
		finished_subscription_id = subscription_id_t{-1}; // we can still attempt to subscribe, but with new a new subscription_id
	}
	subscribed_muxes[subscription_id] = mux_to_scan;

	return {subscription_id, finished_subscription_id};
}


/*
	returns the number of pending  muxes to scan, and number of skipped muxes
 */
template<typename mux_t>
std::tuple<int, int> scanner_t::scan_next(db_txn& rtxn, subscription_id_t finished_subscription_id)
{
	std::vector<task_queue_t::future_t> futures;

	// start as many subscriptions as possible
	using namespace chdb;
	auto c = mux_t::find_by_scan_status(rtxn, scan_status_t::PENDING, find_type_t::find_geq,
																			mux_t::partial_keys_t::scan_status);
	int num_pending{0};
	auto c1 = c.clone();
	int num_skipped{0};
	devdb::fe_key_t finished_fe_key;
	std::map<int, bool> skip_map;

	if ((int)finished_subscription_id >= 0) {
		/*
			find the fe used by our existing subscription
		*/
		auto [itold, foundold] = find_in_map_if(subscriptions, [&finished_subscription_id](const auto& x) {
			auto& [fe_key, subscription_id_] = x;
			bool match = (subscription_id_ == finished_subscription_id);
			return match;
		});

		assert(foundold);
		finished_fe_key = itold->first;
	}

#if 0
	dtdebug("-------------------------------------");
	for(auto mux_to_scan: c1.range())  {
		dtdebug(" MUX=" << mux_to_scan);
	}
	dtdebug("++++++++++++++++++++++++++++++++++");
#endif

	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};
	for(auto mux_to_scan: c.range()) {

		assert(mux_to_scan.c.scan_status == scan_status_t::PENDING);
		if ((int)subscriptions.size() >=
				((int)finished_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1))
			continue; // to have accurate num_pending count
		bool& skip_mux = skip_helper(skip_map, mux_to_scan);
		if(skip_mux) {
			num_skipped++;
			num_pending++;
			continue;
		}

		std::tie(subscription_id, finished_subscription_id) =
			scan_try_mux(finished_subscription_id, finished_fe_key, mux_to_scan, nullptr);

		if ((int)subscription_id < 0) {
			// we cannot subscribe the mux right now
			num_pending++;
			skip_mux = true; //ensure that we do not even try muxes on the same sat, pol, band in this run
			continue;
		}

	}


	if ((int)finished_subscription_id >= 0) {
		assert (finished_subscription_id == subscription_id || (int)subscription_id < 0);
		//we have not reused finished_subscription_id so this subscription must be ended
		dtdebugx("Abandoning subscription %d", (int) finished_subscription_id);
		auto wtxn = receiver.devdb.wtxn();
		receiver_thread.unsubscribe(futures, wtxn, finished_subscription_id);
		wtxn.commit();
		report("ERASED", finished_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
		subscriptions.erase(finished_fe_key); //remove old entry
		subscribed_muxes.erase(subscription_id);
		wait_for_all(futures);
	}

	return {num_pending, num_skipped};
}

/*
	Returns number of muxes left to scan
*/
int scanner_t::scan_loop(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux) {
	std::vector<task_queue_t::future_t> futures;
	int error{};
	int num_pending{0};
	int num_skipped{0};

	auto& finished_mux_key = *chdb::mux_key_ptr(finished_mux);
	assert(finished_mux_key.sat_pos == sat_pos_none || finished_fe.sub.owner == getpid());
	subscription_id_t finished_subscription_id_dvbs{-1};
	subscription_id_t finished_subscription_id_dvbc{-1};
	subscription_id_t finished_subscription_id_dvbt{-1};

	try {
		subscription_id_t finished_subscription_id{-1};

		if (finished_mux_key.sat_pos != sat_pos_none) { //we are not called with empty finished_fe and finished_mux_key
			auto [it, found] = find_in_map(subscriptions, finished_fe.k);
			if (!found) {
				dterror("Cannot find subscription for active_adapter");
				assert(0);
			}
			finished_subscription_id = it->second;
			if(finished_mux_key.sat_pos == sat_pos_dvbc)
				finished_subscription_id_dvbc = finished_subscription_id;
			else if(finished_mux_key.sat_pos == sat_pos_dvbt)
				finished_subscription_id_dvbt = finished_subscription_id;
			else
				finished_subscription_id_dvbs = finished_subscription_id;
		}

		// start as many subscriptions as possible
		auto rtxn = receiver.chdb.rtxn();
		using namespace chdb;
		int num_pending1{0};
		int num_skipped1{0};

		std::tie(num_pending1, num_skipped1) = scan_next<chdb::dvbs_mux_t>(rtxn, finished_subscription_id_dvbs);
		num_pending += num_pending1;
		num_skipped += num_skipped1;

		std::tie(num_pending1, num_skipped1) = scan_next<chdb::dvbc_mux_t>(rtxn, finished_subscription_id_dvbc);
		num_pending += num_pending1;
		num_skipped += num_skipped1;

		std::tie(num_pending1, num_skipped1) = scan_next<chdb::dvbt_mux_t>(rtxn, finished_subscription_id_dvbt);
		num_pending += num_pending1;
		num_skipped += num_skipped1;

		dtdebugx("finished_subscription_id=%d pending=%d skipped=%d", (int) finished_subscription_id, num_pending, num_skipped);

		if (finished_fe.sub.usals_pos != sat_pos_none) {
			add_completed(finished_fe, finished_mux, num_pending);
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
void scanner_t::add_completed(const devdb::fe_t& fe, const chdb::any_mux_t& mux, int num_pending) {
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
	for (auto[fe_key, subscription_id] : subscriptions) {
		cb(receiver_thread).unsubscribe(subscription_id);
	}
}


/*
	called from tuner thread when scanning a mux has ended
*/
int scanner_t::on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux)
{
	if (must_end) {
		dtdebug("must_end");
		return 0;
	}
	dtdebug("finished_fe adapter " << finished_fe.adapter_no << " " << finished_fe.sub << " " << " muxes left to scan; "
					<< ((int) subscriptions.size()) << " active");

	auto pending = scan_loop(finished_fe, finished_mux);

	return must_end ? 0 : pending + subscriptions.size();
}


template int scanner_t::add_muxes<chdb::dvbs_mux_t>(const ss::vector_<chdb::dvbs_mux_t>& muxes, bool init);
template int scanner_t::add_muxes<chdb::dvbc_mux_t>(const ss::vector_<chdb::dvbc_mux_t>& muxes, bool init);
template int scanner_t::add_muxes<chdb::dvbt_mux_t>(const ss::vector_<chdb::dvbt_mux_t>& muxes, bool init);


template std::tuple<int,int> scanner_t::scan_next<chdb::dvbs_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);
template std::tuple<int,int> scanner_t::scan_next<chdb::dvbc_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);
template std::tuple<int,int> scanner_t::scan_next<chdb::dvbt_mux_t>(db_txn& rtxn, subscription_id_t finished_subscription_id);
