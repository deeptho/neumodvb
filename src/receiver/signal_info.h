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

namespace chdb {

	struct signal_info_t {
		devdb::fe_key_t fe_key;
		int tune_attempt{0};
		any_mux_t driver_mux; /*contains only confirmed information, with information from driver
														overriding that from si stream. Missing information is filled in with
														confirmed information*/
		any_mux_t consolidated_mux; /*contains the most uuptodate information about the currently
																					tuned mux, including possible corrections received from the si
																					stream
																				*/
		std::optional<any_mux_t> bad_received_si_mux;
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
		int16_t matype{-1};
		ss::vector<int16_t, 8> isi_list;
		ss::vector<uint16_t, 256> matype_list; //size needs to be 256 in current implementation
		fe_status_t lock_status;
		ss::vector_<dtv_fe_constellation_sample> constellation_samples;

		signal_info_t() = default;

		signal_info_t(const devdb::fe_key_t& fe_key)
			: fe_key(fe_key) {
			stat.stats.resize(1);
		}

		~signal_info_t() {
			//printf("signal_info destroyed %p\n");
		}
	};

}
