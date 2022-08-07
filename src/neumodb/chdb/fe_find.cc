/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

#include "neumodb/chdb/chdb_extra.h"
#include "receiver/neumofrontend.h"
#include "stackstring/ssaccu.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace chdb;

int fe::lnb_subscription_count(chdb_txn& rtx, const chdb::lnb_key_t& lnb_key) {
	int ret{0};
	auto c = fe_t::find_by_card_rf_input(rtxn, lnb.k.card_mac_address, lnb.k.rf_input);
	for(const auto& fe: c.range()) {
		ret += (fe.sub.lnb_key == lnb_key);
	}
	return ret;
}

std::optional<chdb::fe_t> fe::find_fe_for_lnb(db_txn& rtxn, const chdb::lnb_t& lnb,
																							int32_t  adapter_mac_address_to_release,
																							bool need_blindscan, bool need_spectrum) {
	auto c = fe_t::find_by_card_rf_input(rtxn, lnb.k.card_mac_address, lnb.k.rf_input);

	best_fe.priority = std::numeric_limits<decltype best_fe.priority>::lowest();


	auto can_be_subscribed = [&](const fe_t& fe) {
		if(fe.adapter_mac_address == adapter_mac_address_to_release && adapter_mac_address_to_release != -1) {
			if( fe::lnb_subscriber_count(lnb.k) > 1)
				return false; //after releasing adapter_to_release, we would still have no controll over the lnb
		} else {
			if( fe::lnb_subscriber_count(rtxn, lnb.k) > 0)
				return false; //lnb is in use
		}

		if(fe::rotor_subscriber_count(lnb.k.dish_id, knb.usals_sat_pos) > 1) {
			return false; //another lnb is using the same rotating dish
		}
		return true;
	}

	for(const auto& fe: c.range()) {
		if(!fe.available)
			continue;

		if(need_blindscan && !fe.supports.blindscan)
			continue;

		if(need_spectrum) {
			if(fe.supports.spectrum_fft) {
				if(!best_fe.supports.spectrum_fft) {
					if(can_be_subscribed(fe))
						best_fe = fe;
					continue;
				}
			} else { //!fe.supports.spectrum_fft
				if(best_fe.supports.spectrum_fft)
					continue; //fft is better
				if(!fe.supports.spectrum_sweep)
					continue; //not suitable
			}
		}
		//fe is suitable and available

		if(fe.priority > best_prio) {
			if(can_be_subscribed(fe)) {
				best_fe  = fe;
			}
			continue;
		}
	}
	if(best_fe.priority  == std::numeric_limits<decltype best_fe.priority>::lowest())
		return {};
	return best_fe;
}
