/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "util/dtassert.h"
//#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>
#include <signal.h>

#include "../util/neumovariant.h"

using namespace devdb;

/*
	returns the remaining use_count of the unsuscribed fe
 */
int fe::unsubscribe(db_txn& wtxn, const fe_key_t& fe_key, fe_t* fe_ret) {
	auto c = devdb::fe_t::find_by_key(wtxn, fe_key);
	assert(c.is_valid());
	auto fe = c.current(); //update in case of external changes
	dtdebugx("adapter %d %d.%03d%s-%d %d use_count=%d unsubscribe", fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.use_count);
	assert(fe.sub.use_count>=1);
	if(--fe.sub.use_count == 0) {
		fe.sub = {};
	}
	put_record(wtxn, fe);
	if (fe_ret)
		*fe_ret = fe;
	return fe.sub.use_count;
}

/*
	returns the remaining use_count of the unsubscribed fe
 */
int fe::unsubscribe(db_txn& wtxn, fe_t& fe) {
	assert(fe::is_subscribed(fe));
	assert(fe.sub.rf_path.card_mac_address != -1);
	auto ret = fe::unsubscribe(wtxn, fe.k, &fe);
	return ret;
}

std::tuple<devdb::fe_t, int> fe::subscribe_fe_in_use(db_txn& wtxn, const fe_key_t& fe_key,
																										 const chdb::mux_key_t &mux_key,
																										 const devdb::fe_key_t* fe_key_to_release) {
	auto c = devdb::fe_t::find_by_key(wtxn, fe_key);
	auto fe = c.is_valid()  ? c.current() : fe_t{}; //update in case of external changes
	int released_fe_usecount{0};
	assert(fe.sub.use_count>=1);
	++fe.sub.use_count;
	assert(is_same_stream(mux_key, fe.sub.mux_key));
	dtdebugx("adapter %d %d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.use_count);

	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, *fe_key_to_release);

	put_record(wtxn, fe);
	return {fe, released_fe_usecount};
}

std::optional<devdb::fe_t> fe::find_best_fe_for_dvtdbc(
	db_txn& rtxn, const devdb::fe_key_t* fe_key_to_release,
	bool need_blindscan, bool need_spectrum, bool need_multistream,
	chdb::delsys_type_t delsys_type, bool ignore_subscriptions) {
	bool need_dvbt = delsys_type == chdb::delsys_type_t::DVB_T;
	bool need_dvbc = delsys_type == chdb::delsys_type_t::DVB_C;
	auto c = devdb::find_first<devdb::fe_t>(rtxn);

	fe_t best_fe{}; //the fe that we will use
	best_fe.priority = std::numeric_limits<decltype(best_fe.priority)>::lowest();

	auto no_best_fe_yet = [&best_fe]()
		{ return best_fe.priority == std::numeric_limits<decltype(best_fe.priority)>::lowest(); };
#if 0
	auto adapter_in_use = [&rtxn, ignore_subscriptions](int adapter_no) {
		if(ignore_subscriptions)
			return false;
		auto c = fe_t::find_by_adapter_no(rtxn, adapter_no);
		for(const auto& fe: c.range()) {
			if(fe::is_subscribed(fe))
				return true;
		}
		return false;
	};
#endif
	for(const auto& fe: c.range()) {
		if (need_dvbc && (!fe.enable_dvbc || !fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_C)))
			continue;
		if (need_dvbt && (!fe.enable_dvbt || !fe::suports_delsys_type(fe, chdb::delsys_type_t::DVB_T)))
			continue;
		bool is_subscribed = ignore_subscriptions ? false: fe::is_subscribed(fe);
		bool is_our_subscription = (ignore_subscriptions || fe.sub.use_count>1) ? false
			: (fe_key_to_release && fe.k == *fe_key_to_release);
		if(!is_subscribed  || is_our_subscription) {

			//find the best fe with all required functionality, without taking into account other subscriptions
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


/*
	In case two rf inputs on the same or different cards are connected to the same
	cable, this can be indicated by rf_input_t records which for each rf input contain a
	switch_id>=0. rf_inputs with the same switch_id care connected. rf_inputs without switch_id are not connected

 */
static inline bool rf_coupler_conflict(db_txn& rtxn, const devdb::lnb_connection_t& lnb_connection,
																			 const devdb::fe_t& fe, bool ignore_subscriptions,
																			 chdb::fe_polarisation_t pol, fe_band_t band, int usals_pos) {
	if(lnb_connection.rf_coupler_id < 0 || ignore_subscriptions)
		return false;
	auto c = find_first<devdb::fe_t>(rtxn);
	for(const auto &otherfe: c.range()) {
		if(fe.k == otherfe.k)
			continue;
		if(otherfe.sub.rf_coupler_id < 0 || otherfe.sub.rf_coupler_id != lnb_connection.rf_coupler_id)
			continue;
		if(!otherfe.enable_dvbs || !devdb::fe::suports_delsys_type(otherfe, chdb::delsys_type_t::DVB_S))
			continue; //no conflict possible (not sat)
		if(otherfe.sub.pol != pol || otherfe.sub.band != band || otherfe.sub.usals_pos != usals_pos)
			return true;
	}
	return false;
}


//how many active fe_t's use lnb?
devdb::resource_subscription_counts_t
devdb::fe::subscription_counts(db_txn& rtxn, const devdb::rf_path_t& rf_path, int rf_coupler_id,
															 const devdb::fe_key_t* fe_key_to_release) {
	devdb::resource_subscription_counts_t ret;
#if 0
	auto c = fe_t::find_by_card_mac_address(rtxn, rf_path.card_mac_address, find_type_t::find_eq,
																					fe_t::partial_keys_t::card_mac_address);
#else
	auto c = devdb::find_first<devdb::fe_t>(rtxn);
#endif
	for(const auto& fe: c.range()) {
		if( !fe::is_subscribed(fe))
			continue;
		if(fe.sub.owner != getpid() && kill((pid_t)fe.sub.owner, 0)) {
			dtdebugx("process pid=%d has died", fe.sub.owner);
			continue;
		}
		bool fe_will_be_released = fe_key_to_release && *fe_key_to_release == fe.k;
		if(!fe_will_be_released) {
			if(fe.sub.rf_path == rf_path)
				ret.rf_path++;
			if(fe.sub.rf_path.card_mac_address == rf_path.card_mac_address &&
				 fe.sub.rf_path.rf_input == rf_path.rf_input)
				ret.tuner++;

			if(fe.sub.rf_coupler_id >=0 && fe.sub.rf_coupler_id == rf_coupler_id)  {
					ret.rf_coupler++;
			}

			if( fe.sub.rf_path.lnb.dish_id == rf_path.lnb.dish_id
					|| fe.sub.rf_path == rf_path) //the last test is in case dish_id is set to -1
				ret.dish++;
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

std::optional<devdb::fe_t> fe::find_best_fe_for_lnb(
	db_txn& rtxn, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
	const devdb::fe_key_t* fe_key_to_release,
	bool need_blindscan, bool need_spectrum, bool need_multistream,
	chdb::fe_polarisation_t pol, fe_band_t band, int usals_pos, bool ignore_subscriptions,
	bool lnb_on_positioner) {

	auto* conn = connection_for_rf_path(lnb, rf_path);
	if (!conn)
		return {};
	auto & lnb_connection = *conn;
	bool need_exclusivity = pol == chdb::fe_polarisation_t::NONE ||
		band == devdb::fe_band_t::NONE || usals_pos == sat_pos_none;

	fe_t best_fe{}; //the fe that we will use
	best_fe.priority = std::numeric_limits<decltype(best_fe.priority)>::lowest();

	auto no_best_fe_yet = [&best_fe]()
		{ return best_fe.priority == std::numeric_limits<decltype(best_fe.priority)>::lowest(); };

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

	/*
		multiple LNBs can be in use on the same dish. In this case only the first
		subscription should be able to move the dish. If any subcription uses the required dish_id,
		we need to check if the dish is tuned to the right position.
		Furthermore,m if we need exclusivity, we cannot use the dish at all if a subscription exists

	 */
	auto shared_positioner_conflict = [usals_pos, need_exclusivity, ignore_subscriptions]
		(const devdb::fe_t& fe, int dish_id) {
		if (dish_id <0)
			return false; //lnb is on a dish of its own (otherwise user needs to set dish_id)
		if(ignore_subscriptions)
			return false;
		if (fe.sub.rf_path.lnb.dish_id <0 || fe.sub.rf_path.lnb.dish_id != dish_id )
			return false; //fe's subscribed lnb is on a dish of its own (otherwise user needs to set dish_id)
		assert(fe.sub.rf_path.lnb.dish_id == dish_id);
		return need_exclusivity || //exclusivity cannot be offered
			(fe.sub.usals_pos == sat_pos_none) ||  //dish is reserved exclusively by fe
			std::abs(usals_pos - fe.sub.usals_pos) > sat_pos_tolerance; //dish would need to be moved more than sat_pos_tolerance
	};

	auto c = fe_t::find_by_card_mac_address(rtxn, rf_path.card_mac_address, find_type_t::find_eq,
																					fe_t::partial_keys_t::card_mac_address);
	//loop over all frontends which can reach the lnb
	for(const auto& fe: c.range()) {
		assert(fe.card_mac_address == rf_path.card_mac_address);
		bool is_subscribed = ignore_subscriptions ? false: fe::is_subscribed(fe);
		bool is_our_subscription = (ignore_subscriptions || fe.sub.use_count>1) ? false
			: (fe_key_to_release && fe.k == *fe_key_to_release);
		if(!is_subscribed || is_our_subscription) {
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
			if( !fe.rf_inputs.contains(rf_path.rf_input) ||
					(need_blindscan && !fe.supports.blindscan) ||
					(need_multistream && !fe.supports.multistream)
				)
				continue;  /* we cannot use the LNB with this fe, and as it is not subscribed it
											conflicts for using the LNB with other fes => so no "continue"
									 */

			if(!is_our_subscription && adapter_in_use(fe.adapter_no))
				continue; /*adapter is on use for dvbc/dvt; it cannot be used, but the
										fe we found is not in use and cannot create conflicts with any other frontends*/

				/*
					cables to one rf_input (tuner) on one card can be shared with another rf_input (tuner) on the same or another
					card using external switches, which pass dc power and diseqc commands. Some switches are symmetric, i.e.,
					let through diseqc  from both connectors, others are priority switches, with only one connector being able
					to send diseqc commandswhen the connector with priority has dc power connected.

					Note that priority switches do not need to be treated specifically as neumoDVB
					never neither of the connectors to send diseqc, except initially when both connectors are idle.
				*/


			if(lnb_connection.rf_coupler_id >=0 &&
				 rf_coupler_conflict(rtxn, lnb_connection, fe, ignore_subscriptions, pol, band, usals_pos))
				continue;
			if(need_spectrum) {
				assert (no_best_fe_yet() || best_fe.supports.spectrum_fft || best_fe.supports.spectrum_sweep);

				if(fe.supports.spectrum_fft) { //best choice
					if(no_best_fe_yet() ||
						 !best_fe.supports.spectrum_fft || //fft is better
						 (fe.priority > best_fe.priority ||
							(fe.priority == best_fe.priority && is_our_subscription)) //prefer current adapter
						)
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

					 (fe.priority > best_fe.priority ||
						(fe.priority == best_fe.priority && is_our_subscription)) //prefer current adapter
					)
					best_fe = fe;
			} //end of !need_spectrum
			continue;
		} //end of !is_subscribed

		assert(is_subscribed && !is_our_subscription);

		/*this fe has an active subscription and it is not our own subscription.
			The resources it uses (RF tuner, priority switches or t-splitters, band/pol/usals_pos)
			could conflict with the lnb we want to use. We check for all possible conflicts
		*/
		if(fe.sub.rf_path == rf_path) {
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
		} else if (fe.sub.rf_path.rf_input == rf_path.rf_input) {
      /*case 2: fe does not use our desired lnb, but uses the RF tuner with some other lnb.
				We cannot use the RF tuner, so we can not use the LNB*/
			return {};
		} else {
			/* case 3. the desired LNB and the desired RF tuner are not used by fe

				 The remaining possible conflict is:
				 our lnb is on a dish with a positioner, and this active frontend uses another lnb on the same dish
				 pointing to a different sat
			 */
			if (lnb_on_positioner && shared_positioner_conflict (fe, rf_path.lnb.dish_id))
				return {}; //the lnb is on a cable which is tuned to another sat/pol/band
		}
	}


	if (no_best_fe_yet())
		return {};
	return best_fe;
}

/*
	Return the use_counts as they will be after releasing fe_key_to_release
 */
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t>
fe::find_fe_and_lnb_for_tuning_to_mux(db_txn& rtxn,
																			const chdb::dvbs_mux_t& mux, const devdb::rf_path_t* required_rf_path,
																			const devdb::fe_key_t* fe_key_to_release,
																			bool may_move_dish, bool use_blind_tune,
																			int dish_move_penalty, int resource_reuse_bonus,
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
	auto c = required_rf_path ? lnb_t::find_by_key(rtxn, required_rf_path->lnb, find_type_t::find_eq,
																						 devdb::lnb_t::partial_keys_t::all)
		: find_first<devdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		assert(! required_rf_path || required_rf_path->lnb == lnb.k);
		if(!lnb.enabled || !lnb.can_be_used)
			continue;
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, mux.k.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if(required_rf_path && ! has_network) {
			chdb::sat_t sat;
			sat.sat_pos = mux.k.sat_pos;
			user_error("LNB  " << lnb << ": LNB has no network to tune to sat " << sat);
			break;
		}

		bool lnb_is_on_rotor = devdb::lnb::on_positioner(lnb);
		auto dish_needs_to_be_moved_ = usals_move_amount != 0;

		/* check if lnb  support required frequency, polarisation...*/
		const bool disregard_networks{false};
		if (!devdb::lnb_can_tune_to_mux(lnb, mux, disregard_networks))
			continue;
		const auto need_blindscan = use_blind_tune;
		const bool need_spectrum = false;
		auto pol{mux.pol}; //signifies that non-exclusive control is fine
		auto band{devdb::lnb::band_for_mux(lnb, mux)}; //signifies that non-exlusive control is fine

		bool need_multistream = (mux.k.stream_id >= 0);

		for(const auto& lnb_connection: lnb.connections) {
			if(!lnb_connection.can_be_used || !lnb_connection.enabled)
				continue;
			if(required_rf_path) {
				auto rf_path = devdb::rf_path_for_connection(lnb.k, lnb_connection);
				if (rf_path != *required_rf_path)
					continue;
			}

			bool conn_can_control_rotor = devdb::lnb::can_move_dish(lnb_connection);

			if (lnb_is_on_rotor && (usals_move_amount > sat_pos_tolerance) &&
					(!may_move_dish || ! conn_can_control_rotor)
				)
				continue; //skip because dish movement is not allowed or  not possible

			auto lnb_priority = network_priority >= 0 ? network_priority : lnb.priority;
			auto penalty = dish_needs_to_be_moved_ ? dish_move_penalty : 0;
			if (!has_network ||
					(lnb_priority >= 0 && lnb_priority - penalty < best_lnb_prio) //we already have a better fe
				)
				continue;
			if (lnb_priority >= 0 && lnb_priority - penalty == best_lnb_prio) {
				if(best_rf_path && best_rf_path_prio >= lnb_connection.priority ) {
					continue;
				}
			}

			rf_path_t rf_path;
			rf_path.lnb = lnb.k;
			rf_path.card_mac_address = lnb_connection.card_mac_address;
			rf_path.rf_input = lnb_connection.rf_input;
			auto fe = fe::find_best_fe_for_lnb(rtxn, rf_path, lnb,
																				 fe_key_to_release, need_blindscan, need_spectrum,
																				 need_multistream, pol, band, usals_pos, ignore_subscriptions,
																				 lnb_is_on_rotor);
			if(!fe) {
				dtdebug("LNB " << lnb << " cannot be used");
				continue;
			}

			auto fe_prio = fe->priority;
			auto rf_coupler_id{-1};
			auto* conn = connection_for_rf_path(lnb, rf_path);
			if(conn)
				rf_coupler_id = conn->rf_coupler_id;

			auto use_counts = subscription_counts(rtxn, rf_path, rf_coupler_id, fe_key_to_release);

			if(use_counts.rf_path >= 1 ||
				 use_counts.tuner >= 1 ||
				 use_counts.dish >=1 ||
				 use_counts.rf_coupler >=1)
				fe_prio += resource_reuse_bonus;

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
			best_use_counts = use_counts;
			if (required_rf_path)
				return std::make_tuple(best_fe, best_rf_path, best_lnb,
															 best_use_counts); //we only beed to look at one lnb
		}

		if(required_rf_path && ! best_fe) {
			chdb::sat_t sat;
			sat.sat_pos = mux.k.sat_pos;
			user_error("LNB  " << lnb << ": no suitable connection to tune to sat " << sat);
			break;
		}
	}
	if(!best_fe)
		user_error("Could not find find available lnb, frontend or tuner for mux " << mux);
	return std::make_tuple(best_fe, best_rf_path, best_lnb, best_use_counts);
}

int devdb::fe::reserve_fe_lnb_for_mux(db_txn& wtxn, devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																			const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	assert(sub.use_count == 0);
	sub.owner = getpid();
	assert(sub.use_count==0);
	sub.use_count = 1;

	//the following settings imply that we request a non-exclusive subscription
	sub.rf_path = rf_path;
	sub.rf_coupler_id = -1;
	auto* conn = connection_for_rf_path(lnb, rf_path);
	if(conn)
		sub.rf_coupler_id = conn->rf_coupler_id;

	sub.pol = mux.pol;
	sub.band = 	devdb::lnb::band_for_mux(lnb, mux);
	sub.usals_pos = lnb.usals_pos;
	sub.frequency = mux.frequency;
	sub.mux_key = mux.k;
	dtdebugx("adapter %d %d.%03d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.use_count);
	put_record(wtxn, fe);
	return 0;
}

int devdb::fe::reserve_fe_lnb_exclusive(db_txn& wtxn, devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																				const devdb::lnb_t& lnb)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	assert(sub.use_count == 0);
	sub.owner = getpid();
	assert(sub.use_count==0);
	sub.use_count = 1;
	//the following settings imply that we request a non-exclusive subscription

	sub.rf_path = rf_path;
	sub.rf_coupler_id =  -1;
	auto* conn = connection_for_rf_path(lnb, rf_path);
	if(conn)
		sub.rf_coupler_id = conn->rf_coupler_id;

	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.band = devdb::fe_band_t::NONE;
	sub.usals_pos = sat_pos_none;
	sub.frequency = 0;
	sub.mux_key = {};
	dtdebugx("adapter %d %d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.use_count);
	put_record(wtxn, fe);
	return 0;
}

template<typename mux_t>
int devdb::fe::reserve_fe_for_mux(db_txn& wtxn, devdb::fe_t& fe, const mux_t& mux)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	assert(sub.use_count == 0);
	sub.owner = getpid();
	assert(sub.use_count==0);
	sub.use_count = 1;
	//the following settings imply that we request a non-exclusive subscription
	rf_path_t rf_path;
	rf_path.card_mac_address = fe.card_mac_address;
	rf_path.rf_input = 0;


	sub.rf_path = rf_path;
	sub.rf_coupler_id  = -1;
	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.band = devdb::fe_band_t::NONE;
	sub.usals_pos = mux.k.sat_pos;
	sub.frequency = mux.frequency; //for informational purposes only
	sub.mux_key = mux.k;
	put_record(wtxn, fe);
	return 0;
}


/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	int: the use_count after releasing fe_kkey_to_release, or 0 if fe_key_to_release==nullptr
 */
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_lnb_exclusive(db_txn& wtxn, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																	 const devdb::fe_key_t* fe_key_to_release,
																	 bool need_blind_tune, bool need_spectrum) {
	auto pol{chdb::fe_polarisation_t::NONE}; //signifies that we to exclusively control pol
	auto band{fe_band_t::NONE}; //signifies that we to exclusively control band
	auto usals_pos{sat_pos_none}; //signifies that we want to be able to move rotor
	bool need_multistream = false;
	int released_fe_usecount{0};
	bool lnb_on_positioner = devdb::lnb::on_positioner(lnb);
	auto best_fe = fe::find_best_fe_for_lnb(wtxn, rf_path, lnb,
																					fe_key_to_release, need_blind_tune, need_spectrum,
																					need_multistream, pol, band, usals_pos,
																					false /*ignore_subscriptions*/,
																					lnb_on_positioner);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, *fe_key_to_release);

	if(!best_fe)
		return {best_fe, released_fe_usecount}; //no frontend could be found

	auto ret = devdb::fe::reserve_fe_lnb_exclusive(wtxn, *best_fe, rf_path, lnb);
	assert(ret==0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, released_fe_usecount};
}


/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::lnb_t>: the newly subscribed lnb
	devdb::resource_subscription_counts_t
	int: the use_count after releasing fe_kkey_to_release, or 0 if fe_key_to_release==nullptr
 */
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t, int>
devdb::fe::subscribe_lnb_band_pol_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
																			const devdb::rf_path_t* required_rf_path,
																			const devdb::fe_key_t* fe_key_to_release,
																			bool use_blind_tune, bool may_move_dish,
																			int dish_move_penalty, int resource_reuse_bonus) {
	int released_fe_usecount{0};
	auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_mux(wtxn, mux, required_rf_path,
																					fe_key_to_release,
																					may_move_dish, use_blind_tune,
																					dish_move_penalty, resource_reuse_bonus, false /*ignore_subscriptions*/);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, *fe_key_to_release);
	if(!best_fe)
		return {{}, {}, {}, {}, released_fe_usecount}; //no frontend could be found
	if(fe_key_to_release && best_fe->k == *fe_key_to_release)
		released_fe_usecount++;
	auto ret = devdb::fe::reserve_fe_lnb_for_mux(wtxn, *best_fe, *best_rf_path, *best_lnb, mux);
	best_use_counts.dish++;
	best_use_counts.rf_path++;
	best_use_counts.rf_coupler++;
	best_use_counts.tuner++;
	assert(ret==0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, best_rf_path, best_lnb, best_use_counts, released_fe_usecount};
}

/*returns true if subscription is possible, ignoring any existing subscriptions*/
bool devdb::fe::can_subscribe_lnb_band_pol_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
																							 const devdb::rf_path_t* required_rf_path,
																							 bool use_blind_tune, bool may_move_dish,
																							 int dish_move_penalty, int resource_reuse_bonus) {
	auto[best_fe, best_lnb, best_lnb_connection_no, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_mux(wtxn, mux, required_rf_path,
																					nullptr /*fe_key_to_release*/,
																					may_move_dish, use_blind_tune,
																					dish_move_penalty, resource_reuse_bonus, true /*ignore_subscriptions*/);
	return !!best_fe;
}


/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	int: the use_count after releasing fe_kkey_to_release, or 0 if fe_key_to_release==nullptr
 */
template<typename mux_t>
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, const devdb::fe_key_t* fe_key_to_release,
									 bool use_blind_tune) {

	const bool need_spectrum{false};
	int released_fe_usecount{0};
	const bool need_multistream = (mux.k.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, fe_key_to_release, use_blind_tune,
																										need_spectrum, need_multistream,  delsys_type,
																										false /*ignore_subscriptions*/);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, *fe_key_to_release);

	if(!best_fe)
		return {best_fe, released_fe_usecount}; //no frontend could be found

	if(fe_key_to_release && best_fe->k == *fe_key_to_release)
		released_fe_usecount++;
	auto ret = devdb::fe::reserve_fe_for_mux(wtxn, *best_fe, mux);
	assert(ret == 0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, released_fe_usecount};
}


/*returns true if subscription is possible, ignoring any existing subscriptions*/
template<typename mux_t> bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, bool use_blind_tune) {
	const bool need_spectrum{false};
	const bool need_multistream = (mux.k.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, nullptr /*fe_key_to_release*/, use_blind_tune,
																										need_spectrum, need_multistream,  delsys_type,
																										true /*ignore_subscriptions*/);
	return !!best_fe;
}

bool devdb::fe::is_subscribed(const fe_t& fe) {
	if (fe.sub.owner < 0)
		return false;
	if( kill((pid_t)fe.sub.owner, 0)) {
		dtdebugx("process pid=%d has died", fe.sub.owner);
		return false;
	}
	return true;
}


//instantiation
template std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, const chdb::dvbc_mux_t& mux,
																						 const devdb::fe_key_t* fe_key_to_release,
																						 bool use_blind_tune);

template std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, const chdb::dvbt_mux_t& mux,
																						 const devdb::fe_key_t* fe_key_to_release,
																						 bool use_blind_tune);

template bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, const chdb::dvbc_mux_t& mux, bool use_blind_tune);
template bool
devdb::fe::can_subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, const chdb::dvbt_mux_t& mux, bool use_blind_tune);
