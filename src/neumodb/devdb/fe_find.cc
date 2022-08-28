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

using namespace devdb;

void fe::unsubscribe(db_txn& wtxn, const fe_key_t& fe_key) {
	auto c = devdb::fe_t::find_by_key(wtxn, fe_key);
	auto fe = c.is_valid()  ? c.current() : fe_t{}; //update in case of external changes
	fe.sub = {};
	put_record(wtxn, fe);
}

void fe::unsubscribe(db_txn& wtxn, fe_t& fe) {
	assert (fe::is_subscribed(fe));
	assert(fe.sub.lnb_key.card_mac_address != -1);
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if (c.is_valid())
		fe = c.current(); //update in case of external changes
	fe.sub = {};
	put_record(wtxn, fe);
}


std::optional<devdb::fe_t> fe::find_best_fe_for_dvtdbc(db_txn& rtxn,
																											const devdb::fe_key_t* fe_to_release,
																											bool need_blindscan,
																											bool need_spectrum,
																											bool need_multistream,
																											chdb::delsys_type_t delsys_type) {
	bool need_dvbt = delsys_type == chdb::delsys_type_t::DVB_T;
	bool need_dvbc = delsys_type == chdb::delsys_type_t::DVB_C;
	auto c = devdb::find_first<devdb::fe_t>(rtxn);

	fe_t best_fe{}; //the fe that we will use
	best_fe.priority = std::numeric_limits<decltype(best_fe.priority)>::lowest();

	auto no_best_fe_yet = [&best_fe]()
		{ return best_fe.priority == std::numeric_limits<decltype(best_fe.priority)>::lowest(); };

	auto adapter_in_use = [&rtxn](int adapter_no) {
		auto c = fe_t::find_by_adapter_no(rtxn, adapter_no);
		for(const auto& fe: c.range()) {
			if(fe::is_subscribed(fe))
				return true;
		}
		return false;
	};

	for(const auto& fe: c.range()) {
		if (need_dvbc && (!fe.enable_dvbc || !fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_C)))
			continue;
		if (need_dvbt && (!fe.enable_dvbt || !fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_T)))
			continue;

		if((!fe::is_subscribed(fe) && ! adapter_in_use(fe.adapter_no)) ||
			 (fe_to_release && fe.k == *fe_to_release) //consider the future case where the fe will be unsubscribed
			) {
			//find the best fe will all required functionlity, without taking into account other subscriptions
			if(!fe.present || ! fe.can_be_used)
				continue; 			/* we can use this frontend only if it can be connected to the proper input*/

			//this fe is one that we can potentially use

			if(need_blindscan && !fe.supports.blindscan)
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



int fe::dish_subscription_count(db_txn& rtxn, int dish_id) {
	int ret{0};
	auto c = find_first<fe_t>(rtxn);
	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe) || fe.sub.lnb_key.dish_id != dish_id )
			continue;
		ret ++;
	}
	return ret;
}

int fe::lnb_subscription_count(db_txn& rtxn, const lnb_key_t& lnb_key) {
	int ret{0};
	auto c = fe_t::find_by_card_mac_address(rtxn, lnb_key.card_mac_address, find_type_t::find_eq,
																					fe_t::partial_keys_t::card_mac_address);

	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe) || fe.sub.lnb_key != lnb_key )
			continue;
		ret ++;
	}
	return ret;
}

int fe::switch_subscription_count(db_txn& rtxn, int switch_id) {
	if(switch_id<0)
		return 0;
	int ret{0};
	auto c = find_first<fe_t>(rtxn);
	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe) || lnb::switch_id(rtxn, fe.sub.lnb_key) != switch_id)
			continue;
		ret ++;
	}
	return ret;
}

int fe::tuner_subscription_count(db_txn& rtxn, card_mac_address_t card_mac_address, int rf_input) {
	int ret{0};

	auto c = fe_t::find_by_card_mac_address(rtxn, (int64_t) card_mac_address);
	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe) || fe.sub.lnb_key.rf_input != rf_input)
			continue;
		ret ++;
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
	-if need_blindscan/need_spectrum/need_multistream==true, then the returned fe will be able to
	blindscan/spectrumscan/multistream

	-pol == NONE or band==NONE or usals_pos == sat_pos_none,
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


std::optional<devdb::fe_t> fe::find_best_fe_for_lnb(db_txn& rtxn, const devdb::lnb_t& lnb,
																									 const devdb::fe_key_t* fe_key_to_release,
																									 bool need_blindscan, bool need_spectrum,
																									 bool need_multistream,
																									 chdb::fe_polarisation_t pol, fe_band_t band, int usals_pos) {

	//TODO: clean subscriptions at startup
	auto switch_id = devdb::lnb::switch_id(rtxn, lnb.k);
	auto lnb_on_positioner = devdb::lnb::on_positioner(lnb);

	bool need_exclusivity = pol == chdb::fe_polarisation_t::NONE ||
		band == devdb::fe_band_t::NONE || usals_pos != sat_pos_none;

	fe_t best_fe{}; //the fe that we will use
	best_fe.priority = std::numeric_limits<decltype(best_fe.priority)>::lowest();

	auto no_best_fe_yet = [&best_fe]()
		{ return best_fe.priority == std::numeric_limits<decltype(best_fe.priority)>::lowest(); };

	/*
		One adapter can have multiple frontends, and therefore multiple fe_t records.
		We must check all of them
	 */
	auto adapter_in_use = [&rtxn](int adapter_no) {
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



	/*
		cables to one rf_input (tuner) on one card can be shared with another rf_input (tuner) on the same or another
		card using external switches, which pass dc power and diseqc commands. Some witches are symmetric, i.e.,
		let thorugh diseqc  from both connectors, others are priority switches, with only one connector being able
		to send diseqc commandswhen the connector with priority has dc power connected.

		Bot the types of input sharing are made known to neumoDVB by declaring a tuner_group>=0 to
		each connected rf_input. Tuners in the same group can be used simulataneoulsy but only on the same
		sat, pol, band combination.

		Note that priority swicthes do not need to be treated specifically as neumoDVB
		never neither of the connectors to send diseqc, except initially when both connectors are idle.
	 */
	auto shared_rf_input_conflict = [&rtxn, pol, band, usals_pos] (const devdb::fe_t& fe, int switch_id) {
		if(switch_id <0)
			return false; //not on switch; no conflict possible
		if(devdb::lnb::switch_id(rtxn, fe.sub.lnb_key) != switch_id)
			return false;  //no conflict possible
		if(!fe.enable_dvbs || !devdb::fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_S))
			return false; //no conflict possible (not sat)
		if(fe.sub.pol != pol || fe.sub.band != band || fe.sub.usals_pos != usals_pos)
			return true;
		return false;
	};

	/*
		multiple LNBs can be in use on the same dish. In this case only the first
		subscription should be able to move the dish. If any subcription uses the required dish_id,
		we need to check if the dish is tuned to the right position.
		Furthermore,m if we need exclusivity, we cannot use the dish at all if a subscription exists

	 */
	auto shared_positioner_conflict = [usals_pos, need_exclusivity] (const devdb::fe_t& fe, int dish_id) {
		if (dish_id <0)
			return false; //lnb is on a dish of its own (otherwise user needs to set dish_id)
		if (fe.sub.lnb_key.dish_id <0 || fe.sub.lnb_key.dish_id != dish_id )
			return false; //fe's subscribed lnb is on a dish of its own (otherwise user needs to set dish_id)
		assert(fe.sub.lnb_key.dish_id == dish_id);
		return need_exclusivity || //exclusivity cannot be offered
			(fe.sub.usals_pos == sat_pos_none) ||  //dish is reserved exclusively by fe
			std::abs(usals_pos - fe.sub.usals_pos) >=30; //dish would need to be moved more than 0.3 degree
	};

	auto c = fe_t::find_by_card_mac_address(rtxn, lnb.k.card_mac_address, find_type_t::find_eq,
																					fe_t::partial_keys_t::card_mac_address);
	for(const auto& fe: c.range()) {
		assert(fe.card_mac_address == lnb.k.card_mac_address);
		assert(fe.sub.lnb_key.rf_input != lnb.k.rf_input || fe.sub.lnb_key == lnb.k);
		bool is_subscribed = fe::is_subscribed(fe) && (fe_key_to_release && fe.k != *fe_key_to_release);
		if(!is_subscribed) {
//find the best fe will all required functionality, without taking into account other subscriptions
			if(!fe.present || !devdb::fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_S))
				continue; /* The fe does not currently exist, or it cannot use DVBS. So it
									 can also not create conflicts with other fes*/

			if(! fe.enable_dvbs) //disabled by user
				continue; /*there could still be conflicts with external users (other programs)
										which could impact usage of lnb, but not with other neumoDVB instances,
										as they should also see the same value of fe.enable_dvbs
									*/
#if 0
			if(!fe.can_be_used)
			{/*noop*/}  /* We cannot open the fe and control it, which means another program
										 has control. This other program must be a separare instance of neumoDVB;
										 otherwise there can be no subscription.
									*/
#endif
			if( !fe.rf_inputs.contains(lnb.k.rf_input) ||
					(need_blindscan && !fe.supports.blindscan) ||
					(need_multistream && !fe.supports.multistream)
				)
				continue;  /* we cannot use the LNB with this fe, and as it is not subscribed it
											conflicts for using the LNB with other fes => so no "continue"
									 */

			if(adapter_in_use(fe.adapter_no))
				continue; /*adapter is on use for dvbc/dvt; it cannot be used, but the
										fe we found is not in use and cannot create conflicts with any other frontends*/

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
		} //end of !is_subscribed

		assert(is_subscribed);

		/*this fe has an active subscription and it is not our own subscription.
			The resources it uses (RF tuner, priority switches or t-splitters, band/pol/usals_pos)
			could conflict with the lnb we want to use. We check for all possible conflicts
		*/
		if(fe.sub.lnb_key == lnb.k) {
      /*case 1: fe uses our lnb for another subscription; associated RF tuner is also in use
				Check if our desired (non)exclusivity matches with other subscribers */
			if(need_exclusivity)
				return {}; //only one subscriber can exclusively control the lnb (and the path to it)
			else { /*we do not need exclusivity, and can use this lnb  provided no exclusive subscriptions exist
							 and provided that the lnb parameters (pol/band/diseqc) are compatible*/
				if(fe.sub.pol != pol || fe.sub.band != band || fe.sub.usals_pos != usals_pos)
					return {}; /*parameter do not match, or some other subscription is exclusive (in the latter
													case pol or band will be NONE or usals_pos will be sat_pos_none and the test will
													be true;
										 */
			}
		} else if (fe.sub.lnb_key.rf_input == lnb.k.rf_input) {
      /*case 2: fe does not use our desired lnb, but uses the RF tuner with some other lnb.
				We cannot use the RF tuner, so we can not use the LNB*/
			return {};
		} else {
			/* case 3. the desired LNB and the desired RF tuner are not used by fe

				 The remain conflicts are:
				 1) our lnb is connected through a priority or T-combiner switch,
				    in which case the cable may use another lnb (note the case of the same lnb, but wrong pol/band/sat_pos
				    has already been handled in case 1) and we cannot use it
				 2) our lnb is on a dish with a positioner, and this actuve frontend uses another lnb on the same dish
				    pointing to a different sat
			 */
			if (switch_id >=0 && shared_rf_input_conflict (fe, switch_id))
				return {}; //the lnb is on a cable which is tuned to another sat/pol/band

			if (lnb_on_positioner && shared_positioner_conflict (fe, lnb.k.dish_id))
				return {}; //the lnb is on a cable which is tuned to another sat/pol/band
		}
	}


	if (no_best_fe_yet())
		return {};
	return best_fe;
}




std::tuple<std::optional<devdb::fe_t>,
					 std::optional<devdb::lnb_t>,
					 int /*fe's priority*/, int /*lnb's priority*/ >
fe::find_fe_and_lnb_for_tuning_to_mux(db_txn& rtxn,
																			const chdb::dvbs_mux_t& mux, const devdb::lnb_t* required_lnb,
																			const devdb::fe_key_t* fe_key_to_release,
																			bool may_move_dish, bool use_blind_tune,
																			int dish_move_penalty, int resource_reuse_bonus) {
	using namespace devdb;
	int best_lnb_prio = std::numeric_limits<int>::min();
	int best_fe_prio = std::numeric_limits<int>::min();
	// best lnb sofar, and the corresponding connected frontend
	std::optional<devdb::lnb_t> best_lnb;
	std::optional<devdb::fe_t> best_fe;


	/*
		Loop over all lnbs to find a suitable one.
		In the loop below, check if the lnb is compatible with the desired mux and tune options.
		If the lnb is compatible, check check all existing subscriptions for conflicts.
	*/
	auto c = required_lnb ? lnb_t::find_by_key(rtxn, required_lnb->k, find_type_t::find_eq,
																						 devdb::lnb_t::partial_keys_t::all)
		: find_first<devdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
#if 0
		if (required_lnb && required_lnb->k != lnb.k)
			continue;
		auto* plnb = required_lnb ? required_lnb : &lnb;
		if (!plnb->enabled)
			continue;
#else
		assert(! required_lnb || required_lnb->k == lnb.k);
		if(!lnb.enabled)
			continue;
#endif
		/*
			required_lnb may not have been saved in the database and may contain additional networks or
			edited settings when called from positioner_dialog
		*/
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, mux.k.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		auto dish_needs_to_be_moved_ = usals_move_amount != 0;
		bool lnb_can_control_rotor = devdb::lnb::can_move_dish(lnb);
		bool lnb_is_on_rotor = devdb::lnb::on_positioner(lnb);

		if (lnb_is_on_rotor && (!may_move_dish || (! lnb_can_control_rotor && (usals_move_amount >= 30))))
			continue; //skip because dish movement is not allowed or  not possible

		auto lnb_priority = network_priority >= 0 ? network_priority : lnb.priority;
		auto penalty = dish_needs_to_be_moved_ ? dish_move_penalty : 0;
		if (!has_network ||
				(lnb_priority >= 0 && lnb_priority - penalty < best_lnb_prio) //we already have a better fe
			)
			continue;

    /* check if lnb  support required frequency, polarisation...*/
		const bool disregard_networks{false};
		if (!devdb::lnb_can_tune_to_mux(lnb, mux, disregard_networks))
			continue;


		const auto need_blindscan = use_blind_tune;
		const bool need_spectrum = false;
		auto pol{mux.pol}; //signifies that non-exlusive control is fine
		auto band{devdb::lnb::band_for_mux(lnb, mux)}; //signifies that non-exlusive control is fine

		bool need_multistream = (mux.stream_id >= 0);
		auto fe = fe::find_best_fe_for_lnb(rtxn, lnb, fe_key_to_release,
																				 need_blindscan, need_spectrum, need_multistream, pol, band, usals_pos);
		if(!fe) {
			dtdebug("LNB " << lnb << " cannot be used due to other subscriptions");
			continue;
		}

		auto fe_prio = fe->priority;

		if(lnb_subscription_count(rtxn, lnb.k)>=1 ||
			 tuner_subscription_count(rtxn, card_mac_address_t(fe->card_mac_address), lnb.k.rf_input) >= 1 ||
			 dish_subscription_count(rtxn, lnb.k.dish_id) >=1 ||
			 switch_subscription_count(rtxn, devdb::lnb::switch_id(rtxn, lnb.k)) >=1)
			fe_prio += resource_reuse_bonus;




		if (lnb_priority < 0 || lnb_priority - penalty == best_lnb_prio)
			if (fe_prio - penalty <= best_fe_prio) // use fe_priority to break the tie
				continue;

		/*we cannot move the dish, but we can still use this lnb if the dish
				happens to be pointint to the correct sat
		*/

		best_fe_prio = fe_prio - penalty;
		best_lnb_prio = (lnb_priority < 0 ? fe_prio : lnb_priority) - penalty; //<0 means: use fe_priority
		best_lnb = lnb;
		best_fe = fe;
		if (required_lnb)
			break; //we only beed to look at one lnb
	}
	return std::make_tuple(best_fe, best_lnb, best_fe_prio, best_lnb_prio);
}

int devdb::fe::reserve_fe_lnb_band_pol_sat(db_txn& wtxn, devdb::fe_t& fe, const devdb::lnb_t& lnb,
																						devdb::fe_band_t band,  chdb::fe_polarisation_t pol)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	sub.owner = getpid();

	//the following settings imply that we request a non-exclusive subscription
	sub.lnb_key = lnb.k;
	sub.pol = pol;
	sub.band = band;
	sub.usals_pos = lnb.usals_pos;
	put_record(wtxn, fe);
	return 0;
}

int devdb::fe::reserve_fe_lnb_exclusive(db_txn& wtxn, devdb::fe_t& fe, const devdb::lnb_t& lnb)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	sub.owner = getpid();

	//the following settings imply that we request a non-exclusive subscription
	sub.lnb_key = lnb.k;
	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.band = devdb::fe_band_t::NONE;
	sub.usals_pos = sat_pos_none;
	put_record(wtxn, fe);
	return 0;
}



int devdb::fe::reserve_fe_dvbc_or_dvbt_mux(db_txn& wtxn, devdb::fe_t& fe, bool is_dvbc)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	sub.owner = getpid();

	//the following settings imply that we request a non-exclusive subscription
	sub.lnb_key = devdb::lnb_key_t{};
	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.band = devdb::fe_band_t::NONE;
	sub.usals_pos = is_dvbc ? sat_pos_dvbc : sat_pos_dvbt;
	put_record(wtxn, fe);
	return 0;
}



std::optional<devdb::fe_t>
devdb::fe::subscribe_lnb_exclusive(db_txn& wtxn,  const devdb::lnb_t& lnb, const devdb::fe_key_t* fe_key_to_release,
												bool need_blind_tune, bool need_spectrum) {
	auto pol{chdb::fe_polarisation_t::NONE}; //signifies that we to exclusively control pol
	auto band{fe_band_t::NONE}; //signifies that we to exclusively control band
	auto usals_pos{sat_pos_none}; //signifies that we want to be able to move rotor
	bool need_multistream = false;

	auto best_fe = fe::find_best_fe_for_lnb(wtxn, lnb, fe_key_to_release,
																					need_blind_tune, need_spectrum, need_multistream, pol, band, usals_pos);
	if(fe_key_to_release)
		unsubscribe(wtxn, *fe_key_to_release);
	if(!best_fe)
		return {}; //no frontend could be found

	auto ret = devdb::fe::reserve_fe_lnb_exclusive(wtxn, *best_fe, lnb);
	assert(ret>0); //reservation cannot fail as we have a write lock on the db
	return best_fe;
}

std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::lnb_t>>
devdb::fe::subscribe_lnb_band_pol_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
													 const devdb::lnb_t* required_lnb, const devdb::fe_key_t* fe_key_to_release,
													 bool use_blind_tune, int dish_move_penalty, int resource_reuse_bonus) {
	const bool may_move_dish{true};
	auto[best_fe, best_lnb, best_fe_prio, best_lnb_prio ] =
		fe::find_fe_and_lnb_for_tuning_to_mux(wtxn, mux, required_lnb,
																					fe_key_to_release,
																					may_move_dish, use_blind_tune,
																					dish_move_penalty, resource_reuse_bonus);
	if(fe_key_to_release)
		unsubscribe(wtxn, *fe_key_to_release);
	if(!best_fe)
		return {}; //no frontend could be found

	auto ret = devdb::fe::reserve_fe_lnb_band_pol_sat(wtxn, *best_fe, *best_lnb, devdb::lnb::band_for_mux(*best_lnb, mux),
																									 mux.pol);
	assert(ret==0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, best_lnb};
}

template<typename mux_t>
std::optional<devdb::fe_t>
devdb::fe::subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, const devdb::fe_key_t* fe_key_to_release,
									 bool use_blind_tune) {

	const bool need_spectrum{false};
	const bool need_multistream = (mux.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	bool is_dvbc = delsys_type == chdb::delsys_type_t::DVB_C;
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, fe_key_to_release, use_blind_tune,
																							 need_spectrum, need_multistream,  delsys_type);
	if(fe_key_to_release)
		unsubscribe(wtxn, *fe_key_to_release);

	if(!best_fe)
		return {}; //no frontend could be found

	auto ret = devdb::fe::reserve_fe_dvbc_or_dvbt_mux(wtxn, *best_fe, is_dvbc);
	assert(ret>0); //reservation cannot fail as we have a write lock on the db
	return best_fe;
}

//instantiation
template std::optional<devdb::fe_t>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, const chdb::dvbc_mux_t& mux,
																						 const devdb::fe_key_t* fe_key_to_release,
																						 bool use_blind_tune);

template std::optional<devdb::fe_t>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, const chdb::dvbt_mux_t& mux,
																						 const devdb::fe_key_t* fe_key_to_release,
																						 bool use_blind_tune);
