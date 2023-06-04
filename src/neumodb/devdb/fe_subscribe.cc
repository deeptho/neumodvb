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



static inline subscribe_ret_t no_change(subscription_id_t subscription_id, int other_subscription_id) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	sret.sub_to_reuse = (subscription_id_t) other_subscription_id;
	return sret;
}

static inline subscribe_ret_t reuse_other_subscription(subscription_id_t subscription_id,
																																	int other_subscription_id) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	sret.sub_to_reuse = (subscription_id_t) other_subscription_id;
	return sret;
}

static inline subscribe_ret_t new_service(subscription_id_t subscription_id,
																										 int other_subscription_id) {
	subscribe_ret_t sret{subscription_id, false/*failed*/};
	/* ret.aa_sub_to_reuse = subscribe_ret_t::NONE : keep active_adapter and do not retune*/
	sret.sub_to_reuse = (subscription_id_t) other_subscription_id;
	sret.change_service = true;
	return sret;
}

static inline subscribe_ret_t failed(subscription_id_t subscription_id) {
	subscribe_ret_t sret{subscription_id, true /*failed*/};
	sret.failed=true;
	return sret;
}

#if 0
static inline devdb::fe::subscribe_ret_t lnb_only_change(subscription_id_t subscription_id) {
	devdb::fe::subscribe_ret_t ret;
	ret.sub_to_reuse = (subscription_id_t) subscription_id;
	return ret;
}
#endif


static std::tuple<std::optional<devdb::fe_t>, int>
fe_for_subscription(db_txn& rtxn, subscription_id_t subscription_id)
{
	if((int) subscription_id <0)
		return {{},-1};
	using namespace devdb::fe;
	auto c = find_first<devdb::fe_t>(rtxn);
	auto owner = getpid();

	for(auto fe: c.range()) {
		if(fe.sub.owner != owner)
			continue;
		int idx=0;
		for(const auto& d: fe.sub.subs) {
			if(d.subscription_id == (int)subscription_id)
				return {fe, idx};
			++idx;
		}
	}
	return {{},-1};
}

template<typename mux_t>
int devdb::fe::reserve_fe_in_use(db_txn& wtxn, subscription_id_t subscription_id,
																 devdb::fe_t& fe,  const mux_t& mux, const chdb::service_t* service)
{
	auto& subs = fe.sub.subs;
	if(service)
		subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else
		subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, {}});
	dtdebugx("adapter %d %d.%03d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());

	put_record(wtxn, fe);
	return 0;
}

template <typename mux_t>
std::tuple<devdb::fe_t, int> devdb::fe::subscribe_fe_in_use(
	db_txn& wtxn, subscription_id_t subscription_id, fe_t& fe,
	const mux_t& mux, const chdb::service_t* service, const devdb::fe_key_t* fe_key_to_release) {
	int released_fe_usecount{0};
	assert(fe.sub.subs.size()>=1);
	assert(is_same_stream(mux.k, fe.sub.mux_key));
	dtdebugx("adapter %d %d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());

	reserve_fe_in_use(wtxn, subscription_id, fe, mux, service);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, subscription_id, *fe_key_to_release);

	return {fe, released_fe_usecount};
}


int devdb::fe::reserve_fe_lnb_exclusive(db_txn& wtxn, subscription_id_t subscription_id,
																				devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																				const devdb::lnb_t& lnb)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	assert(sub.subs.size() == 0);
	sub.owner = getpid();
	assert(sub.subs.size()==0);

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
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());
	fe.sub.subs.push_back({(int)subscription_id, false /*has_mux*/, false /*has_service*/, {}});
	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	int: the use_count after releasing fe_kkey_to_release, or 0 if fe_key_to_release==nullptr
 */
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_lnb_exclusive(db_txn& wtxn, subscription_id_t subscription_id,
																	 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																	 const devdb::fe_key_t* fe_key_to_release,
																	 bool need_blind_tune, bool need_spectrum,
																	 const devdb::usals_location_t& loc) {
	auto pol{chdb::fe_polarisation_t::NONE}; //signifies that we to exclusively control pol
	auto band{fe_band_t::NONE}; //signifies that we to exclusively control band
	auto usals_pos{sat_pos_none}; //signifies that we want to be able to move rotor
	bool need_multistream = false;
	int released_fe_usecount{0};
	auto best_fe = fe::find_best_fe_for_lnb(wtxn, rf_path, lnb,
																					fe_key_to_release, need_blind_tune, need_spectrum,
																					need_multistream, pol, band, usals_pos,
																					false /*ignore_subscriptions*/);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, subscription_id, *fe_key_to_release);

	if(!best_fe)
		return {best_fe, released_fe_usecount}; //no frontend could be found

	auto ret = devdb::fe::reserve_fe_lnb_exclusive(wtxn, subscription_id, *best_fe, rf_path, lnb);
	assert(ret==0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, released_fe_usecount};
}

int devdb::fe::reserve_fe_lnb_for_mux(db_txn& wtxn, subscription_id_t subscription_id,
																			devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																			const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
																			const chdb::service_t* service)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
	assert(sub.subs.size() == 0);
	sub.owner = getpid();

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
	sub.mux_key.t2mi_pid = -1;
	if(service)
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else {
		chdb::service_t service;
		service.k.mux = mux.k;
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, service});
	}
	dtdebugx("adapter %d %d.%03d%s-%d %d use_count=%d", fe.adapter_no, fe.sub.frequency/1000, fe.sub.frequency%1000,
					 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());

	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	std::optional<devdb::lnb_t>: the newly subscribed lnb
	devdb::resource_subscription_counts_t
	int: the use_count after releasing fe_key_to_release, or 0 if fe_key_to_release==nullptr
*/
std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
					 devdb::resource_subscription_counts_t, int>
devdb::fe::subscribe_lnb_band_pol_sat(db_txn& wtxn, subscription_id_t subscription_id,
																			const chdb::dvbs_mux_t& mux,
																			const chdb::service_t* service,
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
		released_fe_usecount = unsubscribe(wtxn, subscription_id, *fe_key_to_release);
	if(!best_fe)
		return {{}, {}, {}, {}, released_fe_usecount}; //no frontend could be found
	if(fe_key_to_release && best_fe->k == *fe_key_to_release)
		released_fe_usecount++;
	auto ret = devdb::fe::reserve_fe_lnb_for_mux(wtxn, subscription_id, *best_fe, *best_rf_path, *best_lnb, mux,
																							 service);
	best_use_counts.dish++;
	best_use_counts.rf_path++;
	best_use_counts.rf_coupler++;
	best_use_counts.tuner++;
	assert(ret==0); //reservation cannot fail as we have a write lock on the db

	return {best_fe, best_rf_path, best_lnb, best_use_counts, released_fe_usecount};
}

template<typename mux_t>
int devdb::fe::reserve_fe_for_mux(db_txn& wtxn, subscription_id_t subscription_id, devdb::fe_t& fe,
																	const mux_t& mux, const chdb::service_t* service)
{
	auto c = devdb::fe_t::find_by_key(wtxn, fe.k);
	if( !c.is_valid())
		return -1;
	fe = c.current();
	auto& sub = fe.sub;
#if 0
	assert(sub.subs.size() == 0);
	assert(sub.subs.size()==0);
#endif
	//the following settings imply that we request a non-exclusive subscription
	rf_path_t rf_path;
	rf_path.card_mac_address = fe.card_mac_address;
	rf_path.rf_input = 0;


	sub.owner = getpid();
	sub.rf_path = rf_path;
	sub.pol = chdb::fe_polarisation_t::NONE;
	sub.band = devdb::fe_band_t::NONE;
	sub.usals_pos = mux.k.sat_pos;
	sub.frequency = mux.frequency; //for informational purposes only
	sub.rf_coupler_id  = -1;
	sub.mux_key = mux.k;
	sub.mux_key.t2mi_pid = -1;
	if (service)
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, true /*has_service*/, *service});
	else
		fe.sub.subs.push_back({(int)subscription_id, true /*has_mux*/, false /*has_service*/, {}});
	put_record(wtxn, fe);
	return 0;
}

/*
	returns
	std::optional<devdb::fe_t>: the newly subscribed fe
	int: the use_count after releasing fe_kkey_to_release, or 0 if fe_key_to_release==nullptr
 */
template<typename mux_t>
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, subscription_id_t subscription_id,
																			const mux_t& mux, const chdb::service_t* service,
																			const devdb::fe_key_t* fe_key_to_release,
																			bool use_blind_tune) {

	const bool need_spectrum{false};
	int released_fe_usecount{0};
	const bool need_multistream = (mux.k.stream_id >= 0);
	const auto delsys_type = chdb::delsys_type_for_mux_type<mux_t>();
	auto best_fe = devdb::fe::find_best_fe_for_dvtdbc(wtxn, fe_key_to_release, use_blind_tune,
																										need_spectrum, need_multistream,  delsys_type,
																										false /*ignore_subscriptions*/);
	if(fe_key_to_release)
		released_fe_usecount = unsubscribe(wtxn, subscription_id, *fe_key_to_release);

	if(!best_fe)
		return {best_fe, released_fe_usecount}; //no frontend could be found

	if(fe_key_to_release && best_fe->k == *fe_key_to_release)
		released_fe_usecount++;
	auto ret = devdb::fe::reserve_fe_for_mux(wtxn, subscription_id, *best_fe, mux, service);
	assert(ret == 0); //reservation cannot fail as we have a write lock on the db
	return {best_fe, released_fe_usecount};
}

/*
	subscribe to a frontend and lnb, reusing resources of existing subscriptions (including the
	current one) when possible and unssubcribing no longer needed resources
	Input:
	subscription_id>=0. An existing or new subscription id. This id is unique per process
	require_rf_path: if non-null, this will restrict allowed LNB and fe
	mux: if null, then the subscription will perform an exclusive reservation of the lnb and fe
	              specified in required_rf_path
	     if non null, then the subscription will perform a non-exclusive reservation of this mux,
			 on any lnb/fe (if required_rf_path is null) or on the fe/lnb specified in required_rf_path
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

	service_sub_to_reuse: if subscription_id::NONE, then the caller either did not reserve a service, or the caller
	                         already had this service reserved and nothing has changed
												if equal to the caller's subscription id, then the caller must create a
												   new active_service for the service it wanted to subscribe and remove the old one
												if other value: the caller should reuse the active_service for that other subscription_id
 */
subscribe_ret_t
devdb::fe::subscribe(db_txn& wtxn, subscription_id_t subscription_id,
										 const devdb::rf_path_t* required_rf_path,
										 const chdb::dvbs_mux_t* mux,
										 const chdb::service_t* service,
										 bool use_blind_tune, bool may_move_dish,
										 int dish_move_penalty, int resource_reuse_bonus,
										 bool need_blindscan, bool need_spectrum, const devdb::usals_location_t& loc) {

	auto[ oldfe_, idx] = fe_for_subscription(wtxn, subscription_id);
	auto* fe_key_to_release = oldfe_ ? &oldfe_->k : nullptr;

//try to reuse existing active_adapter and active_service as-is
	if(mux || service) {
		assert(mux); //if service i not null, mux must also be non-null
		auto [fe_, idx] = matching_existing_subscription(wtxn, required_rf_path, mux, service,
																										 false /*match_mux_only*/);
		if(fe_) {
			auto & fe = *fe_;
			auto& sub = fe.sub.subs[idx];
			if (sub.subscription_id == (int)subscription_id)
				//already subscribed and no change in lnb, mux or service
				return no_change(subscription_id, sub.subscription_id);
			else {
				auto sret = reuse_other_subscription(subscription_id, sub.subscription_id);
				subscribe_fe_in_use(wtxn, sret.subscription_id, fe, *mux, service, fe_key_to_release);
				return sret;
			}
		}
	}

	//try to reuse existing mux as-is but add a service
	if(mux && service) {
		assert(mux); //if service i not null, mux must also be non-null
		auto [fe_, idx] = matching_existing_subscription(wtxn, required_rf_path, mux, service,
																										 true /*match_mux_only*/);
		if(fe_) {
			auto& fe = *fe_;
			auto& sub_to_reuse=fe.sub.subs[idx];
			//we can reuse an existing active_mux, but need to add an active service
			auto sret = new_service(subscription_id, sub_to_reuse.subscription_id);
			subscribe_fe_in_use(wtxn, sret.subscription_id, fe, *mux, service, fe_key_to_release);
			return sret;
		}
	}

	/*
		At this stage, either we have to retune our own active_adapter or we have to create a new one
	 */

	subscribe_ret_t sret(subscription_id, false /*failed*/);

	if(mux) {
		auto [fe_, rf_path_, lnb_, use_counts_, released_fe_usecount] =
			devdb::fe::subscribe_lnb_band_pol_sat(
				wtxn, sret.subscription_id, *mux, service, required_rf_path, fe_key_to_release, use_blind_tune,
				may_move_dish, dish_move_penalty, resource_reuse_bonus);
		if(fe_) {
			auto& fe = *fe_;
			bool is_same_fe = oldfe_? false: (fe.k == oldfe_->k);
			sret.retune = is_same_fe;
			sret.change_service = true;
			sret.use_counts = use_counts_;
			if(!is_same_fe) {
				sret.newaa = { fe, *rf_path_, *lnb_};
				sret.sub_to_reuse = subscription_id_t::NONE;
			}
			auto& lnb = *lnb_;
			if(lnb.on_positioner) {
				auto* lnb_network = devdb::lnb::get_network(lnb, mux->k.sat_pos);
				if (!lnb_network) {
					dterror("No network found");
					return failed(subscription_id);
				}
				auto usals_pos = lnb_network->usals_pos;
				dish::update_usals_pos(wtxn, lnb, usals_pos, loc, mux->k.sat_pos /*sat_pos*/);
			}

			return sret;
		}
		return failed(subscription_id);
	} else  {
		assert(required_rf_path);
		auto c = devdb::lnb_t::find_by_key(wtxn, required_rf_path->lnb);
		if (c.is_valid()) {
			auto lnb = c.current();
			auto [fe_, released_fe_usecount] = devdb::fe::subscribe_lnb_exclusive(
				wtxn, sret.subscription_id, *required_rf_path, lnb, fe_key_to_release, need_blindscan, need_spectrum, loc);
			sret.retune = false;
			if(fe_) {
				bool is_same_fe = oldfe_? false: (fe_->k == oldfe_->k);
				sret.retune = is_same_fe;
				if(!is_same_fe) {
					sret.newaa = { *fe_, *required_rf_path, lnb};
					sret.sub_to_reuse = subscription_id_t::NONE;
				}
				return sret;
			}
		}
	}
	return failed(subscription_id);
}

template<typename mux_t>
subscribe_ret_t
devdb::fe::subscribe(db_txn& wtxn, subscription_id_t subscription_id,
										 const mux_t* mux,
										 const chdb::service_t* service,
										 bool use_blind_tune,
										 int resource_reuse_bonus,
										 bool need_blindscan) {
	auto[ oldfe_, idx] = fe_for_subscription(wtxn, subscription_id);
	auto* fe_key_to_release = oldfe_ ? &oldfe_->k : nullptr;

	//try to reuse existing active_adapter and active_service as-is
	if(mux || service) {
		assert(mux); //if service i not null, mux must also be non-null
		auto [fe_, idx] = matching_existing_subscription(wtxn, mux, service,
																										 false /*match_mux_only*/);
		if(fe_) {
			auto & fe = *fe_;
			auto& sub = fe.sub.subs[idx];
			if (sub.subscription_id == (int)subscription_id)
				return no_change(subscription_id, sub.subscription_id);  //already subscribed and no change in lnb, mux or service
			else {
				auto sret = reuse_other_subscription(subscription_id, sub.subscription_id);
				subscribe_fe_in_use(wtxn, sret.subscription_id, fe, *mux, service, fe_key_to_release);
				return sret;
			}
		}
	}

	//try to reuse existing mux as-is but add a service
	if(mux && service) {
		assert(mux); //if service i not null, mux must also be non-null
		auto [fe_, idx] = matching_existing_subscription(wtxn, mux, service,
																										 true /*match_mux_only*/);
		if(fe_) {
			auto& fe = *fe_;
			auto& sub_to_reuse=fe.sub.subs[idx];
			//we can reuse an existing active_mux, but need to add an active service
			auto sret = new_service(subscription_id, sub_to_reuse.subscription_id);
			subscribe_fe_in_use(wtxn, sret.subscription_id, fe, *mux, service, fe_key_to_release);
			return sret;
		}
	}

	/*
		At this stage, either we have to retune our own active_adapter or we have to create a new one
	 */

	subscribe_ret_t sret(subscription_id, false /*failed*/);
	sret.sub_to_reuse = sret.was_subscribed ? sret.subscription_id : subscription_id_t::NONE;
	assert(mux);
	auto [fe_, released_fe_usecount] =
	devdb::fe::subscribe_dvbc_or_dvbt_mux(
			wtxn, sret.subscription_id, *mux, service, fe_key_to_release, use_blind_tune);
	if(fe_) {
		auto& fe = *fe_;
		bool is_same_fe = oldfe_? false: (fe.k == oldfe_->k);
		sret.retune = is_same_fe;
		sret.change_service = !!service;
		if(!is_same_fe) {
			sret.newaa = { fe, {}, {}};
			sret.sub_to_reuse = subscription_id_t::NONE;
		}
		return sret;
	}
	return failed(subscription_id);
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

std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn, const devdb::rf_path_t* required_rf_path,
																					const chdb::dvbs_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only) {
	auto owner = getpid();
	using namespace chdb;
	auto c = find_first<devdb::fe_t>(wtxn);

	for(auto fe: c.range()) {
		if(fe.sub.owner != owner)
			continue;
		int idx=0;
		for(auto & sub: fe.sub.subs) { //loop over all subscriptions
			bool rf_path_matches = ! required_rf_path || (*required_rf_path == fe.sub.rf_path);
			bool mux_matches = mux ? (mux->k == fe.sub.mux_key ||
																sub.has_service &&  mux->k == sub.service.k.mux) : !sub.has_mux;
			bool service_matches = service ? (sub.has_service &&  service->k == sub.service.k) : true;
			service_matches |= match_mux_only;
			//in case we only need a mux, we also check for a match in frquency
			if(rf_path_matches && mux  && ! mux_matches && service_matches) {
				//perhaps the frequency matches but not the mux key
				dvbs_mux_t m;
				m.k = mux->k;
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



template<typename mux_t>
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
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
		for(auto & sub: fe.sub.subs) { //loop over all subscriptions
			bool mux_matches = mux ? (sub.has_mux &&  mux->k == sub.service.k.mux) : !sub.has_mux;
			bool service_matches = service ? (sub.has_service &&  service->k == sub.service.k) : ! sub.has_service;
			service_matches |= match_mux_only;
			//in case we only need a mux, we also check for a match in frquency
			if(mux && !service && ! mux_matches ) {
				//perhaps the frequency matches but not the mux key
				any_mux_t m;
				*mux_key_ptr(m) = mux->k;
				set_member(m, frequency, fe.sub.frequency);
				set_member(m, pol, fe.sub.pol);
				mux_matches = chdb::matches_physical_fuzzy(*mux, m, true /*check_sat_pos*/,
																									 true /*ignore_t2mi_pid*/);
			}
			if(mux_matches && service_matches) {
				return {fe, idx};
			}
			++idx;
		}
	}
	return {{}, -1};
}



static bool unsubscribe_helper(fe_t& fe, subscription_id_t subscription_id) {
	bool erased = false;
	int idx=0;
	if(fe.sub.subs.size()==0 || fe.sub.owner != getpid())
		return false;
	for(auto& sub: fe.sub.subs) {
		if(sub.subscription_id == (int32_t)subscription_id &&  fe.sub.owner == getpid()) {
			if(sub.has_service) {
				auto srv = chdb::to_str(sub.service);
				dtdebugx("adapter %d: subscription_id=%d unsubscribe service=%s",
								 fe.adapter_no, (int) subscription_id, srv.c_str());
			} else if (sub.has_mux) {
				dtdebugx("adapter %d %d.%03d%s-%d %d use_count=%d unsubscribe", fe.adapter_no,
								 fe.sub.frequency/1000, fe.sub.frequency%1000,
								 pol_str(fe.sub.pol), fe.sub.mux_key.stream_id, fe.sub.mux_key.mux_id, fe.sub.subs.size());
			} else {
				assert(fe.sub.subs.size() ==1); // lnb reservation is unique
				dtdebugx("adapter %d use_count=%d unsubscribe", fe.adapter_no, fe.sub.subs.size());
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
int fe::unsubscribe(db_txn& wtxn, subscription_id_t subscription_id, const fe_key_t& fe_key, fe_t* fe_ret) {
	auto c = devdb::fe_t::find_by_key(wtxn, fe_key);
	auto fe = c.is_valid()  ? c.current() : fe_t{}; //update in case of external changes
	assert(fe.sub.subs.size()>=1);
	bool erased = unsubscribe_helper(fe, subscription_id);
	if(!erased) {
		dterrorx("Trying to unsuscribed a non-subscribed service subscription_id=%d", (int) subscription_id);
	}
	else
		put_record(wtxn, fe);

	if (fe_ret)
		*fe_ret = fe;
	return fe.sub.subs.size();
}

/*
	returns the remaining use_count of the unsubscribed fe
 */
int fe::unsubscribe(db_txn& wtxn, subscription_id_t subscription_id, fe_t& fe) {
	assert(fe::is_subscribed(fe));
	assert(fe.sub.rf_path.card_mac_address != -1);
	auto ret = fe::unsubscribe(wtxn, subscription_id, fe.k, &fe);
	return ret;
}

int fe::unsubscribe(db_txn& wtxn, subscription_id_t subscription_id)
{
	auto c = find_first<devdb::fe_t>(wtxn);
	for(auto fe : c.range()) {
		bool erased = unsubscribe_helper(fe, subscription_id);
		if(erased) {
			put_record(wtxn, fe);
			return fe.sub.subs.size();
		}
	}
	return -1;
}


//instatiations

template std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(db_txn& wtxn, subscription_id_t subscription_id,
																												const chdb::dvbc_mux_t& mux,
																												const chdb::service_t* service,
																												const devdb::fe_key_t* fe_key_to_release,
																												bool use_blind_tune);

template std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::subscribe_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(db_txn& wtxn, subscription_id_t subscription_id,
																												const chdb::dvbt_mux_t& mux,
																												const chdb::service_t* service,
																												const devdb::fe_key_t* fe_key_to_release,
																												bool use_blind_tune);
template
subscribe_ret_t
devdb::fe::subscribe(db_txn& wtxn, subscription_id_t subscription_id,
										 const chdb::dvbc_mux_t* mux,
										 const chdb::service_t* service,
										 bool use_blind_tune,
										 int resource_reuse_bonus,
										 bool need_blindscan);

template
subscribe_ret_t
devdb::fe::subscribe(db_txn& wtxn, subscription_id_t subscription_id,
										 const chdb::dvbt_mux_t* mux,
										 const chdb::service_t* service,
										 bool use_blind_tune,
										 int resource_reuse_bonus,
										 bool need_blindscan);


template
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const chdb::dvbc_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only);

template
std::tuple<std::optional<devdb::fe_t>, int>
devdb::fe::matching_existing_subscription(db_txn& wtxn,
																					const chdb::dvbt_mux_t* mux,
																					const chdb::service_t* service,
																					bool match_mux_only);


std::atomic_int subscribe_ret_t::next_subscription_id{0};
