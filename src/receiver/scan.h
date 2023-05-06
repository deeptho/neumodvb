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
#include "tune_options.h"

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

	ss::vector<std::tuple<uint32_t, subscription_id_t>,2> scans_in_progress; //indexed by scan_id

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
	inline bool scan_completed() const {
		for(auto& c: completion_states)
			if(!c.completed && !c.notpresent() && c.required_for_scan)
				return false;
		return true;
	}

	inline int scan_duration() const {
		auto start = steady_time_t::max();
		auto end = steady_time_t::min();
		for(auto& c: completion_states) {
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




struct blindscan_key_t {
	int16_t sat_pos{sat_pos_none};
	int8_t band{0};
	chdb::fe_polarisation_t pol;

	bool operator<(const blindscan_key_t& other) const;

	blindscan_key_t(int16_t sat_pos, chdb::fe_polarisation_t pol, uint32_t frequency);

	blindscan_key_t() = default;
};

//todo: extend to dvbc and dvbt
struct blindscan_t {
	statdb::spectrum_key_t spectrum_key;
	ss::vector_<chdb::spectral_peak_t> peaks;
	bool operator<(const blindscan_key_t& other) const;

	bool valid() const {
		return  this->spectrum_key.sat_pos != sat_pos_none;
	}
};


struct scan_subscription_t {
	bool scan_start_reported{false};
	blindscan_key_t blindscan_key;
	chdb::spectral_peak_t peak;
	std::optional<chdb::any_mux_t> mux;
	devdb::fe_key_t fe_key;
	bool is_peak_scan{false}; //true if we scan the peak rather than a corresponding mux in the db
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
};

struct scan_report_t {
	statdb::spectrum_key_t spectrum_key;
	int band;
	chdb::spectral_peak_t peak;
	std::optional<chdb::any_mux_t> mux;
	devdb::fe_key_t fe_key;
	scan_stats_t scan_stats;
	scan_report_t() = default;
	scan_report_t(const scan_subscription_t& subscription, const statdb::spectrum_key_t spectrum_key,
								const scan_stats_t & scan_stats);
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
	uint32_t scan_id;
	subscription_id_t monitored_subscription_id{-1};
	tune_options_t tune_options{scan_target_t::SCAN_MINIMAL};
	chdb::any_mux_t last_subscribed_mux;
	int max_num_subscriptions_for_retry{std::numeric_limits<int>::max()}; //maximum number of subscriptions that have been in use

	//TODO important: no fe_key_t should occur more than once in subscriptions
	std::map<subscription_id_t, scan_subscription_t> subscriptions;
	std::map<blindscan_key_t, blindscan_t> blindscans;

public:
	using stats_t = safe::Safe<scan_stats_t>;
	stats_t scan_stats;

private:
	bool mux_is_being_scanned(const chdb::any_mux_t& mux);
	std::tuple<int, int> scan_loop(const devdb::fe_t& finished_fe, const chdb::any_mux_t& finished_mux,
																 subscription_id_t finished_subscription_id);

	std::tuple<subscription_id_t, subscription_id_t>
	scan_try_mux(subscription_id_t reusable_subscription_id ,
							 scan_subscription_t& subscription,
							 const devdb::rf_path_t* required_rf_path,
							 bool use_blind_tune);

	bool rescan_peak(blindscan_t& blindscan, subscription_id_t reusable_subscription_id,
									 scan_subscription_t& subscription);

	subscription_id_t scan_peak(db_txn& chdb_rtxn, blindscan_t& blindscan,
															subscription_id_t subscription_id, scan_subscription_t& subscription);
	template<typename mux_t>
	std::tuple<int, int, int, int, subscription_id_t>
	scan_next(db_txn& chdb_rtxn, subscription_id_t finished_subscription_id,
						scan_subscription_t& subscription);

	bool finish_subscription(db_txn& rtxn,  subscription_id_t finished_subscription_id,
																					 scan_subscription_t& subscription,
																					 const chdb::any_mux_t& finished_mux);
	void add_completed(const devdb::fe_t& fe, const chdb::any_mux_t& mux, int num_pending_muxes, int num_pending_peaks);

public:
	scan_t(	scanner_t& scanner, subscription_id_t scan_subscription_id, bool propagate_scan);
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
	bool scan_found_muxes;
	chdb::any_mux_t last_subscribed_mux;
	int id{0};
	bool must_end = false;
	tune_options_t tune_options{scan_target_t::SCAN_MINIMAL};

	ss::vector<devdb::lnb_t, 16> allowed_lnbs;

	std::map<subscription_id_t, scan_t> scans;

	void set_allowed_lnbs(const ss::vector_<devdb::lnb_t>& lnbs);
	void set_allowed_lnbs();

	template<typename mux_t>
	int add_muxes(const ss::vector_<mux_t>& muxes, bool init, subscription_id_t subscription_id);

	int add_peaks(const statdb::spectrum_key_t& spectrum_key, const ss::vector_<chdb::spectral_peak_t>& peaks,
								bool init, subscription_id_t subscription_id);
	void unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
												db_txn& devdb_wtxn, subscription_id_t scan_subscription_id);

	bool on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& mux,
											 uint32_t scan_id, subscription_id_t subscription_id);

	bool housekeeping(bool force);

	void init();
//	void clear_peaks(const chdb::dvbs_mux_t& finished_mux);

	inline static uint32_t make_scan_id(subscription_id_t subscription_id) {
		return (getpid() <<8)| (int) subscription_id;
	}

	subscription_id_t scan_subscription_id_for_scan_id(uint32_t scan_id);
public:
	scanner_t(receiver_thread_t& receiver_thread_,
						//ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<devdb::lnb_t>* lnbs,
						bool scan_found_muxes, int max_num_subscriptions);
	using stats_t = safe::Safe<scan_stats_t>;
	~scanner_t();
	void notify_signal_info(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscriptions,
													const signal_info_t& signal_info);
	void notify_sdt_actual(const subscriber_t& subscriber, const ss::vector_<subscription_id_t>& subscriptions,
												 const sdt_data_t& sdt_data, dvb_frontend_t* fe);

};
