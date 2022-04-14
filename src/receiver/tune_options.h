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


enum class subscription_type_t {
	NORMAL, /*regular viewing: an lnb is reserved non-exclusively, polband and sat_posh are also reserved
						non-exlcusively. This mans other lnbs on the same dish can be used by other subscriptions,
						and slave demods are allowed as well
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


struct tune_options_t {
	scan_target_t scan_target;
	tune_mode_t tune_mode;
	bool use_blind_tune{false};
	bool may_move_dish{true};
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
