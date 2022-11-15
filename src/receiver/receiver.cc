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

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <resolv.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <values.h>

#include "active_adapter.h"
#include "active_playback.h"
#include "active_service.h"
#include "neumo.h"
#include "receiver.h"
#include "subscriber.h"
#include "scan.h"
#include "spectrum.h"
#include "util/dtutil.h"
#include "util/identification.h"

bool wait_for_all(std::vector<task_queue_t::future_t>& futures) {
	bool error = false;
	for (auto& f : futures)
		error |= (f.get() < 0);
	futures.clear();
	return error;
}

/*
	returns number of executed tasks, or -1 in case of exit
*/
int task_queue_t::run_tasks(system_time_t now_) {
	now = now_;
	// dtdebug("start");
	acknowledge();
	// dtdebug("acknowledged");
	int count = 0;
	for (;;) {
		task_t task;
		{
			std::lock_guard<std::mutex> lk(mutex);
			if (tasks.empty()) {
				// dtdebug("empty");
				break;
			}
			task = std::move(tasks.front());
			// dtdebug("pop task");
			tasks.pop();
		}
		// dtdebug("start task");
		task();
		count++;
		// dtdebug("end task");
	}
	if (must_exit_) {
		return -1;
	}
	return count;
}

void receiver_thread_t::remove_active_adapter(std::vector<task_queue_t::future_t>& futures,
																				 subscription_id_t subscription_id) {
	dtdebugx("Unsubscribe subscription_id=%d", (int)subscription_id);
	assert((int)subscription_id >= 0);
	// release subscription's service on this mux, if any
	auto active_adapter_p = active_adapter_for_subscription(subscription_id);
	if (!active_adapter_p.get()) {
		dterrorx("no active_adapter for subscription %d", (int)subscription_id);
		return;
	}

	auto& active_adapter = *(active_adapter_p);

	/*This subscription will use  a different active_adapter than before,
		and the old active_adapter is no longer in use; so release it.

		Asynchronously request active_adapter to detune for this subscription, which is the only one
	*/
	futures.push_back(active_adapter.tuner_thread.push_task([&active_adapter]() {
		auto ret = cb(active_adapter.tuner_thread).remove_active_adapter(active_adapter);
		if (ret < 0)
			dterrorx("deactivate returned %d", ret);
		return ret;
	}));

	receiver.subscribed_aas.writeAccess()->erase(subscription_id);
}

void receiver_thread_t::unsubscribe_active_service(std::vector<task_queue_t::future_t>& futures,
																									 active_service_t& active_service, subscription_id_t subscription_id) {
	assert((int)subscription_id >= 0);

	// release subscription's service on this mux, if any
	auto [itch, found] = find_in_map(this->reserved_services, subscription_id);

	// Asynchronously stop the old service being streamed for this subscription
	// This will not necessarily lead to the tuner becoming free, as it may also be streaming
	// channels for other subscriptions

	active_service_t& service = *(itch->second);

	auto active_adapter_p = this->active_adapter_for_subscription(subscription_id);
	assert(active_adapter_p.get());
	auto& active_adapter = *(active_adapter_p);

	if (service.reservation()->release_service() == 0) { // only called from this thread
		// request active_adapter to remove pmt monitor for the service and to remove its handle
		futures.push_back(active_adapter.tuner_thread.push_task([&active_adapter, &service]() {
			auto ret = cb(active_adapter.tuner_thread).remove_service(active_adapter, service);
			if (ret < 0)
				dterrorx("remove_service returned %d", ret);
			return ret;
		}));
	}
	// if(service.reservation.use_count()==0)
	this->reserved_services.erase(itch);
}

void receiver_thread_t::unsubscribe_(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id,
																		 bool service_only) {
	if ((int)subscription_id < 0)
		return;
	if (scanner.get() && scanner->subscription_id == subscription_id) {
		unsubscribe_scan(futures, subscription_id);
		return;
	}
	auto [itrec, foundrec] = find_in_map(this->reserved_playbacks, subscription_id);
	if (foundrec) {
		this->reserved_playbacks.erase(itrec);
		return;
	}

	// release subscription's service on this mux, if any
	auto [itch, found] = find_in_map(this->reserved_services, subscription_id);

	if (found) {
		active_service_t& active_service = *(itch->second);
		unsubscribe_active_service(futures, active_service, subscription_id);
	}

	if (service_only)
		return;
	remove_active_adapter(futures, subscription_id);
}

/*!
	Check for another subscription tuned to the wanted channel.
	If we find one, we reuse it
*/
std::unique_ptr<playback_mpm_t>
receiver_thread_t::subscribe_service_in_use(std::vector<task_queue_t::future_t>& futures,
																						const chdb::service_t& service, subscription_id_t subscription_id) {
	auto& pr = this->reserved_services;
	for (auto& itch : pr) {
		auto& channel = *itch.second;
		auto& reservation = *channel.reservation();
		if (reservation.is_same_service(service)) {
			/* The service is already subscribed
				 Unsubscribe_ our old mux and service (if any)
			*/
			bool service_only = true;
			if ((int)subscription_id >= 0)
				unsubscribe_(futures, subscription_id, service_only);
			else
				subscription_id = this->next_subscription_id++;
			// place an additional reservation, so that the service will remain tuned if other subscriptions release it
			reservation.reserve_current_service(service);
			pr[subscription_id] = itch.second;
			dtdebug("[" << service << "] sub =" << (int) subscription_id << ": reusing existing service");
			return channel.make_client_mpm(subscription_id);
		}
	}
	return nullptr;
}

template <typename _mux_t>
std::unique_ptr<playback_mpm_t>
receiver_thread_t::subscribe_service_(std::vector<task_queue_t::future_t>& futures, db_txn& txn, const _mux_t& mux,
																			const chdb::service_t& service, active_service_t* old_active_service,
																			subscription_id_t subscription_id) {

	dtdebug("Subscribe " << service << " sub =" << (int) subscription_id << ": tune start");

	/*
		If another subscription is tuned to the wanted service, we do not have to tune.
		We only place a reservation so that the service will remain tuned
	*/
	auto ret = subscribe_service_in_use(futures, service, subscription_id);
	if (ret.get())
		return ret;
	// now create a new active_service, subscribe the related service and send instructions to start it
	std::shared_ptr<active_adapter_t> active_adapter_p;
	{
		auto w = receiver.subscribed_aas.writeAccess();
		active_adapter_p = (*w)[subscription_id];
	}
	auto& active_adapter = *active_adapter_p;

#ifndef NDEBUG
	{
		auto [it, found] = find_in_map(this->reserved_services, subscription_id);
		assert(!found);
	}
#endif

	ss::string<32> prefix;
	prefix.sprintf("CH[%d:%s]", active_adapter.get_adapter_no(), chdb::to_str(service).c_str());
	log4cxx::NDC::push(prefix.c_str());

	auto reader = service.k.mux.t2mi_pid > 0 ? active_adapter.make_embedded_stream_reader(mux)
		: active_adapter.make_dvb_stream_reader();
	auto active_service_p = std::make_shared<active_service_t>(active_adapter, service, std::move(reader));

	log4cxx::NDC::pop();
	auto& active_service = *active_service_p;

	// remember that this service is now in use (for future planning and for later unsubscription)
	active_service.reservation()->reserve_service(active_adapter, service);
	this->reserved_services.emplace(subscription_id, active_service_p);
	dtdebugx("ACTIVE SERVICE reserved");

	/*Ask tuner_thread to add this active_service to its internal lists, so as to communicate
		epg records, scam data and other relevant data to the active_service
	*/
	futures.push_back(active_adapter.tuner_thread.push_task(
											[&]() { return cb(active_adapter.tuner_thread).add_service(active_adapter, active_service); }));
	// dtdebug("[" << service << "] sub =" << subscription_id << ": requesting service tune");
	return active_service_p->make_client_mpm(subscription_id);
}

template <>
std::unique_ptr<playback_mpm_t>
receiver_thread_t::subscribe_service_<chdb::any_mux_t>(std::vector<task_queue_t::future_t>& futures, db_txn& txn,
																											 const chdb::any_mux_t& mux, const chdb::service_t& service,
																											 active_service_t* old_active_service, subscription_id_t subscription_id) {
	/*First unscubscribe the currently reserved service, except if it happens
		to be the desired one, in which case we return immediately
	*/
	assert(!old_active_service || (int) subscription_id >= 0);
	if (old_active_service) {
		// auto& old_tuner = *this->reserved_services[subscription_id];
		auto& channel = *old_active_service;
		auto& old_service = channel.reservation()->reserved_service;
		if (service.k == old_service.k) {
			dtdebug("subscribe " << service << ": already subscribed to service");
			return channel.make_client_mpm(subscription_id);
		}
		unsubscribe_active_service(futures, *old_active_service, subscription_id);
		bool error = wait_for_all(futures);
		if (error) {
			dterror("Unhandled error in unsubscribe");
		}
	}

	/*subscribe the new mux; Note that:
		1. if the new mux is different from the currently tuned one,
		any subscribed service for the the current subscription will
		be unsubscribed
		2. if the new mux equals the currently tuned one, any currently
		subscribed service will remain subscribed at this stage
		Therefore, there is no problem if the requested service to
		subscribe would be already subscried (except for some waste of time)
		3. The code will not needlessly shutdown+restart the frontend if the
		frontend can simply be retuned
		4. The code will give preference to reusing a frontend which is already
		tuned to the desired mux
	*/

	auto ret = subscribe_mux(futures, txn, mux, subscription_id,
													 tune_options_t(scan_target_t::SCAN_FULL_AND_EPG), (const devdb::lnb_t*)nullptr);
	if ((int) ret < 0)
		return nullptr; // we could not reserve a mux
	assert(ret == subscription_id || (int) subscription_id < 0);
	subscription_id = ret;
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback

	*/
	auto dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);

	if (dvbs_mux)
		return subscribe_service_(futures, txn, *dvbs_mux, service, old_active_service, subscription_id);
	else {
		auto dvbc_mux = std::get_if<chdb::dvbc_mux_t>(&mux);
		if (dvbc_mux)
			return subscribe_service_(futures, txn, *dvbc_mux, service, old_active_service, subscription_id);
		else {
			auto dvbt_mux = std::get_if<chdb::dvbt_mux_t>(&mux);
			if (dvbt_mux)
				return subscribe_service_(futures, txn, *dvbt_mux, service, old_active_service, subscription_id);
		}
	}
	return nullptr;
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::subscribe_service(std::vector<task_queue_t::future_t>& futures,
																																		 db_txn& txn, const chdb::any_mux_t& mux,
																																		 const chdb::service_t& service,
																																		 subscription_id_t subscription_id) {
	active_service_t* old_active_service{nullptr};

	if ((int) subscription_id >= 0) {
		auto [itch, found] = find_in_map(this->reserved_services, subscription_id);
		if (found) {
			// auto& old_tuner = *this->reserved_services[subscription_id];
			old_active_service = &*itch->second;
		}
	}

	return subscribe_service_<chdb::any_mux_t>(futures, txn, mux, service, old_active_service, subscription_id);
}

receiver_thread_t::receiver_thread_t(receiver_t& receiver_)
	: task_queue_t(thread_group_t::receiver), adaptermgr(adaptermgr_t::make(receiver_))
		//, rec_manager_thread(*this)
	,
		receiver(receiver_) {}

int receiver_thread_t::exit() {

	receiver.tuner_thread.stop_running(true);
	// tuner_thread.join();

	receiver.scam_thread.stop_running(true);

	// receiver.scam_thread.join();
	{
		auto w = receiver.subscribed_aas.writeAccess();
		w->clear();
	}
	this->adaptermgr->stop();
	this->adaptermgr.reset();
	return 0;
}

scan_stats_t receiver_thread_t::get_scan_stats(subscription_id_t subscription_id) {
	assert((int) subscription_id >= 0);
	// make local copy first, because scanner can be reset at any time
	auto scanner_copy = scanner;
	// assert(scanner_copy); //scan should be in progress
	if (scanner_copy) {
		auto r = scanner_copy->scan_stats.readAccess();
		return *r;
	}
	return {};
}

scan_stats_t receiver_t::get_scan_stats(int subscription_id) {
	return receiver_thread.get_scan_stats((subscription_id_t)subscription_id);
}

receiver_t::~receiver_t() {
	dtdebugx("receiver destroyed");
}

receiver_thread_t::~receiver_thread_t() {
	dtdebugx("receiver thread terminating");
	// detach();
}

void receiver_thread_t::cb_t::abort_scan() {
	if (scanner) {
		dtdebug("Aborting scan in progress");
		scanner.reset();
	}
}


/*
	called from tuner thread when scanning a mux has ended
*/
void receiver_thread_t::cb_t::on_scan_mux_end(const active_adapter_t* active_adapter_p,
																							const chdb::any_mux_t& finished_mux)
{
	if (!scanner.get()) {
		return;
	}

	auto num_left = scanner->on_scan_mux_end(active_adapter_p, finished_mux);
	dterrorx("%d muxes left to scan", num_left);

	if (num_left == 0 ) {
		scanner.reset();
		return;
	}
}


std::shared_ptr<active_adapter_t> receiver_thread_t::active_adapter_for_subscription(subscription_id_t subscription_id) {
	if ((int) subscription_id >= 0) {
		auto [itmux, found] = find_in_safe_map_with_owner_read_ref(receiver.subscribed_aas, subscription_id);
		if (found)
			return itmux->second;
	}
	return nullptr;
}

std::shared_ptr<active_adapter_t> receiver_t::active_adapter_for_subscription(subscription_id_t subscription_id) {
	if ((int) subscription_id >= 0) {
		auto [itmux, found] = find_in_safe_map(subscribed_aas, subscription_id);
		if (found)
			return itmux->second;
	}
	return nullptr;
}

#if 0
std::shared_ptr<active_adapter_t> receiver_thread_t::active_adapter_with_fe(dvb_frontend_t* fe) {

	auto [it, found] = find_in_safe_map_if_with_owner_read_ref(receiver.subscribed_aas, [&](const auto& x) {
		auto [subscription_id_, reserved_mux] = x;
		return reserved_mux->fe.get() == fe;
	});
	if (!found)
		return nullptr;
	else
		return it->second;
}
#endif

subscription_id_t receiver_thread_t::subscribe_mux_(
	std::vector<task_queue_t::future_t>& futures,
	std::shared_ptr<active_adapter_t>& old_active_adapter, db_txn& wtxn,
	const chdb::dvbs_mux_t& mux, subscription_id_t subscription_id, tune_options_t tune_options,
	const devdb::lnb_t* required_lnb) {

	auto* fe_key_to_release = (old_active_adapter && old_active_adapter->fe)
		? 	& old_active_adapter->fe->ts.readAccess()->dbfe.k : nullptr;


	int resource_reuse_bonus;
	int dish_move_penalty;
	{
		auto r = receiver.options.readAccess();
		resource_reuse_bonus = r->resource_reuse_bonus;
		dish_move_penalty = r->dish_move_penalty;
	}

	auto [fe_, lnb_] = devdb::fe::subscribe_lnb_band_pol_sat(
		wtxn, mux, required_lnb, fe_key_to_release, tune_options.use_blind_tune, dish_move_penalty,
		resource_reuse_bonus);

	bool found = !!fe_;
	if (!found) {
		user_error("Subscribe " << mux << ": no suitable adapter found");
		if (old_active_adapter) {
			assert((int)subscription_id >= 0);
			remove_active_adapter(futures, subscription_id);
		}
		return subscription_id_t{-1};
	}
	auto &lnb = *lnb_;
	auto &fe = *fe_;

	assert(!required_lnb || (lnb.k == required_lnb->k)); // we have the right lnb

	bool is_same_frontend = ( fe_key_to_release && *fe_key_to_release == fe.k);
	/*
		If new mux is on the same adapter/frontend as old mux,
		we only need to retune.
	*/
	if (is_same_frontend) {
		futures.push_back(
			old_active_adapter->tuner_thread.push_task([this, old_active_adapter, tune_options, lnb, mux]() {
				/*deactivate the adapter (stop si processing) and remove it from
					active_adapters (the next tune call will read it)
				*/
				auto ret = cb(receiver.tuner_thread).tune(old_active_adapter, lnb, mux, tune_options);
				if (ret < 0)
					dterrorx("tune returned %d", ret);
				return ret;
			}));
		auto adapter_no =  old_active_adapter->get_adapter_no();
		dtdebug("Subscribed: subscription_id=" << (int) subscription_id << " using old adap=" <<
					adapter_no << " " << mux);

	} else {
		if (old_active_adapter) {
			assert((int) subscription_id >= 0);
			remove_active_adapter(futures, subscription_id);
		}

		auto active_adapter = make_active_adapter(fe.k);
		if ((int) subscription_id < 0)
			subscription_id = this->next_subscription_id++;
		{
			auto w = receiver.subscribed_aas.writeAccess();
			(*w)[subscription_id] = active_adapter;
		}
		assert(active_adapter->is_open());

		futures.push_back(active_adapter->tuner_thread.push_task([this, active_adapter, lnb, mux, tune_options]() {
			auto ret = cb(receiver.tuner_thread).tune(active_adapter, lnb, mux, tune_options);
			if (ret < 0)
				dterrorx("tune returned %d", ret);
			return ret;
		}));
		auto adapter_no =  active_adapter->get_adapter_no();
		dtdebug("Subscribed: subscription_id=" << (int) subscription_id << " adap=" <<
					adapter_no << " " << mux);
	}
	return subscription_id;
}

/*!
	Find a suitable lnb  for tuning to a mux
	If subscription_id >=0, then first unregister any service for that subscription
	Then find a suitable adapter/frontend.

	If the frontend is the same as for the current subscription, simply retune,
	otherwise unregister the old mux/frontend/adapter and reserve a new one

	if subscription_id <0, then create a new subscription

	Returns -1 if subscription failed (no free tuners)

	@todo: the code below is mostly the same as  the lnb version; integrate
*/
template <typename _mux_t>
subscription_id_t receiver_thread_t::subscribe_mux_(std::vector<task_queue_t::future_t>& futures,
																										std::shared_ptr<active_adapter_t>& old_active_adapter,
																										db_txn& wtxn, const _mux_t& mux, subscription_id_t subscription_id,
																										tune_options_t tune_options,
																										const devdb::lnb_t* required_lnb /*unused*/) {
	assert(!required_lnb);

	auto* fe_key_to_release = (old_active_adapter && old_active_adapter->fe)
		? 	& old_active_adapter->fe->ts.readAccess()->dbfe.k : nullptr;

	auto fe_ = devdb::fe::subscribe_dvbc_or_dvbt_mux<_mux_t>(wtxn, mux, fe_key_to_release, tune_options.use_blind_tune);
	bool found = !!fe_;

	if (!found) {
		user_error("Subscribe " << mux << ": no suitable adapter found");
		if (old_active_adapter) {
			assert( (int) subscription_id >= 0);
			remove_active_adapter(futures, subscription_id);
		}
		return subscription_id_t{-1};
	}

	auto &fe = *fe_;

	bool is_same_frontend =  ( fe_key_to_release && *fe_key_to_release == fe.k);
	/*
		If new mux is on the same adapter/frontend as old mux,
		we only need to retune, and leave reservation unchanged
	*/
	if (is_same_frontend) {
		/*@todo: possible race: old_active_adapter may
			start calling on_scan_mux_end for the previously tuned mux on
			this subscription
		*/
		futures.push_back(
			old_active_adapter->tuner_thread.push_task([this, old_active_adapter, tune_options, mux]() {
				/*deactivate the adapter (stop si processing) and remove it from
					active_adapters (the next tune call will read it)
				*/
				auto ret = cb(receiver.tuner_thread).tune(old_active_adapter, mux, tune_options);
				if (ret < 0)
					dterrorx("tune returned %d", ret);
				return ret;
			}));
		auto adapter_no =  old_active_adapter->get_adapter_no();
		dtdebug("Subscribed: subscription_id=" << (int) subscription_id << " using old adap=" <<
					adapter_no << " " << mux);

	} else {
		if (old_active_adapter) {
			assert((int) subscription_id >= 0);
			remove_active_adapter(futures, subscription_id);
		}

		auto active_adapter = make_active_adapter(fe.k);
		if ((int) subscription_id < 0)
			subscription_id = this->next_subscription_id++;
		{
			auto w = receiver.subscribed_aas.writeAccess();
			(*w)[subscription_id] = active_adapter;
		}
		assert(active_adapter->is_open());

		futures.push_back(active_adapter->tuner_thread.push_task([this, active_adapter, mux, tune_options]() {
			auto ret = cb(receiver.tuner_thread).tune(active_adapter, mux, tune_options);
			if (ret < 0)
				dterrorx("tune returned %d", ret);
			return ret;
		}));
	}
	dtdebug("Subscribed to: " << mux);
	return subscription_id;
}

template <>
subscription_id_t receiver_thread_t::subscribe_mux_<chdb::any_mux_t>(
	std::vector<task_queue_t::future_t>& futures,
	std::shared_ptr<active_adapter_t>& old_active_adapter,
	db_txn& txn, const chdb::any_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options, const devdb::lnb_t* required_lnb) {

	auto dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
	if (dvbs_mux)
		return subscribe_mux_(futures, old_active_adapter, txn, *dvbs_mux, subscription_id, tune_options, required_lnb);
	else {
		assert(!required_lnb);
		auto dvbc_mux = std::get_if<chdb::dvbc_mux_t>(&mux);
		if (dvbc_mux)
			return subscribe_mux_(futures, old_active_adapter, txn, *dvbc_mux, subscription_id, tune_options, nullptr);
		else {
			auto dvbt_mux = std::get_if<chdb::dvbt_mux_t>(&mux);
			if (dvbt_mux)
				return subscribe_mux_(futures, old_active_adapter, txn, *dvbt_mux, subscription_id, tune_options, nullptr);
		}
	}
	return subscription_id_t{-1};
}

void receiver_thread_t::cb_t::unsubscribe(subscription_id_t subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	bool service_only = false;
	unsubscribe_(futures, subscription_id, service_only);

	/*wait_for_futures is needed because tuners/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/tuner threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/
	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in unsubscribe");
	}
}

template <typename _mux_t>
subscription_id_t receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& txn, const _mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const devdb::lnb_t* required_lnb) {

	auto lnb_ok = [required_lnb](auto& old_active_adapter) {
		return !required_lnb || old_active_adapter->uses_lnb(*required_lnb);
	};

	// If the requested mux happens to be already the active mux for this subscription, simply return;
	auto old_active_adapter = active_adapter_for_subscription(subscription_id);
	if (old_active_adapter) {
		if (lnb_ok(old_active_adapter) && old_active_adapter->is_tuned_to(mux, required_lnb)) {
			if(tune_options.subscription_type == subscription_type_t::NORMAL) {
				auto& tuner_thread = old_active_adapter->tuner_thread;
				futures.push_back(tuner_thread.push_task([&tuner_thread, old_active_adapter,  mux]() {
				cb(tuner_thread).prepare_si(*old_active_adapter, mux, true /*start*/);
				return 0;
				}));
			} else {
				dtdebug("subscribe " << mux << ": already subscribed to mux");
				/// during DX-ing retunes need to be forced
				request_retune(futures, *old_active_adapter, subscription_id);
			}
			return subscription_id;
		}
	}
	/*We now know that the old unsubscribed mux is different from the new one
		or current_lnb != lnb
		We do not unsubscribe it yet, because that would cause the asssociated frontend
		to be released, which is not optimal if we decide to just reuse this frontend
		and tune it to a different mux
	*/

	// check if we can use a mux which is in use by other subscription
	auto ret = subscribe_mux_in_use(futures, mux, subscription_id, tune_options, required_lnb);
	if ((int) ret >= 0) {
		assert((int) subscription_id < 0 || ret == subscription_id);
		return ret;
	}

	// perform the actual mux reservation
	subscription_id = subscribe_mux_(futures, old_active_adapter, txn, mux, subscription_id, tune_options, required_lnb);

	/*wait_for_futures is needed because tuners/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/tuner threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/

	return subscription_id;
}

template <class mux_t>
subscription_id_t receiver_thread_t::subscribe_mux_in_use(
	std::vector<task_queue_t::future_t>& futures, const mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const devdb::lnb_t* required_lnb) {

	auto& pm = receiver.subscribed_aas.owner_read_ref();
	for (auto& itmux : pm) {
		auto& other_active_adapter = itmux.second;
		if (other_active_adapter->is_tuned_to(mux, required_lnb)) {
			/* The mux is already subscribed by another subscription
				 unsubscribe_ our old mux and the service (if any)
			*/

			if ((int) subscription_id >= 0)
				remove_active_adapter(futures, subscription_id);
			else
				subscription_id = this->next_subscription_id++;

			// place an additional reservation, so that the mux will remain tuned if other subscriptions release it
#if 0
			reservation()->reserve_current();
#endif
			{
				auto w = receiver.subscribed_aas.writeAccess();
				(*w)[subscription_id] = other_active_adapter;
			}
			auto& tuner_thread = other_active_adapter->tuner_thread;
			futures.push_back(tuner_thread.push_task([&tuner_thread, other_active_adapter, tune_options, mux]() {
				using namespace chdb;
				namespace m = chdb::update_mux_preserve_t;
				assert( mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE);
				cb(tuner_thread).prepare_si(*other_active_adapter, mux, true /*start*/);
				return cb(tuner_thread).set_tune_options(*other_active_adapter, tune_options);
			}));

			dtdebug("[" << mux << "] subscription_id=" << (int) subscription_id << ": reusing existing mux");
			return subscription_id;
		}
	}
	return subscription_id_t{-1};
}

int
receiver_thread_t::request_retune(std::vector<task_queue_t::future_t>& futures, active_adapter_t& active_adapter,
																			subscription_id_t subscription_id) {
	auto& tuner_thread = active_adapter.tuner_thread;
	futures.push_back(tuner_thread.push_task(
											[&tuner_thread, &active_adapter]() { return cb(tuner_thread).request_retune(active_adapter); }));

	dtdebug("sub =" << (int) subscription_id << ": requesting retune");
	return 0;
}

template <typename mux_t>
subscription_id_t receiver_thread_t::cb_t::scan_muxes(ss::vector_<mux_t>& muxes, subscription_id_t subscription_id)
{
	ss::string<32> s;
	s << "SCAN[" << (int) subscription_id << "] " << (int) muxes.size() << " muxes";
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;

	bool service_only = true;
	if ((int) subscription_id >= 0)
		unsubscribe_(futures, subscription_id, service_only);
	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}
	bool scan_newly_found_muxes = true;
	int max_num_subscriptions = 100;
	subscription_id = this->receiver_thread_t::subscribe_scan(futures, muxes, scan_newly_found_muxes,
																														max_num_subscriptions, subscription_id);
	error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in scan_mux");
	}
	return subscription_id;
}

subscription_id_t receiver_thread_t::cb_t::scan_muxes(ss::vector_<chdb::dvbs_mux_t>& muxes,
																											subscription_id_t subscription_id) {
	ss::string<32> s;
	s << "SCAN[" << (int) subscription_id << "] " << (int) muxes.size() << " muxes";
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;
	bool service_only = true;
	if ((!scanner.get() && (int)subscription_id >= 0) ||
			(scanner.get() && scanner->subscription_id != subscription_id)) {
		unsubscribe_(futures, subscription_id, service_only);
		bool error = wait_for_all(futures);
		if (error) {
			dterror("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
		}
	}
	ss::vector_<devdb::lnb_t>* lnbs = nullptr; //@todo
	bool scan_newly_found_muxes = true;
	int max_num_subscriptions = 100;
	subscription_id = this->receiver_thread_t::subscribe_scan(futures, muxes, lnbs, scan_newly_found_muxes,
																														max_num_subscriptions, subscription_id);
	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in scan_mux");
	}
	return subscription_id;
}

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																											subscription_id_t subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																											subscription_id_t subscription_id);

template <typename _mux_t>
subscription_id_t
receiver_thread_t::cb_t::subscribe_mux(const _mux_t& mux, subscription_id_t subscription_id,
																			 tune_options_t tune_options,
																			 const devdb::lnb_t* required_lnb) {
	ss::string<32> s;
	s << "SUB[" << (int) subscription_id << "] " << to_str(mux);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;

	bool service_only = true;
	if ((int) subscription_id >= 0)
		unsubscribe_(futures, subscription_id, service_only);
	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}

	auto txn = receiver.chdb.wtxn();
	subscription_id =
		this->receiver_thread_t::subscribe_mux(futures, txn, mux, subscription_id, tune_options, required_lnb);
	txn.commit();
	error = wait_for_all(futures);
	if (error) {
		ss::string<256> saved_error{get_error()};
		dterror(get_error());
		if((int) subscription_id >= 0) {
			unsubscribe_(futures, subscription_id, false /*service_only*/);
			wait_for_all(futures);
		}
		user_error(saved_error); //TODO: look into better way of preserving error messages
		return subscription_id_t{-1};
	}
	return subscription_id;
}

subscription_id_t receiver_thread_t::subscribe_lnb(std::vector<task_queue_t::future_t>& futures, db_txn& wtxn,
																									 devdb::lnb_t& lnb, tune_options_t tune_options,
																									 subscription_id_t subscription_id) {

	auto c = devdb::lnb_t::find_by_key(wtxn, lnb.k);
	if (c.is_valid()) {
		auto db_lnb = c.current();
		lnb.lof_offsets = db_lnb.lof_offsets;
	}

	auto old_active_adapter = active_adapter_for_subscription(subscription_id);
	auto* fe_key_to_release = (old_active_adapter && old_active_adapter->fe)
		? 	& old_active_adapter->fe->ts.readAccess()->dbfe.k : nullptr;

	bool need_blindscan = tune_options.tune_mode == tune_mode_t::SCAN_BLIND;
	bool need_spectrum = tune_options.tune_mode == tune_mode_t::SPECTRUM;
	auto fe_ = devdb::fe::subscribe_lnb_exclusive(wtxn, lnb, fe_key_to_release, need_blindscan, need_spectrum);
	bool found = !!fe_;

	if (!found) {
		user_error("Subscribe " << lnb << ": adapter of lnb not suitable");
		dtdebug(get_error());
		if (old_active_adapter) {
			assert ((int) subscription_id >= 0);
			unsubscribe_(futures, subscription_id, false);
		}
		return subscription_id_t{-1};
	}
	auto& fe = *fe_;

	bool is_same_frontend = ( fe_key_to_release && *fe_key_to_release == fe.k);
	/*
		If new mux is on the same adapter/frontend as old mux,
		we only need to retune, and leave reservation unchanged
	*/
	if (is_same_frontend) {

		/*@todo: possible race: old_active_adapter may
			start calling on_scan_mux_end for the previously tuned mux on
			this subscription
		*/
		futures.push_back(old_active_adapter->tuner_thread.push_task([this, old_active_adapter, tune_options, lnb]() {

			auto ret = cb(receiver.tuner_thread).lnb_activate(old_active_adapter, lnb, tune_options);
			if (ret < 0)
				dterrorx("tune returned %d", ret);
			return ret;
		}));
		auto adapter_no =  old_active_adapter->get_adapter_no();
		dtdebug("Subscribed: subscription_id=" << (int) subscription_id << " using old adap=" << adapter_no);
	} else {
		if (old_active_adapter) {
			assert ((int) subscription_id >= 0);
			remove_active_adapter(futures, subscription_id);
		}
		auto active_adapter = make_active_adapter(fe.k);

		if ((int) subscription_id < 0)
			subscription_id = this->next_subscription_id++;
		{
			auto w = receiver.subscribed_aas.writeAccess();
			(*w)[subscription_id] = active_adapter;
		}
		assert(active_adapter->is_open());
		futures.push_back(active_adapter->tuner_thread.push_task([this, active_adapter, lnb, tune_options]() {
				auto ret = cb(receiver.tuner_thread).lnb_activate(active_adapter, lnb, tune_options);
			if (ret < 0)
				dterrorx("tune returned %d", ret);
			return ret;
		}));
	}
	dtdebug("Subscribed to: " << lnb);
	return subscription_id;
}

devdb::lnb_t receiver_t::reread_lnb(const devdb::lnb_t& lnb)

{
	auto txn = chdb.rtxn();
	auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
	if (c.is_valid()) {
		auto ret = lnb;
		ret.lof_offsets = c.current().lof_offsets;
		return ret;
	}
	return lnb;
}

subscription_id_t
receiver_thread_t::cb_t::subscribe_lnb(devdb::lnb_t& lnb_, tune_options_t tune_options,
																			 subscription_id_t subscription_id) {

	auto lnb = receiver.reread_lnb(lnb_);
	ss::string<32> s;
	s << "SUB[" << (int) subscription_id << "] " << to_str(lnb);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	auto wtxn = receiver.chdb.wtxn();
	subscription_id = this->receiver_thread_t::subscribe_lnb(futures, wtxn, lnb, tune_options, subscription_id);
	wtxn.commit();
	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}
	return subscription_id;
}

template subscription_id_t
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbs_mux_t>(
	const chdb::dvbs_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options,
	const devdb::lnb_t* required_lnb);

template subscription_id_t
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbc_mux_t>(
	const chdb::dvbc_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options,
	const devdb::lnb_t* required_lnb);

template subscription_id_t
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbt_mux_t>(
	const chdb::dvbt_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options,
	const devdb::lnb_t* required_lnb);

template <typename mux_t> chdb::any_mux_t mux_for_service(db_txn& txn, const chdb::service_t& service);

static std::tuple<chdb::any_mux_t, int> mux_for_service(db_txn& txn, const chdb::service_t& service) {
	chdb::any_mux_t mux;
	if (service.k.mux.sat_pos == sat_pos_dvbc) {
		auto c = chdb::dvbc_mux_t::find_by_key(txn, service.k.mux, find_eq);
		if (!c.is_valid())
			return std::make_tuple(mux, -1);
		mux = c.current();
	} else if (service.k.mux.sat_pos == sat_pos_dvbt) {
		auto c = chdb::dvbt_mux_t::find_by_key(txn, service.k.mux, find_eq);
		if (!c.is_valid())
			return std::make_tuple(mux, -1);
		mux = c.current();
	} else {
		auto c = chdb::dvbs_mux_t::find_by_key(txn, service.k.mux, find_eq);
		if (!c.is_valid())
			return std::make_tuple(mux, -1);
		mux = c.current();
	}
	return std::make_tuple(mux, 0);
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::subscribe_recording_(const recdb::rec_t& rec, subscription_id_t subscription_id) {

	dtdebug("Subscribe recording  " << rec << " sub =" << (int) subscription_id << ": playback start");

	auto active_playback = std::make_shared<active_playback_t>(receiver, rec);
	this->reserved_playbacks[subscription_id] = active_playback;
	return active_playback->make_client_mpm(receiver, subscription_id);
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::cb_t::subscribe_service(const chdb::service_t& service,
																																					 subscription_id_t subscription_id) {
	ss::string<32> s;
	s << "SUB[" << (int) subscription_id << "] " << to_str(service);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	dtdebug("SUBSCRIBE started");

	auto txn = receiver.chdb.rtxn();
	auto [mux, error1] = mux_for_service(txn, service);
	if (error1 < 0) {
		user_error("Could not find mux for " << service);
		bool service_only = false;
		if ((int) subscription_id >= 0)
			unsubscribe_(futures, subscription_id, service_only);
		txn.abort();
		return nullptr;
	}

	// Stop any playback in progress (in that case there is no suscribed service/mux
	if ((int) subscription_id >= 0) {
		auto [itrec, found1] = find_in_map(this->reserved_playbacks, subscription_id);
		if (found1) {
			// this must be playback
			unsubscribe(subscription_id);
		}
	}

	dtdebug("SUBSCRIBE - calling subscribe_");
	// now perform the requested subscription
	auto mpmptr = this->receiver_thread_t::subscribe_service(futures, txn, mux, service, subscription_id);
	txn.abort();
	/*wait_for_futures is needed because active_adapters/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/active_adapter threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/

	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe");
	}
	dtdebug("SUBSCRIBE - returning to caller");
	return mpmptr;
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::cb_t::subscribe_recording(const recdb::rec_t& rec,
																																						 subscription_id_t subscription_id) {
	ss::string<32> s;
	s << "SUB[" << (int) subscription_id << "] " << to_str(rec);
	log4cxx::NDC ndc(s);
	dtdebug("SUBSCRIBE rec started");

	std::vector<task_queue_t::future_t> futures;
	active_playback_t* active_playback{nullptr};
	if ((int) subscription_id >= 0) {
		// unsubscribe existing service and/or mux if one exists
		auto [itrec, found] = find_in_map(this->reserved_playbacks, subscription_id);
		if (found) {
			active_playback = &*(itrec->second);
			if (rec.epg.k == active_playback->currently_playing_recording.epg.k) {
				dtdebug("subscribe " << rec << ": already subscribed to recording");
				return active_playback->make_client_mpm(receiver, subscription_id);
			}
		}
		unsubscribe(subscription_id);
	} else
		subscription_id = next_subscription_id++;

	return subscribe_recording_(rec, subscription_id);
}

/*
	called by si code when an epg or service change affecting a recording is detected
*/
int receiver_thread_t::update_recording(recdb::rec_t& rec, const epgdb::epg_record_t& epgrec) {
	auto subscription_id = subscription_id_t{rec.subscription_id};
	auto [itch, found] = find_in_map(this->reserved_services, subscription_id);
	if (found) {
		auto& channel = *itch->second;
		auto service = channel.get_current_service();
		// the following update is atomic

		channel.service_thread.push_task([&channel, &service, &rec, &epgrec]() {
			cb(channel.service_thread).update_recording(rec, service, epgrec);
			return 0;
		});
		return 0;
	}
	dterror("Unexpected: could not find live recording: " << rec);
	return -1;
}

/*!
	starts the recording, but also updates some of its fields and returns those
	returns -1 on error
*/
void receiver_thread_t::cb_t::start_recording(
	recdb::rec_t rec_in) // important: should not be a reference (async called!)
{

	ss::string<32> s;
	s << "REC " << to_str(rec_in.service);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	dtdebug("RECORD started");

	auto txn = receiver.chdb.rtxn();
	auto [mux, error1] = mux_for_service(txn, rec_in.service);
	if (error1 < 0) {
		user_error("Could not find mux for " << rec_in.service);
		return;
	}

	dtdebug("RECORD - calling subscribe_");
	// now perform the requested subscription
	auto mpm_ptr = this->receiver_thread_t::subscribe_service(futures, txn, mux, rec_in.service, subscription_id_t{-1});
	subscription_id_t subscription_id = mpm_ptr.get() ? mpm_ptr->subscription_id : subscription_id_t{-1};
	txn.abort();
	/*wait_for_futures is needed because active_adapters/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/active_adapter threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/

	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in unsubscribe");
	}

	if ((int) subscription_id < 0)
		return;
	std::optional<recdb::rec_t> rec;

	// unsubscribe_(futures, subscription_id, service_only);
	auto [itch, found] = find_in_map(this->reserved_services, subscription_id);
	if (found) {
		active_service_t& as = *(itch->second);
		futures.push_back(as.service_thread.push_task(
												// subscription_id is stored in the recording itself
												[&as, &rec, &rec_in, subscription_id]() {
													rec = cb(as.service_thread).start_recording(subscription_id, rec_in);
													return 0;
												}));
	}

	error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in unsubscribe");
	}
	if (!rec) {
		unsubscribe(subscription_id);
		return;
	}
	assert(rec->subscription_id == (int) subscription_id);

	return;
}

/*!
	returns -1 on error
*/
void receiver_thread_t::cb_t::stop_recording(recdb::rec_t rec) // important that this is not a reference (async called)
{
	auto subscription_id = subscription_id_t{rec.subscription_id};
	if ((int) subscription_id < 0)
		return;
	mpm_copylist_t copy_list;
	int ret = -1;
	{
		auto [itch, found] = find_in_map(this->reserved_services, subscription_id);
		if (found) {
			active_service_t& as = *(itch->second);
			as.service_thread
				.push_task([&as, &copy_list, &rec, &ret]() {
					ret = cb(as.service_thread).stop_recording(rec, copy_list);
					return 0;
				})
				.wait();

			if (ret >= 0) {
				assert(copy_list.rec.epg.rec_status == epgdb::rec_status_t::FINISHED);
				copy_list.run();
			}
			receiver.tuner_thread
				.push_task([&copy_list, this] {
					cb(receiver.tuner_thread).update_recording(copy_list.rec);
					return 0;
				})
				.wait();

			as.service_thread
				.push_task([&as, &rec]() {
					cb(as.service_thread).forget_recording(rec);
					return 0;
				})
				.wait();
		}
	}
	unsubscribe((subscription_id_t)rec.subscription_id);
}

/*!
	Toggle between recording on or off, except when start or stop is true
	Returns the new status: 1 = scheduled for recording, 0 = no longer scheduled for recording, -1 error

*/
int receiver_t::toggle_recording_(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	dtdebugx("epg=%s", to_str(epg_record).c_str());
	auto f = tuner_thread.push_task([this, &service, &epg_record]() {
		cb(tuner_thread).toggle_recording(service, epg_record);
		return 0;
	});
	return f.get();
}

/*!
	Returns the new status: 1=recording, 0=stopped recording
*/
int receiver_t::toggle_recording_(const chdb::service_t& service, system_time_t start_time_, int duration,
																	const char* event_name) {
	auto start_time = system_clock_t::to_time_t(start_time_);
	auto x = to_str(service);
	dtdebugx("toggle_recording: %s", x.c_str());
	epgdb::epg_record_t epg;
	epg.k.service.sat_pos = service.k.mux.sat_pos;
	epg.k.service.network_id = service.k.mux.network_id;
	epg.k.service.ts_id = service.k.mux.ts_id;
	epg.k.service.service_id = service.k.service_id;
	epg.k.event_id = TEMPLATE_EVENT_ID; /*prefix 0xffff0000 signifies a non-dvb event_id;
																				all live recordings have the same id
																			*/
	epg.k.anonymous = true;
	epg.k.start_time = start_time;
	epg.end_time = start_time + duration;
	if (event_name)
		epg.event_name.sprintf("%s", event_name);
	else {
		epg.event_name.sprintf("%s: ", service.name.c_str());
		epg.event_name.sprintf(ss::dateTime(epg.k.start_time, "%F %H:%M"));
		epg.event_name.sprintf(" - ");
		epg.event_name.sprintf(ss::dateTime(epg.end_time, "%F %H:%M"));
	}
	return toggle_recording_(service, epg);
}

template <typename _mux_t>
int receiver_t::scan_muxes(ss::vector_<_mux_t>& muxes, int subscription_id) {
	std::vector<task_queue_t::future_t> futures;

	futures.push_back(receiver_thread.push_task([this, &muxes, &subscription_id]() {
		subscription_id = (int) cb(receiver_thread).scan_muxes(muxes, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return subscription_id;
}

template int
receiver_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes, int subscription_id);

template int
receiver_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes, int subscription_id);

template int
receiver_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes, int subscription_id);

template <typename _mux_t> int
receiver_t::subscribe_mux(const _mux_t& mux, bool blindscan, int subscription_id) {

	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.scan_target = scan_target_t::SCAN_FULL;
	tune_options.use_blind_tune = blindscan;

	futures.push_back(receiver_thread.push_task([this, &mux, tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		subscription_id = (int) cb(receiver_thread).subscribe_mux(mux, (subscription_id_t) subscription_id,
																															tune_options, nullptr);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}
template int
receiver_t::subscribe_mux<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux, bool blindscan,
																						int subscription_id);

template int
receiver_t::subscribe_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, bool blindscan,
																						int subscription_id);

template int
receiver_t::subscribe_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, bool blindscan,
																						int subscription_id);

/*!
	for use by spectrum scan. Exclusively reserves lnb
	Reserving is only possible if no other subscriptions have currently reserved.

	Note that low_freq and high_freq should cover the requested band, but may be broader;
	in this case, the caller should make two calls, one for each band

*/
int
receiver_t::subscribe_lnb_spectrum(devdb::lnb_t& lnb_, const chdb::fe_polarisation_t& pol_, int32_t low_freq,
																			 int32_t high_freq, int sat_pos, int subscription_id) {
	auto lnb = reread_lnb(lnb_);
	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.spectrum_scan_options.start_time = time(NULL);
	tune_options.scan_target = scan_target_t::SCAN_FULL;
	tune_options.subscription_type = subscription_type_t::LNB_EXCLUSIVE;
	tune_options.tune_mode = tune_mode_t::SPECTRUM;
	auto& band_pol = tune_options.spectrum_scan_options.band_pol;
	band_pol.pol = pol_;
	if (band_pol.pol == chdb::fe_polarisation_t::NONE) {
		tune_options.spectrum_scan_options.scan_both_polarisations = true;
		band_pol.pol = chdb::fe_polarisation_t::H;
	}
	auto [low_freq_, mid_freq_, high_freq_] = devdb::lnb::band_frequencies(lnb, band_pol.band);
	low_freq = low_freq < 0 ? low_freq_ : low_freq;
	high_freq = high_freq < 0 ? high_freq_ : high_freq;

	if( high_freq <= low_freq  || low_freq < low_freq_ || high_freq > high_freq_) {
		user_errorx("Illegal frequency range for scan: %dkHz - %dKhz", low_freq, high_freq);
		return -1;
	}
	bool has_low = (low_freq_ != mid_freq_);
	bool need_low = ( low_freq < mid_freq_) && has_low;
	bool need_high = ( high_freq >= mid_freq_);
	assert(need_high || need_low);
	band_pol.band = (need_high && need_low) ? devdb::fe_band_t::NONE
		:  need_low ? devdb::fe_band_t::LOW : devdb::fe_band_t::HIGH;

	if (band_pol.band == devdb::fe_band_t::NONE) {
		// start with lowest band
		band_pol.band = devdb::lnb::band_for_freq(lnb, low_freq);
	} else if (devdb::lnb::band_for_freq(lnb, low_freq) != band_pol.band &&
						 devdb::lnb::band_for_freq(lnb, high_freq) != band_pol.band) {
		user_errorx("start and end frequency do not coincide with band");
		return -1;
	}
	tune_options.spectrum_scan_options.start_freq = low_freq;
	tune_options.spectrum_scan_options.end_freq = high_freq;

	if (sat_pos == sat_pos_none) {
		if (devdb::lnb::on_positioner(lnb)) {
		} else {
			if (lnb.networks.size() > 0)
				sat_pos = lnb.networks[0].sat_pos;
		}
	}
	tune_options.sat_pos = sat_pos;
	tune_options.spectrum_scan_options.sat_pos = sat_pos;
	futures.push_back(receiver_thread.push_task([this, &lnb, tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		subscription_id = (int) cb(receiver_thread).subscribe_lnb(lnb, tune_options, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}

/*!
	for use by a blindscan which scans all muxes in a band. Exclusively reserves lnb
*/
int
receiver_t::subscribe_lnb_blindscan(devdb::lnb_t& lnb_, const devdb::fe_band_pol_t& band_pol,
																		int subscription_id) {
	auto lnb = reread_lnb(lnb_);
	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	// tune_options.start_time = now;
	tune_options.scan_target = scan_target_t::SCAN_FULL;
	tune_options.subscription_type = subscription_type_t::LNB_EXCLUSIVE;
	tune_options.tune_mode = tune_mode_t::SCAN_BLIND;
	futures.push_back(receiver_thread.push_task([this, &lnb, tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		subscription_id = (int) cb(receiver_thread).subscribe_lnb(lnb, tune_options, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}

int
receiver_t::subscribe_lnb(devdb::lnb_t& lnb, retune_mode_t retune_mode,
													int subscription_id) {

	{
		auto txn = chdb.rtxn();
		auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
		if (c.is_valid()) {
			auto db_lnb = c.current();
			lnb.lof_offsets = db_lnb.lof_offsets;
			txn.abort();
		}
	}
	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.subscription_type = subscription_type_t::DISH_EXCLUSIVE;
	tune_options.tune_mode = tune_mode_t::POSITIONER_CONTROL;
	tune_options.retune_mode = retune_mode;
	futures.push_back(receiver_thread.push_task([this, &lnb, &tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		subscription_id = (int) cb(receiver_thread).subscribe_lnb(lnb, tune_options, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}

/*! Same as subscribe_lnb, but also subscribes to a mux in the same call
 */
int
receiver_t::subscribe_lnb_and_mux(devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
																			const pls_search_range_t& pls_search_range, retune_mode_t retune_mode,
																			int subscription_id) {

	{
		auto txn = chdb.rtxn();
		auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
		if (c.is_valid()) {
			auto db_lnb = c.current();
			lnb.lof_offsets = db_lnb.lof_offsets;
			txn.abort();
		}
	}
	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.constellation_options.num_samples = 1024 * 16;
	tune_options.scan_target = scan_target_t::SCAN_MINIMAL;
	tune_options.subscription_type = subscription_type_t::DISH_EXCLUSIVE;
	tune_options.use_blind_tune = blindscan;
	tune_options.retune_mode = retune_mode;
	tune_options.pls_search_range = pls_search_range;
	futures.push_back(receiver_thread.push_task([this, &lnb, &mux, tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		subscription_id = (int) cb(receiver_thread).subscribe_mux(mux, (subscription_id_t) subscription_id,
																															tune_options, &lnb);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_service(const chdb::service_t& service,
																															int subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	futures.push_back(receiver_thread.push_task([this, &subscription_id, &service, &ret]() {
		cb(receiver_thread).abort_scan();
		ret = cb(receiver_thread).subscribe_service(service, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	// browse_history.save(service);
	return ret;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_recording(const recdb::rec_t& rec, int subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	futures.push_back(receiver_thread.push_task([this, &subscription_id, &rec, &ret]() {
		ret = cb(receiver_thread).subscribe_recording(rec, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	return ret;
}

void receiver_t::start_recording(const recdb::rec_t& rec_in) {
	std::optional<recdb::rec_t> ret;
	receiver_thread.push_task([this, rec_in]() { // rec_in must not be passed by reference!
		cb(receiver_thread).start_recording(rec_in);
		return 0;
	});
}

void receiver_t::stop_recording(const recdb::rec_t& rec_in) {
	auto epg_key = rec_in.epg.k;

	std::optional<recdb::rec_t> ret;
	receiver_thread.push_task([this, rec_in]() { // rec_in must not be passed by reference!
		cb(receiver_thread).stop_recording(rec_in);
		return 0;
	});
}

int receiver_t::unsubscribe(int subscription_id) {
	receiver_thread.push_task([this, subscription_id]() {
		cb(receiver_thread).abort_scan();
		cb(receiver_thread).unsubscribe((subscription_id_t) subscription_id);
		return 0;
	});
	return -1;
}

int receiver_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	tuner_thread
		.push_task([this, &service, &epg_record]() {
			cb(tuner_thread).toggle_recording(service, epg_record);
			return 0;
		})
		.wait();
	return 0;
}

int receiver_t::toggle_recording(const chdb::service_t& service) {
	auto txnepg = epgdb.rtxn();
	auto now = system_clock_t::now();
	auto epg_record = epgdb::running_now(txnepg, service.k, now);
	if (epg_record) {
		return toggle_recording(service, *epg_record);
	}

	// start=false and stop=false means: toggle
	return toggle_recording_(service, now, options.readAccess()->default_record_time.count(), nullptr);
}

receiver_t::receiver_t(const neumo_options_t* options)
	: receiver_thread(*this)
	, scam_thread(receiver_thread)
	, tuner_thread(*this)
	, browse_history(chdb)
	, rec_browse_history(recdb)
{
	if(options) {
		*(this->options.writeAccess()) = *options;
	}
	set_logconfig(options->logconfig.c_str());
	identify();

	log4cxx_store_threadname();
	// this will throw in case of error
	auto r = this->options.readAccess();
	statdb.open(r->statdb.c_str());
	chdb.extra_flags = MDB_NOSYNC;
	chdb.open(r->chdb.c_str());
	devdb.extra_flags = MDB_NOSYNC;
	devdb.open(r->devdb.c_str(), false /*allow_degraded_mode*/, nullptr /*table_name*/, 2*1024u*1024u /*mapsize*/);
	epgdb.extra_flags = MDB_NOSYNC;
	epgdb.open(r->epgdb.c_str());
	recdb.open(r->recdb.c_str());
	browse_history.init();
	rec_browse_history.init();
	start();
}

void receiver_t::start() {
	receiver_thread.start_running();
	scam_thread.start_running();
}

void receiver_t::stop() {
	dtdebugx("STOP CALLED\n");
	receiver_thread.stop_running(true);
}

int receiver_thread_t::run() {
	/*@todo: The timer below is used to gather signal strength, cnr and ber.
		When used from a gui, it may be better to let the gui handle this asynchronously
	*/
	set_name("receiver");
	logger = Logger::getLogger("receiver"); // override default logger for this thread

	dtdebug("RECEIVER starting\n");
	adaptermgr->start();
	epoll_add_fd(adaptermgr->inotfd, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET);
	now = system_clock_t::now();
	receiver.tuner_thread.start_running();
	double period_sec = 2.0;
	timer_start(period_sec);

	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorx("error in poll: %s", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				ss::string<128> prefix;
				prefix << "RECEIVER-CMD";
				log4cxx::NDC ndc(prefix.c_str());
				// an external request was received
				// run_tasks returns -1 if we must exit
				if (run_tasks(now) < 0) {
					// detach();
					dtdebug("Exiting cleanly");
					return 0;
				}
			} else if (is_timer_fd(evt)) {
				receiver.update_playback_info();
			} else if (evt->data.fd == adaptermgr->inotfd) {
				adaptermgr->run();
			}
		}
	}
	// detach();
	return 0;
}

void receiver_t::notify_signal_info(const chdb::signal_info_t& signal_info) {

	tuner_thread.push_task([this, signal_info =const_cast<chdb::signal_info_t&>(signal_info)] () mutable  {
		cb(tuner_thread).on_notify_signal_info(signal_info);
		return 0;
	});

	{
		auto mpv_map = active_mpvs.readAccess();
		for (auto [mpv_, mpv_shared_ptr] : *mpv_map) {
			auto* mpv = mpv_shared_ptr.get();
			if (!mpv)
				continue;
			mpv->notify(signal_info);
		}
	}
	{
		auto mss = subscribers.readAccess();
		for (auto [ms_, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			ms->notify_signal_info(signal_info);
		}
	}
}

void receiver_t::notify_spectrum_scan(const statdb::spectrum_t& spectrum) {
	{
		auto mss = subscribers.readAccess();
		for (auto [ms_, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			ms->notify_spectrum_scan(spectrum);
		}
	}
}

void receiver_t::update_playback_info() {
	auto mpv_map = active_mpvs.readAccess();
	for (auto [mpv_, mpv_shared_ptr] : *mpv_map) {
		auto* mpv = mpv_shared_ptr.get();
		if (!mpv)
			continue;
		mpv->update_playback_info();
	}
}

/*!
	lnbs: if non-empty, only these lnbs are allowed during scannin
	muxes: if non-empty, it provides a list of muxes to scan (should be non empty)
	bool scan_newly_found_muxes => if new muxes are found from si information, also scan them
*/
subscription_id_t
receiver_thread_t::subscribe_scan(std::vector<task_queue_t::future_t>& futures,
																			ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<devdb::lnb_t>* lnbs,
																			bool scan_found_muxes, int max_num_subscriptions, subscription_id_t subscription_id) {
	bool init = !scanner.get();
	if (scanner.get() && (int) subscription_id < 0) {
		user_error("Scan is already in progress");
		return subscription_id_t{-1};
	}
	if ((int) subscription_id < 0)
		subscription_id = subscription_id_t{this->next_subscription_id++};
	if (!scanner)
		scanner = std::make_shared<scanner_t>(*this, scan_found_muxes, max_num_subscriptions, subscription_id);
	scanner->add_muxes(muxes, init);
	if (lnbs && init)
		scanner->set_allowed_lnbs(*lnbs);
	scanner->housekeeping(); // start initial scan
	return subscription_id;
}

template <typename mux_t>
subscription_id_t
receiver_thread_t::subscribe_scan(std::vector<task_queue_t::future_t>& futures, ss::vector_<mux_t>& muxes,
																			bool scan_found_muxes, int max_num_subscriptions, subscription_id_t subscription_id) {
	bool init = !scanner.get();
	if (scanner.get() && (int) subscription_id < 0) {
		user_error("Scan is already in progress");
		return subscription_id_t{-1};
	}
	if ((int) subscription_id < 0)
		subscription_id = subscription_id_t{this->next_subscription_id++};
	if (!scanner)
		scanner = std::make_unique<scanner_t>(*this, scan_found_muxes, max_num_subscriptions, subscription_id);
	scanner->add_muxes(muxes, init);
	scanner->housekeeping(); // start initial scan
	return subscription_id;
}

/*!
	called when complete channel scan needs to be aborted
*/
void receiver_thread_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id) {
	scanner.reset();
}

void receiver_t::set_options(const neumo_options_t& options) {
	auto r = this->options.writeAccess();
	*r = options;
}

neumo_options_t receiver_t::get_options() {
	auto r = options.readAccess();
	return *r;
}

time_t receiver_thread_t::scan_start_time() const {
	return scanner.get() ? scanner->scan_start_time : -1;
}


inline std::shared_ptr<active_adapter_t> receiver_thread_t::make_active_adapter(const devdb::fe_key_t& fe_key) {
	auto dvb_frontend = adaptermgr->find_fe(fe_key);
	return std::make_shared<active_adapter_t>(receiver, dvb_frontend);
}

thread_local thread_group_t thread_group{thread_group_t::unknown};

/*
	subscription_id = -1 is returned from functions when resource allocation fails
	If an error occurs after resource allocation, subscription_id should not be set to -1
	(unless after releasing resources)

 */
