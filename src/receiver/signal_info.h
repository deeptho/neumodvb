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
//#include "adapter.h"

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
	bool nit_actual_ok{false};
	bool sdt_actual_seen{false};
	bool sdt_actual_ok{false};
	bool pat_seen{false};
	bool pat_ok{false};
	bool si_done{false};
	void clear(bool preserve_wrong_sat) {
		*this = tune_confirmation_t();
	}

	tune_confirmation_t()
		{}
	bool operator== (const tune_confirmation_t& other)  const {
		return on_wrong_sat == other.on_wrong_sat &&
			sat_by == other.sat_by &&
			ts_id_by == other.ts_id_by &&
			network_id_by == other.network_id_by &&
			si_done == other.si_done &&
			nit_actual_ok == other.nit_actual_ok &&
			sdt_actual_ok == other.sdt_actual_ok &&
			pat_ok == other.pat_ok;
		}
	bool operator!= (const tune_confirmation_t& other)  const {
		return ! operator==(other);
	}
};

namespace chdb {

	struct signal_info_t {
		int tune_attempt{0};
		any_mux_t mux;
		std::optional<any_mux_t> si_mux; //as retrieved from stream
		int32_t bitrate{0};
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
		fe_status_t lock_status;
		ss::vector_<dtv_fe_constellation_sample> constellation_samples;

		signal_info_t()  {
			stat.stats.resize(1);
		}

		~signal_info_t() {
			//printf("signal_info destroyed %p\n");
		}
	};

}
