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
	chdb::sat_t sat;
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
	std::optional<devdb::dish_t> dish;
	bool move_dish{false};
	bool send_lnb_commands{false};
	int owner{-1};
	int config_id{-1};
};

struct subscription_options_t : public devdb::tune_options_t {
	std::optional<tune_pars_t> tune_pars; //this variable should only be set after subscribing
	pls_search_range_t pls_search_range;

	//only for spectrum acquisition
	spectrum_scan_options_t spectrum_scan_options;
	constellation_options_t constellation_options;

	devdb::usals_location_t usals_location;

	inline bool rf_path_is_allowed(const devdb::rf_path_t& rf_path) const {
		bool dish_matches = !allowed_dish_ids;
		bool card_matches = !allowed_card_mac_addresses;
		bool rf_path_matches = !allowed_rf_paths;
		if(allowed_rf_paths)
			for(auto& rfp: *allowed_rf_paths) {
				rf_path_matches = rf_path == rfp;
				if(rf_path_matches)
					break;
			}
		if(!rf_path_matches)
			return false;

		if(allowed_dish_ids)
			for(auto dish_id: *allowed_dish_ids) {
				dish_matches = rf_path.lnb.dish_id == dish_id;
				if(dish_matches)
					break;
			}
		if(!dish_matches)
			return false;

		if(allowed_card_mac_addresses)
			for(auto card_mac_address: *allowed_card_mac_addresses) {
				card_matches = rf_path.card_mac_address == card_mac_address;
				if(card_matches)
					break;
			}
		return card_matches;
	}

 	subscription_options_t(devdb::scan_target_t scan_target =  devdb::scan_target_t::SCAN_FULL,
												 devdb::subscription_type_t subscription_type = devdb::subscription_type_t::TUNE)
		{
			this->subscription_type = subscription_type;
			this->scan_target = scan_target;
		}

	subscription_options_t& operator=(const subscription_options_t& other) = default;
 	subscription_options_t(const subscription_options_t& other) = default;
};
