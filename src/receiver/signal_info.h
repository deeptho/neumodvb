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
#include "adapter.h"

namespace chdb {
	struct signal_info_t {
		int tune_attempt{0};
		any_mux_t mux;
		std::optional<any_mux_t> si_mux; //as retrieved from stream

		statdb::signal_stat_t stat;

		tune_confirmation_t tune_confirmation;
		std::optional<int32_t> lnb_lof_offset; //most uptodate version
		//extra
		uint8_t matype{0};
		ss::vector<int16_t, 8> isi_list;
		fe_status_t lock_status;

		ss::vector_<dtv_fe_constellation_sample> constellation_samples;
		~signal_info_t() {
			//printf("signal_info destroyed %p\n");
		}
	};
}
