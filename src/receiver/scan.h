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

#pragma once
#include <boost/context/continuation_fcontext.hpp>
#include "neumodb/chdb/chdb_extra.h"
#include "tune_options.h"

#include <set>

class receiver_thread_t;
class tuner_thread_t;
class receiver_t;
class active_adapter_t;


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

		inline bool notpresent() const {
			const std::chrono::seconds timeout{20}; //seconds
			return (last_active == steady_time_t()) && (steady_clock_t::now() - start_time) > timeout;
		}

		inline bool done() const {
			return !required_for_scan || completed || timedout || notpresent();
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
	bool locked{false};
	bool aborted{false};
};


struct scan_stats_t
{
	int new_muxes{0};
	int updated_muxes{0};
	int failed_muxes{0};
	int scheduled_muxes{0};
	int finished_muxes{0};
	chdb::any_mux_t last_scanned_mux{};
};


class scanner_t {
	friend class receiver_thread_t;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	int subscription_id{-1};
	int max_num_subscriptions{std::numeric_limits<int>::max()};
	bool scan_found_muxes;
	int id{0};
	bool must_end = false;
	tune_options_t tune_options{scan_target_t::SCAN_MINIMAL};
	bool scan_dvbs{false};
	bool scan_dvbc{false};
	bool scan_dvbt{false};

	ss::vector<chdb::lnb_t, 16> allowed_lnbs;
	ss::vector<int, 16> subscriptions;
	int64_t last_seen_txn_id{-1};
	chdb::chdb_t done_db; //database used for remember what has been scanned already

	template<typename mux_t>
	bool add_mux(db_txn& done_txn, mux_t& mux, bool as_completed);

	void set_allowed_lnbs(const ss::vector_<chdb::lnb_t>& lnbs);
	void set_allowed_lnbs();

	int add_initial_muxes(ss::vector_<chdb::dvbs_mux_t>& muxes);
	int add_initial_muxes(ss::vector_<chdb::dvbc_mux_t>& muxes);
	int add_initial_muxes(ss::vector_<chdb::dvbt_mux_t>& muxes);
	//bool has_been_scanned(db_txn& done_txn, chdb::dvbs_mux_t& mux);
	int scan_loop(const active_adapter_t* active_adapter_p, const chdb::any_mux_t* finished_mux);

	void start();
	int housekeeping(const active_adapter_t* active_adapter_p = nullptr, const chdb::any_mux_t* finished_mux = nullptr);

	template<typename mux_t>
	int add_new_muxes_(db_txn& done_txn);

	int add_new_muxes(db_txn& done_txn);


public:
	scanner_t(receiver_thread_t& receiver_thread_,
						//ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<chdb::lnb_t>* lnbs,
						bool scan_found_muxes, int max_num_subscriptions,
						int subscription_id);

	using stats_t = safe::Safe<scan_stats_t>;
	stats_t  scan_stats;
	~scanner_t();
};
