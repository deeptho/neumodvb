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
	: spectrum_key(spectrum_key)
	, band(subscription.blindscan_key.band)
	, peak(subscription.peak)
	, mux(subscription.mux)
	, scan_stats(scan_stats)
		//, lnb_key (lnb_key)
{}


scan_t::scan_t(scanner_t& scanner, subscription_id_t scan_subscription_id)
	: scanner(scanner),
		receiver_thread(scanner.receiver_thread)
	, receiver(receiver_thread.receiver)
	, scan_subscription_id(scan_subscription_id)
{
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
		dtdebugf("Detected exit condition");
		must_end = true;
	}

	dtdebugf("{:d} muxes left to scan; {:d} active", pending, active);

	return must_end ? true : ((pending + active) == 0);
}

static void report(const char* msg, subscription_id_t finished_subscription_id,
									 subscription_id_t subscription_id, const chdb::any_mux_t& mux,
									 const std::map<subscription_id_t, scan_subscription_t>& subscriptions)
{
	if ((int) finished_subscription_id < 0 && (int) subscription_id < 0)
		return;
	ss::string<128> s;
	s.format(" Scan  {:s} {} finished_subscription_id={:d} subscription_id={:d} todo=[",
					 msg, mux, (int)finished_subscription_id, (int)subscription_id);
	for (auto& [id, unused] : subscriptions)
		s.format("[:2d] ", (int)id);
	s.format("] \n");
	dtdebugf("{}", s);
}


bool blindscan_key_t::operator<(const blindscan_key_t& other) const {
	if(sat_pos != other.sat_pos)
		return sat_pos < other.sat_pos;
	if(std::get<0>(band) != std::get<0>(other.band))
		return (int)std::get<0>(band) < (int)std::get<0>(other.band);
	if(std::get<1>(band) != std::get<1>(other.band))
		return (int)std::get<1>(band) < (int)std::get<1>(other.band);
	if(pol != other.pol)
		return (int) pol  < (int) other.pol;
	return false;
	//return (intptr_t) subscriber  < (intptr_t) other.subscriber;
}


//frequency is translated to band
blindscan_key_t::blindscan_key_t(int16_t sat_pos, chdb::fe_polarisation_t pol, uint32_t frequency)
	: sat_pos(sat_pos)
	, band{chdb::sat_band_for_freq(frequency)}
	, pol(pol)
	{}


template<typename mux_t>
requires (! is_same_type_v<chdb::spectral_peak_t, mux_t>)
static inline bool& skip_helper(std::map<blindscan_key_t,bool>& skip_map, const mux_t& mux)
{
	using namespace chdb;
	if constexpr (is_same_type_v<chdb::dvbs_mux_t, mux_t>) {
		return skip_map[blindscan_key_t{mux.k.sat_pos, mux.pol, mux.frequency}]; //frequency is translated to band
	} else {
		return skip_map[blindscan_key_t{mux.k.sat_pos, chdb::fe_polarisation_t::H, mux.frequency}]; //frequency is translated to band
	}
}


/*check if a finished mux belongs to the current scan_t (it could belong to another scan_t
 */
subscription_id_t scanner_t::scan_subscription_id_for_scan_id(const chdb::scan_id_t& scan_id) {
	if (scan_id.pid != getpid())
		return subscription_id_t{-1};
	return subscription_id_t{scan_id.subscription_id};
}

/*
	called from tuner thread when scanning a mux has ended
	returns true if scanner is empty and should be removed
*/
bool scanner_t::on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
																const chdb::scan_id_t& scan_id, subscription_id_t subscription_id)
{

	if (must_end) {
		dtdebugf("must_end");
		return true;
	}
	auto scan_subscription_id = scan_subscription_id_for_scan_id(scan_id);
	if((int)scan_subscription_id >= 0) {
		try {
			last_house_keeping_time = steady_clock_t::now();
			auto& scan = scans.at(scan_subscription_id);
			assert(scan.scan_subscription_id == scan_subscription_id);

			auto [pending, active] = scan.scan_loop(finished_fe, finished_mux, subscription_id);

			dtdebugf("finished_fe adapter {} mux=<{}> muxes to scan: pending={} active={} subs={}",
							 finished_fe.adapter_no, finished_mux, (int) pending, (int) active,
							((int) scan.subscriptions.size()));
			//TODO: why do we perform this "housekeeping" now in the two lines below? Needed?
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
			dtdebugf("Detected exit condition");
			must_end = true;
		}
		return false;
	} else {
		return false; //this scan must be from another running instance of neumoDVB
	}
}

void scanner_t::on_spectrum_band_end(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& fe_subscription_ids,
																		 const statdb::spectrum_t& spectrum)
{
	auto [it, found] = find_in_map(this->scans, subscriber.get_subscription_id());
	if(!found)
		return; //not a scan control subscription_id
	auto &scan = it->second;

	for(auto subscription_id: fe_subscription_ids) {
		auto [it, found] = find_in_map(scan.subscriptions, subscription_id);
		if(!found)
			continue; //this is not a subscription used by this scan
		printf("found scan complete=%d\n", spectrum.is_complete);
	}
}

/*
	rescan a peak after the first scan, which used database parameters and which failed.
	This time, use peak parameters instead (e.g., symbolrate estimated from peak width).

	The scan is performed don the same lnb on which the original mux scan was performed

	Returns false if scan has been launched successfully, or otherwise true, meaning that
	mux scan should be abolished
 */
bool
scan_t::rescan_peak(blindscan_t& blindscan,
										subscription_id_t reuseable_subscription_id, scan_subscription_t& subscription)

{
	using namespace chdb;
	subscription.is_peak_scan = true;
	assert(subscription.mux);

	std::visit([&](auto& mux) {
		assert(!blindscan.spectrum_acquired() || mux.k.sat_pos == blindscan.spectrum_key->sat_pos);

		mux.frequency = subscription.peak.frequency;
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t>) {
			assert(!blindscan.spectrum_acquired() || mux.k.sat_pos == blindscan.spectrum_key->sat_pos);
			assert(!blindscan.spectrum_acquired() || mux.pol == subscription.peak.pol);
			if(blindscan.spectrum_acquired()) {
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
		dtdebugf("SET PENDING: {}", mux);
		mux.c.scan_status = scan_status_t::PENDING;

		const bool use_blind_tune = true;
		reuseable_subscription_id = scan_try_mux(reuseable_subscription_id, subscription, use_blind_tune);
	}, *subscription.mux);



	if ((int)subscription.subscription_id < 0) {
		/* we cannot subscribe the mux right now.  This should never happen
			 as we own a subscription
		*/
		return true;
	} else {
	}
	return false;
}

static inline chdb::scan_rf_path_t scan_rf_path(devdb::rf_path_t & p) {
	chdb::scan_rf_path_t r;
	r.lnb_id = p.lnb.lnb_id;
	r.card_mac_address = p.card_mac_address;
	r.rf_input = p.rf_input;
	r.dish_id = p.lnb.dish_id;
	return r;
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
	assert(blindscan.spectrum_acquired());
	auto scan_id = subscription.peak.scan_id;
	std::visit([&](auto& mux) {
		mux.k.sat_pos = blindscan.spectrum_key->sat_pos;
		mux.frequency = subscription.peak.frequency;
		set_member(mux, pol, subscription.peak.pol);
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t&>) {
			assert(mux.k.sat_pos == blindscan.spectrum_key->sat_pos);
			assert(mux.pol == subscription.peak.pol);
			mux.symbol_rate = subscription.peak.symbol_rate;
			mux.pls_mode = fe_pls_mode_t::ROOT;
			mux.pls_code = 1;
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbt_mux_t>) {
		}
		if constexpr (is_same_type_v<decltype(mux), chdb::dvbc_mux_t>) {
			assert(mux.k.sat_pos == blindscan.spectrum_key->sat_pos);
			mux.symbol_rate = subscription.peak.symbol_rate;
		}
		mux.k.t2mi_pid = -1;
		mux.k.stream_id = -1;
		mux.c.scan_status = scan_status_t::PENDING;
		mux.c.scan_rf_path = scan_rf_path(blindscan.spectrum_key->rf_path);

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
		dtdebugf("SET PENDING: {}", mux);
		mux.c.scan_status = scan_status_t::PENDING;
		mux.c.scan_id = scan_id;
		subscription.mux = mux;
		const bool use_blind_tune = true;
		finished_subscription_id = scan_try_mux(finished_subscription_id, subscription,
																						blindscan.spectrum_acquired()
																						? &blindscan.spectrum_key->rf_path : nullptr, use_blind_tune);
		}, *subscription.mux);
	return subscription.subscription_id;
}



/*
	process a finished subscription, check if we need to retry the scan of this mux:
	typically, when scanning a peak, we first use the known parameters in the database,
	and then if needed blindscan the peak in case parameters have changed. This means
	the same frequency/pol combination is scanned twice.

	Scanning multiple times also occurs when tuning fails temporarily when some resource is
	unailable.

	This call returns false if we are retrying scanning thus frequency/pol for some reason
	Otherwise return true, signaling that this subscription is done
 */
bool scan_t::retry_subscription_if_needed(db_txn& rtxn,  subscription_id_t subscription_id,
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

	if(!blindscan.spectrum_acquired()) {
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
		dtdebugf("Calling rescan_peak for mux: {}", finished_mux);
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
	and subscription to erase, and the new value of reuseable_subscription_id
 */
template<typename mux_t>
std::tuple<int, int, subscription_id_t>
scan_t::scan_next(db_txn& chdb_rtxn,
									subscription_id_t reuseable_subscription_id,
									scan_subscription_t& subscription)
{
	std::vector<task_queue_t::future_t> futures;
	// start as many subscriptions as possible
	using namespace chdb;
	int num_pending_muxes{0};
	int num_pending_peaks{0};
	std::map<blindscan_key_t, bool> skip_map;


/* First scan available spectral peaks
	 When scanning spectral peaks for blindscans launched after spectral scan,
	 we force the lnb used to acquire the spectrum (e.g., in case the lnb is on a large \
	 dish, and scanning another lnb on a smaller dish might not succeed).

	 The user can also launch blindscan on more than one lnb but for the same sat (e.g., on multiple dishes).
	 In this case, in the current code, only one of those lnbs will be considered, as the peaks are stored
	 in the same data structure


	*/

	for(auto& [key, blindscan]:  blindscans) {
		bool& skip_sat_band = skip_map[key]; /*Note that key depends on lnb for a spectral peak, whereas
																					 key below does not depend on lnb for a mux (which can be for any
																					 lnb).
																					 TODO: various strange situations can occur:
																					 1. user asks to scan muxes from dvbs mux and then also
																					 to scan peaks from spectrum dialog. This can lead to the same
																					 mux being scanned on a specific lnb and on "any" lnb
																					 2. even if user only starts blindscan, which will use a specific lnb,
																					 si processing can create new muxes which can then be scanned on any lnb

																				 */
		if(skip_sat_band)
			continue;
		if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) { //TODO: check based on peak freduency, required_lnb and such
			for(int idx = blindscan.peaks.size()-1;  idx>=0 ; --idx) {
				auto &peak = blindscan.peaks[idx];
				using namespace chdb;

				auto max_num_subscriptions = scanner.max_num_subscriptions;
				if ((int)subscriptions.size() >=
						((int)reuseable_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1)) {
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
		//@todo: implement blindscan for DVBC and DVBT
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
			if(!scanner_t::is_our_scan(mux_to_scan.c.scan_id))
				continue;

			if ((int)subscriptions.size() >=
					((int)reuseable_subscription_id < 0 ? scanner.max_num_subscriptions : scanner.max_num_subscriptions + 1)) {
				num_pending_muxes++;
				continue; // to have accurate num_pending count
			}

			bool& skip_mux = skip_helper(skip_map, mux_to_scan);
			if(skip_mux)
				continue;
			subscription.mux = mux_to_scan;

			if(receiver_thread.must_exit())
				throw std::runtime_error("Exit requested");

			auto pol = get_member(mux_to_scan, pol, chdb::fe_polarisation_t::NONE);
			blindscan_key_t key = {mux_to_scan.k.sat_pos, pol, mux_to_scan.frequency}; //frequency is translated to band
			auto [it, found] = find_in_map(blindscans, key);
			devdb::rf_path_t* required_rf_path{nullptr};
			if(found) {
				auto& blindscan = it->second;
				required_rf_path = blindscan.spectrum_acquired()
					? &blindscan.spectrum_key->rf_path : nullptr;
			}
			if(mux_is_being_scanned(mux_to_scan)) {
				dtdebugf("Skipping mux already in progress: {}", mux_to_scan);
				continue;
			}

			//subscription_id_t subscription_id;
			const bool use_blind_tune = false;
			reuseable_subscription_id =
				scan_try_mux(reuseable_subscription_id, subscription, required_rf_path, use_blind_tune);

			if ((int)subscription.subscription_id < 0) {
				// we cannot subscribe the mux right now
				if(subscription.subscription_id == subscription_id_t::RESERVATION_FAILED_PERMANENTLY) {
					skip_mux = false; //ensure that we do not even try muxes on the same sat, pol, band in this run
				} else if(subscription.subscription_id == subscription_id_t::RESERVATION_FAILED) {
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
		dtdebugf("Abandoning subscription_id={:d}", (int) reuseable_subscription_id);
		auto wtxn = receiver.devdb.wtxn();
		receiver_thread.unsubscribe(futures, wtxn, reuseable_subscription_id);
		wtxn.commit();
		report("ERASED", reuseable_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
		//subscriptions.erase(reuseable_subscription_id); //remove old entry
		wait_for_all(futures);
	}
	return {num_pending_peaks, num_pending_muxes, reuseable_subscription_id};
}



subscription_id_t scan_t::try_all_muxes(
	db_txn& chdb_rtxn, scan_subscription_t& subscription,
	const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
	int& num_pending_muxes,
	int& num_pending_peaks
	)
{
	// start as many subscriptions as possible
	using namespace chdb;
	auto& finished_mux_key = *mux_key_ptr(finished_mux);
	subscription_id_t finished_subscription_id_dvbs{-1};
	subscription_id_t finished_subscription_id_dvbc{-1};
	subscription_id_t finished_subscription_id_dvbt{-1};
	bool existing_subscription = (int)subscription.subscription_id >=0;

	if(finished_mux_key.sat_pos == sat_pos_dvbc)
		finished_subscription_id_dvbc = subscription.subscription_id;
	else if(finished_mux_key.sat_pos == sat_pos_dvbt)
		finished_subscription_id_dvbt = subscription.subscription_id;
	else
		finished_subscription_id_dvbs = subscription.subscription_id;

	int num_pending_muxes1{0};
	int num_pending_peaks1{0};
	subscription_id_t subscription_to_erase1{-1};
	subscription_id_t subscription_to_erase{-1};
	/*scan any pending muxes. This is useful to reuse the finished subscription if it is not currently
		busy, but also to take advantage of frontends used for viewing tv but then release */

	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	dtdebugf("Calling scan_next");
	std::tie(num_pending_peaks1, /*num_skipped_peaks1,*/ num_pending_muxes1, /*num_skipped_muxes1,*/
					 subscription_to_erase1) =
		scan_next<chdb::dvbs_mux_t>(chdb_rtxn, finished_subscription_id_dvbs, subscription);

	subscription_to_erase = subscription_to_erase1;
	num_pending_peaks += num_pending_peaks1;
	num_pending_muxes += num_pending_muxes1;
	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");

	dtdebugf("dvbc scan_next round\n");
	std::tie(num_pending_peaks1, /*num_skipped_peaks1, */ num_pending_muxes1, /*num_skipped_muxes1,*/
					 subscription_to_erase1) =
		scan_next<chdb::dvbc_mux_t>(chdb_rtxn, finished_subscription_id_dvbc, subscription);

	assert((int)subscription_to_erase == -1 || (int) subscription_to_erase1 == -1);
	if( (int) subscription_to_erase1 >=0)
		subscription_to_erase = subscription_to_erase1;

	num_pending_peaks += num_pending_peaks1;
	num_pending_muxes += num_pending_muxes1;
	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");

	dtdebugf("dvbt scan_next\n");
	std::tie(num_pending_peaks1, /*num_skipped_peaks1,*/ num_pending_muxes1, /*num_skipped_muxes1,*/
					 subscription_to_erase1) =
		scan_next<chdb::dvbt_mux_t>(chdb_rtxn, finished_subscription_id_dvbt, subscription);

	num_pending_peaks += num_pending_peaks1;
	num_pending_muxes += num_pending_muxes1;
	assert((int)subscription_to_erase == -1 || (int) subscription_to_erase1 == -1);
	if( (int) subscription_to_erase1 >=0)
		subscription_to_erase = subscription_to_erase1;
		dtdebugf("finished_subscription_id={:d} subscription_to_erase ={:d} pending={:d}+{:d}",
					 (int) subscription.subscription_id,
					 (int) subscription_to_erase,
					 num_pending_peaks, num_pending_muxes);
	auto w = scan_stats.writeAccess();
	w->pending_peaks = num_pending_peaks;
	w->pending_muxes = num_pending_muxes;
	w->active_muxes = subscriptions.size();
	if(existing_subscription) {
		bool locked = chdb::mux_common_ptr(finished_mux)->scan_result != chdb::scan_result_t::NOLOCK;
		bool nodvb = chdb::mux_common_ptr(finished_mux)->scan_result == chdb::scan_result_t::NOTS;
		w->finished_muxes++;
		w->failed_muxes += !locked;
		w->locked_muxes += locked;
		w->si_muxes += (locked && !nodvb);
	}
	return subscription_to_erase;
};



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

	if(mux_key_ptr(finished_mux)->sat_pos != sat_pos_none) { //otherwise we are called from housekeeping
		int count{0};
		/*it is essential that subscription is retrieved by val in the loop below
			as it is filled with trial data which is only saved when subscription succeeds
		*/
		[&](){
			auto [it, found] = find_in_map(this->subscriptions, finished_subscription_id);
			if(!found) {
				dtdebugf("Skipping unknown subscription_id={:d}", (int)finished_subscription_id);
				return;
			}
			scan_subscription_t subscription {it->second}; //need to copy
			/*it is essential to start a new transaction at this point
				because scan_next will loop over all pending muxes, changing
				scan_status to ACTIVE for some of the muxes;
				In order to not rescan those muxes, we need to discover the new ACTIVE status,
				which is done by creating a new transaction
			*/
			auto chdb_rtxn = receiver.chdb.rtxn();
			dtdebugf("obtained transaction\n");
			count++;
			auto& blindscan = blindscans[subscription.blindscan_key];
			bool finished = retry_subscription_if_needed(chdb_rtxn,  finished_subscription_id,
																					subscription, finished_mux);
			dtdebugf("after retry_subscription_if_needed: finished_mux={} finished=",
							 finished_mux, finished);
			scan_report_t report{subscription, *blindscan.spectrum_key, {}};
			if (!finished) {
				finished_subscription_ptr = nullptr;
				it = std::next(it);
				return;
			}
			finished_subscription_ptr = & subscription;  //allow try_all_muxes to reuse this subscription
			subscription.mux.reset();
			dtdebugf("calling try_all_muxes\n");
			assert(subscription.subscription_id == finished_subscription_id);
			auto subscription_to_erase = try_all_muxes(chdb_rtxn, /*finished_subscription_id,*/ subscription,
																								 finished_fe, finished_mux,
																								 /*finished_subscription_ptr, finished_mux_key,*/
																								 num_pending_muxes, num_pending_peaks);
			dtdebugf("finished_subscription_id={} subscription_to_erase={} finished_mux={}",
							 (int) finished_subscription_id, (int) subscription_to_erase, finished_mux);
			if((int) subscription_to_erase >=0) {
				assert(subscription_to_erase == finished_subscription_id);
				it = subscriptions.erase(it);
				if (subscription_to_erase == monitored_subscription_id)
					monitored_subscription_id = subscription_id_t::NONE;
			} else {
				it = std::next(it);
			}
			assert(finished_mux_key.sat_pos == sat_pos_none || finished_fe.sub.owner == getpid());
			report.scan_stats = *scan_stats.readAccess();
			auto ss = *scan_stats.readAccess();
			dtdebugf("SCAN REPORT: mux={} pending={} active={}",
							 *report.mux, ss.pending_muxes, ss.active_muxes);
			receiver.notify_scan_mux_end(scan_subscription_id, report);
			dtdebugf("committing\n");
			chdb_rtxn.commit();
		}();

		if(count==0) {
			dterrorf("XXXX finished subscription not found for {}", finished_mux);
		} else if(count!=1) {
			dtdebugf("XXXX multiple finished subscriptions for {}", finished_mux);
		}
	} else {
		//in case new frontends or lnbs have become available for use
		finished_subscription_ptr = nullptr;
		auto finished_subscription_id = subscription_id_t{-1};
		scan_subscription_t subscription{finished_subscription_id};
		dtdebugf("calling try_all_muxes\n");
		auto chdb_rtxn = receiver.chdb.rtxn();
		dtdebugf("obtained transaction\n");
		auto subscription_to_erase = try_all_muxes(chdb_rtxn, /*finished_subscription_id,*/ subscription,
																							 finished_fe, finished_mux,
																							 /*finished_subscription_ptr, finished_mux_key,*/
																							 num_pending_muxes, num_pending_peaks);
		assert((int) subscription_to_erase == -1);
		dtdebugf("committing\n");
		chdb_rtxn.commit();

		if(subscriptions.size() ==0) {
			/*send one more message to inform gui that scan is done
				This may be needed when all pending scans fail
			*/
			subscription.mux = {};
			scan_report_t report{subscription, {}, {}};
			report.scan_stats = *scan_stats.readAccess();
			auto ss = *scan_stats.readAccess();
			ss.pending_muxes = num_pending_muxes;
			if(report.mux) {
				dtdebugf("SCAN REPORT: mux={} pending={} active={}",
								 *report.mux, ss.pending_muxes, ss.active_muxes);
			} else {
				dtdebugf("SCAN REPORT: mux=NONE pending{} active={}",
								 ss.pending_muxes, ss.active_muxes);
			}
			receiver.notify_scan_mux_end(scan_subscription_id, report);
		} else if(!subscription.scan_start_reported) {
			subscription.scan_start_reported = true;
			auto scan_stats_ = *scan_stats.readAccess();
			receiver.notify_scan_start(scan_subscription_id, scan_stats_);
		}
	}

	if (error) {
		dterrorf("Error encountered during scan");
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
subscription_id_t
scan_t::scan_try_mux(subscription_id_t reuseable_subscription_id,
										 scan_subscription_t& subscription, bool use_blind_tune)
{
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{-1};
	std::vector<task_queue_t::future_t> futures;
	assert(subscription.mux);
	std::visit([&](auto& mux)  {
		auto wtxn = receiver.devdb.wtxn();
		auto scan_id = mux_common_ptr(mux)->scan_id;
		tune_options.use_blind_tune = use_blind_tune;
		assert(mux_common_ptr(mux)->scan_id!=0);
		assert(chdb::scan_in_progress(scan_id));
		assert(scanner_t::is_our_scan(scan_id));
			subscription_id =
			receiver_thread.subscribe_mux(futures, wtxn, mux, reuseable_subscription_id, tune_options,
																		scan_id, true /*do_not_unsubscribe_on_failure*/);
		wtxn.commit();
		wait_for_all(futures); //remove later
	}, *subscription.mux);
	if((int)subscription_id >=0) {
		report((int)subscription_id <0 ? "NOT SUBSCRIBED" : "SUBSCRIBED", reuseable_subscription_id, subscription_id,
					 *subscription.mux, subscriptions);
		dtdebugf("Asked to subscribe {} reuseable_subscription_id={} subscription_id={} peak={}",
						 *subscription.mux, (int) reuseable_subscription_id,
						 (int) subscription_id, subscription.peak.frequency);
	}
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
			dtdebugf("SET IDLE ", *subscription.mux);
			c.scan_status = chdb::scan_status_t::IDLE;
			c.scan_result = chdb::scan_result_t::BAD;
			c.scan_id = {};
			namespace m = chdb::update_mux_preserve_t;
			chdb::update_mux(chdb_wtxn, *subscription.mux, now,
											 m::flags{(m::MUX_COMMON|m::MUX_KEY)& ~m::SCAN_STATUS}, /*false ignore_key,*/
											 false /*ignore_t2mi_pid*/,
											 true /*must_exist*/);
			chdb_wtxn.commit();
			subscription_id = subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
		}
		subscription.subscription_id = subscription_id;
		return reuseable_subscription_id;
	}
	last_subscribed_mux = *subscription.mux;
	dtdebugf("subscribed to {} subscription_id={} reuseable_subscription_id={}",
					 *subscription.mux,(int) subscription_id, (int) reuseable_subscription_id);

	/*at this point, we know that our newly subscribed fe differs from our old subscribed fe
	 */
	subscription.subscription_id = subscription_id;
	subscriptions.insert_or_assign(subscription_id, subscription);

	if ((int)reuseable_subscription_id >= 0) {
		assert(subscription_id == reuseable_subscription_id);
		/*
			We have reused an old subscription_id, but are now using a different fe than before
			Find the old fe we are using; not that at this point, subscription_id will be present twice
			as a value in subscriptions; find the old one (by skipping the new one)
		*/
		reuseable_subscription_id = subscription_id_t{-1}; // we can still attempt to subscribe, but with new a new subscription_id
	}

	return reuseable_subscription_id;
}


scanner_t::scanner_t(receiver_thread_t& receiver_thread_,
										 int max_num_subscriptions_)
	: receiver_thread(receiver_thread_)
	, receiver(receiver_thread_.receiver)
	, scan_start_time(system_clock_t::to_time_t(system_clock_t::now()))
	,	max_num_subscriptions(max_num_subscriptions_)
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
			dtdebugf("SET IDLE ", mux);
			mux.c.scan_status = chdb::scan_status_t::IDLE;
			mux.c.scan_id = {};
			put_record(wtxn, mux);
			count++;
		}
		dtdebugf("Cleaned {:d} muxes with {} status\n", count, label);
	};

	fn(scan_status_t::PENDING, "PENDING");
	fn(scan_status_t::RETRY, "RETRY");
}

bool scanner_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
																 db_txn& devdb_wtxn, subscription_id_t scan_subscription_id)
{
	auto [it, found ] = find_in_map(scans, scan_subscription_id);
	if(!found)
		return found;
	auto& scan = scans.at(scan_subscription_id);
	assert(scan.scan_subscription_id == scan_subscription_id);
	for (auto[subscription_id, sub] : scan.subscriptions) {
		receiver_thread.unsubscribe(futures, devdb_wtxn, subscription_id);
	}
	wait_for_all(futures); //remove later?
	scans.erase(scan_subscription_id);
	return found;
}

static inline bool can_subscribe(db_txn& devdb_rtxn, const auto& mux, const tune_options_t& tune_options){
	if constexpr (is_same_type_v<chdb::dvbs_mux_t, decltype(mux)>) {
		return devdb::fe::can_subscribe_mux(devdb_rtxn, mux,
																										 tune_options,
																										 0 /*dish_move_penalty*/, 0/*resource_reuse_bonus*/);
	} else {
		return devdb::fe::can_subscribe_dvbc_or_dvbt_mux(devdb_rtxn, mux, tune_options.need_blind_tune);
	}
	assert(0);
	return false;
}

static inline bool can_subscribe(db_txn& devdb_rtxn, const chdb::sat_t& sat,
																 const chdb::band_scan_t& band_scan,
																 const tune_options_t& tune_options){
	assert(tune_options.need_spectrum);
	return devdb::fe::can_subscribe_sat_band(devdb_rtxn, sat, band_scan,
																									 tune_options,
																									 0 /*dish_move_penalty*/, 0/*resource_reuse_bonus*/);
}

template <typename mux_t>
int scanner_t::add_muxes(const ss::vector_<mux_t>& muxes, const tune_options_t& tune_options,
												 subscription_id_t scan_subscription_id) {
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto chdb_wtxn = receiver.chdb.wtxn();
	/*
		The following call also created default tune_options if needed
	 */
	auto [it, inserted] = scans.try_emplace(scan_subscription_id, *this, tune_options, scan_subscription_id);
	bool init = inserted;
	auto& scan = it->second;

	auto scan_id = scan.make_scan_id(scan_subscription_id, tune_options);

	for(const auto& mux_: muxes) {
		if(!can_subscribe(devdb_rtxn, mux_, scan.tune_options)) {
			dtdebugf("Skipping mux that cannot be tuned: {}", mux_);
			continue;
		}
		mux_t mux;
		dtdebugf("SET PENDING {}", mux);
		/*
			@todo: multiple parallel scans can override each other's scan_status
		 */
		auto c = mux_t::find_by_key(chdb_wtxn, mux.k, find_eq, mux_t::partial_keys_t::all);
		if(c.is_valid()) {
			const auto& db_mux = c.current();
			bool scan_active = chdb::scan_in_progress(db_mux.c.scan_id);
			if(scan_active)
				continue;
			mux = db_mux;
		} else
			mux = mux_;
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
	tune_options_t o;
	o.scan_target = scan_target_t::SCAN_FULL;
	o.propagate_scan = false;
	o.may_move_dish = false;
	o.use_blind_tune = false;
	auto [it, found] = scans.try_emplace(scan_subscription_id, *this, o, scan_subscription_id);
	auto& scan = it->second;

	auto scan_id = scan.make_scan_id(scan_subscription_id, o);

	for(const auto& peak: peaks) {
		blindscan_key_t key = {spectrum_key.sat_pos, spectrum_key.pol, peak.frequency};//frequency is translated to band
		auto& blindscan = scan.blindscans[key];
		if(!blindscan.spectrum_acquired()) {
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

int scanner_t::add_bands(const ss::vector_<chdb::sat_t>& sats,
												 const ss::vector_<chdb::fe_polarisation_t>& pols,
												 int32_t low_freq, int32_t high_freq,
												 const tune_options_t& tune_options,
												 subscription_id_t scan_subscription_id) {
	using namespace chdb;
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto chdb_wtxn = receiver.chdb.wtxn();

	/* ensure that empty entry exists. This can be used to set required_lnb and such
	 */
	/*
		The following call also created default tune_options if needed
	 */
	auto [it, inserted] = scans.try_emplace(scan_subscription_id, *this, tune_options, scan_subscription_id);
	bool init = inserted;
	auto& scan = it->second;
	auto scan_id = scan.make_scan_id(scan_subscription_id, tune_options);
	int added_bands{0};
	/*
		Add a band to scan to a sat, except if the band is already marked for scanning
		Overwrite old data if needed to avoid ever increasing lists
		@todo: if a user removes a band from a satellite, it is not possible to remove the old scan record
	 */
	auto push_bands = [&pols, &devdb_rtxn, &scan, low_freq, high_freq, scan_id](sat_t& sat) {
		int num_added{0};
		auto [l, h] =sat_band_freq_bounds(sat.sat_band);
		l = std::max(l, low_freq);
		h = std::max(h, high_freq);
		if(h<=l)
			return num_added; //no overlap
		for (auto pol: pols) {
			auto& band_scan = sat::band_scan_for_pol(sat, pol);
			auto saved = band_scan;
			band_scan.pol = pol;
			band_scan.start_freq = l;
			band_scan.end_freq = h;
			band_scan.spectrum_scan_status = scan_status_t::PENDING;
			band_scan.mux_scan_status = scan_status_t::PENDING;
			band_scan.scan_id=scan_id;
			if(!can_subscribe(devdb_rtxn, sat, band_scan, scan.tune_options)) {
				dtdebugf("Skipping sat that cannot be tuned: {}", sat);
				band_scan = saved;
				continue;
		}
			num_added++;
		}
		return num_added;
	};

	auto band_low = sat_band_for_freq(low_freq);
	auto band_high = sat_band_for_freq(high_freq-1);

	for(auto sat: sats) {
		auto c = sat_t::find_by_key(chdb_wtxn, sat.sat_pos, sat.sat_band, find_eq, sat_t::partial_keys_t::all);
		if(c.is_valid())
			sat = c.current(); //reload uptodate information from database
		int added = push_bands(sat);
		dtdebugf("Add sat to scan: {}", sat);
		sat.mtime = system_clock_t::to_time_t(now);
		put_record(chdb_wtxn, sat);
	}

	chdb_wtxn.commit();
	{
		auto& scan = scans.at(scan_subscription_id);
		auto w = scan.scan_stats.writeAccess();
		if (init) {
			*w = scan_stats_t{};
		} else{
#ifdef TODO
			w->pending_bands += added_bands; //might be wrong when same mux is added, but scan_loop will fix this later
#endif
		}
		w->active_muxes = scan.subscriptions.size();
	}
	return 0;
}

scanner_t::~scanner_t() {
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();

	//we do not iterate over elements as unsubscribe_scan erases them
	while(scans.size() >0) {
		auto it = scans.begin();
		auto scan_subscription_id = it->first;
		unsubscribe_scan(futures, devdb_wtxn, scan_subscription_id);
	}
	devdb_wtxn.commit();
	wait_for_all(futures);
}


void scanner_t::notify_signal_info(const subscriber_t& subscriber,
																	 const ss::vector_<subscription_id_t>& fe_subscription_ids,
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
			subscriber.notify_signal_info(signal_info);
			return;
		}
	}
	dtdebugf("NOT Notifying: monitored_subscription_id={:d}", (int) scan.monitored_subscription_id);
}

void scanner_t::notify_sdt_actual(const subscriber_t& subscriber,
																	const ss::vector_<subscription_id_t>& fe_subscription_ids,
																	const sdt_data_t& sdt_data)
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
			subscriber.notify_sdt_actual(sdt_data);
			return;
		}
	}
	dterrorf("NOT Notifying: monitored_subscription_id={:d}\n", (int) scan.monitored_subscription_id);
}



template int scanner_t::add_muxes<chdb::dvbs_mux_t>(const ss::vector_<chdb::dvbs_mux_t>& muxes,
																										const tune_options_t& tune_options,
																										subscription_id_t subscription_id);
template int scanner_t::add_muxes<chdb::dvbc_mux_t>(const ss::vector_<chdb::dvbc_mux_t>& muxes,
																										const tune_options_t& tune_options,
																										subscription_id_t subscription_id);
template int scanner_t::add_muxes<chdb::dvbt_mux_t>(const ss::vector_<chdb::dvbt_mux_t>& muxes,
																										const tune_options_t& tune_options,
																										subscription_id_t subscription_id);


template std::tuple<int, int, subscription_id_t> scan_t::scan_next<chdb::dvbs_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
template std::tuple<int, int, subscription_id_t> scan_t::scan_next<chdb::dvbc_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
template std::tuple<int, int, subscription_id_t> scan_t::scan_next<chdb::dvbt_mux_t>(
	db_txn& chdb_rtxn, subscription_id_t finished_subscription_id, scan_subscription_t& subscription);
