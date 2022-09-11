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


using spectral_scan_status_t = chdb::spectral_peak_t::scan_status_t;
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


bool blindscan_key_t::operator<(const blindscan_key_t& other) const {
	if(sat_pos != other.sat_pos)
		return sat_pos < other.sat_pos;
	if(band != other.band)
		return band < other.band;
	if(pol != other.pol)
		return (int) pol  < (int) other.pol;
	return false;
	//return (intptr_t) subscriber  < (intptr_t) other.subscriber;
}

blindscan_key_t::blindscan_key_t(int16_t sat_pos, chdb::fe_polarisation_t pol, uint32_t frequency)
	: sat_pos(sat_pos)
	, band{frequency >= 11700000 && frequency <= 12800000}
	, pol(pol)
	{}


template<typename mux_t>
requires (! std::is_same_v<chdb::spectral_peak_t, mux_t>)
static inline bool& skip_helper(std::map<blindscan_key_t,bool>& skip_map, const mux_t& mux)
{
	using namespace chdb;
	if constexpr (std::is_same_v<chdb::dvbs_mux_t, mux_t>) {
		return skip_map[blindscan_key_t{mux.k.sat_pos, mux.pol, mux.frequency}];
	} else {
		return skip_map[blindscan_key_t{mux.k.sat_pos, chdb::fe_polarisation_t::H, mux.frequency}];
	}
}



/*
	returns the number the new subscription_id and the new value of finished_subscription_id,
	which will be -1 to to indicate that it can no longer be reused
 */
template<typename mux_t>
std::tuple<subscription_id_t, subscription_id_t>
scanner_t::scan_try_mux(subscription_id_t finished_subscription_id ,
												devdb::fe_key_t finished_fe_key,
												const mux_t& mux_to_scan, const devdb::lnb_key_t* required_lnb_key)
{
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};
	std::vector<task_queue_t::future_t> futures;
	{
		auto wtxn = receiver.devdb.wtxn();
			std::tie(subscription_id, subscribed_fe_key) =
				receiver_thread.subscribe_mux(futures, wtxn, mux_to_scan, finished_subscription_id, tune_options,
																			required_lnb_key);
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
	int num_pending{0};

	int num_skipped{0};
	devdb::fe_key_t finished_fe_key;
	std::map<blindscan_key_t, bool> skip_map;

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

	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};

	/*Scan all spectral peaks for blindscans launched from the spectrum dialog is in propgress.
		In this case we force the lnb used to acquire the spectrum (e.g., in case the lnb is on a large \
		this, and scanning another lnb on a smaller dish might not succeed).

		The user can also launch blindscan on more than one lnb but for the same sat (e.g., on multiple dishes).
		In this case, in the current code, only one of those lnbs will be considered, as the peaks are stored
		in the same data structure


	*/

	for(auto& [key, blindscan]:  blindscans) {
		bool& skip_sat_band = skip_map[key]; /*Note that key depends on lnb for a spectral peak, whereas
																					 key below does not depend on lnb for a mux (which can be for any
																					 lnb).
																					 TODO: various strange situations can occur:
																					 1. user asks to scan muxes ffrom dvbs mux and then also
																					 to scan peaks from spectrum dialog. This can lead to the same
																					 mux being scanned on a specific lnb and on "any" lnb
																					 2. even if user only starts blindscan, which will use a specific lnb,
																					 si processing can create new muxes which can then be scanned on any lnb

																				 */
		if(skip_sat_band) {
			num_skipped += blindscan.peaks.size();
			num_pending += blindscan.peaks.size();
			continue;
			}
			for(int idx = blindscan.peaks.size()-1;  idx>=0 ; --idx) {
				auto &peak = blindscan.peaks[idx];
				using namespace chdb;
				if(peak.scan_status == spectral_scan_status_t::NON_BLIND_IN_PROGRESS ||
					 peak.scan_status == spectral_scan_status_t::BLIND_IN_PROGRESS)
					continue;

				dvbs_mux_t mux;
				if ((int)subscriptions.size() >=
						((int)finished_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1)) {
					num_skipped += blindscan.peaks.size();
					num_pending += blindscan.peaks.size();
					break; // to have accurate num_pending count
				}
				mux.k.sat_pos = blindscan.sat_pos;
				mux.frequency = peak.frequency;
				mux.pol = peak.pol;
				mux.symbol_rate = peak.symbol_rate;
				assert(mux.stream_id == -1);
				assert(mux.pls_mode == fe_pls_mode_t::ROOT);
				assert(mux.pls_code == 1);
				/*
					each peak will be scanned twice if we have si data for the mux:
					the first scan will use the si data. The second scan will only take
					place if the si-data based scan fails

					peak.retry_blind is set to true if the first scan has been tried,
					in which case it should not be retried

				 */
				auto new_scan_status = spectral_scan_status_t::BLIND_IN_PROGRESS;
				if(peak.scan_status == spectral_scan_status_t::NON_BLIND_PENDING) {
					auto c = find_by_mux_physical(rtxn, mux, true/*ignore_stream_ids*/);
					if(c.is_valid()) {
						auto db_mux = c.current();
						if(true || db_mux.c.scan_status == scan_status_t::PENDING) {
							/*heuristic: we assume that this means scanning an earlier mux has
								provided si data with correct tuning parameters. This assumption could be
								wrong if the user has simultaneously launched a regular (non-blind) scan)

								We try these values instead of the blind scanned ones
							*/
							mux = db_mux;
							new_scan_status = spectral_scan_status_t::NON_BLIND_IN_PROGRESS;
						}
					} else {
						new_scan_status = spectral_scan_status_t::BLIND_IN_PROGRESS;
					}
				} else {
					assert(peak.scan_status == spectral_scan_status_t::BLIND_PENDING);
					new_scan_status = spectral_scan_status_t::BLIND_IN_PROGRESS;
				}
				mux.c.scan_status = scan_status_t::PENDING;
				std::tie(subscription_id, finished_subscription_id) =
					scan_try_mux(finished_subscription_id, finished_fe_key, mux,
											 blindscan.required_lnb_key ? &*blindscan.required_lnb_key : nullptr);
#if 0
				if(finished_subscription_id == blindscan_list.current_subscription_id)
					blindscan_list.current_subscription_id = subscription_id;
#endif
				if ((int)subscription_id < 0) {
					// we cannot subscribe the mux right now
					num_pending++;
					skip_sat_band = true; //ensure that we do not even try muxes on the same sat, pol, band in this run
					continue;
				} else {
					peak.scan_status = new_scan_status;
				}
			}
	}

	/*
		Now scan any si muxes. These can either be entered by the user
		or added as a result of an earlier scan. The muxes tried are the ones with scan status PENDING
		in the database. The database does not contain any preference for an lnb, so as a general rule
		scanning will use any suitable lnb.
	 */
	auto c = mux_t::find_by_scan_status(rtxn, scan_status_t::PENDING, find_type_t::find_geq,
																			mux_t::partial_keys_t::scan_status);
	for(auto mux_to_scan: c.range()) {
		assert(mux_to_scan.c.scan_status == scan_status_t::PENDING);
		if ((int)subscriptions.size() >=
				((int)finished_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1)) {
			num_pending++;
			continue; // to have accurate num_pending count
		}
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
	remove spectral peaks which have already been successfully scanned
 */

void scanner_t::clear_peaks(const chdb::dvbs_mux_t& finished_mux) {
	bool failed{true};
	using namespace chdb;
	switch(finished_mux.c.scan_result) {
	case  scan_result_t::PARTIAL:
	case  scan_result_t::OK:
	case  scan_result_t::NODATA:
		failed = false;
		break;
	default:
		return;
	}

	blindscan_key_t key{finished_mux.k.sat_pos, finished_mux.pol, finished_mux.frequency};
	auto& blindscan = blindscans[key];
	for(int idx=0; idx < blindscan.peaks.size(); ++idx) {
		auto& peak = blindscan.peaks[idx];
		dvbs_mux_t mux;
		mux.k.sat_pos = blindscan.sat_pos;
		mux.frequency = peak.frequency;
		mux.pol = peak.pol;
		mux.symbol_rate = peak.symbol_rate;
		assert(mux.stream_id == -1);
		assert(mux.pls_mode == fe_pls_mode_t::ROOT);
		assert(mux.pls_code == 1);

		if(chdb::matches_physical_fuzzy(mux, finished_mux, true /*check_sat_pos*/)) {
			if(failed) {
				switch(peak.scan_status) {
				case spectral_scan_status_t::NON_BLIND_PENDING:
				case spectral_scan_status_t::BLIND_PENDING:
					continue;
				case spectral_scan_status_t::NON_BLIND_IN_PROGRESS:
					peak.scan_status = spectral_scan_status_t::BLIND_PENDING;
					continue;
				case spectral_scan_status_t::BLIND_IN_PROGRESS:
					blindscan.peaks.erase(idx);
					continue;
				}
			} else {// ! failed
				blindscan.peaks.erase(idx);
			}
		}
	}
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

	auto* finished_dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&finished_mux);
	if(finished_dvbs_mux)
		clear_peaks(*finished_dvbs_mux);

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
			*w = scan_stats_t(muxes.size() /*num_scheduled_muxes*/, 0 /* num_scheduled_peaks*/,
												last_subscribed_mux);
		} else{
			w->scheduled_muxes += muxes.size(); //might be wrong when same mux is added, but scan_loop will fix this later
		}
		w->active_muxes = subscriptions.size();
	}
	return 0;
}

int scanner_t::add_peaks(const statdb::spectrum_key_t& spectrum_key,
												 const ss::vector_<chdb::spectral_peak_t>& peaks, bool init) {
	for(const auto& peak: peaks) {
		blindscan_key_t key = {spectrum_key.sat_pos, spectrum_key.pol, peak.frequency};
		auto& blindscan = blindscans[key];
		if(blindscan.sat_pos == sat_pos_none) {
			blindscan.sat_pos = spectrum_key.sat_pos;
			blindscan.required_lnb_key = spectrum_key.lnb_key;
		}  else {
			assert(blindscan.required_lnb_key == spectrum_key.lnb_key);
			assert(blindscan.sat_pos == spectrum_key.sat_pos);
			blindscan.peaks.push_back(peak);
		}
	}


	//initialize statistics
	{
		auto w = scan_stats.writeAccess();
		if (init) {
			*w = scan_stats_t(0 /*num_scheduled_muxes*/, peaks.size() /* num_scheduled_peaks*/,
												last_subscribed_mux);
		} else{
			w->scheduled_peaks += peaks.size(); //might be wrong when same mux is added, but scan_loop will fix this later
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
