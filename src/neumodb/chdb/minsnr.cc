/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 *
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

#include "util/dtassert.h"
#include "receiver/neumofrontend.h"
#include "neumodb/chdb/chdb_extra.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

template<> float chdb::min_snr<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux) {
	return 0;
}

template<> float chdb::min_snr<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux) {
	return 0;
}

template <> float chdb::min_snr<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux) {
	switch (mux.delivery_system) {
	case chdb::fe_delsys_dvbs_t::SYS_DVBS2:
		switch (mux.modulation) {
		case chdb::fe_modulation_t::QPSK:
			switch (mux.fec) {
			case chdb::fe_code_rate_t::FEC_1_4:
				return -2.4;
			case chdb::fe_code_rate_t::FEC_1_3:
				return -1.2;
			case chdb::fe_code_rate_t::FEC_2_5:
				return 0;
			case chdb::fe_code_rate_t::FEC_1_2:
				return 1;
			case chdb::fe_code_rate_t::FEC_3_5:
				return 2.2;
			case chdb::fe_code_rate_t::FEC_2_3:
				return 3.1;
			case chdb::fe_code_rate_t::FEC_3_4:
				return 4;
			case chdb::fe_code_rate_t::FEC_4_5:
				return 4.6;
			case chdb::fe_code_rate_t::FEC_5_6:
				return 5.2;
			case chdb::fe_code_rate_t::FEC_8_9:
				return 6.2;
			case chdb::fe_code_rate_t::FEC_9_10:
				return 6.5;
			default:
				return 0.;
			}
			break;
		case chdb::fe_modulation_t::PSK_8:
			switch (mux.fec) {
			case chdb::fe_code_rate_t::FEC_3_5:
				return 5.5;
			case chdb::fe_code_rate_t::FEC_2_3:
				return 6.6;
			case chdb::fe_code_rate_t::FEC_3_4:
				return 7.9;
			case chdb::fe_code_rate_t::FEC_5_6:
				return 9.4;
			case chdb::fe_code_rate_t::FEC_8_9:
				return 10.6;
			case chdb::fe_code_rate_t::FEC_9_10:
				return 11;
			default:
				return 0.;
			}
			break;
		case chdb::fe_modulation_t::APSK_16:
			switch (mux.fec) {
			case chdb::fe_code_rate_t::FEC_2_3:
				return 9;
			case chdb::fe_code_rate_t::FEC_3_4:
				return 10.2;
			case chdb::fe_code_rate_t::FEC_4_5:
				return 11;
			case chdb::fe_code_rate_t::FEC_5_6:
				return 11.6;
			case chdb::fe_code_rate_t::FEC_8_9:
				return 12.9;
			case chdb::fe_code_rate_t::FEC_9_10:
				return 13.1;
			default:
				return 0.;
			}
			break;

		case chdb::fe_modulation_t::APSK_32:
			switch (mux.fec) {
			case chdb::fe_code_rate_t::FEC_3_4:
				return 12.6;
			case chdb::fe_code_rate_t::FEC_4_5:
				return 13.6;
			case chdb::fe_code_rate_t::FEC_5_6:
				return 14.3;
			case chdb::fe_code_rate_t::FEC_8_9:
				return 15.7;
			case chdb::fe_code_rate_t::FEC_9_10:
				return 16.1;
			default:
				return 0.;
			}
			break;

		default:
			break;
		};
		break;
	case chdb::fe_delsys_dvbs_t::SYS_DVBS:
		switch (mux.fec) {
		case chdb::fe_code_rate_t::FEC_1_2:
			return 2.7;
		case chdb::fe_code_rate_t::FEC_2_3:
			return 4.4;
		case chdb::fe_code_rate_t::FEC_3_4:
			return 5.5;
		case chdb::fe_code_rate_t::FEC_5_6:
			return 6.5;
		case chdb::fe_code_rate_t::FEC_7_8:
			return 7.2;
		default:
			return 0.;
		}
		break;
	default:
		return 0.0;
	}
	return 0.0;
}

float chdb::min_snr(const chdb::any_mux_t& mux) {
	using namespace chdb;
	float ret{0.0};
	visit_variant(
		mux, [&](const chdb::dvbs_mux_t& mux) { ret = min_snr(mux); },
		[&](const chdb::dvbc_mux_t& mux) { ret = min_snr(mux); },
		[&](const chdb::dvbt_mux_t& mux) { ret = min_snr(mux); });
	return ret;
}
