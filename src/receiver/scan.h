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
	std::tuple<chdb::sat_band_t, chdb::sat_sub_band_t> band{chdb::sat_band_t::UNKNOWN, chdb::sat_sub_band_t::NONE};
	chdb::fe_polarisation_t pol;

	bool operator<(const blindscan_key_t& other) const;

	blindscan_key_t(int16_t sat_pos, chdb::fe_polarisation_t pol, uint32_t frequency);
	blindscan_key_t(int16_t sat_pos, const chdb::band_scan_t& band_scan);

	blindscan_key_t() = default;
};

struct peak_to_scan_t {
	chdb::spectral_peak_t peak;
	chdb::scan_id_t scan_id;
	peak_to_scan_t() = default;
	peak_to_scan_t(const chdb::spectral_peak_t& peak, chdb::scan_id_t scan_id)
		: peak(peak)
		, scan_id(scan_id)
		{}

	inline bool is_present() const {
		return scan_id.subscription_id >=0;
	}
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
	ss::vector_<peak_to_scan_t> peaks;
	bool operator<(const blindscan_key_t& other) const;

	bool spectrum_acquired() const {
		return  !!this->spectrum_key; //blindscan is valid if spectrum was acquired
	}
};

struct scan_subscription_t {
	subscription_id_t subscription_id{subscription_id_t::NONE}; //positive if tuning in progress
	bool scan_start_reported{false};
	blindscan_key_t blindscan_key;
	peak_to_scan_t peak;
	std::optional<chdb::any_mux_t> mux;
	std::optional<std::tuple<chdb::sat_t, chdb::band_scan_t>> sat_band;
	bool is_peak_scan{false}; //true if we scan the peak rather than a corresponding mux in the db
	scan_subscription_t(subscription_id_t subscription_id)
		: subscription_id(subscription_id) {}
	scan_subscription_t(const scan_subscription_t& other) = default;
	scan_subscription_t(scan_subscription_t&& other) = default;
	scan_subscription_t& operator=(const scan_subscription_t& other) = default;
};


inline bool scan_stats_done(const devdb::scan_stats_t& ss) {
		return ss.pending_peaks + ss.pending_muxes  + ss.pending_bands + ss.active_muxes + ss.active_bands == 0;
}
inline void scan_stats_abort(devdb::scan_stats_t& ss) {
		ss.pending_peaks = 0;
		ss.pending_muxes = 0;
		ss.pending_bands = 0;
		ss.active_muxes = 0;
		ss.active_bands = 0;
}

struct scan_mux_end_report_t {
	statdb::spectrum_key_t spectrum_key;
	peak_to_scan_t peak;
	std::optional<chdb::any_mux_t> mux;
	devdb::fe_key_t fe_key;
	scan_mux_end_report_t() = default;
	scan_mux_end_report_t(const scan_subscription_t& subscription, const statdb::spectrum_key_t spectrum_key);
};

#ifdef TODO //not needed?
struct scan_band_end_report_t {
	statdb::spectrum_key_t spectrum_key;
	chdb::sat_t sat;
	chdb::band_scan_t band_scan;
	devdb::fe_key_t fe_key;
	scan_band_end_report_t() = default;
	scan_band_end_report_t(const scan_subscription_t& subscription, const statdb::spectrum_key_t spectrum_key);
};
#endif

class scanner_t;
class subscriber_t;
using ssptr_t = std::shared_ptr<subscriber_t>;

class scan_t {
	friend class scanner_t;
	friend class receiver_thread_t;
	scanner_t& scanner;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	subscription_id_t scan_subscription_id;

	subscription_id_t monitored_subscription_id{-1};
	steady_time_t monitor_time{}; //time when we displayed new info for monitor_su
	signal_info_t monitor_signal_info{}; //last displayed montiored signal_info

	std::vector<subscription_options_t> tune_options_; //indexexed by opt_id
	int max_num_subscriptions_for_retry{std::numeric_limits<int>::max()}; //maximum number of subscriptions that have been in use

	//TODO important: no fe_key_t should occur more than once in subscriptions
	std::map<subscription_id_t, scan_subscription_t> subscriptions;
	std::map<blindscan_key_t, blindscan_t> blindscans;
	int next_opt_id{0};
	devdb::scan_stats_t scan_stats_dvbs;
	devdb::scan_stats_t scan_stats_dvbc;
	devdb::scan_stats_t scan_stats_dvbt;
	inline devdb::scan_stats_t get_scan_stats() const;

	template<typename mux_t>
	requires (! is_same_type_v<chdb::sat_t, mux_t>)
	inline devdb::scan_stats_t& get_scan_stats_ref(const mux_t& mux);
	inline devdb::scan_stats_t& get_scan_stats_ref(int16_t sat_pos);
	inline devdb::scan_stats_t& get_scan_stats_ref(const chdb::sat_t& sat);

private:
	void update_monitor(const ss::vector_<subscription_id_t>& fe_subscription_ids,
											const signal_info_t& signal_info);

	inline chdb::scan_id_t make_scan_id(subscription_id_t scan_subscription_id,
																			const subscription_options_t& tune_options) {
		chdb::scan_id_t ret;
		assert(tune_options.subscription_type != devdb::subscription_type_t::TUNE);
		ret.subscription_id = (int32_t) scan_subscription_id;
		ret.pid = getpid();
		ret.opt_id = next_opt_id++;
		assert(ret.opt_id == (int)tune_options_.size());
		tune_options_.push_back(tune_options);
		return ret;
	}

	inline subscription_options_t& tune_options_for_scan_id(chdb::scan_id_t scan_id) {
		assert(scan_id.opt_id >= 0 && scan_id.opt_id < (int)tune_options_.size());
		return tune_options_[scan_id.opt_id];
	}
#if 0
	void update_db_sat(const chdb::sat_t& sat, const chdb::band_scan_t& band_scan);
#endif
	bool mux_is_being_scanned(const chdb::any_mux_t& mux);
	bool band_is_being_scanned(const chdb::band_scan_t& band_scan);


	void scan_loop(db_txn& chdb_rtxn, scan_subscription_t& subscription,
								 const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux);

	void scan_loop(db_txn& chdb_rtxn, scan_subscription_t& subscription,
								 const devdb::fe_t& finished_fe, const chdb::sat_t& finished_sat);



	void on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
											 const ssptr_t finished_ssptr);

	void on_spectrum_scan_band_end(const devdb::fe_t& finished_fe, const spectrum_scan_t& spectrum_scan,
																 const ssptr_t finished_ssptr);

	template <typename mux_t>
	std::tuple<ssptr_t, scan_subscription_t*, bool>
	scan_try_mux(ssptr_t reusable_ssptr,
							 const mux_t& mux, bool use_blind_tune, const blindscan_key_t& blindscan_key);

	inline std::tuple<ssptr_t, scan_subscription_t*, bool>
	scan_try_mux(ssptr_t reusable_ssptr,
							 const chdb::any_mux_t& mux, bool use_blind_tune) {
		return std::visit([&](auto&mux) {
			return scan_try_mux(reusable_ssptr, mux, use_blind_tune);
		}, mux);
	}

	std::tuple<ssptr_t, scan_subscription_t*, bool>
	scan_try_band(ssptr_t reuseable_ssptr,
								const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
								const blindscan_key_t& blindscan_key, devdb::scan_stats_t& scan_stats);

	template<typename mux_t>
	bool rescan_peak(const blindscan_t& blindscan, ssptr_t reusable_ssptr,
									 mux_t& mux, const peak_to_scan_t& peak, const blindscan_key_t& blindscan_key);

	template<typename mux_t>
	std::tuple<bool, bool>
	scan_try_peak(db_txn& chdb_rtxn, blindscan_t& blindscan,
						ssptr_t reusable_ssptr, const peak_to_scan_t& peak,
						const blindscan_key_t& blindscan_key);

	ssptr_t
	scan_next_peaks(db_txn& chdb_rtxn,
									ssptr_t reuseable_ssptr,
									std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats);

	template<typename mux_t>
	ssptr_t
	scan_next_muxes(db_txn& chdb_rtxn,
									ssptr_t reuseable_ssptr,
									std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats);

	ssptr_t
	scan_next_bands(db_txn& chdb_rtxn,
									ssptr_t reuseable_ssptr,
									std::map<blindscan_key_t, bool>& skip_map, devdb::scan_stats_t& scan_stats);

	template<typename mux_t>
	ssptr_t
	scan_next(db_txn& chdb_rtxn, ssptr_t finished_ssptr, devdb::scan_stats_t& scan_stats);

	bool retry_subscription_if_needed(ssptr_t finished_ssptr,
																		scan_subscription_t& subscription,
																		const chdb::any_mux_t& finished_mux,
																		const blindscan_key_t& blindscan_key);

	void housekeeping(bool force);

public:
	scan_t(	scanner_t& scanner, subscription_id_t scan_subscription_id);
	scan_t(const scan_t& other) = delete;
	scan_t(scan_t&& other) = delete;
};

class scanner_t {
	friend class receiver_thread_t;
	friend class scan_t;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	time_t scan_start_time{-1};
	steady_time_t last_house_keeping_time{steady_clock_t::now()};
	int max_num_subscriptions{std::numeric_limits<int>::max()};
	bool must_end = false;
	subscription_options_t tune_options{devdb::scan_target_t::SCAN_MINIMAL};
	std::map<subscription_id_t, scan_t> scans;

	void end_scans(subscription_id_t scan_subscription_id);
	template<typename mux_t>
	int add_muxes(const ss::vector_<mux_t>& muxes, const subscription_options_t& tune_options,
								ssptr_t ssptr);

	template<typename peak_t>
	int add_spectral_peaks(const devdb::rf_path_t& rf_path,
												 const statdb::spectrum_key_t& spectrum_key, const ss::vector_<peak_t>& peaks,
												 ssptr_t ssptr, subscription_options_t* options=nullptr);

	int add_bands(const ss::vector_<chdb::sat_t>& sats,
								const ss::vector_<chdb::fe_polarisation_t>& pols,
								const subscription_options_t& tune_options,
								ssptr_t ssptr);

	bool unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
												db_txn& devdb_wtxn, ssptr_t scan_ssptr);

	bool on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& mux,
											 const chdb::scan_id_t& scan_id, const ssptr_t finished_ssptr);

	bool on_spectrum_scan_band_end(const devdb::fe_t& finished_fe, const spectrum_scan_t& spectrum_scan,
																 const chdb::scan_id_t& scan_id,
																 const ss::vector_<subscription_id_t>& subscription_ids);
	bool housekeeping(bool force);
	subscription_id_t scan_subscription_id_for_scan_id(const chdb::scan_id_t& scan_id);

public:
	scanner_t(receiver_thread_t& receiver_thread_, int max_num_subscriptions);

	~scanner_t();

	inline static bool is_our_scan(const chdb::scan_id_t& scan_id) {
		auto pid = getpid();
		return (scan_id.pid == pid) && (int) scan_id.subscription_id >= 0;
	}


	inline static bool is_scanning(const chdb::scan_id_t& scan_id) {
		return scan_id.pid > 0 || (int) scan_id.subscription_id >= 0;
	}

	inline static int32_t scan_subscription_id(const chdb::scan_id_t& scan_id) {
		auto pid = getpid();
		return ((scan_id.pid) == pid) ? scan_id.subscription_id : -1;
	}


	void on_signal_info(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscription_ids,
													const signal_info_t& signal_info);
	void on_sdt_actual(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscription_ids,
												 const sdt_data_t& sdt_data);

};
