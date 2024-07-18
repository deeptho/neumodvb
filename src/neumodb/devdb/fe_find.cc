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

#include "neumodb/chdb/chdb_extra.h"
#include "receiver/neumofrontend.h"
#include "devdb_private.h"
#include "util/dtassert.h"
#include <iomanip>
#include <iostream>
#include <signal.h>

#include "../util/neumovariant.h"

using namespace devdb;

std::optional<devdb::fe_t> fe::find_best_fe_for_dvtdbc(
	db_txn& rtxn, const devdb::fe_key_t* fe_key_to_release,
	bool need_blind_tune, bool need_spectrum, bool need_multistream,
	chdb::delsys_type_t delsys_type, bool ignore_subscriptions) {
	bool need_dvbt = delsys_type == chdb::delsys_type_t::DVB_T;
	bool need_dvbc = delsys_type == chdb::delsys_type_t::DVB_C;
	auto c = devdb::find_first<devdb::fe_t>(rtxn);

	fe_t best_fe{}; //the fe that we will use
	best_fe.priority = std::numeric_limits<decltype(best_fe.priority)>::lowest();

	auto no_best_fe_yet = [&best_fe]()
		{ return best_fe.priority == std::numeric_limits<decltype(best_fe.priority)>::lowest(); };
	for(const auto& fe: c.range()) {
		if (need_dvbc && (!fe.enable_dvbc || !fe::supports_delsys_type(fe, chdb::delsys_type_t::DVB_C)))
			continue;
		if (need_dvbt && (!fe.enable_dvbt || !fe::supports_delsys_type(fe, chdb::delsys_type_t::DVB_T)))
			continue;
		bool is_subscribed = ignore_subscriptions ? false: fe::is_subscribed(fe);
		bool is_our_subscription = (ignore_subscriptions || fe.sub.subs.size()>1) ? false
			: (fe_key_to_release && fe.k == *fe_key_to_release);
		if(!is_subscribed  || is_our_subscription) {
			/* we found an fe that might work*/
			if(!fe.present || ! fe.can_be_used)
				continue; 			/* we can use this frontend only if it can be connected to the proper input*/

			//this fe is one that we can potentially use

			if(need_blind_tune && !fe.supports.blindscan)
				continue;
			if(need_multistream && !fe.supports.multistream)
				continue;

			if(need_spectrum) {
				assert (no_best_fe_yet() || best_fe.supports.spectrum_fft || best_fe.supports.spectrum_sweep);

				if(fe.supports.spectrum_fft) { //best choice
					if(no_best_fe_yet() ||
						 !best_fe.supports.spectrum_fft || //fft is better
						 fe.priority > best_fe.priority)
						best_fe = fe;
				} else if (fe.supports.spectrum_sweep) { //second best choice
					if( no_best_fe_yet() ||
							( !best_fe.supports.spectrum_fft && //best_fe with fft beats fe without fft
							 fe.priority > best_fe.priority ))
						best_fe = fe;
				} else {
         //no spectrum support at all -> not useable
				}
			} else { /* if !need_spectrum; in this case fe's with and without fft support can be
									used, but we prefer one without fft, to keep fft-hardware available for other
									subscriptions*/
				if(no_best_fe_yet() ||
					 (best_fe.supports.spectrum_fft && !fe.supports.spectrum_fft) || //prefer non-fft
					 (best_fe.supports.spectrum_sweep && !fe.supports.spectrum_fft
						&& !fe.supports.spectrum_sweep) || //prefer fe with least unneeded functionality
					 fe.priority > best_fe.priority )
					best_fe = fe;
			} //end of !need_spectrum
			continue;
		} //end of !is_subscribed (fe)

		//this fe has an active subscription and it is not our own subscription
	}
	if (no_best_fe_yet())
		return {};
	return best_fe;
}

/* Check if those resources for a candidate new scubscription are compatible with our intended use.
	 Returns the use_counts if we we can actuually use all of the resources,
	 otherwise returns nothing.
	 s: subscription_parameters: owner, rf_path, pol, band, usals_pos, dish_usals_pos, rf_coupler_id
*/
	std::optional<resource_subscription_counts_t>
devdb::fe::check_for_resource_conflicts(db_txn& rtxn,
																				const fe_subscription_t& s, //desired subscription_parameter
																				const devdb::fe_key_t* fe_key_to_release,
																				bool on_positioner) {
	using namespace  devdb::fe_subscription;
	devdb::resource_subscription_counts_t ret;
	auto c = devdb::find_first<devdb::fe_t>(rtxn);
	assert(s.owner>=0);
	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe))
			continue; //no conflict possible
		if(fe.sub.owner != s.owner && kill((pid_t)fe.sub.owner, 0)) {
			dtdebugf("process pid={} has died", fe.sub.owner);
			continue; //no conflict possible
		}
		/* at this point, fe is known to be subscribed*/

		/*
			if the frontend was used by the current subscription, then it will be released and
			cannot cause a conflict. So it needs specific treatment.
		 */
		bool fe_will_be_released = fe_key_to_release && *fe_key_to_release == fe.k;

		if(fe_will_be_released) {
			assert(fe.sub.subs.size()==1);
			/*
				there will be no subscriptions and so no possible conflicts
			 */
		} else {
			assert(fe.sub.config_id>=0);
			assert(fe.sub.owner>=0);
			//Note: this could be our subscription, but only if it is shared by some other subscription
			bool same_lnb = fe.sub.rf_path == s.rf_path;
      /* dish_id < 0 is a special case: it signifies that the dish is different
				 from any other dish*/
			bool same_dish = fe.sub.dish_id == s.dish_id && s.dish_id >=0;
      /* rf_coupler_id < 0 is a special case: it signifies there is no coupler*/
			bool same_rf_coupler = fe.sub.rf_coupler_id == s.rf_coupler_id && s.rf_coupler_id >=0;
			/*
				check for conflicting tuner use (voltage, tone, diseqc)
			 */
			bool same_tuner = (fe.sub.rf_path.card_mac_address == s.rf_path.card_mac_address &&
												 fe.sub.rf_path.rf_input == s.rf_path.rf_input);
			bool same_sat_band_pol =  (fe.sub.usals_pos == s.usals_pos &&
																 fe.sub.pol == s.pol && fe.sub.band == s.band);
			bool same_positioner = same_dish && on_positioner;
			/*
				if sub and s share at least one resource and if either one
				wants exclusive control, then there is a conflict
			 */
			if( (is_exclusive(fe.sub) || is_exclusive(s)) &&
					(same_lnb || same_dish || same_tuner || same_rf_coupler))
			return {};

			/*
				if sub and s use the same dish and either one may want to move the dish,
				there is a conflict
			 */
			if( same_dish && (may_move_dish(fe.sub) || may_move_dish(s)))
				return {};

			//check for incompatible parameters
			if(same_lnb && ! same_sat_band_pol )
				return {};
			if(same_tuner && (!same_lnb || ! same_sat_band_pol))
				return {}; //we can only reuse tuner for same sat, band and pol
			ret.lnb += same_lnb;
			ret.rf_coupler += same_rf_coupler;
			ret.tuner += same_tuner;
			ret.positioner += same_positioner;
			ret.config_id = fe.sub.config_id;
			ret.owner = fe.sub.owner;
		}
	}
	return ret;
}

/*
	Find out if the desired lnb can be subscribed and then switched to the desired band and polarisation
	and find a frontend that can be used with this lnb
	The rules are:
	-if a result is found, then the user can subscribe the lnb and frontend and can also switch the rf_mux
	 to connect the lnb to the frontend
  -if an adapter has mulitple frontends, then in dvbapi this means that the frontends share the demod
	 and that multiple frontends on the same adapter cannot be used simultaneously (tbs 6504).
	 Most multi-standard (dvbs+c+t) devices provide two frontends because typically two tuners are needed:
   one for dvbs and one for dvbc+t. Devices with only dvbc+t tend to use one tuner for both standards and
	 thus have one frontend
	-if need_blind_tune/need_spectrum/need_multistream==true, then the returned fe will be able to
	blindscan/spectrumscan/multistream

	-pol == NONE or band==NONE or dish_usals_pos == sat_pos_none,
	 then the subscription is exclusive: no other subscription
	 will be able to send diseqc commands to pick a different lnb, or polarisation or band

	-if pol != NONE or band!=NONE or sat_pos!=sat_pos_none, then the subscription is
   non-exclusive: other subscribers can reserve another frontend to use the same LNB.
   None of the  subscribers	can send diseqc command to pick a differen lnb, or to rotate a dish or
	 change polarisation or band, except if they are the only subscriber left to this lnb

	 Note that this function does not make the subscription. The caller can first investigate alternatives (e.g.,
	 using other LNBs) and then make a final decision. By using a read/write database transaction the result
	 will be atomic
 */

std::optional<std::tuple<devdb::fe_t, resource_subscription_counts_t>>
fe::find_best_fe_for_lnb(
	db_txn& rtxn, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
	const devdb::fe_key_t* fe_key_to_release,
	bool need_blind_tune, bool need_spectrum, bool need_multistream,
	int sat_pos, chdb::fe_polarisation_t pol, chdb::sat_sub_band_t band,
	int usals_pos, bool ignore_subscriptions) {

	auto* conn = connection_for_rf_path(lnb, rf_path);
	if (!conn)
		return {};
	auto & lnb_connection = *conn;
	std::optional<fe_t> best_fe; //the fe that we will use
	std::optional<resource_subscription_counts_t> best_use_counts; //the fe that we will use

	/*
		One adapter can have multiple frontends, and therefore multiple fe_t records.
		We must check all of them
	 */
	auto adapter_in_use = [&rtxn, ignore_subscriptions](int adapter_no) {
		if(ignore_subscriptions)
			return false;
		auto c = fe_t::find_by_adapter_no(rtxn, adapter_no, find_type_t::find_eq, devdb::fe_t::partial_keys_t::adapter_no);
		for(const auto& fe: c.range()) {
			assert(fe.adapter_no  == adapter_no);
			if(!fe.present)
				continue;
			if(fe::is_subscribed(fe))
				return true;
		}
		return false;
	};

	auto c = fe_t::find_by_card_mac_address(rtxn, rf_path.card_mac_address, find_type_t::find_eq,
																					fe_t::partial_keys_t::card_mac_address);
	//loop over all frontends which can reach the lnb
	for(const auto& fe: c.range()) {
		assert(fe.card_mac_address == rf_path.card_mac_address);
		bool is_subscribed = ignore_subscriptions ? false: fe::is_subscribed(fe);
		bool is_our_subscription = (ignore_subscriptions || fe.sub.subs.size()>1) ? false
			: (fe_key_to_release && fe.k == *fe_key_to_release);
		if(!is_subscribed || is_our_subscription) {
			/* we found an fe that is free (or that will be freed by caller now*/

      //find the best fe will all required functionality, without taking into account other subscriptions
			if(!fe.can_be_used || !fe.present || !devdb::fe::supports_delsys_type(fe, chdb::delsys_type_t::DVB_S))
				continue; /* The fe does not currently exist, or it cannot use DVBS. So it
									 can also not create conflicts with other fes*/

			if(! fe.enable_dvbs) //disabled by user
				continue; /*there could still be conflicts with external users (other programs)
										which could impact usage of lnb, but not with other neumoDVB instances,
										as they should also see the same value of fe.enable_dvbs
									*/
			if( !fe.rf_inputs.contains(rf_path.rf_input) ||
					(need_blind_tune && !fe.supports.blindscan) ||
					(need_multistream && !fe.supports.multistream)
				)
				continue;  /* we cannot use the LNB with this fe, and as it is not subscribed it
											conflicts for using the LNB with other fes => so no "continue"
									 */

			if(!is_our_subscription && adapter_in_use(fe.adapter_no))
				continue; /*adapter is in use for dvbc/dvt; it cannot be used, but the
										fe we found is not in use and cannot create conflicts with any other frontends*/
			/*
				check the resources which will be in use after we will have released any
				existing resources that our caller will release
			 */

			fe_subscription_t s;
			s.owner = getpid();
			s.rf_path = rf_path;
			s.pol =pol;
			s.band = band;
			s.usals_pos = usals_pos;
			s.sat_pos = sat_pos;
			s.dish_id = lnb.k.dish_id;
			s.dish_usals_pos = lnb.on_positioner ? s.usals_pos : lnb.usals_pos;
			s.rf_coupler_id = lnb_connection.rf_coupler_id;
			auto use_counts_ = check_for_resource_conflicts(rtxn, s, fe_key_to_release, lnb.on_positioner);
			if(!use_counts_) {
				//dtdebugf("Cannot use this fe because of resource conflicts");
				continue;
			}
			auto use_counts = *use_counts_;
			if(need_spectrum) {
				assert (!best_fe || best_fe->supports.spectrum_fft || best_fe->supports.spectrum_sweep);

				if(fe.supports.spectrum_fft) { //best choice
					if(!best_fe ||
						 !best_fe->supports.spectrum_fft || //fft is better
						 (fe.priority > best_fe->priority ||
							(fe.priority == best_fe->priority && is_our_subscription)) //prefer current adapter
						) {
						best_fe = fe;
						best_fe->sub.config_id = use_counts.config_id;
						best_fe->sub.owner = use_counts.owner;
						best_use_counts = use_counts;
					}
					} else if (fe.supports.spectrum_sweep) { //second best choice
					if( !best_fe ||
							( !best_fe->supports.spectrum_fft && //best_fe with fft beats fe without fft
								fe.priority > best_fe->priority )) {
						best_fe = fe;
						best_fe->sub.config_id = use_counts.config_id;
						best_fe->sub.owner = use_counts.owner;
						best_use_counts = use_counts;
					}
				} else {
					//no spectrum support at all -> not useable
				}
			} else { /* if !need_spectrum; in this case fe's with and without fft support can be
										used, but we prefer one without fft, to keep fft-hardware available for other
										subscriptions*/
				if( !best_fe ||
					 ( best_fe->supports.spectrum_fft && !fe.supports.spectrum_fft) || //prefer non-fft
					 ( best_fe->supports.spectrum_sweep && !fe.supports.spectrum_fft
						&& !fe.supports.spectrum_sweep) || //prefer fe with least unneeded functionality

						(fe.priority > best_fe->priority ||
						(fe.priority == best_fe->priority && is_our_subscription)) //prefer current adapter
					) {
					best_fe = fe;
					best_fe->sub.config_id = use_counts.config_id;
					best_fe->sub.owner = use_counts.owner;
					best_use_counts = use_counts;
				}
			} //end of !need_spectrum
		} //end of !is_subscribed
	}
	if(best_fe)
		return {{*best_fe, *best_use_counts}};
	return {};
}

/*
	Return the use_counts as they will be after releasing fe_key_to_release
 */
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t>
fe::find_fe_and_lnb_for_tuning_to_mux(db_txn& rtxn,
																			const chdb::dvbs_mux_t& mux,
																			const subscription_options_t& tune_options,
																			const devdb::fe_key_t* fe_key_to_release,
																			bool ignore_subscriptions) {
	using namespace devdb;
	int best_lnb_prio = std::numeric_limits<int>::min();
	int best_fe_prio = std::numeric_limits<int>::min();
	int best_rf_path_prio = std::numeric_limits<int>::min();
	// best lnb sofar, and the corresponding connected frontend
	std::optional<devdb::lnb_t> best_lnb;
	std::optional<devdb::rf_path_t> best_rf_path;
	std::optional<devdb::fe_t> best_fe;
	resource_subscription_counts_t best_use_counts;

	/*
		Loop over all lnbs to find a suitable one.
		In the loop below, check if the lnb is compatible with the desired mux and tune options.
		If the lnb is compatible, check check all existing subscriptions for conflicts.
	*/
	auto c = find_first<devdb::lnb_t>(rtxn);

	for (auto const& lnb : c.range()) {
		if(!lnb.enabled || !lnb.can_be_used)
			continue;
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, mux.k.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/

		auto dish_needs_to_be_moved_ = usals_move_amount != 0;

		/* check if lnb  support required frequency, polarisation...*/
		const bool disregard_networks{false};
		if (!devdb::lnb_can_tune_to_mux(lnb, mux, disregard_networks))
			continue;

		auto pol{mux.pol}; //signifies that non-exclusive control is fine
		auto band{devdb::lnb::band_for_mux(lnb, mux)}; //signifies that non-exlusive control is fine

		bool need_multistream = (mux.k.stream_id >= 0);

		for(const auto& lnb_connection: lnb.connections) {
			if(!lnb_connection.can_be_used || !lnb_connection.enabled)
				continue;

			auto rf_path = devdb::rf_path_for_connection(lnb.k, lnb_connection);
			if (!tune_options.rf_path_is_allowed(rf_path))
				continue;

			bool conn_can_control_rotor = devdb::lnb::can_move_dish(lnb_connection);

			if (lnb.on_positioner && (usals_move_amount > sat_pos_tolerance) &&
					(!tune_options.may_move_dish || ! conn_can_control_rotor)
				)
				continue; //skip because dish movement is not allowed or  not possible

			auto lnb_priority = network_priority >= 0 ? network_priority : lnb.priority;
			auto penalty = dish_needs_to_be_moved_ ? tune_options.dish_move_penalty : 0;
			if (!has_network ||
					(lnb_priority >= 0 && lnb_priority - penalty < best_lnb_prio) //we already have a better fe
				)
				continue;
			if (lnb_priority >= 0 && lnb_priority - penalty == best_lnb_prio) {
				if(best_rf_path && best_rf_path_prio >= lnb_connection.priority ) {
					continue;
				}
			}
#ifdef TOCHECK
			rf_path_t rf_path;
			rf_path.lnb = lnb.k;
			rf_path.card_mac_address = lnb_connection.card_mac_address;
			rf_path.rf_input = lnb_connection.rf_input;
#endif
#if 0
			test_frequency = mux.frequency;
			test_pol = mux.pol;
#endif
			assert(!tune_options.need_spectrum);
			auto fe_and_use_counts = fe::find_best_fe_for_lnb(
				rtxn, rf_path, lnb, fe_key_to_release, tune_options.use_blind_tune, tune_options.need_spectrum,
				mux.k.sat_pos, need_multistream, pol, band, usals_pos, ignore_subscriptions);
			if(!fe_and_use_counts) {
				dtdebugf("LNB {} cannot be used", lnb);
				continue;
			}
			auto& [fe, use_counts ] = *fe_and_use_counts;
			auto fe_prio = fe.priority;
			if(use_counts.config_id >= 0) //prefer to reuse tuners or rf_ins
				fe_prio += tune_options.resource_reuse_bonus;

			if (lnb_priority < 0 || lnb_priority - penalty == best_lnb_prio)
				if (best_rf_path_prio >= lnb_connection.priority) // use connection priority to break the tie
					continue;

			if (lnb_priority < 0 || best_rf_path_prio == lnb_connection.priority)
				if (fe_prio - penalty <= best_fe_prio) // use fe_priority to break the tie
					continue;

			/*we cannot move the dish, but we can still use this lnb if the dish
				happens to be pointint to the correct sat
			*/
			best_fe_prio = fe_prio - penalty;
			best_lnb_prio = (lnb_priority < 0 ? fe_prio : lnb_priority) - penalty; //<0 means: use fe_priority
			best_lnb = lnb;
			best_rf_path = devdb::rf_path_t{lnb.k, lnb_connection.card_mac_address, lnb_connection.rf_input};
			best_rf_path_prio = lnb_connection.priority;
			best_fe = fe;
			best_fe->sub.config_id = use_counts.config_id;
			best_fe->sub.owner = use_counts.owner;
			best_use_counts = use_counts;
		}
	}
	//during scanning, it is expected to see many failure; don't report them
	if(!best_fe && tune_options.subscription_type == subscription_type_t::TUNE)
		user_errorf("Could not find find available lnb, frontend or tuner for mux {}", mux);
	return std::make_tuple(best_fe, best_rf_path, best_lnb, best_use_counts);
}

/*
	Return the use_counts as they will be after releasing fe_key_to_release
 */
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t>
fe::find_fe_and_lnb_for_tuning_to_band(db_txn& rtxn,
																			 const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
																			 const subscription_options_t& tune_options,
																			 const devdb::fe_key_t* fe_key_to_release,
																			 bool ignore_subscriptions) {
	using namespace devdb;
	int best_lnb_prio = std::numeric_limits<int>::min();
	int best_fe_prio = std::numeric_limits<int>::min();
	int best_rf_path_prio = std::numeric_limits<int>::min();
	// best lnb sofar, and the corresponding connected frontend
	std::optional<devdb::lnb_t> best_lnb;
	std::optional<devdb::rf_path_t> best_rf_path;
	std::optional<devdb::fe_t> best_fe;
	resource_subscription_counts_t best_use_counts;

	/*
		Loop over all lnbs to find a suitable one.
		In the loop below, check if the lnb is compatible with the desired sat and tune options.
		If the lnb is compatible, check check all existing subscriptions for conflicts.
	*/
	auto c = find_first<devdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		if(!lnb.enabled || !lnb.can_be_used)
			continue;
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, sat.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if(!has_network)
			continue;
		auto dish_needs_to_be_moved_ = usals_move_amount != 0;

		/* check if lnb  support required frequency, polarisation...*/
		const bool disregard_networks{true}; //has already been checked above
		if (!devdb::lnb_can_scan_sat_band(lnb, sat, band_scan, disregard_networks))
			continue;

		auto pol{band_scan.pol}; //signifies that non-exclusive control is fine


		for(const auto& lnb_connection: lnb.connections) {
			if(!lnb_connection.can_be_used || !lnb_connection.enabled)
				continue;

			auto rf_path = devdb::rf_path_for_connection(lnb.k, lnb_connection);
			if (!tune_options.rf_path_is_allowed(rf_path))
				continue;

			bool conn_can_control_rotor = devdb::lnb::can_move_dish(lnb_connection);

			if (lnb.on_positioner && (usals_move_amount > sat_pos_tolerance) &&
					(!tune_options.may_move_dish || ! conn_can_control_rotor)
				)
				continue; //skip because dish movement is not allowed or  not possible

			auto lnb_priority = network_priority >= 0 ? network_priority : lnb.priority;
			auto penalty = dish_needs_to_be_moved_ ? tune_options.dish_move_penalty : 0;
			if (!has_network ||
					(lnb_priority >= 0 && lnb_priority - penalty < best_lnb_prio) //we already have a better fe
				)
				continue;
			if (lnb_priority >= 0 && lnb_priority - penalty == best_lnb_prio) {
				if(best_rf_path && best_rf_path_prio >= lnb_connection.priority ) {
					continue;
				}
			}
#ifdef TOCHECK
			rf_path_t rf_path;
			rf_path.lnb = lnb.k;
			rf_path.card_mac_address = lnb_connection.card_mac_address;
			rf_path.rf_input = lnb_connection.rf_input;
#endif
			auto fe_and_use_counts = fe::find_best_fe_for_lnb(
				rtxn, rf_path, lnb, fe_key_to_release, tune_options.use_blind_tune, tune_options.need_spectrum,
				false /*need_multistream*/,
				sat.sat_pos, pol, chdb::sat_sub_band_t::NONE /*force exclusive access
																								 @todo: improve code to better encode
																								 the need for exclusive access ?*/, usals_pos, ignore_subscriptions);
			if(!fe_and_use_counts) {
				//dtdebugf("LNB {} cannot be used", lnb);
				continue;
			}
			auto& [fe, use_counts ] = *fe_and_use_counts;
			auto fe_prio = fe.priority;
			if(use_counts.config_id >= 0) //prefer to reuse tuners or rf_ins
				fe_prio += tune_options.resource_reuse_bonus;

			if (lnb_priority < 0 || lnb_priority - penalty == best_lnb_prio)
				if (best_rf_path_prio >= lnb_connection.priority) // use connection priority to break the tie
					continue;

			if (lnb_priority < 0 || best_rf_path_prio == lnb_connection.priority)
				if (fe_prio - penalty <= best_fe_prio) // use fe_priority to break the tie
					continue;

			/*we cannot move the dish, but we can still use this lnb if the dish
				happens to be pointint to the correct sat
			*/
			best_fe_prio = fe_prio - penalty;
			best_lnb_prio = (lnb_priority < 0 ? fe_prio : lnb_priority) - penalty; //<0 means: use fe_priority
			best_lnb = lnb;
			best_rf_path = devdb::rf_path_t{lnb.k, lnb_connection.card_mac_address, lnb_connection.rf_input};
			best_rf_path_prio = lnb_connection.priority;
			best_fe = fe;
			best_fe->sub.config_id = use_counts.config_id;
			best_fe->sub.owner = use_counts.owner;
			best_use_counts = use_counts;
		}
	}
	if(!best_fe)
		user_errorf("Could not find find available lnb, frontend or tuner for sat {}", sat);
	return std::make_tuple(best_fe, best_rf_path, best_lnb, best_use_counts);
}



/*returns true if subscription is possible, ignoring any existing subscriptions*/
bool devdb::fe::can_subscribe_mux(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
																							 const subscription_options_t& tune_options) {
	auto[best_fe, best_lnb, best_lnb_connection_no, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_mux(wtxn, mux,
																					tune_options,
																					nullptr /*fe_key_to_release*/,
																					true /*ignore_subscriptions*/);
	return !!best_fe;
}


/*returns true if subscription is possible, ignoring any existing subscriptions*/
bool devdb::fe::can_subscribe_sat_band(db_txn& wtxn, const chdb::sat_t& sat,
																							 const chdb::band_scan_t& band_scan,
																							 const subscription_options_t& tune_options) {
	auto[best_fe, best_lnb, best_lnb_connection_no, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_band(wtxn, sat, band_scan,
																					 tune_options,
																					 nullptr /* fe_key_to_release*/,
																					 true /*ignore_subscriptions*/);
	return !!best_fe;
}





/*returns true if subscription is possible, ignoring any existing subscriptions*/
template<typename mux_t> bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, bool need_blind_tune) {
	const bool need_spectrum{false};
	const bool need_multistream = (mux.k.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, nullptr /*fe_key_to_release*/, need_blind_tune,
																										need_spectrum, need_multistream,  delsys_type,
																										true /*ignore_subscriptions*/);
	return !!best_fe;
}

bool devdb::fe::is_subscribed(const fe_t& fe) {
	if (fe.sub.owner < 0)
		return false;
	if( kill((pid_t)fe.sub.owner, 0)) {
		dtdebugf("process pid={:d} has died", fe.sub.owner);
		return false;
	}
	return true;
}


//instantiation

template bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, const chdb::dvbc_mux_t& mux, bool need_blind_tune);
template bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, const chdb::dvbt_mux_t& mux, bool need_blind_tune);
