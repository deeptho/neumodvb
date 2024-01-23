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

#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/statdb/statdb_extra.h"
//#include "devmanager.h"

enum class confirmed_by_t {
		NONE,
		PAT,
		SDT,
		NIT,
		TIMEOUT,
		AUTO,
		FAKE
};


class dvb_frontend_t;

struct tune_confirmation_t {
	constexpr static  std::chrono::duration sat_pos_change_timeout = 15000ms; //in ms

	bool on_wrong_sat{false};
	bool unstable_sat{false}; //caused by dish motion; return needed
	confirmed_by_t sat_by{confirmed_by_t::NONE};
	confirmed_by_t ts_id_by{confirmed_by_t::NONE};
	confirmed_by_t network_id_by{confirmed_by_t::NONE};
	bool nit_actual_seen{false};
	bool nit_actual_received{false};
	bool sdt_actual_seen{false};
	bool sdt_actual_received{false};
	bool pat_seen{false};
	bool pat_received{false};
	bool si_done{false};
	void clear(bool preserve_wrong_sat) {
		*this = tune_confirmation_t();
	}

	tune_confirmation_t()
		{}
	bool operator== (const tune_confirmation_t& other)  const {
		return on_wrong_sat == other.on_wrong_sat &&
			//unstable_sat
			sat_by == other.sat_by &&
			ts_id_by == other.ts_id_by &&
			network_id_by == other.network_id_by &&
			nit_actual_seen == other.nit_actual_seen &&
			sdt_actual_seen == other.sdt_actual_seen &&
			pat_seen == other.pat_seen &&
			nit_actual_received == other.nit_actual_received &&
			sdt_actual_received == other.sdt_actual_received &&
			pat_received == other.pat_received &&
			si_done == other.si_done;
		}
	bool operator!= (const tune_confirmation_t& other)  const {
		return ! operator==(other);
	}
};

enum class fem_state_t {
	IDLE,                  //fe_monitor is not executing any tuning/spectral acq... command
	STARTED,               //fe_monitor is starting
	FAILED,               //fe_monitor has encountered a tuning error
	POSITIONER_MOVING,     //Positioner has started moving
	SEC_POWERED_UP,        //Sufficient time has passed for all diseqc devices and lnb to be powered up
	                       //and positioner to move, and tuning, spectral acq... has started
	MONITORING,            //ioctl returned, we are not monitorin
};

struct fe_lock_status_t {
	fem_state_t fem_state;
	int tune_time_ms; //
	bool lock_lost{false}; //
	fe_status_t fe_status{};
	int16_t matype{-1};
	//true if we detected this is not a dvbs transport stream
	inline bool is_locked() {
		return fe_status & FE_HAS_VITERBI;
	}

	inline chdb::lock_result_t tune_lock_result() {
		if(fe_status & FE_HAS_SYNC)
			return chdb::lock_result_t::SYNC;
		else if (fe_status & FE_HAS_VITERBI)
			return chdb::lock_result_t::FEC;
		else if (fe_status & FE_HAS_LOCK)
			return chdb::lock_result_t::CAR;
		else if (fe_status & FE_HAS_TIMING_LOCK)
			return chdb::lock_result_t::TMG;
		return chdb::lock_result_t::NOLOCK;
	}

	inline bool has_soft_tune_failure() {
		return fe_status & FE_OUT_OF_RESOURCES;
	}
	inline bool is_not_ts() {
		return is_locked() && matype >=0 && // otherwise we do not know matype yet
			matype != 256 && //dvbs
			(matype >> 6) != 3; //not a transport stream
		}
	inline bool is_dvb() {
		if(matype==-2) {
			//dvbt or dvbc
			return is_locked();
		}
		return is_locked() && matype >=0 && // otherwise we do not know matype yet
			(matype == 256 || //dvbs
			 (matype >> 6) == 3); //a transport stream
		}
};

struct signal_info_t {
	int tune_count{0}; //increases by 1 with every tune
	bool driver_data_reliable{true};
	dvb_frontend_t* fe{nullptr};
	devdb::fe_key_t fe_key;
	uint32_t uncorrected_driver_freq{0};
	chdb::any_mux_t driver_mux; /*contains only confirmed information, with information from driver
													overriding that from si stream. Missing information is filled in with
													confirmed information*/
	std::optional<chdb::any_mux_t> received_si_mux;
	bool received_si_mux_is_bad{false};
	int32_t bitrate{0};
	int32_t locktime_ms{0};
	statdb::signal_stat_t stat;
	inline statdb::signal_stat_entry_t& last_stat() {
		return stat.stats[stat.stats.size()-1];
	}

	inline const statdb::signal_stat_entry_t& last_stat() const {
		return stat.stats[stat.stats.size()-1];
	}

	tune_confirmation_t tune_confirmation;
	std::optional<int32_t> lnb_lof_offset; //most uptodate version
	//extra
	//int16_t matype{-1};
	ss::vector<int16_t, 8> isi_list;
	ss::vector<uint16_t, 256> matype_list; //size needs to be 256 in current implementation
	fe_lock_status_t lock_status;
	steady_time_t last_new_matype_time;
	ss::vector_<dtv_fe_constellation_sample> constellation_samples;
	signal_info_t() = default;

	signal_info_t(dvb_frontend_t* fe, const devdb::fe_key_t& fe_key)
		: fe(fe), fe_key(fe_key) {
		stat.stats.resize(1);
	}

	~signal_info_t() {
		//printf("signal_info destroyed %p\n");
	}
};

struct positioner_motion_report_t {
	devdb::dish_t dish;
	time_t start_time{-1};
	time_t end_time{-1};
};
