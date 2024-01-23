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
#include "neumodb/epgdb/epgdb_extra.h"
#include "util/logger.h"


#include "neumofrontend.h"
#ifndef null_pid
#define null_pid (0x1fff)
#endif

struct PACKED tuner_stats_t {
	fe_status_t fe_status{}; //current frontend status
	dvb_frontend_parameters fe_parameters{};

	const double snr_conversion_factor = 0.001;
	const double signal_conversion_factor = 0.001;
	fecap_scale_params sig_scale = FE_SCALE_RELATIVE;
	uint32_t sig_value =0;

	fecap_scale_params snr_scale = FE_SCALE_RELATIVE;
	uint32_t snr =0;


	uint32_t pre_error_bit_count = 0;
	uint32_t pre_total_bit_count = 0;
	uint32_t post_error_bit_count = 0;
	uint32_t post_total_bit_count = 0;
	uint32_t error_block_count = 0;
	uint32_t total_block_count = 0;

	double get_snr() const {
		return double(snr) *snr_conversion_factor;
	}

	double get_signal() const {
		return double(sig_value) *signal_conversion_factor;
	}

	uint32_t& strength;
	uint32_t& ber;

 tuner_stats_t() : strength(sig_value), ber(pre_total_bit_count)
	{}

};
