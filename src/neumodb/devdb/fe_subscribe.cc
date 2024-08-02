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
#include <fmt/std.h>
#include <iomanip>
#include <iostream>
#include <signal.h>

#include "../util/neumovariant.h"

using namespace devdb;
static
std::tuple<bool,std::optional<devdb::dish_t>>
update_dish_helper(db_txn& devdb_wtxn, devdb::lnb_t&lnb,
									 const devdb::rf_path_t& rf_path, const devdb::usals_location_t& loc,
									 int16_t lnb_sat_pos, bool may_move_dish)
{
	if(lnb.on_positioner) {
		auto* lnb_network = devdb::lnb::get_network(lnb, lnb_sat_pos);
		if (!lnb_network) {
			dterrorf("No network found");
			return {false, {}};
		}
		auto usals_pos = lnb_network->usals_pos;
		auto old_usals_pos = lnb.usals_pos;
		lnb.usals_pos = usals_pos;
		if (may_move_dish && old_usals_pos != usals_pos) {
			bool move_dish =true;
			auto dish = devdb::dish::schedule_move(devdb_wtxn, lnb, usals_pos, lnb_sat_pos, loc, false /*move_has_finished*/);
			return {move_dish, dish};
		}
	}
	bool move_dish = false;
	auto dish = devdb::dish::get_dish(devdb_wtxn, lnb.k.dish_id);
	return {move_dish, dish};
}

static bool unsubscribe_helper(fe_t& fe, subscription_id_t subscription_id) {
	bool erased = false;
	int idx=0;
	if(fe.sub.subs.size()==0 || fe.sub.owner != getpid())
		return false;
	for(auto& sub: fe.sub.subs) {
		if(sub.subscription_id == (int32_t)subscription_id &&  fe.sub.owner == getpid()) {
			if(sub.has_service) {
				dtdebugf("adapter {:d}: subscription_id={:d} unsubscribe service={}",
								 fe.adapter_no, (int) subscription_id, sub.v);
			} else if (sub.has_mux) {
				dtdebugf("adapter {:d} {:d}.{:03d}{}-{:d} {:d} use_count={:d} unsubscribe", fe.adapter_no,
								 fe.sub.frequency/1000, fe.sub.frequency%1000,
								 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());
			} else {
				assert(fe.sub.subs.size() ==1); // lnb reservation is unique
				dtdebugf("adapter {} use_count={} unsubscribe", fe.adapter_no, fe.sub.subs.size());
			}
			fe.sub.subs.erase(idx);
			erased = true;
			break;
		}
		++idx;
	}

	if(fe.sub.subs.size() == 0) {
		fe.sub = {};
	}
	return erased;
}

/*
	returns the remaining use_count of the unsuscribed fe
 */
static std::optional<devdb::fe_t>
unsubscribe(db_txn& wtxn, subscription_id_t subscription_id, const fe_key_t& fe_key) {
	auto c = devdb::fe_t::find_by_key(wtxn, fe_key);
	auto fe = c.is_valid()  ? c.current() : fe_t{}; //update in case of external changes
	assert(fe.sub.subs.size()>=1);
	bool erased = unsubscribe_helper(fe, subscription_id);
	if(!erased) {
		dterrorf("Trying to unsubcribe a non-subscribed service subscription_id={}", (int) subscription_id);
		return {};
	}
	else {
		put_record(wtxn, fe);
		return fe;
	}
}

std::optional<devdb::fe_t> fe::unsubscribe(db_txn& wtxn, subscription_id_t subscription_id)
{
	auto c = find_first<devdb::fe_t>(wtxn);
	for(auto fe : c.range()) {
		bool erased = unsubscribe_helper(fe, subscription_id);
		if(erased) {
			put_record(wtxn, fe);
			return fe;
		}
	}
	return {};
}

static inline subscribe_ret_t no_change(subscription_id_t subscription_id,
																				const std::optional<devdb::fe_t>& fe) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	sret.sub_to_reuse = subscription_id;
	sret.aa = {.updated_old_dbfe=fe, .updated_new_dbfe=fe, .rf_path={}, .lnb={}};
	assert(fe->sub.owner == getpid());
	assert(fe->sub.config_id >= 0);
	sret.tune_pars.owner = fe->sub.owner;
	sret.tune_pars.config_id = fe->sub.config_id;
	return sret;
}

static inline subscribe_ret_t reuse_other_subscription(
	subscription_id_t subscription_id,  int other_subscription_id,
	const std::optional<devdb::fe_t>& new_fe, const std::optional<devdb::fe_t>& old_fe) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	sret.aa = {.updated_old_dbfe=old_fe, .updated_new_dbfe=new_fe, .rf_path={}, .lnb={}};
	sret.sub_to_reuse =(subscription_id_t) other_subscription_id;
	sret.tune_pars.send_lnb_commands = false;
	sret.tune_pars.move_dish = false;
	sret.tune_pars.dish = {};
	assert(new_fe.sub.owner == getpid());
	assert(new_fe.sub.config_id != -1);
	sret.tune_pars.owner = new_fe->sub.owner;
	sret.tune_pars.config_id = new_fe->sub.config_id;
	return sret;
}

static inline subscribe_ret_t new_service(
	subscription_id_t subscription_id, int other_subscription_id,
	const std::optional<devdb::fe_t>& new_fe, const std::optional<devdb::fe_t>& old_fe) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	/* ret.aa_sub_to_reuse = subscribe_ret_t::NONE : keep active_adapter and do not retune*/
	sret.aa = {.updated_old_dbfe=old_fe, .updated_new_dbfe=new_fe, .rf_path={}, .lnb={}};
	sret.sub_to_reuse = (subscription_id_t) other_subscription_id;
	sret.change_service = true;
	sret.tune_pars.send_lnb_commands = false;
	sret.tune_pars.move_dish = false;
	sret.tune_pars.dish = {};
	assert(new_fe.sub.owner == getpid());
	assert(new_fe.sub.config_id != -1);
	sret.tune_pars.owner = new_fe->sub.owner;
	sret.tune_pars.config_id = new_fe->sub.config_id;
	return sret;
}

static inline subscribe_ret_t failed(subscription_id_t subscription_id,
																		 const std::optional<devdb::fe_t>& old_fe) {
	subscribe_ret_t sret{subscription_id, true /*failed*/};
	sret.aa = {.updated_old_dbfe=old_fe, .updated_new_dbfe={}, .rf_path={}, .lnb={}};
	sret.failed=true;
	return sret;
}

/*
	Find the fe for a subscription.
	Also return a flag indicating whether the fe is only used by the subscription
 */
static std::tuple<std::optional<fe_t>, bool>
fe_for_subscription(db_txn& rtxn, subscription_id_t subscription_id)
{
	if((int) subscription_id <0)
		return {};
	using namespace devdb::fe;
	auto c = find_first<devdb::fe_t>(rtxn);
	auto owner = getpid();

	for(auto fe: c.range()) {
		if(fe.sub.owner != owner)
			continue;
		int num_other_subscriptions{0};
		bool found{false};
		for(const auto& d: fe.sub.subs) {
			num_other_subscriptions += (d.subscription_id != (int)subscription_id);
			found |= (d.subscription_id == (int)subscription_id);
		}
		if(found) {
			return {fe, num_other_subscriptions==0};
		}
	}
	return {};
}

template<typename mux_t>
int devdb::fe::reserve_fe_in_use(db_txn& wtxn, subscription_id_t subscription_id,
																 devdb::fe_t& fe,  const mux_t& mux, const chdb::service_t* service)
{
	auto& subs = fe.sub.subs;
	assert(fe.sub.owner == getpid());
	assert(fe.sub.config_id >= 0);
	assert((int)subscription_id >=0);
	if(service)
		subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else {
		chdb::service_t service;
		service.k.mux = mux.k;
		subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, service});
	}
	dtdebugf("subscription_id={:d} adapter {:d} {:d}.{:03d}{:s}-{:d} {:d} use_count={:d}", (int) subscription_id,
					 fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());

	put_record(wtxn, fe);
	return 0;
}

template <typename mux_t>
devdb::fe_t devdb::fe::subscribe_fe_in_use(
	db_txn& wtxn, subscription_id_t subscription_id, fe_t& fe,
	const mux_t& mux, const chdb::service_t* service) {
	assert(fe.sub.subs.size()>=0); //==0 can happen when we are tuning to a different service on the same mux
	assert(fe.sub.owner == getpid());
	assert(fe.sub.config_id >=0);
	assert(is_same_stream(mux.k, fe.sub.mux_key));

	dtdebugf("subscribe_fe_in_use subscription_id={:d} adapter {:d} {:d}{:s}-{:d} {:d} use_count={:d}", (int) subscription_id,
					 fe.adapter_no, fe.sub.frequency/1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());
	assert((int)subscription_id >=0);
	reserve_fe_in_use(wtxn, subscription_id, fe, mux, service);
	return fe;
}

static std::atomic_int next_config_id{0};
/*
	sat == nullptr: subscriptuon can later change sat
*/
int devdb::fe::reserve_fe_lnb_for_sat_band(db_txn& wtxn, subscription_id_t subscription_id,
																					 const subscription_options_t& tune_options,
																					 devdb::fe_t& fe, const resource_subscription_counts_t& use_counts,
																					 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																					 const chdb::sat_t* sat,
																					 const chdb::band_scan_t* band_scan)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;

	if(use_counts.config_id<0) {
		assert(sub.config_id <0);
		assert(sub.owner <0);
		sub.config_id = next_config_id++;
		sub.owner = getpid();
		dtdebugf("new config_id={:d}", sub.config_id);
	} else {
		assert(use_counts.config_id == sub.config_id);
		assert(use_counts.owner == sub.owner);
		assert(sub.owner>=0);
		sub.owner = use_counts.owner;
		sub.config_id = use_counts.config_id;
		dtdebugf("existing config_id={:d}", sub.config_id);
	}
	//the following settings imply that we request a non-exclusive subscription
	sub.rf_path = rf_path;
	sub.sat_pos = sat ? sat->sat_pos : sat_pos_none;
	sub.pol = band_scan? band_scan->pol : chdb::fe_polarisation_t::NONE;
	sub.band = band_scan ? band_scan->sat_sub_band : chdb::sat_sub_band_t::NONE;
	sub.usals_pos = tune_options.may_move_dish ? sat_pos_none : lnb.usals_pos;
	sub.dish_usals_pos = lnb.on_positioner ? sub.usals_pos : lnb.usals_pos;
	sub.dish_id = lnb.k.dish_id;
	sub.frequency = 0;

	auto* conn = connection_for_rf_path(lnb, rf_path);
	sub.rf_coupler_id = conn ? conn->rf_coupler_id :-1;

	sub.mux_key = {};
	if(sat && band_scan) {
		dtdebugf("SUBSCRIBED subscription_id={:d} band={}/{}  adapter {:d} lnb={} use_count={:d}", (int) subscription_id,
						 *sat, *band_scan, fe.adapter_no, rf_path.lnb, fe.sub.subs.size());
		fe.sub.subs.push_back({(int)subscription_id, false /*has_mux*/, false /*has_service*/, *band_scan});
	} else {
		dtdebugf("SUBSCRIBED subscription_id={:d} adapter {:d} lnb={} use_count={:d}", (int) subscription_id,
						 fe.adapter_no, rf_path.lnb, fe.sub.subs.size());
		fe.sub.subs.push_back({(int)subscription_id, false /*has_mux*/, false /*has_service*/, chdb::service_t{}});
	}
	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::fe_t>: the updated version of the old (unsubscribed) fe
	bool is_master
 */
//@todo: replace this with subscribe_mux with sat and band_scan argument
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>, bool>
devdb::fe::subscribe_lnb_(db_txn& wtxn, subscription_id_t subscription_id,
												 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
												 const subscription_options_t&  tune_options,
												 std::optional<devdb::fe_t>& oldfe, const devdb::fe_key_t* fe_key_to_release)
{
	auto pol{chdb::fe_polarisation_t::NONE}; //signifies that we to exclusively control pol
	auto band{chdb::sat_sub_band_t::NONE}; //signifies that we to exclusively control band
	auto sat_pos{sat_pos_none}; //signifies that we want to be able to move rotor
	auto usals_pos{sat_pos_none}; //signifies that we want to be able to move rotor
	bool need_multistream = false;

	auto fe_and_use_counts = fe::find_best_fe_for_lnb(wtxn, rf_path, lnb,
																										fe_key_to_release, tune_options.use_blind_tune,
																										tune_options.need_spectrum,
																										need_multistream, sat_pos, pol, band, usals_pos,
																					false /*ignore_subscriptions*/);
	std::optional<devdb::fe_t> updated_old_dbfe;
	if(oldfe)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe->k);

	if(!fe_and_use_counts)
		return {{}, updated_old_dbfe, false}; //no frontend could be found
	auto& [best_fe, best_use_counts ] = *fe_and_use_counts;
	assert(tune_options.may_move_dish  && tune_options.may_control_lnb);
	bool is_master = !best_use_counts.is_shared();
#ifndef NDEBUG
	auto ret =
#endif
		devdb::fe::reserve_fe_lnb_for_sat_band(wtxn, subscription_id, tune_options, best_fe, best_use_counts, rf_path, lnb,
																				 nullptr /*sat*/ , nullptr /*band_scan*/);
	assert(ret==0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, updated_old_dbfe, is_master};
}


/*
	subscribe to a specific frontend and lnb, reusing resources of the current subscription
	and unssubcribing no longer needed resources
	Input:
	subscription_id>=0. An existing or new subscription id. This id is unique per process
	rf_path: defining  the allowed LNB and fe

  Return values:
	aa: only set in case a new active adapter must be created, otherwise empty
	aa_sub_to_reuse: if subscription_t::NONE, and aa is set, then the caller must create a new active adapter with
                      these parameters;
									 if equal to the subscription_id of the caller, the caller must reuse its current active_adapter
									    and use it
 */
subscribe_ret_t
devdb::fe::subscribe_rf_path_(db_txn& wtxn, subscription_id_t subscription_id,
														 const subscription_options_t& tune_options,
														 const rf_path_t& rf_path, std::optional<int16_t> sat_pos_to_move_to) {

	auto[ oldfe_, will_be_released ] = fe_for_subscription(wtxn, subscription_id);
	auto* fe_key_to_release = (oldfe_ && will_be_released) ? &oldfe_->k : nullptr;

	subscribe_ret_t sret(subscription_id, false /*failed*/);
	assert(tune_options.rf_path_is_allowed(rf_path));

	auto c = devdb::lnb_t::find_by_key(wtxn, rf_path.lnb);
	if (!c.is_valid()) {
		std::optional<devdb::fe_t> updated_old_dbfe;
		if(oldfe_) {
			assert((int)subscription_id >=0);
			updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe_->k);
		}
		return failed(subscription_id, updated_old_dbfe);
	}
	auto lnb = c.current();
	auto [fe_, updated_old_dbfe, is_master] = devdb::fe::subscribe_lnb_(
		wtxn, sret.subscription_id, rf_path, lnb, tune_options, oldfe_, fe_key_to_release);

	sret.retune = false;
	sret.tune_pars.send_lnb_commands = is_master;
	if(fe_) {
		assert(fe_.sub.owner == getpid());
		assert(fe_.sub.config_id >= 0);
		sret.tune_pars.owner = fe_->sub.owner;
		sret.tune_pars.config_id = fe_->sub.config_id;
		bool is_same_fe = oldfe_? (fe_->k == oldfe_->k) : false;
		sret.retune = is_same_fe;
		sret.aa = {.updated_old_dbfe = updated_old_dbfe, .updated_new_dbfe = fe_, .rf_path= rf_path, .lnb= lnb};

		if(!is_same_fe) {
			assert(sret.aa.is_new_aa());
			sret.sub_to_reuse = subscription_id_t::NONE;
		} else {
			assert(!sret.aa.is_new_aa());
		}
		if (sat_pos_to_move_to) {
			if(sret.tune_pars.send_lnb_commands)  {
				std::tie(sret.tune_pars.move_dish, sret.tune_pars.dish)
					=  update_dish_helper(wtxn, *sret.aa.lnb, rf_path, tune_options.usals_location,
																*sat_pos_to_move_to, tune_options.may_move_dish);
				if(!sret.tune_pars.dish)
					return failed(sret.subscription_id, updated_old_dbfe);
			} else {
				sret.tune_pars.move_dish = false;
				sret.tune_pars.dish = devdb::dish::get_dish(wtxn, lnb.k.dish_id);
			}
		}
		return sret;
	} else {
		auto c = fe_t::find_by_card_mac_address(wtxn, rf_path.card_mac_address, find_type_t::find_eq,
																						fe_t::partial_keys_t::card_mac_address);
		if(c.is_valid()) {
			auto fe = c.current();
			user_errorf("Cannot currently use LNB {} with card {}", lnb, fe.card_short_name);
		} else {
			user_errorf("Cannot currently use LNB {}", lnb);
		}
	}

	if(oldfe_)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe_->k);
	return failed(subscription_id, updated_old_dbfe);
}


subscribe_ret_t
devdb::fe::	subscribe_rf_path(db_txn& devdb_wtxn, subscription_id_t subscription_id,
															const subscription_options_t& tune_options,
															const rf_path_t& rf_path,
															std::optional<int16_t> sat_pos_to_move_to) {
	auto sret = subscribe_rf_path_(devdb_wtxn, subscription_id, tune_options, rf_path,
																sat_pos_to_move_to);
	if(!sret.failed) {
		sret.tune_pars.send_lnb_commands = true;
		sret.tune_pars.move_dish=true;
		sret.tune_pars.dish = devdb::dish::get_dish(devdb_wtxn, sret.aa.lnb->k.dish_id);
	}
	return sret;
}

int devdb::fe::reserve_fe_lnb_for_mux(db_txn& wtxn, subscription_id_t subscription_id,
																			devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																			const devdb::lnb_t& lnb, const resource_subscription_counts_t& use_counts,
																			const chdb::dvbs_mux_t& mux, const chdb::service_t* service)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;

	if(use_counts.config_id<0) {
		assert(sub.config_id <0);
		assert(sub.owner <0);
		sub.config_id = next_config_id++;
		sub.owner = getpid();
		dtdebugf("new config_id={:d}", sub.config_id);
	} else {
		assert(sub.config_id <0 || use_counts.config_id == sub.config_id);
		assert(sub.owner<0 || use_counts.owner == sub.owner);
		assert(use_counts.owner>=0);
		sub.owner = use_counts.owner;
		sub.config_id = use_counts.config_id;
		dtdebugf("existing config_id={:d}", sub.config_id);
	}

	//the following settings imply that we request a non-exclusive subscription
	sub.rf_path = rf_path;
	sub.sat_pos = mux.k.sat_pos;
	sub.pol = mux.pol;
	sub.band = 	devdb::lnb::band_for_mux(lnb, mux);
	sub.usals_pos = lnb.usals_pos;
	sub.dish_usals_pos = lnb.on_positioner ? sub.usals_pos : lnb.usals_pos;
	sub.dish_id = lnb.k.dish_id;
	sub.frequency = mux.frequency;

	auto* conn = connection_for_rf_path(lnb, rf_path);
	sub.rf_coupler_id = conn ? conn->rf_coupler_id :-1;

	sub.mux_key = mux.k;
	sub.mux_key.t2mi_pid = -1;
	if(service)
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else {
		chdb::service_t service;
		service.k.mux = mux.k;
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, service});
	}
	dtdebugf("subscription_id={:d} adapter {:d} {:d}.{:03d}{:s}-{:d} mux_id={:d} sat={} lnb={}  use_count={:d}",
					 (int) subscription_id, fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id,
					 chdb::sat_pos_str(sub.sat_pos), lnb, fe.sub.subs.size());

	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::lnb_t>: the newly subscribed lnb
	devdb::resource_subscription_counts_t
	int: the use_count after releasing fe_to_release, or 0 if fe_to_release=={}
*/
//TODO: make this return subscribe_ret_t
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t, std::optional<devdb::fe_t> >
devdb::fe::subscribe_mux_helper(db_txn& wtxn, subscription_id_t subscription_id,
												 const chdb::dvbs_mux_t& mux,
												 const chdb::service_t* service,
												 const subscription_options_t& tune_options,
												 const std::optional<devdb::fe_t>& oldfe,
												 const devdb::fe_key_t* fe_key_to_release,
												 bool do_not_unsubscribe_on_failure) {
	auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_mux(wtxn, mux,
																					tune_options,
																					fe_key_to_release,
																					false /*ignore_subscriptions*/);
	if(do_not_unsubscribe_on_failure && ! best_fe)
		return {{}, {}, {}, {}, {}}; //no frontend could be found
	std::optional<devdb::fe_t> updated_old_dbfe;
	if(oldfe)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe->k);
	if(!best_fe)
		return {{}, {}, {}, {}, updated_old_dbfe}; //no frontend could be found
#ifndef NDEBUG
	auto ret =
#endif
		devdb::fe::reserve_fe_lnb_for_mux(wtxn, subscription_id, *best_fe, *best_rf_path, *best_lnb, best_use_counts, mux,
																							 service);
	assert(ret==0); //reservation cannot fail as we have a write lock on the db

	return {best_fe, best_rf_path, best_lnb, best_use_counts, updated_old_dbfe};
}

template<typename mux_t>
int devdb::fe::reserve_fe_for_dvbc_or_dvbt_mux(db_txn& wtxn, subscription_id_t subscription_id, devdb::fe_t& fe,
																	const mux_t& mux, const chdb::service_t* service)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;


	//the following settings imply that we request a non-exclusive subscription
	rf_path_t rf_path;
	rf_path.card_mac_address = fe.card_mac_address;
	rf_path.rf_input = 0;

	assert(fe.sub.owner == -1);
	assert(fe.sub.config_id = -1);

	sub.owner = getpid();
	sub.config_id = next_config_id++;

	sub.rf_path = rf_path;
	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.sat_pos = mux.k.sat_pos;
	sub.band = chdb::sat_sub_band_t::NONE;
	sub.usals_pos = mux.k.sat_pos;
	sub.frequency = mux.frequency; //for informational purposes only
	sub.rf_coupler_id  = -1;
	sub.mux_key = mux.k;
	sub.mux_key.t2mi_pid = -1;
	if (service)
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else {
		chdb::service_t service;
		service.k.mux = mux.k;
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, service});
	}
	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::fe_t>: the updated old (unsubscribed) fe
 */
template<typename mux_t>
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>>
devdb::fe::subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, subscription_id_t subscription_id,
																			const mux_t& mux, const chdb::service_t* service,
																			const std::optional<devdb::fe_t>& oldfe,
																			const devdb::fe_key_t* fe_key_to_release,
																			const subscription_options_t& tune_options,
																			bool do_not_unsubscribe_on_failure) {

	assert(!tune_options.need_spectrum);
	const bool need_multistream = (mux.k.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, fe_key_to_release, tune_options.use_blind_tune,
																										tune_options.need_spectrum, need_multistream,
																										delsys_type, false /*ignore_subscriptions*/);
	if(do_not_unsubscribe_on_failure && !best_fe)
		return {best_fe, {}}; //no frontend could be found

	std::optional<devdb::fe_t> updated_old_dbfe;
	if(oldfe)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe->k);

	if(!best_fe)
		return {best_fe, updated_old_dbfe}; //no frontend could be found

#ifndef NDEBUG
	auto ret =
#endif
		devdb::fe::reserve_fe_for_dvbc_or_dvbt_mux(wtxn, subscription_id, *best_fe, mux, service);
	assert(ret == 0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, updated_old_dbfe};
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::lnb_t>: the newly subscribed lnb
	devdb::resource_subscription_counts_t
	int: the use_count after releasing fe_to_release, or 0 if fe_to_release=={}
*/
//TODO: make this return subscribe_ret_t
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t, std::optional<devdb::fe_t> >
devdb::fe::subscribe_sat_band_(db_txn& wtxn, subscription_id_t subscription_id,
															const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
															const subscription_options_t& tune_options,
															const std::optional<devdb::fe_t>& oldfe,
															const devdb::fe_key_t* fe_key_to_release,
															bool do_not_unsubscribe_on_failure) {
	auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
		fe::find_fe_and_lnb_for_tuning_to_band(wtxn, sat, band_scan,
																					tune_options,
																					fe_key_to_release,
																					 false /*ignore_subscriptions*/);
	if(do_not_unsubscribe_on_failure && ! best_fe)
		return {{}, {}, {}, {}, {}}; //no frontend could be found
	std::optional<devdb::fe_t> updated_old_dbfe;
	if(oldfe)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe->k);
	if(!best_fe)
		return {{}, {}, {}, {}, updated_old_dbfe}; //no frontend could be found

#ifndef NDEBUG
	auto ret =
#endif
		devdb::fe::reserve_fe_lnb_for_sat_band(wtxn, subscription_id,
																					 tune_options,
																					 *best_fe, best_use_counts, *best_rf_path, *best_lnb,
																					 &sat, &band_scan);

	assert(ret==0); //reservation cannot fail as we have a write lock on the db

	return {best_fe, best_rf_path, best_lnb, best_use_counts, updated_old_dbfe};
}

/*
	subscribe to a specific mux, reusing resources of existing subscriptions (including the
	current one) when possible and unssubcribing no longer needed resources
	Input:
	subscription_id>=0. An existing or new subscription id. This id is unique per process
	mux: the subscription will perform a non-exclusive reservation of this mux,
			 on any lnb/fe (if tune_options.allowed_rf_paths is null) or on the fe/lnb specified in
			 tune_options.allowed_rf_paths
	service: if non-null, then mux must also be non-null and in addition to reserving the mux,
	     the service will be reserved as well

  Return values:
	aa: only set in case a new active adapter must be created, otherwise empty
	aa_sub_to_reuse: if subscription_t::NONE, and aa is set, then the caller must create a new active adapter with
                      these parameters;
                   if subscription_t::NONE, aa is not set, then the caller requested a reservation
											for a mux it has already subscribed, and active_adapter remains the same
									 if equal to the subscription_id of the caller, the caller must reuse its current active_adapter
									    but retune to the new mux
									 if other value: the caller should reuse the active adapter for that other subscription_id;
	If the caller does not reuse its existing aa, then it should  release it and deactivate
	it when it is the last user;

 */
template<typename mux_t>
subscribe_ret_t
devdb::fe::subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
										 const subscription_options_t& tune_options,
										 const mux_t& mux,
										 const chdb::service_t* service,
										 bool do_not_unsubscribe_on_failure) {

	auto[ oldfe_, will_be_released ] = fe_for_subscription(wtxn, subscription_id);
	auto* fe_key_to_release = (oldfe_ && will_be_released) ? &oldfe_->k : nullptr;
	std::optional<devdb::fe_t> updated_old_dbfe;

	//try to reuse existing active_adapter and active_service as-is
	if(service) {
		auto [fe_, idx] = matching_existing_subscription(wtxn,
																										 tune_options,
																										 &mux, service,
																										 false /*match_mux_only*/);
		if(fe_) {
			auto & fe = *fe_;
			auto& sub = fe.sub.subs[idx];
			if (sub.subscription_id == (int)subscription_id)
				return no_change(subscription_id, fe_);  //already subscribed and no change in lnb, mux or service
			else {
				if(oldfe_) {
					assert((int) subscription_id >=0);
					/*we need to unsubscribe first, because otherwise we may end up
						adding two subscriptions with the same subscription_id, which
						makes it impossible to unsubscribe, as that is done by subscription_id*/
					updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe_->k);
					if(updated_old_dbfe && updated_old_dbfe->k == fe.k) {
						/*in this case, removing our old subscription
							may have removed all old subscriptions, which then
							resulted in clearing update_old_fe.sub;
							Also update_old_fe.sub has one fewer subscription
						*/
						fe.sub.subs = updated_old_dbfe->sub.subs;
						updated_old_dbfe = {};
					}
				}
				if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
					assert(fe.sub.subs.size() ==1 ||
								 (!tune_options.may_move_dish  && !tune_options.may_control_lnb &&
									!fe_subscription::may_move_dish(fe.sub) && ! fe_subscription::may_change_lnb(fe.sub)));
				}
				auto sret = reuse_other_subscription(subscription_id, sub.subscription_id, fe_, updated_old_dbfe);
				subscribe_fe_in_use(wtxn, sret.subscription_id, fe, mux, service);
				return sret;
			}
		}
	}

	//try to reuse existing mux as-is, except that we will also add a service, or just use the tuned mux
	if(true) {
		auto [fe_, idx] = matching_existing_subscription(wtxn,
																										 tune_options,
																										 &mux, service,
																										 true /*match_mux_only*/);
		if(fe_) {
			auto& fe = *fe_;
			auto& sub_to_reuse=fe.sub.subs[idx];
			if(oldfe_) {
				assert((int)subscription_id >=0);
			/*we can reuse an existing active_mux, but need to add an active service
				Ee need to unsubscribe first, because otherwise we may end up
				adding two subscriptions with the same subscription_id, which
				makes it impossible to unsubscribe, as that is done by subscription_id
			*/
				updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe_->k);
				if(updated_old_dbfe && updated_old_dbfe->k == fe.k) {
					/*in this case, removing our old subscription
						may have removed all old subscriptions, which then
						resulted in clearing update_old_fe.sub;
						Also update_old_fe.sub has one fewer subscription
					*/
					fe.sub.subs = updated_old_dbfe->sub.subs;
				}

			}
			if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
				assert(fe.sub.subs.size() ==1 ||
							 (!tune_options.may_move_dish  && !tune_options.may_control_lnb &&
								!fe_subscription::may_move_dish(fe.sub) && ! fe_subscription::may_change_lnb(fe.sub)));
			}
			auto sret = new_service(subscription_id, sub_to_reuse.subscription_id, fe_, updated_old_dbfe);
			//we can reuse an existing active_mux, but need to add an active service
			subscribe_fe_in_use(wtxn, sret.subscription_id, fe, mux, service);
			return sret;
		}
	}

	/*
		At this stage, either we have to retune our own active_adapter or we have to create a new one
	 */

	subscribe_ret_t sret(subscription_id, false /*failed*/);

	if(true) {
		std::optional<devdb::fe_t> fe_;
		std::optional<devdb::rf_path_t> rf_path_;
		std::optional<devdb::lnb_t> lnb_;
		devdb::resource_subscription_counts_t use_counts_;
		std::optional<devdb::fe_t> updated_old_dbfe;

		if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
			std::tie(fe_, rf_path_, lnb_, use_counts_, updated_old_dbfe) =
				devdb::fe::subscribe_mux_helper(
					wtxn, sret.subscription_id, mux, service,
					tune_options,
					oldfe_, fe_key_to_release,
					do_not_unsubscribe_on_failure);
		} else {
			std::tie(fe_, updated_old_dbfe) =
			devdb::fe::subscribe_dvbc_or_dvbt_mux(
				wtxn, sret.subscription_id, mux, service, oldfe_, fe_key_to_release,
				tune_options,
				do_not_unsubscribe_on_failure);
		}
		if(fe_) {
#ifdef TODO //positioner dialog calls like this using "subscribe_mux" instead of "subscribe_lnb_and_mux"
			if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
				assert(!tune_options.may_control_lnb);
				assert(!tune_options.may_control_dish);
			}
#endif
			auto& fe = *fe_;
			bool is_same_fe = oldfe_? (fe.k == oldfe_->k) : false;
			sret.retune = is_same_fe;
			sret.change_service = true;
			assert(fe.sub.owner != -1);
			assert(fe.sub.config_id >= 0);
			sret.tune_pars.owner = fe.sub.owner;
			sret.tune_pars.config_id = fe.sub.config_id;

			if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
				sret.tune_pars.send_lnb_commands = ! use_counts_.is_shared();
				assert(fe.sub.owner == getpid());
				assert(fe.sub.config_id >=0);
				sret.tune_pars.owner = fe.sub.owner;
				sret.tune_pars.config_id = fe.sub.config_id;
				sret.aa = {.updated_old_dbfe = updated_old_dbfe, .updated_new_dbfe = fe_, .rf_path= rf_path_, .lnb= *lnb_};
			} else {
				sret.aa = { .updated_old_dbfe = updated_old_dbfe, .updated_new_dbfe = fe, .rf_path={}, .lnb={}};
			}
			if(!is_same_fe) {
				assert(sret.aa.is_new_aa());
				sret.sub_to_reuse = subscription_id_t::NONE;
				dtdebugf("fe::subscribe: newaa subscription_id={}  adapter={:x}/{} mux={}",
								 (int ) subscription_id, (int64_t) fe.k.adapter_mac_address,
								 (int) oldfe_->k.frontend_no, mux);
			} else {
				assert(!sret.aa.is_new_aa());
				dtdebugf("fe::subscribe: no newaa subscription_id={} adapter={:x} mux={}",
								 (int)subscription_id, (int64_t) fe.k.adapter_mac_address, mux);
			}
			if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
				auto& lnb = *lnb_;
				if(sret.tune_pars.send_lnb_commands)  {
					std::tie(sret.tune_pars.move_dish, sret.tune_pars.dish)
						=  update_dish_helper(wtxn, *sret.aa.lnb, *rf_path_, tune_options.usals_location,
																	mux.k.sat_pos, tune_options.may_move_dish);
					if(!sret.tune_pars.dish)
						return failed(sret.subscription_id, updated_old_dbfe);
				} else {
					sret.tune_pars.move_dish = false;
					sret.tune_pars.dish = devdb::dish::get_dish(wtxn, lnb.k.dish_id);
				}
			}
			return sret;
		}
		return failed(sret.subscription_id, updated_old_dbfe);
	}
}

subscribe_ret_t
devdb::fe::subscribe_sat_band(db_txn& wtxn, subscription_id_t subscription_id,
										 const subscription_options_t& tune_options,
										 const chdb::sat_t& sat,
										 const chdb::band_scan_t& band_scan,
										 bool do_not_unsubscribe_on_failure) {

	auto[ oldfe_, will_be_released ] = fe_for_subscription(wtxn, subscription_id);
	auto* fe_key_to_release = (oldfe_ && will_be_released) ? &oldfe_->k : nullptr;

	subscribe_ret_t sret(subscription_id, false /*failed*/);

	auto [fe_, rf_path_, lnb_, use_counts_, updated_old_dbfe] =
		devdb::fe::subscribe_sat_band_(wtxn, sret.subscription_id, sat, band_scan,
																	 tune_options, oldfe_, fe_key_to_release, do_not_unsubscribe_on_failure);
		if(fe_) {
			auto& fe = *fe_;
			bool is_same_fe = oldfe_? (fe.k == oldfe_->k) : false;
			sret.retune = is_same_fe;
			sret.change_service = true;
			sret.tune_pars.send_lnb_commands = ! use_counts_.is_shared();
			assert(fe.sub.owner != -1);
			assert(fe.sub.config_id >= 0);
			sret.tune_pars.owner = fe.sub.owner;
			sret.tune_pars.config_id = fe.sub.config_id;

			sret.aa = {.updated_old_dbfe = updated_old_dbfe, .updated_new_dbfe = fe_, .rf_path= rf_path_, .lnb= *lnb_};
			if(!is_same_fe) {
				assert(sret.aa.is_new_aa());
				sret.sub_to_reuse = subscription_id_t::NONE;
				dtdebugf("fe::subscribe: newaa subscription_id={}  adapter={:x}/{} sat={}",
								 (int ) subscription_id, (int64_t) fe.k.adapter_mac_address,
								 (int) oldfe_->k.frontend_no, sat);
			} else {
				assert(!sret.aa.is_new_aa());
				dtdebugf("fe::subscribe: no newaa subscription_id={} adapter={:x} sat={}",
								 (int)subscription_id, (int64_t) fe.k.adapter_mac_address, sat);
			}
			auto& lnb = *sret.aa.lnb; //use the version of the lnb with the updated usals_pos

			if(sret.tune_pars.send_lnb_commands)  {
				std::tie(sret.tune_pars.move_dish, sret.tune_pars.dish)
					= update_dish_helper(wtxn, *sret.aa.lnb, *rf_path_, tune_options.usals_location, sat.sat_pos,
															 tune_options.may_move_dish);
				if(!sret.tune_pars.dish)
					return failed(sret.subscription_id, updated_old_dbfe);
			}
			else {
				sret.tune_pars.move_dish=false;
				sret.tune_pars.dish= devdb::dish::get_dish(wtxn, lnb.k.dish_id);
			}
			return sret;
		}
		return failed(sret.subscription_id, updated_old_dbfe);

	if(oldfe_)
		updated_old_dbfe = unsubscribe(wtxn, subscription_id, oldfe_->k);
	return failed(subscription_id, updated_old_dbfe);
}

/*
	return a frontend that already is tuning the mux and/or service, by returning
	the frontend and the index of the exising subscription
	Special cases:
    -subscription_id = None => the mux was already tuned by this subscription
    -subscription_id = same as input => active adapter can be kept but must be retuned
		-subscription_id != input => caller needs to referene the existing active adapter
		 and release its old one
 */
template<typename mux_t>
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const subscription_options_t& tune_options,
																					const mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only) {
	auto owner = getpid();
	using namespace chdb;
	auto c = find_first<devdb::fe_t>(wtxn);

	for(auto fe: c.range()) {
		if(fe.sub.owner != owner)
			continue;
		int idx=0;
		if(mux && (mux->k.mux_id ==0 || fe.sub.mux_key.mux_id ==0)) {
			/*the existing subscription or the desired one is for a frequency peak.
				In this case, we prevent reuse
			*/
			continue;
		}
		for(auto & sub: fe.sub.subs) { //loop over all subscriptions
			assert(sub.sat_pos == sub.mux_key.sat_pos);
			bool rf_path_matches = true;
			if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
				rf_path_matches = tune_options.rf_path_is_allowed(fe.sub.rf_path);
			}
			auto* sub_service = std::get_if<chdb::service_t>(&sub.v);
			bool mux_matches = mux ? (mux->k == fe.sub.mux_key ||
																(sub_service && sub.has_mux &&  mux->k == sub_service->k.mux)) : !sub.has_mux;
			bool service_matches = service ? (sub.has_service &&  sub_service &&
																				service->k == sub_service->k) : ! sub.has_service;
			service_matches |= match_mux_only;
			//in case we only need a mux, we also check for a match in frequency
			if(rf_path_matches && sub_service && mux && !mux_matches && (!service || match_mux_only)) {
				//perhaps the frequency matches but not the mux key
				mux_t m;
				m.k = sub_service->k.mux;
				set_member(m, frequency, fe.sub.frequency);
				set_member(m, pol, fe.sub.pol);
				mux_matches = chdb::matches_physical_fuzzy(*mux, m, true /*check_sat_pos*/,
																									 true /*ignore_t2mi_pid*/);
			}
			if(rf_path_matches && mux_matches && service_matches) {
				return {fe, idx};
			}
			++idx;
		}
	}
	return {{}, -1};
}

//instantiations

template std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, subscription_id_t subscription_id,
																												const chdb::dvbc_mux_t& mux,
																												const chdb::service_t* service,
																												const std::optional<devdb::fe_t>& oldfe,
																												const devdb::fe_key_t* fe_key_to_release,
																												const subscription_options_t& tune_options,
																												bool do_not_unsubscribe_on_failure);

template std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, subscription_id_t subscription_id,
																												const chdb::dvbt_mux_t& mux,
																												const chdb::service_t* service,
																												const std::optional<devdb::fe_t>& oldfe,
																												const devdb::fe_key_t* fe_key_to_release,
																												const subscription_options_t& tune_options,
																												bool do_not_unsubscribe_on_failure);
template
subscribe_ret_t
devdb::fe::subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
												 const subscription_options_t& tune_options,
												 const chdb::dvbs_mux_t& mux,
												 const chdb::service_t* service,
												 bool do_not_unsubscribe_on_failure);

template
subscribe_ret_t
devdb::fe::subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
												 const subscription_options_t& tune_options,
												 const chdb::dvbc_mux_t& mux,
												 const chdb::service_t* service,
												 bool do_not_unsubscribe_on_failure);

template
subscribe_ret_t
devdb::fe::subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
												 const subscription_options_t& tune_options,
												 const chdb::dvbt_mux_t& mux,
												 const chdb::service_t* service,
												 bool do_not_unsubscribe_on_failure);

template
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const subscription_options_t& tune_options,
																					const chdb::dvbs_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only);

template
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const subscription_options_t& tune_options,
																					const chdb::dvbc_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only);
template
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const subscription_options_t& tune_options,
																					const chdb::dvbt_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only);


std::atomic_int subscribe_ret_t::next_subscription_id{0};

/*
	Types of subscriptions:
	1. exclusive: the frontend can change to another band, another lnb or move a dish.
	No resource reuse is possible: it is not possible to reuse the output of the tuner of
	the frontend. It is also not possible to use other tuners, via an rf splitter

	This is indicated by sat_pos==sat_pos_none and usals_pos==sat_pos_none in the subscription, along with other fields
	that are set to non-specific values, e.g., mux_key

	2. non-dish-moving; almost the same as exclusive, but subscription cannot change the dish position
	after initial tuning. This is indicated by usals_pos set to a value different from sat_pos_none, i.e.
	to the position the dish will tune to.

	This means it is possible to use other connections of a quad lnb or from an offset lnb on the same
	dish using another tuner and frontend

	3. sat_band: the frontend will not change the rf_connection, i.e., will not switch to another lnb.
	It will also not change to a different band or polarisation on the lnb. It will not move
	the dish if the lnb is on the dish.

	This is indicated by sat_pos!=sat_pos_none, usals_pos!=sat_pos_none, pol!=NONE, and sat_sub_band!= None

	This means it is possible to use this specific sat band using another demodulator on the same card,
	connected to the same tuner as the subscription. This includes spectrum scanning and tuning to
	specific muxes in that band.

	It is also possible to use another connection of the same lnb (using another lnb connection or via
	an rf splitter) using another tuner/connector on the same
	card, or using another card for tuning the same band, or a mux or service on that same band.

	4. mux: the frontend will not change the rf_connection, i.e., will not switch to another lnb.
	It will also not change to a different band or polarisation on the lnb. It will not move
	the dish if the lnb is on the dish. In addition it will also not tune its frontend to another
	mux

	This is indicated by sat_pos!=sat_pos_none, usals_pos!=sat_pos_none, pol!=NONE, and sat_sub_band!= None
	and mux_key.sat_pos != sat_pos_none

	This means it is possible to use this specific sat band using another demodulator on the same card,
	connected to the same tuner as the subscription. This includes spectrum scanning and tuning to
	specific muxes in that band.

	It is also possible to use another connection of the same lnb (using another lnb connection or via
	an rf splitter) using another tuner/connector on the same
	card, or using another card for tuning the same band, or a mux or service on that same band.

	Finally, it is also possible to just use the frontend itself for tuning the same mux (not very useul)
	or for tuning different services on that mux.




 */
