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
#include "util/neumovariant.h"
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

int task_queue_t::future_t::get() {
	auto ret= base.get();
	user_error_.append(ret.errmsg);
	return ret.retval;
}

bool wait_for_all(std::vector<task_queue_t::future_t>& futures, bool clear_errors) {
	if(clear_errors)
		user_error_.clear();
	bool error = false;
	for (auto& f : futures) {
		auto ret= f.get();
		error |= (ret < 0);
	}
	futures.clear();
	return error;
}

/*
	returns number of executed tasks, or -1 in case of exit
*/
int task_queue_t::run_tasks(system_time_t now_, bool do_acknowledge) {
	now = now_;
	// dtdebugf("start");
	if(do_acknowledge)
		acknowledge();
	// dtdebugf("acknowledged");
	int count = 0;
	for (;;) {
		task_t task;
		{
			std::lock_guard<std::mutex> lk(mutex);
			if (tasks.empty()) {
				// dtdebugf("empty");
				break;
			}
			if(has_exited_) {
				dtdebugf("Ignoring tasks after exit");
				tasks = {};
				break;
			}

			task = std::move(tasks.front());
			// dtdebugf("pop task");
			tasks.pop();
		}
		// dtdebugf("start task");
		task();
		count++;
		// dtdebugf("end task");
	}
	if (must_exit_) {
		return -1;
	}
	return count;
}


/*
	service_only=true called by
         receiver_thread_t::subscribe_service_in_use
         receiver_thread_t::cb_t::scan_muxes
				 receiver_thread_t::cb_t::subscribe_mux

 */
void receiver_thread_t::unsubscribe_playback_only(std::vector<task_queue_t::future_t>& futures,
																								 subscription_id_t subscription_id) {
	if ((int)subscription_id < 0)
		return;
	auto [itrec, foundrec] = find_in_map(this->reserved_playbacks, subscription_id);
	if (foundrec) {
		this->reserved_playbacks.erase(itrec);
		return;
	}
}


/*
	called from:
    unsubscribe_mux_and_service_only
		receiver_thread_t::subscribe_lnb to release no longer needed current active_adapter
 */
void receiver_thread_t::unsubscribe_mux_and_service_only(std::vector<task_queue_t::future_t>& futures,
																						 db_txn& devdb_wtxn, subscription_id_t subscription_id) {
	dtdebugf("Unsubscribe subscription_id={:d}", (int)subscription_id);
	assert((int)subscription_id >= 0);
	// release subscription's service on this mux, if any
	auto updated_dbfe = devdb::fe::unsubscribe(devdb_wtxn, subscription_id);
	if(!updated_dbfe) {
		return; //can happen when unsubscribing a scan
	}
	dtdebugf("release_active_adapter subscription_id={:d} use_count={:d}", (int) subscription_id,
					 updated_dbfe ? updated_dbfe->sub.subs.size() : 0);
	assert(updated_dbfe);
	release_active_adapter(futures, subscription_id, *updated_dbfe);
}

void receiver_thread_t::release_active_adapter(std::vector<task_queue_t::future_t>& futures,
																							 subscription_id_t subscription_id,
																							 const devdb::fe_t& updated_dbfe) {
	dtdebugf("release_active_adapter subscription_id={:d}", (int)subscription_id);
	assert((int)subscription_id >= 0);
	// release subscription's service on this mux, if any

	/* The active_adapter is no longer in use; so ask tuner thread to release it.
	 */
	{
		auto w = this->active_adapters.writeAccess();
		auto& m = *w;
		auto [it, found] = find_in_map(m, subscription_id);
		assert(found);
		auto& tuner_thread = it->second->tuner_thread;
		if(updated_dbfe.sub.subs.size() ==0) {
			//ask tuner thread to exit, but do not wait
			dtdebugf("Pushing tuner_thread.stop_running");
			tuner_thread.update_dbfe(updated_dbfe);
			futures.push_back(tuner_thread.stop_running(false/*wait*/));
		} else {
			futures.push_back(tuner_thread.push_task([&tuner_thread, updated_dbfe = updated_dbfe]() {
				tuner_thread.update_dbfe(updated_dbfe);
				return 0;
			}));
		}
		dtdebugf("released");
		m.erase(it); //if this is the last reference, it will release the tuner_thread
	}
}



/*
	Release all resources (services, muxes) and also update database

  service_only=false called by
	       receiver_thread_t::cb_t::subscribe_mux after subscribing fails
				 receiver_thread_t::subscribe_lnb after subscribing fails
				 receiver_thread_t::cb_t::subscribe_service after subscribing fails
  service_only=false/true called by
         receiver_thread_t::cb_t::unsubscribe
*/
void receiver_thread_t::unsubscribe_all(std::vector<task_queue_t::future_t>& futures,
																				db_txn& devdb_wtxn, subscription_id_t subscription_id) {
	if ((int)subscription_id < 0)
		return;
	auto scanner = get_scanner();
	if (scanner.get()) {
		if(unsubscribe_scan(futures, devdb_wtxn, subscription_id)) {
			 dtdebugf("unsubscribed scan");
		}
	}
	unsubscribe_playback_only(futures, subscription_id);
	dtdebugf("calling unsubscribe_mux_and_service_only");
	unsubscribe_mux_and_service_only(futures, devdb_wtxn, subscription_id);
	dtdebugf("calling unsubscribe_mux_and_service_only -done");
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::subscribe_service(
	const chdb::any_mux_t& mux, const chdb::service_t& service, subscription_id_t subscription_id) {

	std::vector<task_queue_t::future_t> futures;
	auto tune_options = receiver.get_default_tune_options(subscription_type_t::TUNE);
	tune_options.scan_target = scan_target_t::SCAN_FULL_AND_EPG;
	assert(!tune_options.need_spectrum);
	subscribe_ret_t sret;
	auto devdb_wtxn = receiver.devdb.wtxn();
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	&service,
																	false /*do_not_unsubscribe_on_failure*/);
	devdb_wtxn.commit();
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Service reservation failed: {}", service);
		return {};
	}
	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe);
	}
	auto* active_adapter_p = find_or_create_active_adapter(futures, devdb_wtxn, sret);
	assert(active_adapter_p);
	auto& aa = *active_adapter_p;
	tune_options.tune_pars = sret.tune_pars;

	std::unique_ptr<playback_mpm_t> playback_mpm_ptr;
	futures.push_back(aa.tuner_thread.push_task([&playback_mpm_ptr, &aa, &mux, &service, &sret,
																						&tune_options]() {
		playback_mpm_ptr = cb(aa.tuner_thread).subscribe_service(sret, mux, service, tune_options);
		return 0;
	}));
	wait_for_all(futures); //essential
	return playback_mpm_ptr;
}

subscription_id_t receiver_thread_t::subscribe_service_for_recording(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::any_mux_t& mux,
	recdb::rec_t& rec, subscription_id_t subscription_id) {
	assert((int)subscription_id == rec.subscription_id);
	tune_options_t tune_options(scan_target_t::SCAN_FULL_AND_EPG);
	assert(!tune_options.need_spectrum);
	subscribe_ret_t sret;
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	&rec.service,
																	false /*do_not_unsubscribe_on_failure*/);
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Service reservation failed: {}", rec.service);
		return subscription_id_t::NONE;
	}
	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe);
	}
	auto* active_adapter_p = find_or_create_active_adapter(futures, devdb_wtxn, sret);
	assert(active_adapter_p);
	auto& aa = *active_adapter_p;
	tune_options.tune_pars = sret.tune_pars;

	futures.push_back(aa.tuner_thread.push_task([&aa, mux, rec, sret,
																							 tune_options]() mutable {
		cb(aa.tuner_thread).subscribe_service_for_recording(sret, mux, rec, tune_options);
		return 0;
	}));
	return sret.subscription_id;
}

receiver_thread_t::receiver_thread_t(receiver_t& receiver_)
	: task_queue_t(thread_group_t::receiver),
		adaptermgr(adaptermgr_t::make(receiver_))
		//, rec_manager_thread(*this)
	, receiver(receiver_) {}

int receiver_thread_t::exit() {
	dtdebugf("Receiver thread exiting");

	std::vector<task_queue_t::future_t> futures;
	{
		auto mss = receiver.subscribers.readAccess();
		auto devdb_wtxn = receiver.devdb.wtxn();
		for (auto [ptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			unsubscribe_all(futures, devdb_wtxn, ms->get_subscription_id());
		}
		devdb_wtxn.commit();
	}

	dtdebugf("Receiver thread exiting -starting to wait");
	wait_for_all(futures, true /*clear_all_errors*/);

	dtdebugf("Receiver thread exiting -stopping recmgr");
	receiver.rec_manager.recmgr_thread.stop_running(true);

	dtdebugf("Receiver thread exiting -stopping tuner threads");
	{
		auto w = active_adapters.writeAccess();
		auto& m = *w;
		for(auto& [ subscription_id, aa] : m) {
			auto& tuner_thread = aa->tuner_thread;
			//ask tuner thread to exit, but do not wait
			futures.push_back(tuner_thread.stop_running(false/*wait*/));
		}
	}
	wait_for_all(futures, true /*clear all errors*/);
	{
		auto w = active_adapters.writeAccess();
		w->clear();
	}

	dtdebugf("Receiver thread exiting - stopping scam");
	receiver.scam_thread.stop_running(true);

	dtdebugf("Receiver thread exiting - stopping adaptermgr");
	this->adaptermgr->stop();
	this->adaptermgr.reset();
	dtdebugf("Receiver thread exiting - done");
	return 0;
}

receiver_t::~receiver_t() {
	dtdebugf("receiver destroyed");
}

receiver_thread_t::~receiver_thread_t() {
	dtdebugf("receiver thread terminating");
	// detach();
}

void receiver_thread_t::cb_t::abort_scan() {
	//TODO: this should reset only the subscribed scan
	auto scanner = get_scanner();
	if (scanner) {
		dtdebugf("Aborting scan in progress");
		reset_scanner();
	}
}


/*
	called from tuner thread when scanning a mux has ended
*/
void receiver_thread_t::cb_t::send_scan_mux_end_to_scanner(const devdb::fe_t& finished_fe,
																													 const chdb::any_mux_t& finished_mux,
																													 const chdb::scan_id_t& scan_id,
																													 subscription_id_t subscription_id)
{
	auto scanner = get_scanner();
	if (!scanner.get()) {
		return;
	}
	dtdebugf("Calling scanner->on_scan_mux_end: adapter={}  mux={} subscription_id={}",
					finished_fe.adapter_no, finished_mux, (int) subscription_id);
	/*call scanner to start scanning new muxes and to prepare a scan report
		which will be asynchronously returned to receiver_thread by calling notify_scan_mux_end,
		which will pass the message to the GUI
	*/
	auto remove_scanner = scanner->on_scan_mux_end(finished_fe, finished_mux, scan_id, subscription_id);
	if (remove_scanner) {
		reset_scanner();
	}
}


void receiver_t::on_scan_mux_end(const devdb::fe_t& finished_fe,
																 const chdb::any_mux_t& finished_mux,
																 const chdb::scan_id_t& scan_id, subscription_id_t subscription_id) {

	bool has_scanning_subscribers{true};
	auto& receiver_thread = this->receiver_thread;
	if(has_scanning_subscribers) {
		//capturing by value is essential
		receiver_thread.push_task([&receiver_thread, finished_fe, finished_mux,
															 scan_id, subscription_id]() {
			cb(receiver_thread).send_scan_mux_end_to_scanner(finished_fe, finished_mux,
																											 scan_id, subscription_id);
			return 0;
		});
	}
}


subscription_id_t
receiver_thread_t::subscribe_spectrum(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::sat_t& sat,
	const chdb::band_scan_t& band_scan,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const chdb::scan_id_t& scan_id,
	bool do_not_unsubscribe_on_failure) {
	subscribe_ret_t sret;
	assert(tune_options.need_spectrum);

	sret = devdb::fe::subscribe_sat_band(devdb_wtxn, subscription_id,
																			 tune_options,
																			 sat, band_scan, do_not_unsubscribe_on_failure);

	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Sat band reservation failed: {}:{}", sat, band_scan);
		return subscription_id_t::RESERVATION_FAILED;
	}
	tune_options.tune_pars = sret.tune_pars;
	dtdebugf("lnb activate subscription_id={:d}", (int) sret.subscription_id);

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe);
	}

	auto* activate_adapter_p = find_or_create_active_adapter(futures, devdb_wtxn, sret);
	assert(activate_adapter_p);
	auto& aa = *activate_adapter_p;

	futures.push_back(aa.tuner_thread.push_task([&aa, subscription_id, sret, tune_options]() {
		auto ret = cb(aa.tuner_thread).lnb_spectrum_scan(subscription_id, sret, tune_options);
		if (ret < 0)
			dterrorf("tune returned {:d}", ret);
		return ret;
	}));

	dtdebugf("Subscribed to: sat band={}:{}", sat, band_scan);
	return sret.subscription_id;
}

template <typename _mux_t>
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const _mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const chdb::scan_id_t& scan_id,
	bool do_not_unsubscribe_on_failure) {
	subscribe_ret_t sret;

	assert(!tune_options.need_spectrum);
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	(const chdb::service_t*) nullptr /*service*/,
																	do_not_unsubscribe_on_failure);

	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Mux reservation failed: {}", mux);
		return subscription_id_t::RESERVATION_FAILED;
	}

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe);
	}

	auto* active_adapter_p = find_or_create_active_adapter(futures, devdb_wtxn, sret);
	assert(active_adapter_p);
	auto& aa = *active_adapter_p;
	tune_options.tune_pars = sret.tune_pars;

	futures.push_back(aa.tuner_thread.push_task([&aa, mux, sret, tune_options]() {
		cb(aa.tuner_thread).subscribe_mux(sret, mux, tune_options);
			return 0;
	}));

	return sret.subscription_id;
}

template <typename mux_t>
subscription_id_t receiver_thread_t::cb_t::scan_muxes(ss::vector_<mux_t>& muxes,
																											const tune_options_t& tune_options,
																											subscription_id_t& subscription_id)
/*
	somewhat ugly hack: subscription_id needs to be passed by reference so that the calling
	subscriber_t has its internal subscription_id set when scanning starts. Otherwise the
	initial scan report woill not be received
 */
{
	auto s = fmt::format("SCAN[{}] {} muxes", (int) subscription_id, (int) muxes.size());
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;
	auto scanner = get_scanner();
	if ((!scanner.get() && (int)subscription_id >= 0)) {
		unsubscribe_playback_only(futures, subscription_id);
		auto devdb_wtxn = receiver.devdb.wtxn();
		unsubscribe_mux_and_service_only(futures, devdb_wtxn, subscription_id);
		devdb_wtxn.commit();
		bool error = wait_for_all(futures);
		if (error) {
			dterrorf("Unhandled error in scan_muxes"); // This will ensure that tuning is retried later
		}
	}
	int max_num_subscriptions = 100;
	subscription_id = this->receiver_thread_t::scan_muxes(futures, muxes,
																												tune_options,
																												max_num_subscriptions, subscription_id);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return subscription_id;
}

subscription_id_t receiver_thread_t::cb_t::scan_spectral_peaks(
	ss::vector_<chdb::spectral_peak_t>& peaks,
	const statdb::spectrum_key_t& spectrum_key, subscription_id_t subscription_id)
{
	auto s = fmt::format("SCAN[{:d}] {} peaks", (int) subscription_id, (int) peaks.size());
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;

	if ((int) subscription_id >= 0)
		unsubscribe_playback_only(futures, subscription_id);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}
	bool scan_newly_found_muxes = true;
	int max_num_subscriptions = 100;
	subscription_id = this->receiver_thread_t::scan_spectral_peaks(futures, peaks, spectrum_key,
																																 scan_newly_found_muxes,
																																 max_num_subscriptions, subscription_id);
	error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return subscription_id;
}

subscription_id_t receiver_thread_t::cb_t::scan_bands(
	const ss::vector_<chdb::sat_t>& sats,
	const ss::vector_<chdb::fe_polarisation_t>& pols,
	tune_options_t tune_options, subscription_id_t& subscription_id)
{
	auto s = fmt::format("SCAN[{}] {} sats", (int) subscription_id, (int) sats.size());
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;
	auto scanner = get_scanner();

	if ((!scanner.get() && (int)subscription_id >= 0)) {
		unsubscribe_playback_only(futures, subscription_id);
		auto devdb_wtxn = receiver.devdb.wtxn();
		unsubscribe_mux_and_service_only(futures, devdb_wtxn, subscription_id);
		devdb_wtxn.commit();
		bool error = wait_for_all(futures);
		if (error) {
			dterrorf("Unhandled error in scan_bands"); // This will ensure that tuning is retried later
		}
	}
	int max_num_subscriptions = 100;

	assert(tune_options.need_spectrum);
	subscription_id = this->receiver_thread_t::scan_bands(futures, sats, pols, tune_options,
																												max_num_subscriptions, subscription_id);

	auto error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return subscription_id;
}


/*
	called by receiver_t::subscribe_lnb_and_mux
	sets up futures
	unregisters service if any
	calls receiver_thread_t::subscribe_mux which does the mux work:
	if something goes wrong, removes any subscription. Not sure if this has any effect at all
 */
template <typename _mux_t>
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux(const _mux_t& mux, subscription_id_t subscription_id,
																			 tune_options_t tune_options, const chdb::scan_id_t& scan_id) {
	devdb::fe_key_t subscribed_fe_key;
	auto s = fmt::format("SUB[{}] {}",  (int) subscription_id, mux);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	if ((int) subscription_id >= 0)
		unsubscribe_playback_only(futures, subscription_id);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}

	auto devdb_wtxn = receiver.devdb.wtxn();
	auto ret_subscription_id =
		this->receiver_thread_t::subscribe_mux(futures, devdb_wtxn, mux, subscription_id, tune_options,
																					 scan_id, false /*do_not_unsubscribe_on_failure*/);
	devdb_wtxn.commit();
	error = wait_for_all(futures);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(subscription_id);
		set_error(saved_error); //restore error message
		return {subscription_id_t::TUNE_FAILED, {}};
	}
	else
		return {ret_subscription_id, subscribed_fe_key};
}

active_adapter_t* receiver_thread_t::find_or_create_active_adapter
(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,  const subscribe_ret_t& sret)
{
#ifndef NDEBUG
	bool failed = sret.subscription_failed();
#endif
	assert(!failed);
	{
		auto w = this->active_adapters.writeAccess();
		auto& m = *w;

		if((int)sret.sub_to_reuse >=0 && sret.sub_to_reuse != sret.subscription_id) {
			auto [it, found] = find_in_map(m, sret.sub_to_reuse);
			assert(found);
			m[sret.subscription_id] = it->second;
			auto& tuner_thread = it->second->tuner_thread;
			assert(sret.aa.updated_new_dbfe);
			futures.push_back(tuner_thread.push_task([&tuner_thread, updated_dbfe = *sret.aa.updated_new_dbfe]() {
				tuner_thread.update_dbfe(updated_dbfe);
				return 0;
			}));
			return it->second.get();
		}
		assert((int) sret.sub_to_reuse  < 0  || sret.sub_to_reuse == sret.subscription_id);
		auto [it, found] = find_in_map(m, sret.subscription_id);
		if(found)
			return it->second.get();
	}

	assert(!sret.retune);
	assert(sret.aa.is_new_aa());
	assert(sret.aa.updated_new_dbfe);
	auto dvb_frontend = receiver.fe_for_dbfe(sret.aa.updated_new_dbfe->k);
	auto aa = active_adapter_t::make(receiver, dvb_frontend);
	dtdebugf("created new AA: {:p}", fmt::ptr(aa.get()));
	{
		auto w = this->active_adapters.writeAccess();
		auto& m = *w;
		m[sret.subscription_id] = aa;
		auto& tuner_thread = aa->tuner_thread;
		assert(sret.aa.updated_new_dbfe);
		futures.push_back(tuner_thread.push_task([&tuner_thread, updated_dbfe = *sret.aa.updated_new_dbfe]() {
			tuner_thread.update_dbfe(updated_dbfe);
			return 0;
		}));
	}
	return aa.get();
}

subscription_id_t receiver_thread_t::subscribe_lnb(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
																									 devdb::rf_path_t& rf_path,
																									 devdb::lnb_t& lnb, tune_options_t tune_options,
																									 subscription_id_t subscription_id) {
	bool need_spectrum = tune_options.tune_mode == tune_mode_t::SPECTRUM;
	tune_options.allowed_rf_paths = {rf_path};
	tune_options.need_spectrum = need_spectrum;
	tune_options.may_control_lnb = true;
	tune_options.may_move_dish = true;
	tune_options.may_control_dish = true;
	auto sret = devdb::fe::subscribe_rf_path(devdb_wtxn, subscription_id,
																					 tune_options,
																					 rf_path,
																					 false /*do_not_unsubscribe_on_failure*/);
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Lnb reservation failed: {}", lnb);

		return subscription_id_t::RESERVATION_FAILED;
	}
	tune_options.tune_pars = sret.tune_pars;

	dtdebugf("lnb activate subscription_id={:d}", (int) sret.subscription_id);

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe);
	}

	auto* activate_adapter_p = find_or_create_active_adapter(futures, devdb_wtxn, sret);
	assert(activate_adapter_p);
	auto& aa = *activate_adapter_p;

	futures.push_back(aa.tuner_thread.push_task([&aa, subscription_id, sret, tune_options]() {
		auto ret = cb(aa.tuner_thread).lnb_activate(subscription_id, sret, tune_options);
		if (ret < 0)
			dterrorf("tune returned {:d}", ret);
		return ret;
	}));

	dtdebugf("Subscribed to: lnb={}", lnb);
	return sret.subscription_id;
}

devdb::lnb_t receiver_t::reread_lnb(const devdb::lnb_t& lnb)

{
	auto txn = devdb.rtxn();
	auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
	if (c.is_valid()) {
		auto ret = lnb;
		ret.lof_offsets = c.current().lof_offsets;
		return ret;
	}
	return lnb;
}

subscription_id_t
receiver_thread_t::cb_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb_, tune_options_t tune_options,
																			 subscription_id_t subscription_id) {

	auto lnb = receiver.reread_lnb(lnb_);
	auto s = fmt::format("SUB[{}]",  (int) subscription_id, lnb);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();
	user_error_.clear();
	auto ret_subscription_id = this->receiver_thread_t::subscribe_lnb(
		futures, devdb_wtxn, rf_path, lnb, tune_options, subscription_id);
	devdb_wtxn.commit();
	bool error = wait_for_all(futures, false /*clear_errors*/) ||
		(int)ret_subscription_id <0; //the 2nd case occurs when reservation failed (no futures to wait for)
	if (error) {
		auto saved_error = get_error();
		unsubscribe(subscription_id);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
	}
	return ret_subscription_id;
}

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbs_mux_t>(
	const chdb::dvbs_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options, const chdb::scan_id_t& scan_id);

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbc_mux_t>(
	const chdb::dvbc_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options, const chdb::scan_id_t& scan_id);

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbt_mux_t>(
	const chdb::dvbt_mux_t& mux, subscription_id_t subscription_id,
	tune_options_t tune_options, const chdb::scan_id_t& scan_id);

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

std::unique_ptr<playback_mpm_t> receiver_thread_t::subscribe_playback_(const recdb::rec_t& rec,
																																				subscription_id_t subscription_id) {

	dtdebugf("Subscribe recording {} sub={}: playback start", rec, (int) subscription_id);

	auto active_playback = std::make_shared<active_playback_t>(receiver, rec);
	this->reserved_playbacks[subscription_id] = active_playback;
	return active_playback->make_client_mpm(receiver, subscription_id);
}

std::unique_ptr<playback_mpm_t> receiver_thread_t::cb_t::subscribe_service(const chdb::service_t& service,
																																					 subscription_id_t subscription_id) {
	auto s = fmt::format("SUB[{}] {}", (int) subscription_id, service);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	dtdebugf("SUBSCRIBE started");

	auto chdb_txn = receiver.chdb.rtxn();
	auto [mux, error1] = mux_for_service(chdb_txn, service);
	if (error1 < 0) {
		user_errorf("Could not find mux for {}", service);
		if ((int) subscription_id >= 0) {
			auto devdb_wtxn = receiver.devdb.wtxn();
			unsubscribe_all(futures, devdb_wtxn, subscription_id);
			devdb_wtxn.commit();
		}
		chdb_txn.abort();
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
	chdb_txn.abort();

	dtdebugf("SUBSCRIBE - calling subscribe_service");
	// now perform the requested subscription
	auto mpmptr = this->receiver_thread_t::subscribe_service(mux, service, subscription_id);
	/*wait_for_futures is needed because active_adapters/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/active_adapter threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/
	dtdebugf("SUBSCRIBE - returning to caller");
	return mpmptr;
}

std::unique_ptr<playback_mpm_t>
receiver_thread_t::cb_t::subscribe_playback(const recdb::rec_t& rec,
																												 subscription_id_t subscription_id) {
	auto s = fmt::format("SUB[{}] {}", (int) subscription_id, rec);
	log4cxx::NDC ndc(s);
	dtdebugf("SUBSCRIBE rec started");

	std::vector<task_queue_t::future_t> futures;
	active_playback_t* active_playback{nullptr};

	if ((int) subscription_id >= 0) {
		// unsubscribe existing service and/or mux if one exists
		auto [itrec, found] = find_in_map(this->reserved_playbacks, subscription_id);
		if (found) {
			active_playback = &*(itrec->second);
			if (rec.epg.k == active_playback->currently_playing_recording.epg.k) {
				dtdebugf("subscribe {}: already subscribed to recording", rec);
				return active_playback->make_client_mpm(receiver, subscription_id);
			}
		}
		unsubscribe(subscription_id);
	}
	auto sret = subscribe_ret_t{subscription_id, false /*failed*/};

	return subscribe_playback_(rec, sret.subscription_id);
}

/*!
	starts the recording, but also updates some of its fields and returns those
	returns -1 on error
*/
void receiver_thread_t::cb_t::start_recording(
	recdb::rec_t rec_in) // important: should not be a reference (async called!)
{
	auto s =fmt::format("REC {}", rec_in.service);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	dtdebugf("RECORD started");

	auto chdb_txn = receiver.chdb.rtxn();
	auto [mux, error1] = mux_for_service(chdb_txn, rec_in.service);
	if (error1 < 0) {
		user_errorf("Could not find mux for {}", rec_in.service);
		return;
	}
	chdb_txn.abort();

	auto devdb_wtxn =  receiver.devdb.wtxn();

	dtdebugf("RECORD - calling subscribe_");
	// now perform the requested subscription
	this->receiver_thread_t::subscribe_service_for_recording(futures, devdb_wtxn, mux, rec_in,
																													 subscription_id_t{rec_in.subscription_id});
	devdb_wtxn.commit();
	return;
}


/*!
	Toggle between recording on or off, except when start or stop is true
	Returns the new status: 1 = scheduled for recording, 0 = no longer scheduled for recording, -1 error

*/
int receiver_t::toggle_recording_(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	dtdebugf("epg={}", epg_record);
	//call by reference allows because of subsequent .get
	int ret{-1};
	auto& recmgr_thread = rec_manager.recmgr_thread;
	recmgr_thread.push_task([&recmgr_thread, &ret, &service, &epg_record]() {
		ret=cb(recmgr_thread).toggle_recording(service, epg_record);
		return 0;
	}).wait();

	if(ret<0)
		global_subscriber->notify_error(get_error());
	return ret;
}

/*!
	Returns the new status: 1=recording, 0=stopped recording
*/
int receiver_t::toggle_recording_(const chdb::service_t& service, system_time_t start_time_, int duration,
																	const char* event_name) {
	auto start_time = system_clock_t::to_time_t(start_time_);
	dtdebugf("toggle_recording: {}", service);
	epgdb::epg_record_t epg;
	epg.k.service = service.k;
	epg.k.event_id = TEMPLATE_EVENT_ID; /*prefix 0xffff0000 signifies a non-dvb event_id;
																				all live recordings have the same id
																			*/
	epg.k.anonymous = true;
	epg.k.start_time = start_time;
	epg.end_time = start_time + duration;
	if (event_name)
		epg.event_name.format("{:s}", event_name);
	else {
		epg.event_name.format("{:s}: ", service.name.c_str());
		epg.event_name.format(":%F %H:%M",epg.k.start_time);
		epg.event_name.format(" - ");
		epg.event_name.format(":%F %H:%M", epg.end_time);
	}
	return toggle_recording_(service, epg);
}


/*
	main entry point
 */
template <typename _mux_t>
subscription_id_t receiver_t::scan_muxes(ss::vector_<_mux_t>& muxes,
																				 const tune_options_t& tune_options,
																				 subscription_id_t& subscription_id) {
	std::vector<task_queue_t::future_t> futures;

	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &muxes, &tune_options, &subscription_id]() {
		subscription_id = cb(receiver_thread).scan_muxes(muxes, tune_options, subscription_id);
		cb(receiver_thread).scan_now(); // start initial scan
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return subscription_id;
}

subscription_id_t receiver_t::scan_spectral_peaks(ss::vector_<chdb::spectral_peak_t>& peaks,
																			const statdb::spectrum_key_t& spectrum_key,
																		subscription_id_t subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &peaks, &spectrum_key, &subscription_id]() {
		subscription_id = cb(receiver_thread).scan_spectral_peaks(peaks, spectrum_key, subscription_id);
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return subscription_id;
}

subscription_id_t receiver_t::scan_bands(const ss::vector_<chdb::sat_t>& sats,
																				 const ss::vector_<chdb::fe_polarisation_t>& pols,
																				 tune_options_t tune_options,
																				 subscription_id_t& subscription_id) {
	std::vector<task_queue_t::future_t> futures;

	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &sats, pols, &tune_options, &subscription_id]() {
		subscription_id = cb(receiver_thread).scan_bands(sats, pols, tune_options, subscription_id);
		cb(receiver_thread).scan_now(); // start initial scan
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return subscription_id;
}



/*
	main entry point
	called by code in receiver_pybind.cc
	and subscriber_pynbind.cc
 */
template <typename _mux_t>
subscription_id_t
receiver_t::subscribe_mux(const _mux_t& mux, bool blindscan, subscription_id_t subscription_id_) {

	std::vector<task_queue_t::future_t> futures;
	auto tune_options = this->get_default_tune_options(subscription_type_t::TUNE);
	tune_options.need_blind_tune = blindscan;
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t subscription_id{subscription_id_};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &mux, tune_options, &subscription_id, &subscribed_fe_key]() {
		cb(receiver_thread).abort_scan();
		std::tie(subscription_id, subscribed_fe_key)
			= cb(receiver_thread).subscribe_mux(mux, (subscription_id_t) subscription_id, tune_options);
		return 0;
	}));
	wait_for_all(futures);
	return subscription_id;
}

/*!
	for use by spectrum scan. Exclusively reserves lnb
	Reserving is only possible if no other subscriptions have currently reserved.

	Note that low_freq and high_freq should cover the requested band, but may be broader;
	in this case, the caller should make two calls, one for each band

*/
subscription_id_t
receiver_t::subscribe_lnb_spectrum(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb_,
																	 const chdb::fe_polarisation_t& pol_, int32_t low_freq,
																	 int32_t high_freq, int sat_pos, subscription_id_t subscription_id) {
	auto lnb = reread_lnb(lnb_);

	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.scan_target = scan_target_t::SCAN_FULL;
	tune_options.subscription_type = subscription_type_t::SPECTRUM_SCAN;
	tune_options.tune_mode = tune_mode_t::SPECTRUM;
	auto& band_pol = tune_options.spectrum_scan_options.band_pol;
	band_pol.pol = pol_;

	assert(band_pol.pol != chdb::fe_polarisation_t::NONE);
	auto [low_freq_, mid_freq_, high_freq_, lof_low_, lof_high_, inverted_spectrum] =
		devdb::lnb::band_frequencies(lnb, band_pol.band);
	low_freq = low_freq < 0 ? low_freq_ : low_freq;
	high_freq = high_freq < 0 ? high_freq_ : high_freq;

	if( high_freq <= low_freq  || low_freq < low_freq_ || high_freq > high_freq_) {
		user_errorf("Illegal frequency range for scan: {:d}kHz - {:d}Khz", low_freq, high_freq);
		return subscription_id_t::TUNE_FAILED;
	}
	bool has_low = (low_freq_ != mid_freq_);
	bool need_low = ( low_freq < mid_freq_) && has_low;
	bool need_high = ( high_freq >= mid_freq_);
	assert(need_high || need_low);
	band_pol.band = (need_high && need_low) ? chdb::sat_sub_band_t::NONE
		:  need_low ? chdb::sat_sub_band_t::LOW : chdb::sat_sub_band_t::HIGH;

	if (band_pol.band == chdb::sat_sub_band_t::NONE) {
		// start with lowest band
		band_pol.band = devdb::lnb::band_for_freq(lnb, low_freq);
	} else if (devdb::lnb::band_for_freq(lnb, low_freq) != band_pol.band &&
						 devdb::lnb::band_for_freq(lnb, high_freq) != band_pol.band) {
		user_errorf("start and end frequency do not coincide with band");
		return subscription_id_t::TUNE_FAILED;
	}
	tune_options.spectrum_scan_options.start_freq = low_freq;
	tune_options.spectrum_scan_options.end_freq = high_freq;

	if (sat_pos == sat_pos_none) {
		if (lnb.on_positioner) {
		} else {
			if (lnb.networks.size() > 0)
				sat_pos = lnb.networks[0].sat_pos;
		}
	}
	tune_options.spectrum_scan_options.sat_pos = sat_pos;
	//call by reference ok because of subsequent wait_for_all
	subscription_id_t ret_subscription_id;
	futures.push_back(receiver_thread.push_task([this, &lnb, &rf_path, tune_options, &ret_subscription_id,
																							 subscription_id]() {
		cb(receiver_thread).abort_scan();
		ret_subscription_id = cb(receiver_thread).subscribe_lnb(rf_path, lnb, tune_options,
																												(subscription_id_t) subscription_id);
		return 0;
	}));
	auto error = wait_for_all(futures, true /*clear_all_errors*/);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(subscription_id);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
	}
	return ret_subscription_id;
}

subscription_id_t receiver_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, retune_mode_t retune_mode,
																						subscription_id_t subscription_id) {

	{
		auto txn = devdb.rtxn();
		auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
		if (c.is_valid()) {
			auto db_lnb = c.current();
			lnb.lof_offsets = db_lnb.lof_offsets;
			txn.abort();
		}
	}
	std::vector<task_queue_t::future_t> futures;
	tune_options_t tune_options;
	tune_options.subscription_type = subscription_type_t::LNB_CONTROL;
	tune_options.tune_mode = tune_mode_t::POSITIONER_CONTROL;
	tune_options.retune_mode = retune_mode;
	subscription_id_t ret_subscription_id;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &rf_path, &lnb, &tune_options,
																							 &ret_subscription_id, subscription_id]() {
		cb(receiver_thread).abort_scan();
		ret_subscription_id = cb(receiver_thread).subscribe_lnb(rf_path, lnb, tune_options, subscription_id);
		return 0;
	}));
	auto error = wait_for_all(futures);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(subscription_id);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
		this->global_subscriber->notify_error(get_error());
	}
	return ret_subscription_id;
}

/*! Same as subscribe_lnb, but also subscribes to a mux in the same call
 */
subscription_id_t
receiver_t::subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
																	bool blindscan, const pls_search_range_t& pls_search_range,
																	retune_mode_t retune_mode, subscription_id_t subscription_id_) {
	subscription_id_t subscription_id{subscription_id_};
	devdb::fe_key_t subscribed_fe_key;

	{
		auto txn = devdb.rtxn();
		auto c = devdb::lnb_t::find_by_key(txn, lnb.k);
		if (c.is_valid()) {
			auto db_lnb = c.current();
			lnb.lof_offsets = db_lnb.lof_offsets;
			txn.abort();
		}
	}
	std::vector<task_queue_t::future_t> futures;
	auto tune_options = this->get_default_tune_options(subscription_type_t::LNB_CONTROL);
	tune_options.need_blind_tune = blindscan;
	tune_options.tune_mode = tune_options.need_blind_tune ? tune_mode_t::BLIND : tune_mode_t::NORMAL;
	tune_options.retune_mode = retune_mode;
	tune_options.allowed_rf_paths = {rf_path};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task(
											[this, &mux, tune_options, &subscription_id]() {
		cb(receiver_thread).abort_scan();
		devdb::fe_key_t subscribed_fe_key;
		std::tie(subscription_id, subscribed_fe_key)
			= cb(receiver_thread).subscribe_mux(mux, (subscription_id_t) subscription_id, tune_options);
		return 0;
	}));
	wait_for_all(futures, true /*clear all errors*/);
	return subscription_id;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_service(const chdb::service_t& service,
																															subscription_id_t subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &subscription_id, &service, &ret]() {
		cb(receiver_thread).abort_scan();
		ret = cb(receiver_thread).subscribe_service(service, (subscription_id_t) subscription_id);
		return 0;
	}));
	wait_for_all(futures);
	return ret;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_playback(const recdb::rec_t& rec, subscription_id_t subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &subscription_id, &rec, &ret]() {
		ret = cb(receiver_thread).subscribe_playback(rec, (subscription_id_t) subscription_id);
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

int receiver_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {

	auto& recmgr_thread = rec_manager.recmgr_thread;
	recmgr_thread 	//call by reference ok because of subsequent .wait
		.push_task([&recmgr_thread, &service, &epg_record]() {
			cb(recmgr_thread).toggle_recording(service, epg_record);
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

int receiver_t::update_autorec(recdb::autorec_t& autorec) {

	auto& recmgr_thread = rec_manager.recmgr_thread;
	recmgr_thread.push_task([&recmgr_thread, &autorec]() { 	//call by reference ok because of subsequent .wait
			cb(recmgr_thread).update_autorec(autorec);
			return 0;
		})
		.wait();
	return 0;
}

int receiver_t::delete_autorec(const recdb::autorec_t& autorec) {
	auto& recmgr_thread = rec_manager.recmgr_thread;

	recmgr_thread 	//call by reference ok because of subsequent .wait
		.push_task([&recmgr_thread, autorec]() {
			cb(recmgr_thread).delete_autorec(autorec);
			return 0;
		}).wait();
	return 0;
}

bool receiver_t::init() {
	if(inited)
		return inited;
	try  {
		auto r = this->options.readAccess();
		statdb.open(r->statdb.c_str());
		chdb.extra_flags = MDB_NOSYNC;
		chdb.open(r->chdb.c_str());
		devdb.extra_flags = MDB_NOSYNC;
		devdb.open(r->devdb.c_str(), false /*allow_degraded_mode*/, nullptr /*table_name*/, 2*1024u*1024u /*mapsize*/);
		epgdb.extra_flags = MDB_NOSYNC;
		epgdb.open(r->epgdb.c_str());
		recdb.open(r->recdb.c_str());
	} catch (db_upgrade_info_t& db_upgrade_info) {
		this->db_upgrade_info = db_upgrade_info;
		statdb.close();
		chdb.close();
		devdb.close();
		epgdb.close();
		recdb.close();
		return false;
	}

	{
		auto devdb_wtxn = this->devdb.wtxn();
		auto w = this->options.writeAccess();
		auto & options = *w;
		options.load_from_db(devdb_wtxn);
		devdb_wtxn.commit();
	}
	browse_history.init();
	rec_browse_history.init();
	start();
	inited = true;
	return inited;
}

receiver_t::receiver_t(const neumo_options_t* options)
	: receiver_thread(*this)
	, scam_thread(receiver_thread)
	, rec_manager(*this)
	, browse_history(chdb)
	, rec_browse_history(recdb)
{
	if(options) {
		auto w = this->options.writeAccess();
		*w = *options;
		//options will be overwritten by values in db, when calling init below
	}
	set_logconfig(options->logconfig.c_str());
	identify();

	log4cxx_store_threadname();
	// this will throw in case of error
	init();
}

void receiver_t::start() {
	receiver_thread.start_running();
	scam_thread.start_running();
}

void receiver_t::stop() {
	dtdebugf("STOP CALLED");
	receiver_thread.stop_running(true);
}

int receiver_thread_t::run() {
	/*@todo: The timer below is used to gather signal strength, cnr and ber.
		When used from a gui, it may be better to let the gui handle this asynchronously
	*/
	set_name("receiver");
	logger = Logger::getLogger("receiver"); // override default logger for this thread

	dtdebugf("RECEIVER starting");
	adaptermgr->start();
	epoll_add_fd(adaptermgr->inotfd, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET);
	now = system_clock_t::now();
	receiver.rec_manager.recmgr_thread.start_running();
	double period_sec = 2.0;
	timer_start(period_sec);

	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorf("error in poll: {:s}", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				ss::string<128> prefix;
				prefix.format("RECEIVER-CMD");
				log4cxx::NDC ndc(prefix.c_str());
				// an external request was received
				// run_tasks returns -1 if we must exit
				if (run_tasks(now) < 0) {
					// detach();
					dtdebugf("Exiting cleanly");
					return 0;
				}
			} else if (is_timer_fd(evt)) {
				receiver.update_playback_info();
				auto scanner = get_scanner();
				if(scanner) {
					auto remove_scanner = scanner->housekeeping(false);
					if (remove_scanner) {
						reset_scanner();
					}
				}
			} else if (evt->data.fd == adaptermgr->inotfd) {
				adaptermgr->run();
			}
		}
	}
	// detach();
	return 0;
}

int receiver_thread_t::cb_t::scan_now() {
	auto scanner = get_scanner();
	auto remove_scanner = scanner->housekeeping(true);
	if (remove_scanner) {
		reset_scanner();
	}

	return 0;
}

void receiver_thread_t::cb_t::send_signal_info_to_scanner(
	const signal_info_t& signal_info,
	const ss::vector_<subscription_id_t>& subscription_ids) {

	/*
		send a signal_info message to a specific wxpython window (positioner_dialog)
		which will then receive a EVT_COMMAND_ENTER
		signal. Each window is associated with a subscription. The message is passed on if the subscription's
		active_adapter uses the lnb which is stored in the signal_info message
	 */
	{
		auto mss = receiver.subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms || !ms->is_scanning())
				continue;

			if(subscription_ids.size() > 0) {
				auto scanner = this->get_scanner();
				/*
					Inform the scanner of all its currently active child subscriptions.
					The scanner will then decide for which of these subscriptions it will show signal_info
					and will ignore unwanted signal_info
				*/
				if(scanner.get())
					scanner->on_signal_info(*ms, subscription_ids, signal_info);
			}
		}
	}
}

void receiver_t::on_signal_info(const signal_info_t& signal_info,
																const ss::vector_<subscription_id_t>& subscription_ids) {
	/*
		provide all mpv's with updated signal information for the OSD
	 */
	{
		auto mpv_map = this->active_mpvs.readAccess();
		for (auto [mpv_, mpv_shared_ptr] : *mpv_map) {
			auto* mpv = mpv_shared_ptr.get();
			if (!mpv)
				continue;
			mpv->notify(signal_info);
		}
	}


	/*
		send a signal_info message to a specific wxpython window (positioner_dialog)
		which will then receive a EVT_COMMAND_ENTER
		signal. Each window is associated with a subscription. The message is passed on if the subscription's
		active_adapter uses the lnb which is stored in the signal_info message
	 */
	bool has_scanning_subscribers{false};
	{
		auto mss = this->subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms) //scanning subscribers are handled by scanner
				continue;
			if(ms->is_scanning()) {
				has_scanning_subscribers = true;
				continue;
			}
			/*
				a subscriber can either directly handle the received signal info via
				ms->notify_signal_info, or indirectly via scanner->notify_signal_info(*ms...)
			 */

			/*Notify positioner dialog screens and spectrum_dialog screens which
				are busy tuning a single mux.

				Note that during a blindscan, spectrum_dialog
				will not react to the following call, as they have no active subscriber_t, i.e.,
				one associated with a specific adapter or lnb. A "blindscan all" spectrum_dialog will
				ignore the following call
			*/
			if(subscription_ids.contains(ms->get_subscription_id()))
				ms->notify_signal_info(signal_info);
		}
	}
	auto& receiver_thread = this->receiver_thread;
	if(has_scanning_subscribers) {
		//capturing by value is essential
		receiver_thread.push_task([&receiver_thread, signal_info, subscription_ids]() {
			cb(receiver_thread).send_signal_info_to_scanner(signal_info, subscription_ids);
			return 0;
		});
	}
}

//called from tuner thread when SDT ACTUAL has completed to report services to gui
void receiver_thread_t::cb_t::send_sdt_actual_to_scanner(const sdt_data_t& sdt_data,
																												 const ss::vector_<subscription_id_t>& subscription_ids) {
	/*
		send an sdt_data message to a specific wxpython window (positioner_dialog)
		which will then receive a EVT_COMMAND_ENTER
		signal. Each window is associated with a subscription. The message is passed on if the subscription's
		active_adapter uses the lnb which is stored in the sdt_data message
	 */
	auto mss = receiver.subscribers.readAccess();
	for (auto [subsptr, ms_shared_ptr] : *mss) {
		auto* ms = ms_shared_ptr.get();
		if (!ms || ! ms->is_scanning())
			continue;

		if(subscription_ids.size() > 0) {
			auto scanner = this->get_scanner();

			/*
				Inform the scanner of all its currently active child subscriptions.
				The scanner will then decide for which of these subscriptions it will show signal_info
				and will ignore unwanted signal_info
			*/
			if(scanner.get())
				scanner->on_sdt_actual(*ms, subscription_ids, sdt_data);
		}
	}
}

//called from tuner thread when SDT ACTUAL has completed to report services to gui
void receiver_t::on_sdt_actual(const sdt_data_t& sdt_data,
																					const ss::vector_<subscription_id_t>& subscription_ids) {
	/*
		send an sdt_data message to a specific wxpython window (positioner_dialog)
		which will then receive a EVT_COMMAND_ENTER
		signal. Each window is associated with a subscription. The message is passed on if the subscription's
		active_adapter uses the lnb which is stored in the sdt_data message
	 */
	bool has_scanning_subscribers{false};
	if(sdt_data.mux_key.t2mi_pid<0) { //only notify for physical SDT

		auto mss = this->subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			if(ms->is_scanning()) {
				has_scanning_subscribers = true;
				continue;
			}
			if(subscription_ids.contains(ms->get_subscription_id()))
				ms->notify_sdt_actual(sdt_data);
		}
	}
	auto& receiver_thread = this->receiver_thread;
	if(has_scanning_subscribers) {
		//capturing by value is essential
		receiver_thread.push_task([&receiver_thread, sdt_data, subscription_ids]() {
			cb(receiver_thread).send_sdt_actual_to_scanner(sdt_data, subscription_ids);
			return 0;
		});
	}
}

/*called from scanner loop to inform gui that a specific mux has finished scanning
 */
void receiver_t::notify_scan_mux_end(subscription_id_t scan_subscription_id, const scan_mux_end_report_t& report) {
	{
		auto mss = this->subscribers.readAccess();
		auto [it, found] = find_in_map_if(
			*mss, [scan_subscription_id](auto&x) {
				auto& sub= x.second;
				return sub->get_subscription_id() == scan_subscription_id;
			});
		if(!found)
			return;
		auto& ms = it->second;
		if (!ms)
			return;
		//Notify spectrum dialog and muxlist
		ms->notify_scan_mux_end(report);
	}
}

/*called from scanner loop to inform gui that a specific sat band has finished spectrum scanning
 */
void receiver_t::notify_spectrum_scan_band_end(subscription_id_t scan_subscription_id, const statdb::spectrum_t& spectrum) {
	{
		auto mss = this->subscribers.readAccess();
		auto [it, found] = find_in_map_if(
			*mss, [scan_subscription_id](auto&x) {
				auto& sub= x.second;
				return sub->get_subscription_id() == scan_subscription_id;
			});
		if(!found)
			return;
		auto& ms = it->second;
		if (!ms)
			return;
		//Notify spectrum dialog and muxlist
		ms->notify_spectrum_scan_band_end(spectrum);
	}
}

//called from scanner loop to inform about scan statistics at start (for display on mux screen)
void receiver_t::notify_scan_progress(subscription_id_t scan_subscription_id,
																			const scan_stats_t& stats) {
	{
		auto mss = this->subscribers.readAccess();
		auto [it, found] = find_in_map_if(
			*mss, [scan_subscription_id](auto&x) {
				auto& sub= x.second;
				return sub->get_subscription_id() == scan_subscription_id;
			});
		if(!found)
			return;
		auto& ms = it->second;
		if (!ms)
			return;
		//Notify spectrum dialog and muxlist
		ms->notify_scan_progress(stats);
	}
}


void receiver_thread_t::cb_t::send_spectrum_to_scanner(const devdb::fe_t& finished_fe,
																											 const spectrum_scan_t& spectrum_scan,
																											 const chdb::scan_id_t& scan_id,
																											 const ss::vector_<subscription_id_t>& subscription_ids) {
	auto scanner = this->get_scanner();
	if (!scanner.get() || subscription_ids.size()== 0) {
		return;
	}
	auto remove_scanner = scanner->on_spectrum_scan_band_end(finished_fe, spectrum_scan,
																													 scan_id, subscription_ids);
	if (remove_scanner) {
		reset_scanner();
		}
}

//called from fe_monitor code
void receiver_t::on_spectrum_scan_end(const devdb::fe_t& finished_fe, const spectrum_scan_t& spectrum_scan,
																			const ss::vector_<subscription_id_t>& subscription_ids) {
	auto scan_id = deactivate_spectrum_scan(spectrum_scan);
	bool has_scanning_subscribers{false};
	subscription_id_t subscription_id{-1};
	{
		auto mss = subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			if(ms->is_scanning()) {
				assert((int)subscription_id < 0); /*only one scan_t can scan a band;
																						it is possible that a spectrum_dialog
																						also wants this spectrum
																					*/
				subscription_id = ms->get_subscription_id();
				has_scanning_subscribers = true;
				continue;
			}
			if (!subscription_ids.contains(ms->get_subscription_id()))
				continue;
			/*
				This is a spectrum_dialog window. We can directly send it a copy of the spectrum
			*/
			if(spectrum_scan.spectrum &&  subscription_ids.contains(ms->get_subscription_id()))
				ms->notify_spectrum_scan_band_end(*spectrum_scan.spectrum);
		}
	}
	assert(has_scanning_subscribers == scanner_t::is_our_scan(scan_id));
	if(has_scanning_subscribers) {
		auto& receiver_thread = this->receiver_thread;
		//capturing by value is essential
		receiver_thread.push_task([&receiver_thread, finished_fe, spectrum_scan, scan_id, subscription_ids]() {
			cb(receiver_thread).send_spectrum_to_scanner(finished_fe, spectrum_scan, scan_id, subscription_ids);
			return 0;
		});
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
	lnbs: if non-empty, only these lnbs are allowed during scanning (set to nullptr for non-dvbs)
	muxes: if non-empty, it provides a list of muxes to scan (should be non empty)
	bool scan_newly_found_muxes => if new muxes are found from si information, also scan them
*/
template <typename mux_t>
subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<mux_t>& muxes,
															const tune_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id) {
	auto scanner = get_scanner();
	subscribe_ret_t sret{subscription_id, false /*failed*/};
	if (!scanner) {
		scanner = std::make_unique<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}

	scanner->add_muxes(muxes, tune_options, sret.subscription_id);
	return sret.subscription_id;
}

/*!
	lnbs: if non-empty, only these lnbs are allowed during scanning (set to nullptr for non-dvbs)
	muxes: if non-empty, it provides a list of muxes to scan (should be non empty)
	bool scan_newly_found_muxes => if new muxes are found from si information, also scan them
*/
subscription_id_t
receiver_thread_t::scan_bands(std::vector<task_queue_t::future_t>& futures,
															const ss::vector_<chdb::sat_t>& sats,
															const ss::vector_<chdb::fe_polarisation_t>& pols,
															const tune_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id) {
	auto scanner = get_scanner();
	subscribe_ret_t sret{subscription_id, false /*failed*/};
	if (!scanner) {
		scanner = std::make_unique<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}
	scanner->add_bands(sats, pols, tune_options, sret.subscription_id);
	return sret.subscription_id;
}

/*!
	called when complete channel scan needs to be aborted
*/
bool receiver_thread_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
																				 db_txn& devdb_wtxn, subscription_id_t subscription_id) {
	auto scanner = get_scanner();
	if(scanner.get()) {
		return scanner->unsubscribe_scan(futures, devdb_wtxn, subscription_id);
	}
	return false;
}


/*!
	lnbs: if non-empty, only these lnbs are allowed during scannin
	muxes: if non-empty, it provides a list of muxes to scan (should be non empty)
	bool scan_newly_found_muxes => if new muxes are found from si information, also scan them
*/
subscription_id_t
receiver_thread_t::scan_spectral_peaks(std::vector<task_queue_t::future_t>& futures,
																			 ss::vector_<chdb::spectral_peak_t>& peaks,
																			 const statdb::spectrum_key_t& spectrum_key,
																			 bool scan_found_muxes, int max_num_subscriptions,
																			 subscription_id_t subscription_id, subscriber_t* subscriber_ptr) {
	auto scanner = get_scanner();
	subscribe_ret_t sret{subscription_id, false /*failed*/};
	if (!scanner){
		scanner = std::make_shared<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}
	scanner->add_spectral_peaks(spectrum_key, peaks, sret.subscription_id);
	bool remove_scanner = scanner->housekeeping(true); // start initial scan
	if(remove_scanner) {
		reset_scanner();
		return subscription_id_t::TUNE_FAILED;
	}
	return sret.subscription_id;
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
	auto scanner = get_scanner();
	return scanner.get() ? scanner->scan_start_time : -1;
}



void receiver_thread_t::unsubscribe(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
																		subscription_id_t subscription_id) {
	dtdebugf("calling unsubscribe_all");
	unsubscribe_all(futures, devdb_wtxn, subscription_id);
	/*wait_for_futures is needed because tuners/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/tuner threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/
	dtdebugf("calling unsubscribe_all -done");
}


/*
	called by
      receiver_thread_t::cb_t::subscribe_service to stop any playback or recording in progress
			user initated unsubscribe
			scan.cc when scan mux finishes
 */
void receiver_thread_t::cb_t::unsubscribe(subscription_id_t subscription_id) {
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();
	this->receiver_thread_t::unsubscribe(futures, devdb_wtxn, subscription_id);
	devdb_wtxn.commit();
	/*wait_for_futures is needed because tuners/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/tuner threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/
	bool error = wait_for_all(futures);
	dtdebugf("Waiting for all futures done");
	if (error) {
		dterrorf("Unhandled error in unsubscribe");
	}
}

/*
	called by receiver_pybind.cc and python MuxScanStop
	which handles the subscription created by python  self.receiver.scan_muxes
 */
subscription_id_t receiver_t::unsubscribe(subscription_id_t subscription_id) {
	receiver_thread.push_task([this, subscription_id]() {
		cb(receiver_thread).abort_scan();
		cb(receiver_thread).unsubscribe((subscription_id_t) subscription_id);
		return 0;
	});
	return subscription_id_t::NONE;
}

std::tuple<std::string, int> receiver_thread_t::get_api_type() const {
	return this->adaptermgr->get_api_type();
}

std::tuple<std::string, int> receiver_t::get_api_type() const {
	return receiver_thread.get_api_type();
}

void receiver_thread_t::cb_t::renumber_card(int old_number, int new_number) {
	adaptermgr->renumber_card(old_number, new_number);
}

void receiver_t::renumber_card(int old_number, int new_number) {
	receiver_thread.push_task([this, old_number, new_number]() {
		cb(receiver_thread).renumber_card(old_number, new_number);
		return 0;
	}).wait();
}


/*
	set a new usals_pos, for tuning to sat at sat_pos (which may be left unspecified as sat_pos_none)
 */
int receiver_thread_t::cb_t::update_usals_pos(const devdb::lnb_t& lnb) {
	auto loc = receiver.get_usals_location();

	auto devdb_wtxn = receiver.devdb.wtxn();
	int ret = devdb::dish::update_usals_pos(devdb_wtxn, lnb, lnb.usals_pos, loc, sat_pos_none);
	devdb_wtxn.commit();
	return ret;
}

int receiver_thread_t::cb_t::positioner_cmd(subscription_id_t subscription_id, devdb::positioner_cmd_t cmd,
																				 int par) {
	auto aa = receiver.find_active_adapter(subscription_id);
	int ret{-1};
	if(aa) {
		aa->tuner_thread.push_task([subscription_id, &aa, cmd, par, &ret]() {
			ret = cb(aa->tuner_thread).positioner_cmd(subscription_id, cmd, par);
			return 0;
		}).wait();
	}
	return ret;
}

tune_options_t receiver_t::get_default_tune_options(subscription_type_t subscription_type) const
{
	tune_options_t ret;
	ret.subscription_type = subscription_type;
	bool for_scan = false;
	bool for_lnb_control = false;
	switch(subscription_type) {
	case subscription_type_t::TUNE:
		ret.constellation_options.num_samples = 0;
		ret.tune_mode = tune_mode_t::NORMAL;
		ret.scan_target =  scan_target_t::SCAN_FULL_AND_EPG;
		break;
	case subscription_type_t::SPECTRUM_SCAN:
		ret.tune_mode = tune_mode_t::SPECTRUM;
		ret.scan_target =  scan_target_t::SCAN_FULL;
		ret.retune_mode = retune_mode_t::NEVER;
		ret.constellation_options.num_samples = 0;
		ret.need_spectrum = true;
		ret.spectrum_scan_options.recompute_peaks = true;
		for_scan=true;
		break;
	case subscription_type_t::MUX_SCAN: //for scan
		ret.tune_mode = tune_mode_t::NORMAL;
		ret.scan_target =  scan_target_t::SCAN_FULL;
		ret.retune_mode = retune_mode_t::NEVER;
		ret.constellation_options.num_samples = 0;
		for_scan=true;
		break;
	case subscription_type_t::LNB_CONTROL:
		ret.scan_target =  scan_target_t::SCAN_MINIMAL;
		ret.tune_mode = tune_mode_t::POSITIONER_CONTROL;
		ret.constellation_options.num_samples = 16*1024;
		ret.may_control_dish = true;
		for_lnb_control = true;
		break;
	default:
		assert(0);
		break;
	}
	auto r = options.readAccess();
	ret.resource_reuse_bonus = r->resource_reuse_bonus;
	ret.dish_move_penalty = r->dish_move_penalty;
	ret.usals_location = r->usals_location;
	ret.need_blind_tune =  for_scan ? r->scan_use_blind_tune: r->tune_use_blind_tune;
	ret.may_move_dish = for_lnb_control ? true : (for_scan ? r->scan_may_move_dish: r->tune_may_move_dish);
	ret.may_control_lnb = for_lnb_control;
	ret.max_scan_duration = 	r->max_scan_duration;

	return ret;
}

static chdb::scan_id_t activate_spectrum_scan_(chdb::chdb_t&chdb, int16_t sat_pos,
																	 chdb::fe_polarisation_t pol,
																	 int start_freq, int end_freq,
																	 bool spectrum_obtained) {
	using namespace chdb;
	scan_id_t scan_id;
	auto [sat_band, sat_sub_band] = sat_band_for_freq(start_freq);
#ifndef NDEBUG
		auto [sat_band1, sat_sub_band1] = sat_band_for_freq(end_freq-1);
		assert(sat_band1 == sat_band);
#endif
	auto chdb_rtxn = chdb.rtxn();
	auto c = chdb::sat_t::find_by_key(chdb_rtxn, sat_pos, sat_band, find_type_t::find_eq);
	assert(c.is_valid());
	auto sat = c.current();
	chdb_rtxn.abort();
	for(auto & band_scan : sat.band_scans) {
		if(!scanner_t::is_our_scan(band_scan.scan_id))
			continue;
		if (band_scan.pol != pol)
			continue;
		auto [l, h] =sat_band_freq_bounds(band_scan.sat_band, band_scan.sat_sub_band);
		if(start_freq > l)
				l = start_freq;
		if(end_freq < h)
				h = end_freq;
		if(l>=h)
			continue;

		assert((int)scan_id.subscription_id < 0); //there can only be one scan per band
		scan_id = band_scan.scan_id;
		assert(scan_id.subscription_id >= 0);
		//we have found a matching band
		if(spectrum_obtained != (band_scan.scan_status == scan_status_t::ACTIVE)) {
			//some other subscription is already scanning
		} else {

			if(band_scan.scan_status == scan_status_t::PENDING ||
				 band_scan.scan_status == scan_status_t::RETRY)  {
				assert(!spectrum_obtained);
				dtdebugf("SET ACTIVE {}", band_scan);
				band_scan.scan_status = scan_status_t::ACTIVE;
				assert (chdb::scan_in_progress(band_scan.scan_id));
			} else if (band_scan.scan_status == scan_status_t::ACTIVE) {
				assert(spectrum_obtained);
								dtdebugf("SET ACTIVE {}", band_scan);
				if(spectrum_obtained) {
					band_scan.scan_status = scan_status_t::IDLE;
					band_scan.scan_id = {};
					band_scan.scan_time = system_clock_t::to_time_t(now);
				}
			}
			sat.mtime = system_clock_t::to_time_t(now);
			auto chdb_wtxn = chdb.wtxn();
			put_record(chdb_wtxn, sat);
			chdb_wtxn.commit();
			dtdebugf("committed write");

		}
	}
	return scan_id;
}

void receiver_t::activate_spectrum_scan(const spectrum_scan_options_t& spectrum_scan_options) {
	auto sat_pos = spectrum_scan_options.sat_pos;
	auto pol = spectrum_scan_options.band_pol.pol;
	auto start_freq = spectrum_scan_options.start_freq;
	auto end_freq = spectrum_scan_options.end_freq;
	activate_spectrum_scan_(chdb, sat_pos, pol, start_freq, end_freq, false/*spectrum_obtained*/);
}

chdb::scan_id_t receiver_t::deactivate_spectrum_scan(const spectrum_scan_t&spectrum_scan) {
	using namespace chdb;

	auto sat_pos = spectrum_scan.sat_pos;
	auto pol = spectrum_scan.band_pol.pol;
	auto start_freq = spectrum_scan.start_freq;
	auto end_freq = spectrum_scan.end_freq;
	return activate_spectrum_scan_(chdb, sat_pos, pol, start_freq, end_freq, true/*spectrum_obtained*/);
}


thread_local thread_group_t thread_group{thread_group_t::unknown};

/*
	subscription_id = -1 is returned from functions when resource allocation fails
	If an error occurs after resource allocation, subscription_id should not be set to -1
	(unless after releasing resources)

 */

//template instantiations

template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbc_mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbt_mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);

template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbs_mux_t& mux,
	subscription_id_t subscription_id, tune_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template subscription_id_t
receiver_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes,
																				 const tune_options_t& tune_options,
																				 subscription_id_t& subscription_id);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																				 const tune_options_t& tune_options,
																				 subscription_id_t& subscription_id);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																				 const tune_options_t& tune_options,
																				 subscription_id_t& subscription_id);


template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux, bool blindscan,
																						subscription_id_t subscription_id);

template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, bool blindscan,
																						subscription_id_t subscription_id);

template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, bool blindscan,
																						subscription_id_t subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes,
																											const tune_options_t& tune_options,
																											subscription_id_t& subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																											const tune_options_t& tune_options,
																											subscription_id_t& subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																											const tune_options_t& tune_options,
																											subscription_id_t& subscription_id);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbs_mux_t>& muxes,
															const tune_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbc_mux_t>& muxes,
															const tune_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);
template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbt_mux_t>& muxes,
															const tune_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);
