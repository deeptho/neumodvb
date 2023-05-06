/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "subscriber.h"
#include "active_adapter.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "neumodb/db_keys_helper.h"
#include "receiver.h"
#include "util/template_util.h"

scan_report_t::scan_report_t(const scan_subscription_t& subscription,
														 const statdb::spectrum_key_t spectrum_key,
														 const scan_stats_t& scan_stats)
//: sat_pos(subscription.blindscan_key.sat_pos)
	: spectrum_key(spectrum_key)
	, band(subscription.blindscan_key.band)
	, peak(subscription.peak)
	, mux(subscription.mux)
	, fe_key(subscription.fe_key)
	, scan_stats(scan_stats)
		//, lnb_key (lnb_key)
{}


scan_t::scan_t(	scanner_t& scanner, subscription_id_t scan_subscription_id, bool propagate_scan)
	: scanner(scanner),
		receiver_thread(scanner.receiver_thread)
	, receiver(receiver_thread.receiver)
	, scan_subscription_id(scan_subscription_id)
	, scan_id(scanner_t::make_scan_id(scan_subscription_id))
{
	dtdebugx("MAKE SCAN_ID: pid=0x%x subscription_id=%d ret=%d\n", getpid(), (int)scan_subscription_id, scan_id);
			tune_options.scan_target =  scan_target_t::SCAN_FULL;
			tune_options.propagate_scan = propagate_scan; //if false, we scan only peaks
			tune_options.retune_mode = retune_mode_t::NEVER;
			tune_options.subscription_type = subscription_type_t::SCAN;
			tune_options.may_move_dish = false; //could become an option
			tune_options.use_blind_tune = false; //could become an option
			tune_options.constellation_options.num_samples = 1024 * 16;
}



/*
	Processes events and continues scanning.
	found_mux_keys = muxes found in nit_actual and nit_other
	Return 0 when scanning is done; otherwise number of muxes left

*/
bool scanner_t::housekeeping(bool force) {
	if (must_end)
		return true;
	auto now = steady_clock_t::now();
	if (!force && now - last_house_keeping_time < 60s)
		return false;
	last_house_keeping_time = now;
	int pending{0};
	int active{0};
	try {
		for(auto& [subscription_id, scan]: scans) {
			auto [pending1, active1] = scan.scan_loop({}, {}, {});
			active += active1;
			pending += pending1;
		}
	} catch(std::runtime_error) {
		dtdebug("Detected exit condition");
		must_end = true;
	}

	dtdebugx("%d muxes left to scan; %d active", pending, active);

	return must_end ? true : ((pending + active)==0);
}

static void report(const char* msg, subscription_id_t finished_subscription_id,
									 subscription_id_t subscription_id, const chdb::any_mux_t& mux,
									 const std::map<subscription_id_t, scan_subscription_t>& subscriptions)
{
	if ((int) finished_subscription_id < 0 && (int) subscription_id < 0)
		return;
	ss::string<128> s;
	//to_str(s, mux);
	s.sprintf(" Scan  %s %s finished_subscription_id=%d subscription_id=%d todo=[",
						msg, to_str(mux).c_str(), finished_subscription_id, subscription_id);
	for (auto& [id, unused] : subscriptions)
		s.sprintf("%2d ", (int)id);
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
requires (! is_same_type_v<chdb::spectral_peak_t, mux_t>)
static inline bool& skip_helper(std::map<blindscan_key_t,bool>& skip_map, const mux_t& mux)
{
	using namespace chdb;
	if constexpr (is_same_type_v<chdb::dvbs_mux_t, mux_t>) {
		return skip_map[blindscan_key_t{mux.k.sat_pos, mux.pol, mux.frequency}];
	} else {
		return skip_map[blindscan_key_t{mux.k.sat_pos, chdb::fe_polarisation_t::H, mux.frequency}];
	}
}


/*check if a finished mux belongs to the current scan_t (it could belong to another scan_t
 */
subscription_id_t scanner_t::scan_subscription_id_for_scan_id(uint32_t scan_id) {
	if (scan_id >>8 != getpid())
		return subscription_id_t{-1};
	auto scan_subscription_id = subscription_id_t(scan_id & 0xff);
	return scan_subscription_id;
}

/*
	called from tuner thread when scanning a mux has ended
	returns true if scanner is empty and should be removed
*/
bool scanner_t::on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
																uint32_t scan_id, subscription_id_t subscription_id)
{

	if (must_end) {
		dtdebug("must_end");
		return true;
	}
	auto scan_subscription_id = scan_subscription_id_for_scan_id(scan_id);
	if((int)scan_subscription_id >= 0) {
		try {
			last_house_keeping_time = steady_clock_t::now();
			auto& scan = scans.at(scan_subscription_id);

			auto [pending, active] = scan.scan_loop(finished_fe, finished_mux, subscription_id);

			dtdebug("finished_fe adapter " << finished_fe.adapter_no <<
							" mux=<" << finished_mux << "> " <<
							" muxes to scan: pending=" << (int) pending << " active=" << (int) active << " subs="  <<
							((int) scan.subscriptions.size()));

			while(!must_end && ( pending + active != 0) && scan.subscriptions.size() == 0 ) {
				std::tie(pending, active) = scan.scan_loop({}, {}, {});
			}
			auto ret = must_end ? 0 : pending + active;
			if( !ret ) {
				std::vector<task_queue_t::future_t> futures;
				auto devdb_wtxn = receiver.devdb.wtxn();
				unsubscribe_scan(futures, devdb_wtxn, scan_subscription_id);
				devdb_wtxn.commit();
				wait_for_all(futures); //remove later
				return true;
			}
		} catch(std::runtime_error) {
			dtdebug("Detected exit condition");
			must_end = true;
		}
		return false;
	} else {
		return false; //this scan must be from another running instance of neumoDVB
	}
}

/*
	rescan a peak after the first scan, which used database parameters and which failed.
	This time, use peak parameters
	returns false if scan has been launched successfully, or otherwise true, meaning that
	mux scan should be abolished
 */
bool
scan_t::rescan_peak(blindscan_t& blindscan,
										subscription_id_t reuseable_subscription_id, scan_subscription_t& subscription)

{
	using namespace chdb;
	//auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&subscription.mux);
	//assert (dvbs_mux);
	//auto& mux = *dvbs_mux;
	subscription.is_peak_scan = true;
	subscription_id_t subscription_id{-1};
	assert(subscription.mux);

	std::visit([&](auto& mux) {
		assert(!blindscan.valid() || mux.k.sat_pos == blindscan.spectrum_key.sat_pos);

		mux.frequency = subscription.peak.frequency;
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t>) {
			assert(!blindscan.valid() || mux.k.sat_pos == blindscan.spectrum_key.sat_pos);
			assert(!blindscan.valid() || mux.pol == subscription.peak.pol);
			if(blindscan.spectrum_key.sat_pos != sat_pos_none) {
				mux.symbol_rate = subscription.peak.symbol_rate;
			}
			mux.pls_mode = fe_pls_mode_t::ROOT;
			mux.pls_code = 1;
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbt_mux_t>) {
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbc_mux_t>) {
		}
		mux.k.stream_id = -1;
		dtdebug("SET PENDING " << mux);
		mux.c.scan_status = scan_status_t::PENDING;
		assert(mux.c.scan_id == scan_id);
		const bool use_blind_tune = true;
		std::tie(subscription_id, reuseable_subscription_id) =
			scan_try_mux(reuseable_subscription_id, subscription,
									 blindscan.spectrum_key.sat_pos != sat_pos_none
									 ? &blindscan.spectrum_key.rf_path : nullptr, use_blind_tune);
	}, *subscription.mux);



	if ((int)subscription_id < 0) {
		/* we cannot subscribe the mux right now.  This should never happen
			 as we own a subscription
		*/
		return true;
	} else {
	}
	return false;
}


subscription_id_t
scan_t::scan_peak(db_txn& chdb_rtxn, blindscan_t& blindscan,
									subscription_id_t finished_subscription_id, scan_subscription_t& subscription)
{
	using namespace chdb;
	subscription.is_peak_scan = true;
	subscription_id_t subscription_id;
	assert(subscription.mux);
	auto saved = * subscription.mux;
	std::visit([&](auto& mux) {
		mux.k.sat_pos = blindscan.spectrum_key.sat_pos;
		mux.frequency = subscription.peak.frequency;
		set_member(mux, pol, subscription.peak.pol);
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t&>) {
			assert(mux.k.sat_pos == blindscan.spectrum_key.sat_pos);
			assert(mux.pol == subscription.peak.pol);
			mux.symbol_rate = subscription.peak.symbol_rate;
			mux.pls_mode = fe_pls_mode_t::ROOT;
			mux.pls_code = 1;
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbt_mux_t>) {
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbc_mux_t>) {
			assert(mux.k.sat_pos == blindscan.spectrum_key.sat_pos);
			mux.symbol_rate = subscription.peak.symbol_rate;
		}
		mux.k.t2mi_pid = -1;
		mux.k.stream_id = -1;
		mux.c.scan_status = scan_status_t::PENDING;
		/* ignore_t2mi_pid = false to ensure that we always find the encapsulating mux in case of t2mi*/
		auto c = find_by_mux_physical(chdb_rtxn, mux, true/*ignore_stream_id*/, /*false *ignore_key, */
																	false /*ignore_t2mi_pid*/);
		if(c.is_valid()) {
			auto db_mux = c.current();
			/*heuristic: we assume that this means scanning an earlier mux has
				provided si data with correct tuning parameters. This assumption could be
				wrong if the user has simultaneously launched a regular (non-blind) scan)

				We try these values instead of the blind scanned ones
			*/
			mux = db_mux;
			subscription.is_peak_scan = false;
		} else {
			subscription.is_peak_scan = true;
			mux.c.tune_src = tune_src_t::TEMPLATE;
		}
		dtdebug("SET PENDING " << mux);
		mux.c.scan_status = scan_status_t::PENDING;
		mux.c.scan_id = scan_id;
		subscription.mux = mux;
		const bool use_blind_tune = true;
		std::tie(subscription_id, finished_subscription_id) =
			scan_try_mux(finished_subscription_id, subscription,
									 blindscan.spectrum_key.sat_pos != sat_pos_none
									 ? &blindscan.spectrum_key.rf_path : nullptr, use_blind_tune);
	}, *subscription.mux);
	return subscription_id;
}



/*
	process a finished subscription;
	returns false if we have restarted the subscription for the same frequency/pol
	but with a different stream_id or a t2mi_pid.
	Otherwise return true, signaling that this subscription is done
 */
bool scan_t::finish_subscription(db_txn& rtxn,  subscription_id_t subscription_id,
																 scan_subscription_t& subscription,
																 const chdb::any_mux_t& finished_mux)
{
	using namespace chdb;
	assert(subscription.mux);
	auto& blindscan = blindscans[subscription.blindscan_key];

	auto saved = *subscription.mux;
	subscription.mux = finished_mux;
	auto* c = mux_common_ptr(*subscription.mux);
	bool failed{true};
	switch(c->scan_result) {
	case  scan_result_t::PARTIAL:
	case  scan_result_t::OK:
	case  scan_result_t::NODATA:
		failed = false;
		break;
	case chdb::scan_result_t::TEMPFAIL: {
		/*
			This currently happens on tbs6909x if the LLR budget has been exceeded.
			As a heuristic we will rescan only if the number of subscriptions drops by 1,
			including the rescanned mux.
			Such muxes will have the scan status RETRY if they were saved in the database

			TODO: if we are scanning a peak not yet in the database, we have to re-add the peak
		*/
		auto temp = std::max((int)subscriptions.size() -1, 0);
		if(max_num_subscriptions_for_retry == std::numeric_limits<int>::max())
			 max_num_subscriptions_for_retry = std::min(temp, max_num_subscriptions_for_retry);

		if(subscription.is_peak_scan)
			blindscan.peaks.push_back(subscription.peak);
		return true;
	}
		break;
	default:
		break;
	}

	if(!blindscan.valid()) {
		return true; //regular scan, not spectrum scan, this can be finished
	}

	if(failed) {
		int frequency;
		int symbol_rate;
		std::visit([&frequency, &symbol_rate](auto& mux)  {
			frequency= get_member(mux, frequency, -1);
			symbol_rate = get_member(mux, symbol_rate, -1);
		}, finished_mux);

		if (subscription.is_peak_scan)
			return true; //we have blindscanned already
		if(std::abs(symbol_rate -(int) subscription.peak.symbol_rate) <
			 std::min(symbol_rate, (int) subscription.peak.symbol_rate)/4) //less than 25% difference in symbol rate
			return true; //blindscanned will not likely lead to different results
		dtdebug("Calling rescan_peak for mux: " << finished_mux);
		return rescan_peak(blindscan, subscription_id, subscription);
	}

	/*
		Note: this mux may have to be scanned again with a different stream_id,
		but this is handled elsewhere as such muxes have scan_status_t::PENDING
	 */
	return true;
}


/*
	returns the number of pending  muxes to scan, and number of skipped muxes
	and subscription to erase, and the new value of reusable_subscription_id
 */
template<typename mux_t>
std::tuple<int, int, int, int, subscription_id_t> scan_t::scan_next(db_txn& chdb_rtxn,
																													subscription_id_t reuseable_subscription_id,
																													scan_subscription_t& subscription)
{
	std::vector<task_queue_t::future_t> futures;
	// start as many subscriptions as possible
	using namespace chdb;
	int num_pending_muxes{0};
	int num_skipped_muxes{0};
	int num_pending_peaks{0};
	int num_skipped_peaks{0};

	std::map<blindscan_key_t, bool> skip_map;


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
			num_skipped_peaks += blindscan.peaks.size();
			num_pending_peaks += blindscan.peaks.size();
			continue;
		}
		if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) { //TODO: check based on peak freduency, required_lnb and such
			for(int idx = blindscan.peaks.size()-1;  idx>=0 ; --idx) {
				auto &peak = blindscan.peaks[idx];
				using namespace chdb;

				auto max_num_subscriptions = scanner.max_num_subscriptions;
				if ((int)subscriptions.size() >=
						((int)reuseable_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1)) {
					num_skipped_peaks += blindscan.peaks.size();
					num_pending_peaks += blindscan.peaks.size();
					break; // to have accurate num_pending count
				}
				subscription.blindscan_key = key;
				subscription.mux = mux_t{};
				subscription.peak = peak;

				if(receiver_thread.must_exit())
					throw std::runtime_error("Exit requested");

				auto subscription_id = scan_peak(chdb_rtxn, blindscan, reuseable_subscription_id, subscription);
				if ((int)subscription_id < 0) {
					// we cannot subscribe the mux right now
					if(subscription_id == subscription_id_t::RESERVATION_FAILED) {
						num_pending_peaks++;
						skip_sat_band = true; /*ensure that we do not even try muxes on the same sat, pol, band in this run
																		if the error is due to not being able to make a reservation
																	*/
					} else {
						//it is not possible to tune, probably because symbol_rate is out range
						blindscan.peaks.erase(idx);
					}
					continue;
				} else {
					reuseable_subscription_id = (subscription_id_t) -1;
					blindscan.peaks.erase(idx);
				}
			}
		}
	}

	for(int pass=0; pass < 2; ++pass) {
		/*
			Now scan any si muxes. If such scans match our scan_id then they must have been added
			for some peak we failed to discover
		*/
		if(pass == 0 && (int)subscriptions.size() >=
			 max_num_subscriptions_for_retry - (((int)reuseable_subscription_id >=0) ? 1 : 0)) {
			/*heuristic: require at least 2 demods free before retrying a temp fail
			 */
			continue;
		}

		auto c = mux_t::find_by_scan_status(chdb_rtxn, pass ==0
																				? scan_status_t::RETRY :
																				scan_status_t::PENDING, find_type_t::find_geq,
																				mux_t::partial_keys_t::scan_status);
		for(auto mux_to_scan: c.range()) {

			assert(mux_to_scan.c.scan_status == scan_status_t::PENDING ||
						 mux_to_scan.c.scan_status == scan_status_t::RETRY);
			if(mux_to_scan.c.scan_id != scan_id)
				continue;

			if ((int)subscriptions.size() >=
					((int)reuseable_subscription_id < 0 ? scanner.max_num_subscriptions : scanner.max_num_subscriptions + 1)) {
				num_pending_muxes++;
				continue; // to have accurate num_pending count
			}

			bool& skip_mux = skip_helper(skip_map, mux_to_scan);
			if(skip_mux) {
				num_skipped_muxes++;
				num_pending_muxes++;
				continue;
			}
			subscription.mux = mux_to_scan;

			if(receiver_thread.must_exit())
				throw std::runtime_error("Exit requested");

			auto pol = get_member(mux_to_scan, pol, chdb::fe_polarisation_t::NONE);
			blindscan_key_t key = {mux_to_scan.k.sat_pos, pol, mux_to_scan.frequency};
			auto [it, found] = find_in_map(blindscans, key);
			devdb::rf_path_t* required_rf_path{nullptr};
			if(found) {
				auto& blindscan = it->second;
				required_rf_path = blindscan.valid()
					? &blindscan.spectrum_key.rf_path : nullptr;
			}
			if(mux_is_being_scanned(mux_to_scan)) {
				dtdebug("Skipping mux already in progress: " << mux_to_scan);
				continue;
			}

			subscription_id_t subscription_id;
			const bool use_blind_tune = false;
			std::tie(subscription_id, reuseable_subscription_id) =
				scan_try_mux(reuseable_subscription_id, subscription, required_rf_path, use_blind_tune);

			if ((int)subscription_id < 0) {
				// we cannot subscribe the mux right now
				if(subscription_id == subscription_id_t::RESERVATION_FAILED_PERMANENTLY) {
					skip_mux = false; //ensure that we do not even try muxes on the same sat, pol, band in this run
				} else if(subscription_id == subscription_id_t::RESERVATION_FAILED) {
					num_pending_muxes++;
					skip_mux = true; //ensure that we do not even try muxes on the same sat, pol, band in this run
				} else {
					//it is not possible to tune, probably because symbol_rate is out range
				}
				continue;
			}
		}
	}
	if ((int)reuseable_subscription_id >= 0) {
		subscription_id_t subscription_id{-1};
		//we have not reused reuseable_subscription_id so this subscription must be ended
		dtdebugx("Abandoning subscription %d", (int) reuseable_subscription_id);
		auto wtxn = receiver.devdb.wtxn();
		receiver_thread.unsubscribe(futures, wtxn, reuseable_subscription_id);
		wtxn.commit();
		report("ERASED", reuseable_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
		//subscriptions.erase(reuseable_subscription_id); //remove old entry
		wait_for_all(futures);
	}
	return {num_pending_peaks, num_skipped_peaks, num_pending_muxes, num_skipped_muxes, reuseable_subscription_id};
}



/*
	Returns number of muxes left to scan, and number of running subscriptions
*/
std::tuple<int, int>
scan_t::scan_loop(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
									subscription_id_t finished_subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	int error{};
	int num_pending_muxes{0};
	int num_skipped_muxes{0};
	int num_pending_peaks{0};
	int num_skipped_peaks{0};
	auto& finished_mux_key = *mux_key_ptr(finished_mux);
	scan_subscription_t* finished_subscription_ptr{nullptr};
	auto try_all = [&](
		db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription) mutable {
		// start as many subscriptions as possible
		using namespace chdb;

		subscription_id_t finished_subscription_id_dvbs{-1};
		subscription_id_t finished_subscription_id_dvbc{-1};
		subscription_id_t finished_subscription_id_dvbt{-1};
		assert((finished_subscription_ptr== nullptr) == ((int) finished_subscription_id == -1));
		if (finished_subscription_ptr) { //a subscription can be reused
			assert((int) finished_subscription_id != -1);
			if(finished_mux_key.sat_pos == sat_pos_dvbc)
				finished_subscription_id_dvbc = finished_subscription_id;
			else if(finished_mux_key.sat_pos == sat_pos_dvbt)
				finished_subscription_id_dvbt = finished_subscription_id;
			else
				finished_subscription_id_dvbs = finished_subscription_id;
		}

		int num_pending_muxes1{0};
		int num_skipped_muxes1{0};
		int num_pending_peaks1{0};
		int num_skipped_peaks1{0};
		subscription_id_t subscription_to_erase1{-1};
		subscription_id_t subscription_to_erase{-1};
		/*scan any pending muxes. This is useful to reuse the finished subscription if it is not currently
			busy, but also to take advantage of frontends used for viewing tv but then release */

		if(receiver_thread.must_exit())
			throw std::runtime_error("Exit requested");

		dtdebugx("Calling scan_next");
		std::tie(num_pending_peaks1, num_skipped_peaks1, num_pending_muxes1, num_skipped_muxes1,
						 subscription_to_erase1) =
			scan_next<chdb::dvbs_mux_t>(chdb_rtxn, finished_subscription_id_dvbs, subscription);

		subscription_to_erase = subscription_to_erase1;
		num_pending_peaks += num_pending_peaks1;
		num_skipped_peaks += num_skipped_peaks1;
		num_pending_muxes += num_pending_muxes1;
		num_skipped_muxes += num_skipped_muxes1;

		if(receiver_thread.must_exit())
			throw std::runtime_error("Exit requested");

		dtdebugx("dvbc scan_next round\n");
		std::tie(num_pending_peaks1, num_skipped_peaks1, num_pending_muxes1, num_skipped_muxes1,
						 subscription_to_erase1) =
			scan_next<chdb::dvbc_mux_t>(chdb_rtxn, finished_subscription_id_dvbc, subscription);

		assert((int)subscription_to_erase == -1 || (int) subscription_to_erase1 == -1);
		if( (int) subscription_to_erase1 >=0)
			subscription_to_erase = subscription_to_erase1;

		num_pending_peaks += num_pending_peaks1;
		num_skipped_peaks += num_skipped_peaks1;
		num_pending_muxes += num_pending_muxes1;
		num_skipped_muxes += num_skipped_muxes1;

		if(receiver_thread.must_exit())
			throw std::runtime_error("Exit requested");

		dtdebugx("dvbt scan_next\n");
		std::tie(num_pending_peaks1, num_skipped_peaks1, num_pending_muxes1, num_skipped_muxes1,
						 subscription_to_erase1) =
			scan_next<chdb::dvbt_mux_t>(chdb_rtxn, finished_subscription_id_dvbt, subscription);

		num_pending_peaks += num_pending_peaks1;
		num_skipped_peaks += num_skipped_peaks1;
		num_pending_muxes += num_pending_muxes1;
		num_skipped_muxes += num_skipped_muxes1;

		assert((int)subscription_to_erase == -1 || (int) subscription_to_erase1 == -1);
		if( (int) subscription_to_erase1 >=0)
			subscription_to_erase = subscription_to_erase1;

		dtdebugx("finished_subscription_id=%d subscription_to_erase =%d pending=%d+%d skipped=%d+%d",
						 (int) finished_subscription_id,
						 (int) subscription_to_erase,
						 num_pending_peaks, num_pending_muxes, num_skipped_peaks, num_skipped_muxes);

		add_completed(finished_fe, finished_mux, num_pending_muxes, num_pending_peaks);
		return subscription_to_erase;
	};

	if(mux_key_ptr(finished_mux)->sat_pos != sat_pos_none) { //otherwise we are called from housekeeping
		int count{0};
		/*it is essential that subscription is retrieved by val in the loop below
			as it is filled with trial data which is only saved when subscription succeeds
		*/
		[&](){
			auto [it, found] = find_in_map(this->subscriptions, finished_subscription_id);
			if(!found) {
				dtdebugx("Skipping unknown subscription_id=%d", (int)finished_subscription_id);
				return;
			}
			scan_subscription_t subscription {it->second}; //need to copy
			/*in general only one subscription will match, but in specific cases
				multiple ones may exist. For example: is a mux is misdetected as two peaks,
				both peaks end up selecting the same database mux. That mux is successfully scanned
				and we need to terminate both subscriptions
			*/
			if( subscription.fe_key != finished_fe.k) {
				dtdebug("finished_fe keys do not match: fe_key=" << subscription.fe_key << " finished_fe.k="
								<< finished_fe.k);
				return; //no match
			}
			/*it is essential to start a new transaction at this point
				because scan_next will loop over all pending muxes, changing
				scan_status to ACTIVE for some of the muxes;
				In order to not rescan those muxes, we need to discover the new ACTIVE status,
				which is done by creating a new transaction
			*/
			auto chdb_rtxn = receiver.chdb.rtxn();
			dtdebugx("obtained transaction\n");
			count++;
			auto& blindscan = blindscans[subscription.blindscan_key];
			bool finished = finish_subscription(chdb_rtxn,  finished_subscription_id,
																					subscription, finished_mux);
			dtdebug("after finish_subscription: finished_mux=" << finished_mux << " finished=" << finished);
			scan_report_t report{subscription, blindscan.spectrum_key, {}};
			if (!finished) {
				finished_subscription_ptr = nullptr;
				it = std::next(it);
				return;
			} else {
				finished_subscription_ptr = & subscription;  //allow try_all to reuse this subscription
				subscription.mux.reset();
				dtdebug("calling try_all\n");
				auto subscription_to_erase = try_all(chdb_rtxn, finished_subscription_id, subscription);
				dtdebug("finished_subscription_id=" << (int) finished_subscription_id << " subscription_to_erase=" <<
								(int) subscription_to_erase
								<< " finished_mux=" << finished_mux);
				if((int) subscription_to_erase >=0) {
					assert(subscription_to_erase == finished_subscription_id);
					it = subscriptions.erase(it);
					if (subscription_to_erase == monitored_subscription_id)
						monitored_subscription_id = subscription_id_t::NONE;
					scan_stats.writeAccess()->active_muxes = subscriptions.size();
				} else {
					bool locked = chdb::mux_common_ptr(finished_mux)->scan_result != chdb::scan_result_t::NOLOCK;
					bool nodvb = chdb::mux_common_ptr(finished_mux)->scan_result == chdb::scan_result_t::NOTS;
					auto w = scan_stats.writeAccess();
					w->active_muxes = subscriptions.size();
					w->finished_muxes++;
					w->failed_muxes += !locked;
					w->locked_muxes += locked;
					w->si_muxes += (locked && !nodvb);
					it = std::next(it);
				}
			}
			assert(finished_mux_key.sat_pos == sat_pos_none || finished_fe.sub.owner == getpid());
			report.scan_stats = *scan_stats.readAccess();
			auto ss = *scan_stats.readAccess();
			dtdebug("SCAN REPORT: mux=" << *report.mux << " pending=" << ss.pending_muxes << " active=" << ss.active_muxes);
			receiver_thread.notify_scan_mux_end(scan_subscription_id, report);
			dtdebugx("committing\n");
			chdb_rtxn.commit();
		}();

		if(count==0) {
			dterror("XXXX finished subscription not found for " << finished_mux);
		} else if(count!=1) {
			dtdebug("XXXX multiple finished subscriptions for " << finished_mux);
		}
	} else {
		//in case new frontends or lnbs have become available for use
		scan_subscription_t subscription;
		finished_subscription_ptr = nullptr;
		auto finished_subscription_id = subscription_id_t{-1};
		dtdebug("calling try_all\n");
		auto chdb_rtxn = receiver.chdb.rtxn();
		dtdebugx("obtained transaction\n");
		auto subscription_to_erase = try_all(chdb_rtxn, finished_subscription_id, subscription);
		assert((int) subscription_to_erase == -1);
		dtdebugx("committing\n");
		chdb_rtxn.commit();

		if(subscriptions.size() ==0) {
			/*send one more message to inform gui that scan is done
				This may be needed when all pending scans fail
			*/
			subscription.mux = {};
			scan_report_t report{subscription, {}, {}};
			report.scan_stats = *scan_stats.readAccess();
			auto ss = *scan_stats.readAccess();
			if(report.mux) {
				dtdebug("SCAN REPORT: mux=" << *report.mux << " pending=" <<
							ss.pending_muxes << " active=" << ss.active_muxes);
			} else {
								dtdebug("SCAN REPORT: mux=" << "NONE" << " pending=" <<
							ss.pending_muxes << " active=" << ss.active_muxes);
			}
			receiver_thread.notify_scan_mux_end(scan_subscription_id, report);
		} else if(!subscription.scan_start_reported) {
			subscription.scan_start_reported = true;
			auto scan_stats_ = *scan_stats.readAccess();
			receiver_thread.notify_scan_start(scan_subscription_id, scan_stats_);
		}
	}

	if (error) {
		dterror("Error encountered during scan");
	}
	return {num_pending_muxes + num_pending_peaks, subscriptions.size()};
}


/*
	Because tune thread runs asynchronously, muxes are not immediately marked as active in the database,
	which could cause them to be scanned twice.
	The call returns true if a (pending) mux is already being scanned
 */
bool scan_t::mux_is_being_scanned(const chdb::any_mux_t& mux)
{

	for(auto it = subscriptions.begin(); it != subscriptions.end(); ++it) {
			scan_subscription_t subscription {it->second}; //need to copy
			if(!subscription.mux)
				continue;
			bool ret{false};
			std::visit([&](const auto& mux) {
				auto* p = std::get_if<typename std::remove_cvref<decltype(mux)>::type>(& (*subscription.mux));
				ret = p && ((mux.k == p->k) ||
										chdb::matches_physical_fuzzy(mux, *p, true /*check_sat_pos*/));  //or stream_id or t2mi_pid does not match
			}, mux);
			if(ret)
				return true;
	}
	return false;
}

/*
	returns the number the new subscription_id and the new value of reuseable_subscription_id,
	which will be -1 to to indicate that it can no longer be reused
 */
//template<typename mux_t>
std::tuple<subscription_id_t, subscription_id_t>
scan_t::scan_try_mux(subscription_id_t reuseable_subscription_id,
										 scan_subscription_t& subscription, const devdb::rf_path_t* required_rf_path,
										 bool use_blind_tune)
{
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};
	std::vector<task_queue_t::future_t> futures;
	assert(subscription.mux);
	std::visit([&](auto& mux)  {
		auto wtxn = receiver.devdb.wtxn();
		tune_options.use_blind_tune = use_blind_tune;
		assert(mux_common_ptr(mux)->scan_id!=0);
		std::tie(subscription_id, subscribed_fe_key) =
			receiver_thread.subscribe_mux(futures, wtxn, mux, reuseable_subscription_id, tune_options,
																		required_rf_path, scan_id);
		wtxn.commit();
		wait_for_all(futures); //remove later
	}, *subscription.mux);

	report((int)subscription_id <0 ? "NOT SUBSCRIBED" : "SUBSCRIBED", reuseable_subscription_id, subscription_id,
				 *subscription.mux, subscriptions);
	dtdebug("Asked to subscribe " << *subscription.mux << " reuseable_subscription_id="  <<
					(int) reuseable_subscription_id << " subscription_id=" << (int) subscription_id <<
					" peak=" << subscription.peak.frequency);

	/*
		When tuning fails, it is essential that tuner_thread does NOT immediately unsubscribe the mux,
		but rather informs us about the failure via scan_mux_end. Otherwise we would have to
		update the mux status ourselves (here in the code), which also would force us to reacqauire a new
		read transaction in the middle of a read loop
	 */
	assert((int)subscription_id >=0 || subscription_id != subscription_id_t::TUNE_FAILED);

	/*An error can occur when resources cannot be reserved; this is reported immediately
		by returning subscription_id_t == RESERVATION_FAILED;
		Afterwards only tuning errors can occur. These errors will be noticed by error==true after
		calling wait_for_all. Such errors indicated TUNE_FAILED.
	*/
	if ((int)subscription_id < 0) {
		if(subscriptions.size()==0) {
			/* we cannot subscribe the mux  because of some permanent failure or because of
				 subscriptions by another program
				 => Give up
			*/
			auto chdb_wtxn = receiver.chdb.wtxn();
			auto &c = *chdb::mux_common_ptr(*subscription.mux);
			dtdebug("SET IDLE " << *subscription.mux);
			c.scan_status = chdb::scan_status_t::IDLE;
			c.scan_result = chdb::scan_result_t::BAD;
			c.scan_id = 0;
			namespace m = chdb::update_mux_preserve_t;
			chdb::update_mux(chdb_wtxn, *subscription.mux, now,
											 m::flags{(m::MUX_COMMON|m::MUX_KEY)& ~m::SCAN_STATUS}, /*false ignore_key,*/
											 false /*ignore_t2mi_pid*/,
											 true /*must_exist*/);
			chdb_wtxn.commit();
			subscription_id = subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
		}
		return {subscription_id, reuseable_subscription_id};
	}
	subscription.fe_key = subscribed_fe_key;
	last_subscribed_mux = *subscription.mux;
	dtdebug("subscribed to " << *subscription.mux << " subscription_id="  << (int) subscription_id <<
					" reuseable_subscription_id=" << (int) reuseable_subscription_id);

	/*at this point, we know that our newly subscribed fe differs from our old subscribed fe
	 */
	subscriptions[subscription_id] = subscription;

	if ((int)reuseable_subscription_id >= 0) {
		assert(subscription_id == reuseable_subscription_id);
		/*
			We have reused an old subscription_id, but are now using a different fe than before
			Find the old fe we are using; not that at this point, subscription_id will be present twice
			as a value in subscriptions; find the old one (by skipping the new one)
		*/
		reuseable_subscription_id = subscription_id_t{-1}; // we can still attempt to subscribe, but with new a new subscription_id
	}

	return {subscription_id, reuseable_subscription_id};
}


scanner_t::scanner_t(receiver_thread_t& receiver_thread_,
										 //ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<devdb::lnb_t>* lnbs_,
										 bool scan_found_muxes_, int max_num_subscriptions_)
	: receiver_thread(receiver_thread_)
	, receiver(receiver_thread_.receiver)
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
	auto fn = [&](auto scan_status, const char* label) {
		int count{0};
		auto c = mux_t::find_by_scan_status(wtxn, scan_status, find_type_t::find_geq,
																				mux_t::partial_keys_t::scan_status);

		for(auto mux: c.range())  {
			assert (mux.c.scan_status == scan_status);
			dtdebug("SET IDLE " << mux);
			mux.c.scan_status = chdb::scan_status_t::IDLE;
			put_record(wtxn, mux);
			count++;
		}
		dtdebugx("Cleaned %d muxes with %s status\n", count, label);
	};

	fn(scan_status_t::PENDING, "PENDING");
	fn(scan_status_t::RETRY, "RETRY");
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
void scan_t::add_completed(const devdb::fe_t& fe, const chdb::any_mux_t& mux, int num_pending_muxes,
	int num_pending_peaks) {
		auto w = scan_stats.writeAccess();
		w->pending_peaks = num_pending_peaks;
		//w->finished_muxes++;
		w->pending_muxes = num_pending_muxes;
		w->active_muxes = subscriptions.size();
}


void scanner_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
																 db_txn& devdb_wtxn, subscription_id_t scan_subscription_id)
{
	auto [it, found ] = find_in_map(scans, scan_subscription_id);
	if(!found)
		return;
	auto& scan = scans.at(scan_subscription_id);
	for (auto[subscription_id, sub] : scan.subscriptions) {
		receiver_thread.unsubscribe(futures, devdb_wtxn, subscription_id);
	}
	wait_for_all(futures); //remove later?
	scans.erase(scan_subscription_id);
}

static inline bool can_subscribe(db_txn& devdb_rtxn, const auto& mux, const tune_options_t& tune_options){
	if constexpr (is_same_type_v<chdb::dvbs_mux_t, decltype(mux)>) {
		const devdb::rf_path_t* required_rf_path{nullptr};
		return devdb::fe::can_subscribe_lnb_band_pol_sat(devdb_rtxn, mux, required_rf_path,
																										 tune_options.use_blind_tune, tune_options.may_move_dish,
																										 0 /*dish_move_penalty*/, 0/*resource_reuse_bonus*/);
	} else {
		return devdb::fe::can_subscribe_dvbc_or_dvbt_mux(devdb_rtxn, mux, tune_options.use_blind_tune);
	}
	assert(0);
	return false;
}

template <typename mux_t>
int scanner_t::add_muxes(const ss::vector_<mux_t>& muxes, bool init, subscription_id_t scan_subscription_id) {
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto chdb_wtxn = receiver.chdb.wtxn();
	auto scan_id = make_scan_id(scan_subscription_id);
	dtdebugx("MAKE SCAN_ID: pid=0x%x subscription_id=%d ret=%d\n", getpid(), (int)scan_subscription_id, scan_id);
	/* ensure that empty entry exists. This camn be used to set required_lnb and such
	 */
	scans.try_emplace(scan_subscription_id, *this, scan_subscription_id, true /*propagate_scan*/);
	for(const auto& mux_: muxes) {
		if(!can_subscribe(devdb_rtxn, mux_, tune_options)) {
			dtdebug("Skipping mux that cannot be tuned: " << mux_);
			continue;
		}
		auto mux = mux_;
		dtdebug("SET PENDING " << mux);
		/*
			@todo: multiple parallel scans can override each other's scan_status
		 */
		mux.c.scan_status = chdb::scan_status_t::PENDING;
		mux.c.scan_id = scan_id;
		put_record(chdb_wtxn, mux);
	}

	chdb_wtxn.commit();
	{
		auto& scan = scans.at(scan_subscription_id);
		auto w = scan.scan_stats.writeAccess();
		if (init) {
			*w = scan_stats_t{};
		} else{
			w->pending_muxes += muxes.size(); //might be wrong when same mux is added, but scan_loop will fix this later
		}
		w->active_muxes = scan.subscriptions.size();
	}
	return 0;
}

int scanner_t::add_peaks(const statdb::spectrum_key_t& spectrum_key, const ss::vector_<chdb::spectral_peak_t>& peaks,
												 bool init, subscription_id_t scan_subscription_id) {
	assert((int) scan_subscription_id >=0);
	//auto scan_id = make_scan_id(scan_subscription_id);
	auto [it, found] = scans.try_emplace(scan_subscription_id, *this, scan_subscription_id, false /*propagate_scan*/);
	auto& scan = it->second;

	for(const auto& peak: peaks) {
		blindscan_key_t key = {spectrum_key.sat_pos, spectrum_key.pol, peak.frequency};
		auto& blindscan = scan.blindscans[key];
		if(!blindscan.valid()) {
			blindscan.spectrum_key = spectrum_key;
		}  else {

			assert(blindscan.spectrum_key == spectrum_key);
		}
		blindscan.peaks.push_back(peak);
	}

	//initialize statistics
	{
		auto& scan = scans.at(scan_subscription_id);
		auto w = scan.scan_stats.writeAccess();
		if (init) {
			*w = scan_stats_t{};
		}
		w->active_muxes = scan.subscriptions.size();
	}
	return 0;
}



void scanner_t::set_allowed_lnbs(const ss::vector_<devdb::lnb_t>& lnbs) { allowed_lnbs = lnbs; }

void scanner_t::set_allowed_lnbs() { allowed_lnbs.clear(); }

scanner_t::~scanner_t() {
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();
	for(auto it = scans.begin(); it != scans.end(); ) {
		auto subscription_id = it->first;
		++it; //needed because the current scan will be erased
		unsubscribe_scan(futures, devdb_wtxn, subscription_id);
	}
	devdb_wtxn.commit();
	wait_for_all(futures);
}


void scanner_t::notify_signal_info(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& fe_subscription_ids,
																	 const signal_info_t& signal_info)
{

	auto [it, found] = find_in_map(this->scans, subscriber.get_subscription_id());
	if(!found)
		return; //not a scan control subscription_id
	auto &scan = it->second;

	for(auto subscription_id: fe_subscription_ids) {
		auto [it, found] = find_in_map(scan.subscriptions, subscription_id);
		if(!found)
			continue; //this is not a subscription used by this scan
		if (scan.monitored_subscription_id == subscription_id_t::NONE) {
			scan.monitored_subscription_id = subscription_id;
		}
		if(subscription_id == scan.monitored_subscription_id) {
			subscriber.notify_signal_info(signal_info, true /*from_scanner*/);
			return;
		}
	}
	dterrorx("NOT Notifying: monitored_subscription_id=%d\n", (int) scan.monitored_subscription_id);
}

void scanner_t::notify_sdt_actual(const subscriber_t& subscriber,
																	const ss::vector_<subscription_id_t>& fe_subscription_ids,
																	const sdt_data_t& sdt_data, dvb_frontend_t* fe)
{

	auto [it, found] = find_in_map(this->scans, subscriber.get_subscription_id());
	if(!found)
		return; //not a scan control subscription_id
	auto &scan = it->second;

	for(auto subscription_id: fe_subscription_ids) {
		auto [it, found] = find_in_map(scan.subscriptions, subscription_id);
		if(!found)
			continue; //this is not a subscription used by this scan
		if (scan.monitored_subscription_id == subscription_id_t::NONE) {
			scan.monitored_subscription_id = subscription_id;
		}
		if(subscription_id == scan.monitored_subscription_id) {
			subscriber.notify_sdt_actual(sdt_data, fe, true /*from_scanner*/);
			return;
		}
	}
	dterrorx("NOT Notifying: monitored_subscription_id=%d\n", (int) scan.monitored_subscription_id);
}


template int scanner_t::add_muxes<chdb::dvbs_mux_t>(const ss::vector_<chdb::dvbs_mux_t>& muxes, bool init,
																										subscription_id_t subscription_id);
template int scanner_t::add_muxes<chdb::dvbc_mux_t>(const ss::vector_<chdb::dvbc_mux_t>& muxes, bool init,
																										subscription_id_t subscription_id);
template int scanner_t::add_muxes<chdb::dvbt_mux_t>(const ss::vector_<chdb::dvbt_mux_t>& muxes, bool init,
																										subscription_id_t subscription_id);


template std::tuple<int, int, int, int, subscription_id_t> scan_t::scan_next<chdb::dvbs_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
template std::tuple<int, int,  int, int, subscription_id_t> scan_t::scan_next<chdb::dvbc_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
template std::tuple<int, int,  int, int, subscription_id_t> scan_t::scan_next<chdb::dvbt_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
