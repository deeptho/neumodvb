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

#pragma once
#include <boost/context/continuation_fcontext.hpp>
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "devmanager.h"
#include "neumodb/devdb/tune_options.h"

#include <set>

class receiver_thread_t;
class tuner_thread_t;
class receiver_t;
class active_adapter_t;
struct sdt_data_t;

struct scan_state_t {
	/*
		possible modes of operation

		-regular tune: scan sdt, nit, bat, epg and remain tuned for ever
		-simple channel scan: scan sdt, nit, bat, pat NO EPG
		-full channel scan: scan sdt, nit, bat, pat NO EPG, but scan all PMTs

		-with all above, epg can be included.

	*/

		enum  completion_index_t  {
			PAT,
			NIT_ACTUAL,
			NIT_OTHER,
			SDT_ACTUAL, //current transport stream
			SDT_OTHER,
			SDT_NETWORK, //current netork
			BAT,
			FST_BAT,
			EIT_ACTUAL_EPG,
			EIT_OTHER_EPG,
			SKYUK_EPG,
			MHW2_EPG,
			VIASAT_EPG,
			FST_EPG,
			NUM
		};

	struct completion_state_t {
		bool active{false};
		bool timedout{false};
		bool completed{false};
		bool required_for_scan{false};
		steady_time_t start_time;
		steady_time_t done_time;
		steady_time_t last_active;
		completion_state_t() :
			start_time(steady_clock_t::now()) {
		}
		void reset(bool active, bool required_for_scan) {
			*this = completion_state_t();
			this->active = active;
			this->required_for_scan = required_for_scan;
		}

		//never received data
		inline bool notpresent() const {
			const std::chrono::seconds timeout{20}; //seconds
			return (last_active == steady_time_t()) && (steady_clock_t::now() - start_time) > timeout;
		}

		//last received data too old
		inline bool notactive() const {
			const std::chrono::seconds timeout{50}; //seconds, e.g., can happen if continuously receiving sections with CRC errors
			return (last_active != steady_time_t()) && ((steady_clock_t::now() - last_active) > timeout);
		}

		inline bool done() const {
			return !required_for_scan || completed || timedout || notpresent() || notactive();
		}

		const char* str() const {
			if(!active)
				return "NP";
			if(completed)
				return "OK";
			if(timedout)
				return "TO";
			return "BUSY";
		}
	};

	ss::vector<completion_state_t, completion_index_t::NUM> completion_states;

	ss::vector<std::tuple<chdb::scan_id_t, subscription_id_t>,2> scans_in_progress; //indexed by scan_id

	scan_state_t() {
		completion_states.resize(completion_index_t::NUM);
	}

	inline const char* str(completion_index_t idx) {
		return completion_states[idx].str();
	}

	inline bool done(completion_index_t idx) const {
		return completion_states[idx].done();
	}

	inline bool completed(completion_index_t idx) const {
		return completion_states[idx].completed;
	}

	inline bool notpresent(completion_index_t idx) const {
		return completion_states[idx].notpresent();
	}

	inline bool set_active(completion_index_t idx) {
		auto& c = completion_states[idx];
		if(c.timedout || c.completed)
			c.reset(true, c.required_for_scan);
		c.last_active = steady_clock_t::now();
		auto old = c.active;
		c.active = true;
		return old;
	}
	inline void set_timedout(completion_index_t idx) {
		auto& c = completion_states[idx];
		if(c.timedout)
			c.reset(true, c.required_for_scan); //multiple timeouts can happen e.g., for EI_ACTUAL
		if(!c.completed)
			c.done_time = steady_clock_t::now();
		c.timedout = true;
		c.last_active = steady_clock_t::now();
	}


	inline void set_completed(completion_index_t idx) {
		auto& c = completion_states[idx];
		if(!c.completed) {
			c.done_time = steady_clock_t::now();
			assert(!c.completed);
			c.completed = true;
		}
		c.last_active = steady_clock_t::now();
	}

	inline bool scan_done() const {

		for(auto& c: completion_states)
			if(!c.done()  && c.required_for_scan)
				return false;
		return true;
	}

	/*
		commpleted means that each parser made it to the end without any unfinished
		business
	 */
	inline bool nit_sdt_scan_completed() const {
		for(int idx = completion_index_t::PAT; idx <= completion_index_t::SDT_NETWORK; ++idx) {
			auto& c =  completion_states[idx];
			if(!c.completed && !c.notpresent() && c.required_for_scan)
				return false;
		}
		return true;
	}

	/*
		commpleted means that each parser made it to the end without any unfinished
		business
	 */
	inline bool epg_scan_completed() const {
		for(int idx = completion_index_t::EIT_ACTUAL_EPG; idx < completion_index_t::NUM; ++idx) {
			auto& c =  completion_states[idx];
			if(!c.completed && !c.notpresent() && c.required_for_scan)
				return false;
		}
		return true;
	}

	inline int nit_sdt_scan_duration() const {
		auto start = steady_time_t::max();
		auto end = steady_time_t::min();
		for(int idx = completion_index_t::PAT; idx <= completion_index_t::SDT_NETWORK; ++idx) {
			auto& c =  completion_states[idx];
			if(c.active) {
				start = std::min(start, c.start_time);
				end = std::max(end, c.last_active);
			}
		}
		return std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
	}

	inline int epg_scan_duration() const {
		auto start = steady_time_t::max();
		auto end = steady_time_t::min();
		for(int idx = completion_index_t::EIT_ACTUAL_EPG; idx < completion_index_t::NUM; ++idx) {
			auto& c =  completion_states[idx];
			if(c.active) {
				start = std::min(start, c.start_time);
				end = std::max(end, c.last_active);
			}
		}
		return std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
	}

	inline void start(completion_index_t idx, bool required_for_scan) {
		completion_states[idx].reset(false, required_for_scan);
	}

	inline void reset() {
		for(auto& c: completion_states)
			c.reset(false, c.required_for_scan); //mark all as inactive
	}

	steady_time_t last_update_time;
};


/*
	internal state of blindscan is organised per satellite band (all data that can be tuned without changing diseqc
	settings, or switching to another tuner)
 */
struct blindscan_key_t {
	int16_t sat_pos{sat_pos_none};
	std::tuple<chdb::sat_band_t, devdb::fe_band_t> band{chdb::sat_band_t::Ku, devdb::fe_band_t::LOW};
	chdb::fe_polarisation_t pol;

	bool operator<(const blindscan_key_t& other) const;

	blindscan_key_t(int16_t sat_pos, chdb::fe_polarisation_t pol, uint32_t frequency);

	blindscan_key_t() = default;
};

//todo: extend to dvbc and dvbt
struct blindscan_t {
	/*
		spectrum_key is set only after spectral peaks have been found. In that case it is still possible
		that peaks.size() ==0 (empty spectrum)

		Scanning peaks is performed in two steps (first try to use mux parameters from database; then try blind),
		but this scanning is always done using the rf_path (spectrum_key.rf_path) used to scan the mux

		Therefore, peaks will always be scanned on the lnb/tuner on which they were located

	*/
	std::optional<statdb::spectrum_key_t> spectrum_key; //set after peaks to scan have been added to provide correct sat_pos
	ss::vector_<chdb::spectral_peak_t> peaks;
	bool operator<(const blindscan_key_t& other) const;

	bool spectrum_acquired() const {
		return  !!this->spectrum_key; //blindscan is valid if spectrum was acquired
	}
};


struct scan_subscription_t {
	subscription_id_t subscription_id;
	bool scan_start_reported{false};
	blindscan_key_t blindscan_key;
	chdb::spectral_peak_t peak;
	std::optional<chdb::any_mux_t> mux;
	bool is_peak_scan{false}; //true if we scan the peak rather than a corresponding mux in the db
	scan_subscription_t(subscription_id_t subscription_id) :
		subscription_id(subscription_id) {}
	scan_subscription_t(const scan_subscription_t& other) = default;
	scan_subscription_t(scan_subscription_t&& other) = default;
	scan_subscription_t& operator=(const scan_subscription_t& other) = default;
};


struct scan_stats_t
{
	int pending_peaks{0};
	int pending_muxes{0};
	int active_muxes{0};
	int finished_muxes{0}; //total number of muxes we tried to scan
	int failed_muxes{0}; //muxes which could not be locked
	int locked_muxes{0}; //mixes which locked
	int si_muxes{0}; //muxes with si data
	scan_stats_t() = default;
	friend bool operator == (const scan_stats_t&, const scan_stats_t&) = default;
};

struct scan_mux_end_report_t {
	statdb::spectrum_key_t spectrum_key;
	std::tuple<chdb::sat_band_t, devdb::fe_band_t> band{chdb::sat_band_t::Ku, devdb::fe_band_t::LOW};
	peak_to_scan_t peak;
	std::optional<chdb::any_mux_t> mux;
	devdb::fe_key_t fe_key;
	scan_mux_end_report_t() = default;
	scan_mux_end_report_t(const scan_subscription_t& subscription, const statdb::spectrum_key_t spectrum_key);
};


class scanner_t;
class subscriber_t;

class scan_t {
	friend class scanner_t;
	friend class receiver_thread_t;
	scanner_t& scanner;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	subscription_id_t scan_subscription_id;
	subscription_id_t monitored_subscription_id{-1};
	std::vector<tune_options_t> tune_options_; //indexexed by opt_id
	chdb::any_mux_t last_subscribed_mux;
	int max_num_subscriptions_for_retry{std::numeric_limits<int>::max()}; //maximum number of subscriptions that have been in use

	//TODO important: no fe_key_t should occur more than once in subscriptions
	std::map<subscription_id_t, scan_subscription_t> subscriptions;
	std::map<blindscan_key_t, blindscan_t> blindscans;
	int next_opt_id{0};

public:
	using stats_t = safe::Safe<scan_stats_t>;
	stats_t scan_stats;

private:
	inline chdb::scan_id_t make_scan_id(subscription_id_t scan_subscription_id,
																			const tune_options_t& tune_options) {
		chdb::scan_id_t ret;
		ret.subscription_id = (int32_t) scan_subscription_id;
		ret.pid = getpid();
		ret.opt_id = next_opt_id++;
		assert(ret.opt_id == (int)tune_options_.size());
		tune_options_.push_back(tune_options);
		return ret;
	}

	inline tune_options_t& tune_options_for_scan_id(chdb::scan_id_t scan_id) {
		assert(scan_id.opt_id >= 0 && scan_id.opt_id < (int)tune_options_.size());
		return tune_options_[scan_id.opt_id];
	}

	bool mux_is_being_scanned(const chdb::any_mux_t& mux);

	std::tuple<int, int> scan_loop(db_txn& chdb_rtxn, scan_subscription_t& subscription,
													 int& num_pending_muxes, int& num_pending_peaks,
													 const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux);


	std::tuple<int, int> on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
																			 subscription_id_t finished_subscription_id);

	subscription_id_t scan_try_mux(subscription_id_t reusable_subscription_id ,
																 scan_subscription_t& subscription, bool use_blind_tune);

	bool rescan_peak(blindscan_t& blindscan, subscription_id_t reusable_subscription_id,
									 scan_subscription_t& subscription);

	subscription_id_t scan_peak(db_txn& chdb_rtxn, blindscan_t& blindscan,
															subscription_id_t subscription_id, scan_subscription_t& subscription);
	template<typename mux_t>
	std::tuple<int, int, /*int, int,*/ subscription_id_t>
	scan_next(db_txn& chdb_rtxn, subscription_id_t finished_subscription_id,
						scan_subscription_t& subscription);

	subscription_id_t try_all_muxes(
		db_txn& chdb_rtxn, /*subscription_id_t finished_subscription_id,*/ scan_subscription_t& subscription,

///ffffffff
		const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
		//scan_subscription_t* finished_subscription_ptr,
		//const chdb::mux_key_t& finished_mux_key,
		int& num_pending_muxes, int& num_pending_peaks);

	bool retry_subscription_if_needed(subscription_id_t finished_subscription_id,
																		scan_subscription_t& subscription,
																		const chdb::any_mux_t& finished_mux);

public:
	scan_t(	scanner_t& scanner, std::optional<tune_options_t> tune_options, subscription_id_t scan_subscription_id);
	scan_t(const scan_t& other) = delete;
	scan_t(scan_t&& other)
		:	scanner (other.scanner)
		,	receiver_thread(other.receiver_thread)
		, receiver(other.receiver)
		, scan_id(other.scan_id)
		, tune_options(std::move(other.tune_options))
		, last_subscribed_mux (std::move(other.last_subscribed_mux))
		, subscriptions(std::move(other.subscriptions))
		, blindscans (std::move(other.blindscans))
			/*deliberately omitting scan_stats as that cannot be moved
				we need the move constructor try_emplace in scanner_t::scans
			 */
		{}

};

class scanner_t {
	friend class receiver_thread_t;
	friend class scan_t;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	time_t scan_start_time{-1};
	steady_time_t last_house_keeping_time{steady_clock_t::now()};
	int max_num_subscriptions{std::numeric_limits<int>::max()};
	chdb::any_mux_t last_subscribed_mux;
	bool must_end = false;
	tune_options_t tune_options{scan_target_t::SCAN_MINIMAL};
	std::map<subscription_id_t, scan_t> scans;
	template<typename mux_t>
	int add_muxes(const ss::vector_<mux_t>& muxes, const tune_options_t& tune_options,
								subscription_id_t subscription_id);

	int add_spectral_peaks(const statdb::spectrum_key_t& spectrum_key, const ss::vector_<chdb::spectral_peak_t>& peaks,
												 bool init, subscription_id_t subscription_id);

	int add_bands(const ss::vector_<chdb::sat_t>& sats,
								const ss::vector_<chdb::fe_polarisation_t>& pols,
								int32_t low_freq, int32_t high_freq,
								const tune_options_t& tune_options,
								subscription_id_t scan_subscription_id);

	bool unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
												db_txn& devdb_wtxn, subscription_id_t scan_subscription_id);

	bool on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& mux,
											 const chdb::scan_id_t& scan_id, subscription_id_t subscription_id);

	void on_spectrum_band_end(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscription_ids,
														const statdb::spectrum_t& spectrum);
	bool housekeeping(bool force);
	subscription_id_t scan_subscription_id_for_scan_id(const chdb::scan_id_t& scan_id);
public:
	scanner_t(receiver_thread_t& receiver_thread_, int max_num_subscriptions);
	using stats_t = safe::Safe<scan_stats_t>;
	~scanner_t();

	inline static bool is_our_scan(const chdb::scan_id_t& scan_id) {
		auto pid = getpid();
		return (scan_id.pid == pid) && (int) scan_id.subscription_id >= 0;
	}

	inline static int32_t scan_subscription_id(const chdb::scan_id_t& scan_id) {
		auto pid = getpid();
		return ((scan_id.pid) == pid) ? scan_id.subscription_id : -1;
	}


	void notify_signal_info(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscription_ids,
													const signal_info_t& signal_info);
	void notify_sdt_actual(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscription_ids,
												 const sdt_data_t& sdt_data);

};
