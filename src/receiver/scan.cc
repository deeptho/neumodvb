/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
#include "neumofrontend.h"

static inline void clear_pending(devdb::scan_stats_t& scan_stats) {
	scan_stats.pending_peaks = 0;
	scan_stats.pending_muxes = 0;
	scan_stats.pending_bands = 0;
}

static inline devdb::scan_stats_t operator+(const devdb::scan_stats_t& a, const devdb::scan_stats_t&b) {
	devdb::scan_stats_t ret= a;
	ret.pending_peaks += b.pending_peaks;
	ret.pending_muxes += b.pending_muxes;
	ret.pending_bands += b.pending_bands;
	ret.active_muxes += b.active_muxes;
	ret.active_bands += b.active_bands;
	ret.finished_muxes += b.finished_muxes;
	ret.failed_muxes += b.failed_muxes;
	ret.locked_muxes += b.locked_muxes;
	ret.si_muxes += b.si_muxes;
	return ret;
}

inline devdb::scan_stats_t scan_t::get_scan_stats() const {
	return scan_stats_dvbs + scan_stats_dvbc + scan_stats_dvbt;
}

template<typename mux_t>
requires (! is_same_type_v<chdb::sat_t, mux_t>)
inline devdb::scan_stats_t& scan_t::get_scan_stats_ref(const mux_t& mux) {
	auto& k = *chdb::mux_key_ptr(mux);
	if(k.sat_pos == sat_pos_dvbc)
		return scan_stats_dvbc;
	else if(k.sat_pos == sat_pos_dvbt)
		return scan_stats_dvbt;
	return scan_stats_dvbs;
}

inline devdb::scan_stats_t& scan_t::get_scan_stats_ref(int16_t sat_pos) {
	if(sat_pos == sat_pos_dvbc)
		return scan_stats_dvbc;
	else if(sat_pos == sat_pos_dvbt)
		return scan_stats_dvbt;
	return scan_stats_dvbs;
}

inline devdb::scan_stats_t& scan_t::get_scan_stats_ref(const chdb::sat_t& sat) {
	if(sat.sat_pos == sat_pos_dvbc)
		return scan_stats_dvbc;
	else if(sat.sat_pos == sat_pos_dvbt)
		return scan_stats_dvbt;
	return scan_stats_dvbs;
}

static void deactivate_band_scans
(db_txn & chdb_wtxn, int scan_subscription_id, time_t now) {
	using namespace chdb;

	auto c = find_first<chdb::sat_t>(chdb_wtxn);
	for(auto sat: c.range()) {
		bool changed{false};
		for(auto& band_scan: sat.band_scans) {
			if(band_scan.scan_id.subscription_id != scan_subscription_id)
				continue;
			if(!scanner_t::is_our_scan(band_scan.scan_id))
				continue;
					//we have found a matching band
			if(band_scan.scan_status == scan_status_t::PENDING ||
				 band_scan.scan_status == scan_status_t::RETRY ||
				 band_scan.scan_status == scan_status_t::ACTIVE) {
				band_scan.scan_result = scan_result_t::ABORTED;
				band_scan.scan_status = scan_status_t::IDLE;
			}
			band_scan.scan_id = {};
			changed = true;
		}
		if(changed) {
			sat.mtime = now;
			put_record(chdb_wtxn, sat);
		}
	}
}

template<typename mux_t>
static void deactivate_mux_scans
(db_txn & chdb_wtxn, int scan_subscription_id, time_t now) {
	using namespace chdb;
	scan_id_t scan_id;
	auto c = find_first<mux_t>(chdb_wtxn);
	for(auto mux: c.range()) {
		if(mux.c.scan_id.subscription_id != scan_subscription_id)
			continue;
		if(!scanner_t::is_our_scan(mux.c.scan_id))
				continue;
		mux.c.scan_status = chdb::scan_status_t::IDLE;
		mux.c.scan_id= {};
		mux.c.mtime = now;
		put_record(chdb_wtxn, mux);
	}
}

void scanner_t::end_scans(subscription_id_t scan_subscription_id)
{
	auto now_ = system_clock_t::to_time_t(now);
	auto chdb_wtxn = receiver.chdb.wtxn();
	deactivate_band_scans(chdb_wtxn, (int)scan_subscription_id, now_);
	deactivate_mux_scans<chdb::dvbs_mux_t>(chdb_wtxn, (int)scan_subscription_id, now_);
	deactivate_mux_scans<chdb::dvbc_mux_t>(chdb_wtxn, (int)scan_subscription_id, now_);
	deactivate_mux_scans<chdb::dvbt_mux_t>(chdb_wtxn, (int)scan_subscription_id, now_);
	chdb_wtxn.commit();
}

scan_mux_end_report_t::scan_mux_end_report_t(const scan_subscription_t& subscription,
																						 const statdb::spectrum_key_t spectrum_key)
	: spectrum_key(spectrum_key)
	, peak(subscription.peak)
	, mux(subscription.mux)
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
void scan_t::housekeeping(bool force) {
	using namespace chdb;

	auto chdb_rtxn = receiver.chdb.rtxn();
	using namespace chdb;
	const devdb::fe_t& finished_fe={};
	const chdb::any_mux_t& finished_mux = {};
	dtdebugf("calling try_all_muxes\n");
	auto scan_stats_before = get_scan_stats();

	// start as many subscriptions as possible

	ssptr_t finished_ssptr{};

	/*scan any pending muxes. This is useful to reuse the finished subscription if it is not currently
		busy, but also to take advantage of frontends used for viewing tv but then released */


	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	dtdebugf("dvbc scan_next\n");
	auto ssptr_to_erase = scan_next<chdb::dvbs_mux_t>(chdb_rtxn, finished_ssptr, scan_stats_dvbs);
	if(ssptr_to_erase) {
		this->subscriptions.erase(ssptr_to_erase->get_subscription_id());
		if (ssptr_to_erase->get_subscription_id() == monitored_subscription_id)
			monitored_subscription_id = subscription_id_t::NONE;
	}

	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	dtdebugf("dvbt scan_next\n");
	ssptr_to_erase = scan_next<chdb::dvbc_mux_t>(chdb_rtxn, finished_ssptr, scan_stats_dvbt);
	if(ssptr_to_erase) {
		this->subscriptions.erase(ssptr_to_erase->get_subscription_id());
		if (ssptr_to_erase->get_subscription_id() == monitored_subscription_id)
			monitored_subscription_id = subscription_id_t::NONE;
	}

	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	ssptr_to_erase = scan_next<chdb::dvbt_mux_t>(chdb_rtxn, finished_ssptr, scan_stats_dvbc);
	if(ssptr_to_erase) {
		this->subscriptions.erase(ssptr_to_erase->get_subscription_id());
		if (ssptr_to_erase->get_subscription_id() == monitored_subscription_id)
			monitored_subscription_id = subscription_id_t::NONE;
	}

	auto ss = get_scan_stats();
	dtdebugf("pending={:d}+{:d}", ss.pending_peaks, ss.pending_muxes);

	if(ss != scan_stats_before)
		receiver.notify_scan_progress(scan_subscription_id, ss);

	chdb_rtxn.commit();
}

/*
	Processes events and continues scanning.
	found_mux_keys = muxes found in nit_actual and nit_other
	Return 0 when scanning is done; otherwise number of muxes left

*/
bool scanner_t::housekeeping(bool force) {
	using namespace chdb;
	if (must_end)
		return true;
	auto now = steady_clock_t::now();
	if (!force && now - last_house_keeping_time < 60s)
		return false;
	last_house_keeping_time = now;
	devdb::scan_stats_t ss;
	try {
		for(auto& [scan_subscription_id, scan]: scans) {
			scan.housekeeping(force);
			ss = ss + scan.get_scan_stats();
		}
	} catch(std::runtime_error) {
		dtdebugf("Detected exit condition");
		must_end = true;
	}
	dtdebugf("{:d} bands left to scan; {:d} active", ss.pending_bands, ss.active_bands);
	dtdebugf("{:d} muxes left to scan; {:d} active", ss.pending_muxes, ss.active_muxes);

	return must_end ? true : scan_stats_done(ss);
}

static void report(const char* msg, ssptr_t finished_ssptr,
									 ssptr_t ssptr, const chdb::any_mux_t& mux,
									 const std::map<subscription_id_t, scan_subscription_t>& subscriptions)
{
	if (!finished_ssptr && !ssptr )
		return;
	ss::string<128> s;
	s.format(" Scan  {:s} {} finished_subscription={} subscription={} todo=[",
					 msg, mux, finished_ssptr, ssptr);
	for (auto& [id, unused] : subscriptions)
		s.format("[{:2d}] ", (int)id);
	s.format("] \n");
	dtdebugf("{}", s);
}

static void report(const char* msg, ssptr_t finished_ssptr,
									 ssptr_t ssptr, const chdb::sat_t& sat,
									 const chdb::band_scan_t& band_scan,
									 const std::map<subscription_id_t, scan_subscription_t>& subscriptions)
{
	if (!finished_ssptr && !ssptr )
		return;
	ss::string<128> s;
	s.format(" Scan  {:s} {}:{} finished_subscription={} subscription={} todo=[",
					 msg, sat, band_scan, finished_ssptr, ssptr);
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

//band_scan is translated to band
blindscan_key_t::blindscan_key_t(int16_t sat_pos, const chdb::band_scan_t& band_scan)
	: sat_pos(sat_pos)
	, band{band_scan.sat_band, band_scan.sat_sub_band}
	, pol(band_scan.pol)
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

static inline bool& skip_helper_band(std::map<blindscan_key_t,bool>& skip_map, int16_t sat_pos,
																const chdb::band_scan_t& band_to_scan)
{
	using namespace chdb;
	return skip_map[blindscan_key_t{sat_pos, band_to_scan}]; //band_to_scan is translated to band
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
																const chdb::scan_id_t& scan_id, ssptr_t ssptr)
{

	if (must_end) {
		dtdebugf("must_end");
		return true;
	}
	auto scan_subscription_id = scan_subscription_id_for_scan_id(scan_id);
	if((int)scan_subscription_id >= 0) {
		auto& scan = scans.at(scan_subscription_id);
		try {
			last_house_keeping_time = steady_clock_t::now();
			assert(scan.scan_subscription_id == scan_subscription_id);

			scan.on_scan_mux_end(finished_fe, finished_mux, ssptr);
		} catch(std::runtime_error) {
			dtdebugf("Detected exit condition");
			must_end = true;
		}

		bool done = scan_stats_done(scan.get_scan_stats());
		if(must_end || done) {
			std::vector<task_queue_t::future_t> futures;
			auto devdb_wtxn = receiver.devdb.wtxn();
			auto scan_ssptr = receiver.get_ssptr(scan_subscription_id);
			unsubscribe_scan(futures, devdb_wtxn, scan_ssptr);
			devdb_wtxn.commit();
			wait_for_all(futures); //remove later
			return true;
		}
		return false;
	}
	return false; //this scan must be from another running instance of neumoDVB
}

bool scanner_t::on_spectrum_scan_band_end(
	const devdb::fe_t& finished_fe, const spectrum_scan_t& spectrum_scan,
	const chdb::scan_id_t& scan_id, const ss::vector_<subscription_id_t>& fe_subscription_ids)
{
	auto& spectrum_key = spectrum_scan.spectrum->k;
	if (must_end) {
		dtdebugf("must_end");
		return true;
	}
	auto scan_subscription_id = scan_subscription_id_for_scan_id(scan_id);
	if((int)scan_subscription_id >= 0) {
		auto& scan = scans.at(scan_subscription_id);
		auto& tune_options = scan.tune_options_for_scan_id(scan_id);
		auto scan_ssptr = receiver.get_ssptr(scan_subscription_id);
		add_spectral_peaks(spectrum_key.rf_path, spectrum_key, spectrum_scan.peaks, scan_ssptr, &tune_options);

		try {
			last_house_keeping_time = steady_clock_t::now();
			assert(scan.scan_subscription_id == scan_subscription_id);

			for(auto subscription_id: fe_subscription_ids) {
				auto [it, found] = find_in_map(scan.subscriptions, subscription_id);
				if(!found)
					continue;
				auto& scan_subscription = it->second;
				auto& sat_band = scan_subscription.sat_band;
				assert(sat_band);
				auto ssptr = receiver.get_ssptr(scan_subscription.subscription_id);
				assert(ssptr->get_subscription_id() == subscription_id);
				scan.on_spectrum_scan_band_end(finished_fe, spectrum_scan, ssptr);
			}
		} catch(std::runtime_error) {
			dtdebugf("Detected exit condition");
			must_end = true;
		}
		bool done = scan_stats_done(scan.get_scan_stats());
		if(must_end || done) {
			std::vector<task_queue_t::future_t> futures;
			auto devdb_wtxn = receiver.devdb.wtxn();
			unsubscribe_scan(futures, devdb_wtxn, scan_ssptr);
			devdb_wtxn.commit();
			wait_for_all(futures); //remove later
			return true;
		}
	}

	return false;
}

/*
	rescan a peak after the first scan, which used database parameters and which failed.
	This time, use peak parameters instead (e.g., symbolrate estimated from peak width).

	The scan is performed don the same lnb on which the original mux scan was performed

	Returns false if scan has been launched successfully, or otherwise true, meaning that
	mux scan should be abolished
 */
template<typename mux_t>
bool
scan_t::rescan_peak(const blindscan_t& blindscan, ssptr_t reusable_ssptr,
										mux_t& mux, const peak_to_scan_t& peak, const blindscan_key_t& blindscan_key)

{
	using namespace chdb;
	scan_subscription_t* ss_ptr{nullptr};

	if (mux.c.scan_rf_path.lnb_id >=0 &&  mux.c.scan_rf_path.card_mac_address >=0 && mux.c.scan_rf_path.rf_input >=0) {
		auto devdb_rtxn = receiver.devdb.rtxn();
		auto lnb = devdb::lnb_for_lnb_id(devdb_rtxn, mux.c.scan_rf_path.dish_id, mux.c.scan_rf_path.lnb_id);
		devdb_rtxn.abort();
	}

	mux.frequency = peak.peak.frequency;
	if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t>) {
		assert(!blindscan.spectrum_acquired() || std::abs(mux.k.sat_pos - blindscan.spectrum_key->sat_pos)<=100);
		assert(!blindscan.spectrum_acquired() || mux.pol == peak.peak.pol);
		if(blindscan.spectrum_acquired()) {
			mux.symbol_rate = peak.peak.symbol_rate;
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
	bool failed_permanently{false};

	std::tie(reusable_ssptr, ss_ptr, failed_permanently) =
		scan_try_mux(reusable_ssptr, mux, use_blind_tune, blindscan_key);
	if(ss_ptr) {
		ss_ptr->peak = peak;
		ss_ptr->is_peak_scan = true;
	}
	return !ss_ptr;
}

static inline chdb::scan_rf_path_t scan_rf_path(devdb::rf_path_t & p) {
	chdb::scan_rf_path_t r;
	r.lnb_id = p.lnb.lnb_id;
	r.card_mac_address = p.card_mac_address;
	r.rf_input = p.rf_input;
	r.dish_id = p.lnb.dish_id;
	return r;
}

/*
	Try to scan a peak with subscription_id reusable_subscription_id;
	return
	-bool which is true if reservation failed due to lack of resources
	-bool which is true if reservation failed permanently
 */
template<typename mux_t> //template argument needed to support dvbc, dvbt and dvbs
std::tuple<bool, bool>
scan_t::scan_try_peak(db_txn& chdb_rtxn, blindscan_t& blindscan,
											ssptr_t reusable_ssptr, const peak_to_scan_t& peak,
											const blindscan_key_t& blindscan_key)
{
	using namespace chdb;
	assert(blindscan.spectrum_acquired());
	auto scan_id = peak.scan_id;
	scan_subscription_t* ss_ptr{nullptr};
	mux_t mux;

	mux.k.sat_pos = blindscan.spectrum_key->sat_pos;
	mux.frequency = peak.peak.frequency;
	if constexpr (is_same_type_v<decltype(mux), chdb::dvbs_mux_t&>) {
		assert(mux.k.sat_pos == blindscan.spectrum_key->sat_pos);
		mux.pol = peak.peak.pol;
		mux.symbol_rate = peak.peak.symbol_rate;
		mux.pls_mode = fe_pls_mode_t::ROOT;
		mux.pls_code = 1;
	}
	if constexpr (is_same_type_v<decltype(mux), chdb::dvbt_mux_t>) {
	}
	if constexpr (is_same_type_v<decltype(mux), chdb::dvbc_mux_t>) {
		assert(mux.k.sat_pos == blindscan.spectrum_key->sat_pos);
		mux.symbol_rate = peak.peak.symbol_rate;
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
	} else {
		mux.c.tune_src = tune_src_t::TEMPLATE;
	}
	dtdebugf("SET PENDING: {}", mux);
	mux.c.scan_status = scan_status_t::PENDING;
	mux.c.scan_id = scan_id;
	const bool use_blind_tune = true;
	bool failed_permanently{false};
	std::tie(reusable_ssptr, ss_ptr, failed_permanently) =
		scan_try_mux(reusable_ssptr, mux, use_blind_tune, blindscan_key);
	if(ss_ptr) {
		ss_ptr->peak = peak;
		ss_ptr->is_peak_scan = !c.is_valid(); //immediately go to peak_scan if no datatabse mux found
	} else if(!failed_permanently) {
		get_scan_stats_ref(mux).pending_peaks++;
	}

	return {!ss_ptr, failed_permanently};
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
bool scan_t::retry_subscription_if_needed(ssptr_t finished_ssptr,
																					scan_subscription_t& subscription,
																					const chdb::any_mux_t& finished_mux,
																					const blindscan_key_t& blindscan_key)
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
	case  scan_result_t::NOTS:
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

	if(!subscription.peak.is_present()) {
		return true; //regular scan, not a peak scan, this can be finished
	}

	if(failed) {
		int frequency;
		int symbol_rate;
		if (subscription.is_peak_scan)
			return true; //we have blindscanned already
		std::visit([&frequency, &symbol_rate](auto& mux) {
			frequency= get_member(mux, frequency, -1);
			symbol_rate = get_member(mux, symbol_rate, -1);
		}, finished_mux);

		if(std::abs(symbol_rate -(int) subscription.peak.peak.symbol_rate) <
			 std::min(symbol_rate, (int) subscription.peak.peak.symbol_rate)/4) //less than 25% difference in symbol rate
			return true; //blindscanned will not likely lead to different results
		dtdebugf("Calling rescan_peak for mux: {}", finished_mux);
		assert(subscription.mux);
		*mux_common_ptr(*subscription.mux) = *mux_common_ptr(saved); //restore scan_id and other fields
		return std::visit([&](auto&mux) {
			return rescan_peak(blindscan, finished_ssptr, mux, subscription.peak, blindscan_key);
		}, *subscription.mux);
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
ssptr_t
scan_t::scan_next_peaks(db_txn& chdb_rtxn,
												ssptr_t reusable_ssptr,
												std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats)
{
	// start as many subscriptions as possible
	using namespace chdb;
/* First scan available spectral peaks
	 When scanning spectral peaks for blindscans launched after spectral scan,
	 we force the lnb used to acquire the spectrum (e.g., in case the lnb is on a large \
	 dish, and scanning another lnb on a smaller dish might not succeed).

	 The user can also launch blindscan on more than one lnb but for the same sat (e.g., on multiple dishes).
	 In this case, in the current code, only one of those lnbs will be considered, as the peaks are stored
	 in the same data structure


	*/


	for(auto& [blindscan_key, blindscan]:  blindscans) {
		bool& skip_sat_band = skip_map[blindscan_key];
          /*Note that key depends on lnb for a spectral peak, whereas
						key below does not depend on lnb for a mux (which can be for any
						lnb).
						TODO: various strange situations can occur:
						1. user asks to scan muxes from dvbs mux and then also
						to scan peaks from spectrum dialog. This can lead to the same
						mux being scanned on a specific lnb and on "any" lnb
						2. even if user only starts blindscan, which will use a specific lnb,
						si processing can create new muxes which can then be scanned on any lnb
						*/
		if(skip_sat_band) {
			get_scan_stats_ref(blindscan_key.sat_pos).pending_peaks++;
			continue;
		}
		for(int idx = blindscan.peaks.size()-1;  idx>=0 ; --idx) {
			auto &peak = blindscan.peaks[idx];
			using namespace chdb;

			auto max_num_subscriptions = scanner.max_num_subscriptions;
			if ((int)subscriptions.size() >=
					(!reusable_ssptr ? max_num_subscriptions : max_num_subscriptions + 1)) {
				break; // to have accurate num_pending count
			}
			if(receiver_thread.must_exit())
				throw std::runtime_error("Exit requested");

			auto sat_pos = blindscan_key.sat_pos;
			auto[ failed, failed_permanently] =
				sat_pos == sat_pos_dvbt ?
				scan_try_peak<dvbt_mux_t>(chdb_rtxn, blindscan, reusable_ssptr, peak, blindscan_key)
				: sat_pos == sat_pos_dvbc ?
				scan_try_peak<dvbc_mux_t>(chdb_rtxn, blindscan, reusable_ssptr, peak, blindscan_key)
				:
				scan_try_peak<dvbs_mux_t>(chdb_rtxn, blindscan, reusable_ssptr, peak, blindscan_key);

			if (failed) {
				// we cannot subscribe the mux right now

				if(failed_permanently)  {
					//it is not possible to tune, probably because symbol_rate is out range
					blindscan.peaks.erase(idx);
				} else  {
					skip_sat_band = true; /*ensure that we do not even try muxes on the same sat, pol, band in this run
																	if the error is due to not being able to make a reservation*/
				}
				continue;
			} else {
				reusable_ssptr = {};
				blindscan.peaks.erase(idx);
			}
		}
		//@todo: implement blindscan for DVBC and DVBT
	}

	return reusable_ssptr;
}



/*
	returns the number of pending  muxes to scan, and number of skipped muxes
	and subscription to erase, and the new value of reusable_subscription_id
 */
template<typename mux_t>
ssptr_t
scan_t::scan_next_muxes(db_txn& chdb_rtxn,
												ssptr_t reusable_ssptr,
												std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats)
{
	// start as many subscriptions as possible
	using namespace chdb;
	for(int pass=0; pass < 2; ++pass) {
		/*
			Now scan any si muxes. If such scans match our scan_id then they must have been added
			for some peak we failed to discover.

			In the first pass, we try muxes that temporarily failed earlier
			In the second pass we try pending muxes
		*/
		if(pass == 0 && (int)subscriptions.size() >=
			 max_num_subscriptions_for_retry - (reusable_ssptr ? 1 : 0)) {
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
					(!reusable_ssptr ? scanner.max_num_subscriptions : scanner.max_num_subscriptions + 1)) {
				scan_stats.pending_muxes++;
				continue; // to have accurate num_pending count
			}

			bool& skip_mux = skip_helper(skip_map, mux_to_scan);
			if(skip_mux) {
				scan_stats.pending_muxes++;
				continue;
			}
			if(receiver_thread.must_exit())
				throw std::runtime_error("Exit requested");
			auto pol = get_member(mux_to_scan, pol, chdb::fe_polarisation_t::NONE);
			blindscan_key_t blindscan_key =
				{mux_to_scan.k.sat_pos, pol, mux_to_scan.frequency}; //frequency is translated to band
			if(mux_is_being_scanned(mux_to_scan)) {
				dtdebugf("Skipping mux already in progress: {}", mux_to_scan);
				continue;
			}

			const bool use_blind_tune = false;
			scan_subscription_t* ss_ptr{nullptr};
			bool failed_permanently{false};
			std::tie(reusable_ssptr, ss_ptr, failed_permanently) =
				scan_try_mux(reusable_ssptr, mux_to_scan, use_blind_tune, blindscan_key);

			if (!ss_ptr) {
				// we cannot subscribe the mux right now
				if(failed_permanently) {
					skip_mux = false;
					auto& blindscan = blindscans[blindscan_key];
					scan_mux_end_report_t report;
					report.spectrum_key = *blindscan.spectrum_key;
					report.mux = mux_to_scan;
					receiver.notify_scan_mux_end(scan_subscription_id, report); //we tried but failed immediately
				} else {
					scan_stats.pending_muxes++;
					skip_mux = true; //ensure that we do not even try muxes on the same sat, pol, band in this run
				}
				continue;
			}
		}
	}

	return reusable_ssptr;
}
/*
	returns the number of pending  muxes to scan, and number of skipped muxes
	and subscription to erase, and the new value of reusable_ssptr
 */

ssptr_t
scan_t::scan_next_bands(db_txn& chdb_rtxn,
												ssptr_t reusable_ssptr,
												std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats)
{
	// start as many subscriptions as possible
	using namespace chdb;
	auto c = find_first<sat_t>(chdb_rtxn);
	for(auto sat_to_scan: c.range()) {
		for(auto& band_scan: sat_to_scan.band_scans) {
			if(!scanner_t::is_our_scan(band_scan.scan_id))
				continue;

			if ((int)subscriptions.size() >=
					(!reusable_ssptr ? scanner.max_num_subscriptions : scanner.max_num_subscriptions + 1)) {
				scan_stats.pending_bands++;
				continue; // to have accurate num_pending count
			}

			bool& skip_band = skip_helper_band(skip_map, sat_to_scan.sat_pos, band_scan);
			if(skip_band) {
				scan_stats.pending_bands++;
				continue;
			}
			if(receiver_thread.must_exit())
				throw std::runtime_error("Exit requested");

			blindscan_key_t blindscan_key = {sat_to_scan.sat_pos, band_scan}; //band_scan is translated to band
			if(band_is_being_scanned(band_scan)) {
				dtdebugf("Skipping sat band already in progress: {}", band_scan);
				continue;
			}
			bool failed_permanently{false};
			scan_subscription_t* ss_ptr{nullptr};
			std::tie(reusable_ssptr, ss_ptr, failed_permanently)
				= scan_try_band(reusable_ssptr, sat_to_scan, band_scan, blindscan_key, scan_stats);

			if (!ss_ptr) {
				// we cannot subscribe the mux right now
				if(failed_permanently) {
					skip_band = false;
					auto& blindscan = blindscans[blindscan_key];
					statdb::spectrum_t spectrum;
					spectrum.k = *blindscan.spectrum_key;
					receiver.notify_spectrum_scan_band_end(scan_subscription_id, spectrum); //we tried but failed immediately
				} else {
					scan_stats.pending_bands++;
					skip_band = true; //ensure that we do not even try muxes on the same sat, pol, band in this run
				}
				continue;
			}
		}
	}
	return reusable_ssptr;
}

/* subscription to erase (which should then be equal to reusable_ssptr
 */
template<typename mux_t>
ssptr_t
scan_t::scan_next(db_txn& chdb_rtxn,
									ssptr_t reusable_ssptr, devdb::scan_stats_t& scan_stats)
{
	std::vector<task_queue_t::future_t> futures;
	// start as many subscriptions as possible
	using namespace chdb;
	std::map<blindscan_key_t, bool> skip_map;

	clear_pending(scan_stats);

/* First scan available spectral peaks
	 When scanning spectral peaks for blindscans launched after spectral scan,
	 we force the lnb used to acquire the spectrum (e.g., in case the lnb is on a large \
	 dish, and scanning another lnb on a smaller dish might not succeed).
*/
	reusable_ssptr =
		scan_next_peaks(chdb_rtxn, reusable_ssptr, skip_map, scan_stats);

	/*
		Next we try to scan muxes
	 */
	reusable_ssptr =
		scan_next_muxes<mux_t>(chdb_rtxn, reusable_ssptr, skip_map, scan_stats);

	if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
		/*
			Finally we attempt pending spectrum scans
		*/
		reusable_ssptr = scan_next_bands(chdb_rtxn, reusable_ssptr, skip_map, scan_stats);
	}

	if (reusable_ssptr) {
		//we have not reused reusable_ssptr so this subscription must be ended
		dtdebugf("Abandoning subscription {}", reusable_ssptr);
		auto wtxn = receiver.devdb.wtxn();
		receiver_thread.unsubscribe(futures, wtxn, reusable_ssptr);
		reusable_ssptr->remove_ssptr();
		reusable_ssptr = {};
		wtxn.commit();
		report("ERASED", reusable_ssptr, {}, chdb::dvbs_mux_t(), subscriptions);
		wait_for_all(futures);
	}
	return reusable_ssptr;
}

void scan_t::scan_loop(db_txn& chdb_rtxn, scan_subscription_t& subscription,
											 const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux) {
	// start as many subscriptions as possible
	using namespace chdb;
	auto& finished_mux_key = *mux_key_ptr(finished_mux);

	bool existing_subscription = (int)subscription.subscription_id >=0;
	assert(existing_subscription);
	assert(std::abs(subscription.blindscan_key.sat_pos-finished_mux_key.sat_pos)<=100);
	auto sat_pos =  subscription.blindscan_key.sat_pos;

	ssptr_t ssptr_to_erase;

/*scan any pending muxes of the same type as the just finished mux. This is useful to reuse the finished
	subscription if it is not currently busy, but also to take advantage of frontends used for viewing tv
	but then release */

	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	auto ssptr = receiver.get_ssptr(subscription.subscription_id);
	if(sat_pos == sat_pos_dvbc) {
		dtdebugf("dvbc scan_next\n");
		ssptr_to_erase = scan_next<chdb::dvbc_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbc);
	} else if(sat_pos == sat_pos_dvbt) {
		dtdebugf("dvbt scan_next\n");
		ssptr_to_erase = scan_next<chdb::dvbt_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbt);
	} else {
		ssptr_to_erase =  scan_next<chdb::dvbs_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbs);
	}

	if(ssptr_to_erase) {
		this->subscriptions.erase(ssptr_to_erase->get_subscription_id());
		if (ssptr_to_erase->get_subscription_id() == monitored_subscription_id)
			monitored_subscription_id = subscription_id_t::NONE;
	}

	auto ss = get_scan_stats();

	subscription.scan_start_reported = true;

	dtdebugf("finished_ssptr={} ssptr_to_erase ={} pending={:d}+{:d} active={:d}",
					 ssptr, ssptr_to_erase, ss.pending_peaks, ss.pending_muxes, ss.active_muxes);
}

void scan_t::on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
														 ssptr_t finished_ssptr) {
	auto scan_stats_before = get_scan_stats();
	assert(finished_ssptr);
	auto finished_subscription_id = finished_ssptr->get_subscription_id();

	auto [it, found] = find_in_map(this->subscriptions, finished_subscription_id);
	assert(found);
	auto subscription = it->second; //need to copy
	assert(subscription.subscription_id == finished_ssptr->get_subscription_id());

	auto&scan_stats = get_scan_stats_ref(finished_mux);
	scan_stats.active_muxes--;
	static int m =0;
	if (m < scan_stats.active_muxes)
		m= scan_stats.active_muxes;
	if( m>=14 && scan_stats.active_muxes <12)
		printf("here\n");
	assert(scan_stats.active_muxes>=0);

	bool is_peak = subscription.peak.is_present();

	auto chdb_rtxn = receiver.chdb.rtxn();

	auto& blindscan = blindscans[subscription.blindscan_key];
	bool finished = retry_subscription_if_needed(finished_ssptr,
																							 subscription, finished_mux,
																							 subscription.blindscan_key);

	bool locked = chdb::mux_common_ptr(finished_mux)->scan_result != chdb::scan_result_t::NOLOCK;
	bool nodvb = chdb::mux_common_ptr(finished_mux)->scan_result == chdb::scan_result_t::NOTS;
	if(finished) {
		//no retry
		scan_stats.finished_muxes++;
		scan_stats.failed_muxes += !locked;
		scan_stats.locked_muxes += locked;
		if(is_peak) {
			//note that these are also counted as muxes
			scan_stats.finished_peaks++;
			scan_stats.failed_peaks += !locked;
			scan_stats.locked_peaks += locked;
		}
	} else {
		/*This must be a peak scan where trying to use a database mux failed
		 */
	}
	scan_stats.si_muxes += (locked && !nodvb);

	/*if not finished, then peak will be retried and trying to tune new muxes
		is unlikely to succeed. Scan statistics also do not change in that case*/
	if (finished) {
		assert(subscription.subscription_id == finished_ssptr->get_subscription_id());
		scan_mux_end_report_t report{subscription, *blindscan.spectrum_key};
		receiver.notify_scan_mux_end(scan_subscription_id, report);

		scan_loop(chdb_rtxn, subscription, finished_fe, finished_mux);
    chdb_rtxn.commit();
	}
	auto ss= get_scan_stats();
	dtdebugf("SCAN REPORT finished_fe adapter {} mux=<{}> muxes to scan: pending={} active={}",
					 finished_fe.adapter_no, finished_mux, ss.pending_muxes,
					 (int) ss.active_muxes);
	if(ss != scan_stats_before)
		receiver.notify_scan_progress(scan_subscription_id, ss);

}

void scan_t::scan_loop(db_txn& chdb_rtxn, scan_subscription_t& subscription,
											 const devdb::fe_t& finished_fe, const chdb::sat_t& finished_sat) {

	// start as many subscriptions as possible
	using namespace chdb;

	bool existing_subscription = (int)subscription.subscription_id >=0;
	assert(existing_subscription);
	assert(subscription.blindscan_key.sat_pos==finished_sat.sat_pos);
	auto sat_pos =  subscription.blindscan_key.sat_pos;
	ssptr_t ssptr_to_erase{};

/*scan any pending muxes of the same type as the just finished mux. This is useful to reuse the finished
	subscription if it is not currently busy, but also to take advantage of frontends used for viewing tv
	but then release */

	if(receiver_thread.must_exit())
		throw std::runtime_error("Exit requested");
	auto ssptr = receiver.get_ssptr(subscription.subscription_id);
	if(sat_pos == sat_pos_dvbc) {
		dtdebugf("dvbc scan_next\n");
		ssptr_to_erase = scan_next<chdb::dvbc_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbc);
	} else if(sat_pos == sat_pos_dvbt) {
		dtdebugf("dvbt scan_next\n");
		ssptr_to_erase = scan_next<chdb::dvbt_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbt);
	} else {
		ssptr_to_erase = scan_next<chdb::dvbs_mux_t>(chdb_rtxn, ssptr, scan_stats_dvbs);
	}

	auto& scan_stats= get_scan_stats_ref(finished_sat);

	scan_stats.finished_bands++;

	subscription.scan_start_reported = true;

	if(ssptr_to_erase) {
		assert(scan_stats.active_bands >=0);
		this->subscriptions.erase(ssptr_to_erase->get_subscription_id());
		if (ssptr_to_erase->get_subscription_id() == monitored_subscription_id)
			monitored_subscription_id = subscription_id_t::NONE;
	}
	auto ss = get_scan_stats();
	dtdebugf("finished_ssptr={} ssptr_to_erase={} pending={:d}+{:d}",
					 ssptr, ssptr_to_erase, ss.pending_peaks, ss.pending_muxes);
}

void scan_t::on_spectrum_scan_band_end(const devdb::fe_t& finished_fe, const spectrum_scan_t& spectrum_scan,
																			 ssptr_t finished_ssptr) {
	auto scan_stats_before = get_scan_stats();
	assert(finished_ssptr);
	auto finished_subscription_id = finished_ssptr->get_subscription_id();
	assert((int)finished_subscription_id>=0);

	auto [it, found] = find_in_map(this->subscriptions, finished_subscription_id);
	assert(found);
	auto subscription = it->second; //need to copy
	assert(subscription.subscription_id == finished_ssptr->get_subscription_id());
	assert(subscription.sat_band);
	//auto& blindscan = blindscans[subscription.blindscan_key];

	auto& [finished_sat, finished_band_scan] = *subscription.sat_band;

	auto& scan_stats = get_scan_stats_ref(finished_sat);
	scan_stats.active_bands--;
	assert(scan_stats.active_bands>=0);
#if 0
	update_db_sat(finished_sat, finished_band_scan);
#endif
	auto chdb_rtxn = receiver.chdb.rtxn();

	scan_loop(chdb_rtxn, subscription, finished_fe, finished_sat);
	chdb_rtxn.commit();

	auto ss = get_scan_stats();
	dtdebugf("SCAN REPORT: finished_fe adapter {} band={}/{} pending={} active={}",
					 finished_fe.adapter_no,
					 finished_sat, finished_band_scan, ss.pending_bands, ss.active_bands);
	if(ss != scan_stats_before)
		receiver.notify_scan_progress(scan_subscription_id, ss);
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
	Because tune thread runs asynchronously, muxes are not immediately marked as active in the database,
	which could cause them to be scanned twice.
	The call returns true if a (pending) mux is already being scanned
 */
bool scan_t::band_is_being_scanned(const chdb::band_scan_t& band_scan)
{

	for(auto it = subscriptions.begin(); it != subscriptions.end(); ++it) {
			scan_subscription_t subscription {it->second}; //need to copy
			if(!subscription.sat_band)
				continue;
			auto& [sat_, band_] = *subscription.sat_band;
			if(band_scan.scan_id == band_.scan_id)
				return true;
	}
	return false;
}

/*
	returns
	-the new value of reusable_ssptr, which will be nullptr to to indicate that it can no longer be reused
	-a pointer to the created scan_subscription if one exists
	-bool which is true if reservation failed permanently
 */
template<typename mux_t>
std::tuple<ssptr_t, scan_subscription_t*, bool>
scan_t::scan_try_mux(ssptr_t reusable_ssptr,
										 const mux_t& mux_to_scan, bool use_blind_tune,
										 const blindscan_key_t& blindscan_key)
{
	auto & scan_stats = get_scan_stats_ref(mux_to_scan);
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t ret{-1};
	std::vector<task_queue_t::future_t> futures;

	auto wtxn = receiver.devdb.wtxn();
	auto scan_id = mux_to_scan.c.scan_id;
	auto& tune_options = tune_options_for_scan_id(scan_id);
	tune_options.use_blind_tune = use_blind_tune;
	tune_options.need_spectrum = false;
	assert(tune_options.subscription_type == devdb::subscription_type_t::MUX_SCAN);
	assert(chdb::scan_in_progress(scan_id));
	assert(scanner_t::is_our_scan(scan_id));
	dtdebugf("Asking to subscribe {} reusable_ssptr={}", mux_to_scan, reusable_ssptr);
	if(!reusable_ssptr)
		reusable_ssptr = subscriber_t::make(&receiver, nullptr /*window*/);
	scan_stats.active_muxes++;
	ret =
		receiver_thread.subscribe_mux(futures, wtxn, mux_to_scan, reusable_ssptr, tune_options,
																	scan_id, true /*do_not_unsubscribe_on_failure*/);
	wtxn.commit();
	wait_for_all(futures); //remove later
	if((int)ret >=0) {
		dtdebugf("SUBSCRIBED {} reusable_ssptr={} ret={}",
						 mux_to_scan, reusable_ssptr, (int) ret);
	} else {
		scan_stats.active_muxes--;
	}
	/*
		When tuning fails, it is essential that tuner_thread does NOT immediately unsubscribe the mux,
		but rather informs us about the failure via scan_mux_end. Otherwise we would have to
		update the mux status ourselves (here in the code), which also would force us to reacquire a new
		read transaction in the middle of a read loop
	 */
	assert((int)ret >=0 || ret != subscription_id_t::TUNE_FAILED);
	/*An error can occur when resources cannot be reserved; this is reported immediately
		by returning subscription_id_t == RESERVATION_FAILED;
		Afterwards only tuning errors can occur. These errors will be noticed by error==true after
		calling wait_for_all. Such errors indicated TUNE_FAILED.
	*/
	if ((int)ret < 0) {
		if(subscriptions.size()== 0) {
			/* we cannot subscribe the mux  because of some permanent failure or because of
				 subscriptions by another program
				 => Give up
			*/
			auto chdb_wtxn = receiver.chdb.wtxn();
			mux_t mux = mux_to_scan; //create copy
			dtdebugf("SET IDLE ", mux);
			mux.c.scan_status = chdb::scan_status_t::IDLE;
			mux.c.scan_result = chdb::scan_result_t::BAD;
			mux.c.scan_id = {};
			namespace m = chdb::update_mux_preserve_t;
			chdb::update_mux(chdb_wtxn, mux, now,
											 m::flags{(m::MUX_COMMON|m::MUX_KEY)& ~m::SCAN_STATUS}, /*false ignore_key,*/
											 false /*ignore_t2mi_pid*/,
											 true /*must_exist*/);
			chdb_wtxn.commit();
			ret = subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
		}
		return {reusable_ssptr, nullptr, ret == subscription_id_t::RESERVATION_FAILED_PERMANENTLY};
	}
	dtdebugf("subscribed to {} ret={} reusable_ssptr={}",
					 mux_to_scan, (int) ret, reusable_ssptr);

	/*at this point, we know that our newly subscribed fe differs from our old subscribed fe
	 */
	scan_subscription_t ss{reusable_ssptr->get_subscription_id()};
	ss.blindscan_key = blindscan_key;
	ss.mux = mux_to_scan;
	ss.is_peak_scan=false;
	auto[it, inserted] = subscriptions.insert_or_assign(ret, ss);
	assert((int)ret >= 0);
	if (reusable_ssptr) {
		assert(ret == reusable_ssptr->get_subscription_id());
		/*
			We have reused an old subscription_id, but are now using a different fe than before
			Find the old fe we are using; not that at this point, subscription_id will be present twice
			as a value in subscriptions; find the old one (by skipping the new one)
		*/
		reusable_ssptr = {}; // we can still attempt to subscribe, but with new a new subscription_id
	}

	return {reusable_ssptr, &it->second, false};
}


/*
	returns
	-the new value of reusable_ssptr, which will be -1 to to indicate that it can no longer be reused
	-a pointer to the created subscription if one exists
	-bool which is true if reservation failed permanently
 */
std::tuple<ssptr_t, scan_subscription_t*, bool>
scan_t::scan_try_band(ssptr_t reusable_ssptr,
											const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
											const blindscan_key_t& blindscan_key, devdb::scan_stats_t& scan_stats)
{

	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t ret{-1};
	std::vector<task_queue_t::future_t> futures;
	auto wtxn = receiver.devdb.wtxn();
	auto scan_id = band_scan.scan_id;
	auto& tune_options = tune_options_for_scan_id(scan_id);
	assert(tune_options.need_spectrum );
	assert(chdb::scan_in_progress(scan_id));
	assert(scanner_t::is_our_scan(scan_id));
	if(!reusable_ssptr)
		reusable_ssptr = subscriber_t::make(&receiver, nullptr /*window*/);
	ret =
		receiver_thread.subscribe_spectrum(futures, wtxn, sat, band_scan, reusable_ssptr, tune_options,
																			 scan_id, true /*do_not_unsubscribe_on_failure*/);
	wtxn.commit();
	wait_for_all(futures); //remove later

	if((int)ret >=0) {
		assert(reusable_ssptr->get_subscription_id() == ret);
		scan_stats.active_bands++;
		report("SUBSCRIBED", reusable_ssptr, reusable_ssptr,
					 sat, band_scan, subscriptions);
		dtdebugf("Asked to subscribe {}:{} reusable_ssptr={} ret={}",
						 sat, band_scan, reusable_ssptr, (int) ret);
	}

	assert((int)ret >=0 || ret != subscription_id_t::TUNE_FAILED);

	/*An error can occur when resources cannot be reserved; this is reported immediately
		by returning subscription_id_t == RESERVATION_FAILED;
		Afterwards only tuning errors can occur. These errors will be noticed by error==true after
		calling wait_for_all. Such errors indicated TUNE_FAILED.
	*/
	if ((int)ret < 0) {
		if(subscriptions.size()== 0) {
			/* we cannot subscribe the sat band spectrum because of some permanent failure or because of
				 subscriptions by another program
				 => Give up
			*/
			auto chdb_wtxn = receiver.chdb.wtxn();
			dtdebugf("SET IDLE {}:{}", sat, band_scan);
			auto bs = band_scan;  //create copy
			auto sat_ = sat; //create copy
			bs.scan_status  =  chdb::scan_status_t::IDLE;
			bs.scan_result = chdb::scan_result_t::BAD;
			bs.scan_id = {};
			bs.scan_rf_path = {}; //not valid
			bs.scan_time = 0; //TODO: start time is now set in spectrum_scan_options, but this will not work

			chdb::sat::band_scan_for_pol_sub_band(sat_, bs.pol, bs.sat_sub_band) = bs;
			sat_.mtime = system_clock_t::to_time_t(now);
			chdb_wtxn.commit();
			ret = subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
		}
		return {reusable_ssptr, nullptr, ret == subscription_id_t::RESERVATION_FAILED_PERMANENTLY};
	}
	dtdebugf("subscribed to {}:{} ret={} reusable_ssptr={}",
					 sat, band_scan, (int)ret, reusable_ssptr);

	/*at this point, we know that our newly subscribed fe differs from our old subscribed fe
	 */
	scan_subscription_t ss{reusable_ssptr->get_subscription_id()};
	ss.blindscan_key = blindscan_key;
	ss.sat_band = {sat, band_scan};
	ss.is_peak_scan=false;
	auto[it, inserted] = subscriptions.insert_or_assign(ret, ss);
	assert((int)ret >= 0);
	if (reusable_ssptr) {
		assert(ret == reusable_ssptr->get_subscription_id());
		/*
			We have reused an old subscription_id, but are now using a different fe than before
			Find the old fe we are using; not that at this point, subscription_id will be present twice
			as a value in subscriptions; find the old one (by skipping the new one)
		*/
		reusable_ssptr = {}; // we can still attempt to subscribe, but with new a new subscription_id
	}
	return {reusable_ssptr, &it->second, false};
}



scanner_t::scanner_t(receiver_thread_t& receiver_thread_,
										 int max_num_subscriptions_)
	: receiver_thread(receiver_thread_)
	, receiver(receiver_thread_.receiver)
	, scan_start_time(system_clock_t::to_time_t(system_clock_t::now()))
	,	max_num_subscriptions(max_num_subscriptions_)
{
	tune_options.scan_target =  devdb::scan_target_t::SCAN_FULL;
	tune_options.may_move_dish = false; //could become an option
	tune_options.use_blind_tune = false; //could become an option
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

/*
	called when scanner finds that scan has ended
 */
bool scanner_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
																 db_txn& devdb_wtxn, ssptr_t scan_ssptr)
{
	auto scan_subscription_id = scan_ssptr->get_subscription_id();
	auto [it, found ] = find_in_map(scans, scan_subscription_id);
	if(!found)
		return found; //note that we are called for all subscription_ids, so failing is normal
	auto& scan = scans.at(scan_subscription_id);
	assert(scan.scan_subscription_id == scan_subscription_id);
	end_scans(scan_subscription_id);
	auto ss = scan.get_scan_stats();
	scan_stats_abort(ss);
	ss.finished = true;

	receiver.notify_scan_progress(scan_subscription_id, ss);
	for (auto[subscription_id, sub] : scan.subscriptions) {
		auto ssptr = receiver_thread.receiver.get_ssptr(subscription_id);
		receiver_thread.unsubscribe(futures, devdb_wtxn, ssptr);
		ssptr = {};
	}
	receiver_thread.on_scan_command_end(devdb_wtxn, scan_ssptr, ss);
	scans.erase(scan_subscription_id);
	scan_ssptr = {};
	return found;
}

static inline bool can_subscribe(db_txn& devdb_rtxn, const auto& mux, const subscription_options_t& tune_options){
	if constexpr (is_same_type_v<chdb::dvbs_mux_t, decltype(mux)>) {
		return devdb::fe::can_subscribe_mux(devdb_rtxn, mux, tune_options);
	} else {
		return devdb::fe::can_subscribe_dvbc_or_dvbt_mux(devdb_rtxn, mux, tune_options.use_blind_tune);
	}
	assert(0);
	return false;
}

static inline bool can_subscribe(db_txn& devdb_rtxn, const chdb::sat_t& sat,
																 const chdb::band_scan_t& band_scan,
																 const subscription_options_t& tune_options){
	assert(tune_options.need_spectrum);
	return devdb::fe::can_subscribe_sat_band(devdb_rtxn, sat, band_scan, tune_options);
}

template <typename mux_t>
int scanner_t::add_muxes(const ss::vector_<mux_t>& muxes, const subscription_options_t& tune_options,
												 ssptr_t scan_ssptr) {
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto chdb_wtxn = receiver.chdb.wtxn();
	/*
		The following call also created default tune_options if needed
	 */
	auto scan_subscription_id = scan_ssptr->get_subscription_id();
	auto [it, inserted] = scans.try_emplace(scan_subscription_id, *this, scan_subscription_id);
	auto& scan = it->second;
	assert(scan.scan_subscription_id == scan_subscription_id);

	auto scan_id = scan.make_scan_id(scan_subscription_id, tune_options);
	int num_added{0};

	for(const auto& mux_: muxes) {
		if(!can_subscribe(devdb_rtxn, mux_, scan.tune_options_for_scan_id(scan_id))) {
			//dtdebugf("Skipping mux that cannot be tuned: {}", mux_);
			continue;
		}
		mux_t mux;
		num_added++;
		dtdebugf("SET PENDING {}", mux_);
		/*
			@todo: multiple parallel scans can override each other's scan_status
		 */
		auto c = mux_t::find_by_key(chdb_wtxn, mux_.k, find_eq, mux_t::partial_keys_t::all);
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
	if(num_added==0) {
		user_errorf("Could not add any of the {} muxes", muxes.size());
	}
	return num_added > 0;
}

template<typename peak_t>
int scanner_t::add_spectral_peaks(const devdb::rf_path_t& rf_path,
																	const statdb::spectrum_key_t& spectrum_key,
																	const ss::vector_<peak_t>& peaks,
																	ssptr_t scan_ssptr,
																	subscription_options_t* options) {
	auto scan_subscription_id = scan_ssptr->get_subscription_id();
	assert((int) scan_subscription_id >=0);
	auto so = options ? *options:
		receiver.get_default_subscription_options(devdb::subscription_type_t::MUX_SCAN);
	if(!options) {
		so.propagate_scan = false;
		so.may_move_dish = false;
		so.use_blind_tune = false;
		so.allowed_dish_ids = {};
		so.allowed_card_mac_addresses = {};
		so.allowed_rf_paths = {rf_path};
	}
	so.subscription_type = devdb::subscription_type_t::MUX_SCAN;
	auto [it, found] = scans.try_emplace(scan_subscription_id, *this, scan_subscription_id);
	auto& scan = it->second;
	assert(scan.scan_subscription_id == scan_subscription_id);

	auto scan_id = scan.make_scan_id(scan_subscription_id, so);

	for(const auto& peak_: peaks) {
		chdb::spectral_peak_t peak;
		if constexpr (is_same_type_v<chdb::spectral_peak_t, peak_t>) {
			peak = peak_;
		} else {
			peak = chdb::spectral_peak_t{(uint32_t)peak_.freq, (uint32_t)peak_.symbol_rate, spectrum_key.pol};
		}
		blindscan_key_t key{spectrum_key.sat_pos, spectrum_key.pol, peak.frequency};
		auto& blindscan = scan.blindscans[key];
		if(!blindscan.spectrum_acquired()) {
			blindscan.spectrum_key = spectrum_key;
		}  else {
			assert(blindscan.spectrum_key == spectrum_key);
		}
		blindscan.peaks.push_back(peak_to_scan_t(peak, scan_id));
	}

	return 0;
}

int scanner_t::add_bands(const ss::vector_<chdb::sat_t>& sats,
												 const ss::vector_<chdb::fe_polarisation_t>& pols,
												 const subscription_options_t& tune_options,
												 ssptr_t scan_ssptr) {
	auto scan_subscription_id = scan_ssptr->get_subscription_id();
	assert((int)scan_subscription_id >=0);
	using namespace chdb;
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto chdb_wtxn = receiver.chdb.wtxn();
	/* ensure that empty entry exists. This can be used to set required_lnb and such
	 */
	/*
		The following call also created default tune_options if needed
	 */
	auto [it, inserted] = scans.try_emplace(scan_subscription_id, *this, scan_subscription_id);
	auto& scan = it->second;
	assert(scan.scan_subscription_id == scan_subscription_id);
	/*
		Add a band to scan to a sat, except if the band is already marked for scanning
		Overwrite old data if needed to avoid ever increasing lists
		@todo: if a user removes a band from a satellite, it is not possible to remove the old scan record
	 */
	auto push_bands = [&pols, &devdb_rtxn, &scan, &tune_options, scan_subscription_id](
		sat_t& sat, auto band_sub_band_tuple )
		-> std::tuple<int,int> {
		auto [ sat_band, sat_sub_band] = band_sub_band_tuple;
		int num_added{0};
		int num_considered{0};
		for (auto pol: pols) {
			//TODO: avoid adding fake
			auto& band_scan = sat::band_scan_for_pol_sub_band(sat, pol, sat_sub_band);
			auto saved = band_scan;
			band_scan.pol = pol;
			band_scan.sat_band = sat_band;
			band_scan.sat_sub_band = sat_sub_band;
			band_scan.scan_status = scan_status_t::PENDING;
			auto o = tune_options; //make a copy
			auto& so = o.spectrum_scan_options;
			so.band_pol.band = band_scan.sat_sub_band;
			so.band_pol.pol = pol;
			so.sat = sat;

			auto [l, h] =sat_band_freq_bounds(sat_band, sat_sub_band);
			if(so.start_freq < 0 || so.start_freq < l)
				so.start_freq = l;
			if(so.end_freq < 0 || so.end_freq > h)
				so.end_freq = h;
			auto scan_id = scan.make_scan_id(scan_subscription_id, o);
			band_scan.scan_id=scan_id;
			num_considered ++;
			if(!can_subscribe(devdb_rtxn, sat, band_scan, scan.tune_options_for_scan_id(scan_id))) {
				dtdebugf("Skipping sat that cannot be tuned: {}", sat);
				band_scan = saved;
				continue;
		}
			num_added++;
		}
		return {num_added, num_considered};
	};

	int num_added_bands{0};
	int num_bands{0};

	for(auto sat: sats) {
		auto c = sat_t::find_by_key(chdb_wtxn, sat.sat_pos, sat.sat_band, find_eq, sat_t::partial_keys_t::all);
		if(c.is_valid())
			sat = c.current(); //reload uptodate information from database
		auto low_freq = tune_options.spectrum_scan_options.start_freq;
		auto high_freq = tune_options.spectrum_scan_options.end_freq;
		auto [l, h] = sat_band_freq_bounds(sat.sat_band, sat_sub_band_t::NONE);
		l = low_freq == -1 ? l : std::max(l, low_freq);
		h = high_freq == -1 ? h : std::max(h, high_freq);

		auto band_l = chdb::sat_band_for_freq(l);
		auto band_h = chdb::sat_band_for_freq(h-1);
		auto [na, nc] = push_bands(sat, band_l);
		bool save = na>0;
		num_added_bands += na;
		num_bands += nc;
		if(band_h != band_l) {
			std::tie(na, nc) = push_bands(sat, band_h);
			num_added_bands += na;
			num_bands += nc;
			save |= na > 0;
		}
		if(save) {
			dtdebugf("Add sat to scan: {}", sat);
			sat.mtime = system_clock_t::to_time_t(now);
			put_record(chdb_wtxn, sat);
		}
	}

	chdb_wtxn.commit();

	if(num_added_bands==0) {
		user_errorf("Could not add any of the {} satellite bands", num_bands);
	}

	return num_added_bands;
}

scanner_t::~scanner_t() {
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();

	//we do not iterate over elements as unsubscribe_scan erases them
	while(scans.size() >0) {
		auto it = scans.begin();
		auto scan_subscription_id = it->first;
		auto scan_ssptr = receiver.get_ssptr(scan_subscription_id);
		unsubscribe_scan(futures, devdb_wtxn, scan_ssptr);
	}
	devdb_wtxn.commit();
	wait_for_all(futures);
}


inline static bool is_better(const signal_info_t& a, const signal_info_t& b) {
	if(a.tune_confirmation.sdt_actual_received && ! b.tune_confirmation.sdt_actual_received)
		return true;
	if(a.tune_confirmation.nit_actual_received && ! b.tune_confirmation.nit_actual_received)
		return true;
	if(a.tune_confirmation.pat_received && ! b.tune_confirmation.pat_received)
		return true;
	if(a.lock_status.matype >=0 && b.lock_status.matype<0)
		return true;
	if(!!(a.lock_status.fe_status & FE_HAS_LOCK)  && !(b.lock_status.fe_status & FE_HAS_LOCK))
		return true;
	if(!!(a.lock_status.fe_status & FE_HAS_SYNC) && !(b.lock_status.fe_status & FE_HAS_SYNC))
		return true;
	if(!!(a.lock_status.fe_status & FE_HAS_TIMING_LOCK)&& !(b.lock_status.fe_status & FE_HAS_TIMING_LOCK))
		return true;
	if(!!(a.lock_status.fe_status & FE_HAS_CARRIER) && !(b.lock_status.fe_status & FE_HAS_CARRIER))
		return true;
	if(a.stat.stats.size() >0 && b.stat.stats.size() ==0)
		 return true;
	return false;
}
void scan_t::update_monitor(const ss::vector_<subscription_id_t>& fe_subscription_ids,
															 const signal_info_t& signal_info)
{
	auto now=steady_clock_t::now();
	for(auto subscription_id: fe_subscription_ids) {
		if((int) monitored_subscription_id <0 ||
			 (subscription_id == monitored_subscription_id) ?
			 is_better(signal_info, monitor_signal_info) : (now-monitor_time >=5s)
			) {
			monitored_subscription_id = subscription_id;
			monitor_time = now;
			monitor_signal_info = signal_info;
			return;
		}
	}
}

void scanner_t::on_signal_info(const subscriber_t& subscriber,
																	 const ss::vector_<subscription_id_t>& fe_subscription_ids,
																	 const signal_info_t& signal_info) {

	auto [it, found] = find_in_map(this->scans, subscriber.get_subscription_id());
	if(!found) {
		dtdebugf("SIGINFO: NOST FOUND for {}\n", (int)subscriber.get_subscription_id());
		return; //not a scan control subscription_id
	}
	auto &scan = it->second;
	scan.update_monitor(fe_subscription_ids, signal_info);
	for(auto subscription_id: fe_subscription_ids) {
		auto [it, found] = find_in_map(scan.subscriptions, subscription_id);
		if(!found) {
			continue; //this is not a subscription used by this scan
		}
		if(subscription_id == scan.monitored_subscription_id) {
			subscriber.notify_signal_info(signal_info);
			return;
		}
	}
	dtdebugf("NOT Notifying: monitored_subscription_id={:d}", (int) scan.monitored_subscription_id);
}

void scanner_t::on_sdt_actual(const subscriber_t& subscriber,
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
	dtdebugf("NOT Notifying: monitored_subscription_id={:d}\n", (int) scan.monitored_subscription_id);
}



template int scanner_t::add_muxes<chdb::dvbs_mux_t>(const ss::vector_<chdb::dvbs_mux_t>& muxes,
																										const subscription_options_t& tune_options,
																										ssptr_t scan_ssptr);
template int scanner_t::add_muxes<chdb::dvbc_mux_t>(const ss::vector_<chdb::dvbc_mux_t>& muxes,
																										const subscription_options_t& tune_options,
																										ssptr_t scan_ssptr);
template int scanner_t::add_muxes<chdb::dvbt_mux_t>(const ss::vector_<chdb::dvbt_mux_t>& muxes,
																										const subscription_options_t& tune_options,
																										ssptr_t scan_ssptr);


template ssptr_t scan_t::scan_next<chdb::dvbs_mux_t>(db_txn& chdb_rtxn,
																										 ssptr_t ssptr, devdb::scan_stats_t& scan_stats);
template ssptr_t scan_t::scan_next<chdb::dvbc_mux_t>(db_txn& chdb_rtxn,
																										 ssptr_t ssptr, devdb::scan_stats_t& scan_stats);
template ssptr_t scan_t::scan_next<chdb::dvbt_mux_t>(db_txn& chdb_rtxn,
																										 ssptr_t ssptr, devdb::scan_stats_t& scan_stats);

template int scanner_t::add_spectral_peaks(const devdb::rf_path_t& rf_path,
																					 const statdb::spectrum_key_t& spectrum_key,
																					 const ss::vector_<chdb::spectral_peak_t>& peaks,
																					 ssptr_t scan_ssptr, subscription_options_t*options);
template int scanner_t::add_spectral_peaks(const devdb::rf_path_t& rf_path,
																					 const statdb::spectrum_key_t& spectrum_key,
																					 const ss::vector_<spectral_peak_t>& peaks,
																					 ssptr_t scan_ssptr, subscription_options_t*options);
