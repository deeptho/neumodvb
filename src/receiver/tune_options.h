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


enum class subscription_type_t {
	NORMAL, /*regular viewing: resourced are reserved non-exclusively. This means other lnbs on the same dish
						can be used by other subscriptions
					*/
	SCAN, /*scanning muxes in the background: resources are reserved non-exclusively, also reserved
					non-exclusively
					*/
	LNB_EXCLUSIVE,     /*in this case, a second subscriber cannot subscribe to the mux
						at first tune, position data is used from the lnb. Retunes cannot
						change the positioner and diseqc settings afterwards. Instead, the user
						must explicitly force them by a new tune call (diseqc swicthes), or by sending a
						positoner commands (usals, diseqc1.2)

						Also, lnb and dish are reserved exclusively, which means no other lnbs on the dish
						can be used on the same dish
					 */
	DISH_EXCLUSIVE /* To be used for secondary subscriptions which are under the control of a user with a DX
							subscription. E.g., this can be used to spectrum scan using multiple frontends ithout having to
							exclusively lock sat_pos, polband....
							implies all the power of LNB_EXCLUSIVE
						*/
	};

enum class scan_target_t :	int
{
	NONE, //keep current status or use default
	SCAN_MINIMAL, //NIT, SDT, PAT
	SCAN_FULL, //NIT, SDT, PAT, all PMTS
	SCAN_FULL_AND_EPG,      //epg scanning and table scanning
	DONE,
};

enum class tune_mode_t {
	IDLE,
	NORMAL,
	SPECTRUM,
	BLIND, //ask driver to scan blindly (not implemented)
#if 0
	MUX_BLIND,
#endif
	POSITIONER_CONTROL,
	UNCHANGED
	};

enum class retune_mode_t {
	AUTO, //for normal tuning: retune if lock failed or if wrong sat detected
	NEVER, //never retune
	IF_NOT_LOCKED,
	UNCHANGED
	};

struct pls_search_range_t {
	int start{-1};
	int end{-1};
	chdb::fe_pls_mode_t pls_mode;
	int timeoutms{25};
};

struct constellation_options_t {
	//bool get_constellation{false};
	int num_samples{0};
};


struct spectrum_scan_options_t {
	time_t start_time{};
	bool append{false}; //append to existing file
	int16_t sat_pos{sat_pos_none};
	devdb::fe_band_pol_t band_pol; //currently scanning band
	bool scan_both_polarisations{false}; //
	dtv_fe_spectrum_method spectrum_method{SPECTRUM_METHOD_FFT};
	int start_freq{0}; //in kHz
	int end_freq{std::numeric_limits<int>::max()}; //in kHz
	int resolution{0}; //in kHz for spectrum and for blindscan, 0 means: use driver default
	int fft_size{512}; //power of 2; 	int end_freq = -1; //in kHz
	spectrum_scan_options_t() {
		start_time = system_clock::to_time_t(now);
	}
};

struct tune_options_t {
	scan_target_t scan_target;
	tune_mode_t tune_mode;
	bool use_blind_tune{false};
	bool may_move_dish{true};
	bool propagate_scan{true};
	pls_search_range_t pls_search_range;
	retune_mode_t retune_mode{retune_mode_t::AUTO};

	int sat_pos{sat_pos_none}; /*only relevant if tune_mode ==tune_mode_t::LNB_EXCLUSIVE,
															 Its is used to switch the lnb to another sat during spectrum scan

															 For  tune_mode ==tune_mode_t::DISH_EXCLUSIVE (used during blindscan),
															 we rather rely on subscribe_mux, which has the desired sat_pos encoded
															 in the mux
														 */
	//only for spectrum acquisition
	spectrum_scan_options_t spectrum_scan_options;
	constellation_options_t constellation_options;

	//retune_mode_t retune_mode{retune_mode_t::ALLOWED}; //positioner not allowed when in positioner_dialog
	subscription_type_t subscription_type{subscription_type_t::NORMAL};

	explicit tune_options_t(scan_target_t scan_target =  scan_target_t::SCAN_FULL,
													 tune_mode_t tune_mode= tune_mode_t::NORMAL,
													subscription_type_t subscription_type = subscription_type_t::NORMAL)
		: scan_target(scan_target)
		, tune_mode(tune_mode)
		, subscription_type(subscription_type)
		{
		}
};
