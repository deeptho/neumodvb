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
	TUNE, /*regular viewing: resourced are reserved non-exclusively. This means other lnbs on the same dish
						can be used by other subscriptions
					*/
	MUX_SCAN,  /*scanning muxes in the background: resources are reserved non-exclusively, also reserved
							 non-exclusively
						 */
	SPECTRUM_BAND_SCAN,  /*scanning spectral band in the background: resources are reserved non-exclusively
									*/
	SPECTRUM_ACQ,  /*Spectrum acquisition from spectrum_dialog reserved exclusively
									*/
	LNB_CONTROL,     /*in this case, a second subscriber cannot subscribe to the mux
						at first tune, position data is used from the lnb. Retunes cannot
						change the positioner and diseqc settings afterwards. Instead, the user
						must explicitly force them by a new tune call (diseqc swicthes), or by sending a
						positoner commands (usals, diseqc1.2)

						Also, lnb and dish are reserved exclusively, which means no other lnbs on the dish
						can be used on the same dish
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
	chdb::fe_pls_mode_t pls_mode{};
	int timeoutms{25};
};

struct constellation_options_t {
	//bool get_constellation{false};
	int num_samples{0};
};


struct spectrum_scan_options_t {
	bool recompute_peaks{false}; //instead of relying on driver, compute the peak
	bool append{false}; //append to existing file
	int16_t sat_pos{sat_pos_none};
	chdb::sat_sub_band_pol_t band_pol; //currently scanning band
	bool use_fft_scan{true};
	int start_freq{0}; //in kHz
	int end_freq{std::numeric_limits<int>::max()}; //in kHz
	int resolution{0}; //in kHz for spectrum and for blindscan, 0 means use driver default
	int fft_size{512}; //power of 2; 	int end_freq = -1; //in kHz
	bool save_spectrum{true}; //save data to file
	spectrum_scan_options_t() {}
};

/*
	restrictions imposed by subscription, e.g., should lnb commands or dish motion commands be sent
 */
struct tune_pars_t {
	bool send_dish_commands{false};
	bool send_lnb_commands{false};
};

struct tune_options_t {
	std::optional<tune_pars_t> tune_pars; //this variable should only be set after subscribing

	scan_target_t scan_target;
	std::chrono::seconds max_scan_duration{180s}; /*after this time, scan will be forcefull ended*/

	std::optional<ss::vector_<int8_t>> allowed_dish_ids;
	std::optional<ss::vector_<int64_t>> allowed_card_mac_addresses;

	tune_mode_t tune_mode;
	bool need_blind_tune{false};
	bool may_move_dish{false}; /*is allowed to move dish when tuning and no other subscriptions conflict*/
	bool may_control_dish{false}; /*later, we may the subscription to a new dish position as we wish
																	(exclusive use)*/
	bool may_control_lnb{false}; /*later, we may the subscription to a new pol/band and send diseqc as we wish
																 (exclusive use)*/
	bool propagate_scan{true};
	bool need_spectrum{false};
	pls_search_range_t pls_search_range;
	retune_mode_t retune_mode{retune_mode_t::AUTO};
	//only for spectrum acquisition
	spectrum_scan_options_t spectrum_scan_options;
	constellation_options_t constellation_options;

	//retune_mode_t retune_mode{retune_mode_t::ALLOWED}; //positioner not allowed when in positioner_dialog
	subscription_type_t subscription_type{subscription_type_t::TUNE};
	devdb::usals_location_t usals_location;
	int resource_reuse_bonus{0};
	int dish_move_penalty{0};

	inline bool rf_path_is_allowed(const devdb::rf_path_t& rf_path) const {
		bool dish_matches = !allowed_dish_ids;
		bool card_matches = !allowed_card_mac_addresses;
		if(allowed_dish_ids)
			for(auto dish_id: *allowed_dish_ids) {
				dish_matches = rf_path.lnb.dish_id == dish_id;
				if(dish_matches)
					break;
			}
		if(allowed_card_mac_addresses)
			for(auto card_mac_address: *allowed_card_mac_addresses) {
				card_matches = rf_path.card_mac_address == card_mac_address;
				if(card_mac_address)
					break;
			}
		return card_matches && dish_matches;
	}

	tune_options_t(scan_target_t scan_target =  scan_target_t::SCAN_FULL,
								 tune_mode_t tune_mode= tune_mode_t::NORMAL,
								 subscription_type_t subscription_type = subscription_type_t::TUNE)
		: scan_target(scan_target)
		, tune_mode(tune_mode)
		, subscription_type(subscription_type)
		{
		}
};
