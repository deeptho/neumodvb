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
		if(!f.valid()) {
			dterrorf("Skipping future with invalid state"); //can happen when calling stop_running with a "wait" parameter
		}
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
void receiver_thread_t::unsubscribe_playback_only(std::vector<task_queue_t::future_t>& futures, ssptr_t ssptr) {
	if (!ssptr)
		return;
	assert(ssptr);
	assert(ssptr);
	ssptr->remove_active_playback();
}


/*
	called from:
    unsubscribe_mux_and_service_only
		receiver_thread_t::subscribe_lnb to release no longer needed current active_adapter
 */
void receiver_thread_t::unsubscribe_mux_and_service_only(std::vector<task_queue_t::future_t>& futures,
																						 db_txn& devdb_wtxn, ssptr_t ssptr) {
	assert(ssptr);
	dtdebugf("Unsubscribe ssptr={}", ssptr);
	int stream_id = ssptr->get_stream_id();
	if(stream_id>=0) {
		auto c = devdb::stream_t::find_by_key(devdb_wtxn, stream_id);
		if(!c.is_valid()) {
			dterrorf("unexpected: invalid");
		} else {
			auto db_stream = c.current();
			if(db_stream.stream_state == devdb::stream_state_t::ON)
				db_stream.stream_state = devdb::stream_state_t::OFF;
			ssptr->set_stream_id(-1);
			db_stream.streamer_pid = -1;
			db_stream.owner = -1;
			db_stream.subscription_id = -1;
			db_stream.mtime = system_clock_t::to_time_t(now);
			put_record(devdb_wtxn, db_stream);
		}
	}

	auto subscription_id = ssptr->get_subscription_id();
	// release subscription's service on this mux, if any
	auto updated_dbfe = devdb::fe::unsubscribe(devdb_wtxn, subscription_id);
	if(!updated_dbfe) {
		return; //can happen when unsubscribing a scan
	}
	dtdebugf("release_active_adapter ssptr={} use_count={:d}", ssptr,
					 updated_dbfe ? updated_dbfe->sub.subs.size() : 0);
	assert(updated_dbfe);
	bool is_streaming = ssptr->is_streaming();
	release_active_adapter(futures, subscription_id, *updated_dbfe, is_streaming);
	ssptr->clear_subscription_id();
}

void receiver_thread_t::remove_stream(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id) {
	dtdebugf("remove stream subscription_id={}", (int)subscription_id);
	auto w = this->active_adapters.writeAccess();
	auto& m = *w;
	auto [it, found] = find_in_map(m, subscription_id);
	assert(found);
	auto &aa = *it->second;
	auto& tuner_thread = aa.tuner_thread;
	futures.push_back(tuner_thread.push_task([&tuner_thread, subscription_id]() {
		cb(tuner_thread).remove_stream(subscription_id);
		return 0;
	}));
}

void receiver_thread_t::release_active_adapter(std::vector<task_queue_t::future_t>& futures,
																							 subscription_id_t subscription_id,
																							 const devdb::fe_t& updated_dbfe, bool is_streaming) {
	dtdebugf("release_active_adapter subscription_id={}", (int)subscription_id);
	assert((int)subscription_id >= 0);
	// release subscription's service on this mux, if any

	/* The active_adapter is no longer in use; so ask tuner thread to release it.
	 */
	{
		auto w = this->active_adapters.writeAccess();
		auto& m = *w;
		auto [it, found] = find_in_map(m, subscription_id);
		assert(found);
		auto &aa = *it->second;
		auto& tuner_thread = aa.tuner_thread;
		if(is_streaming) {
			futures.push_back(tuner_thread.push_task([&tuner_thread, subscription_id]() {
				cb(tuner_thread).remove_stream(subscription_id);
				return 0;
			}));
		}
		if(updated_dbfe.sub.subs.size() ==0) {
			//ask tuner thread to exit, but do not wait
			dtdebugf("Pushing tuner_thread.stop_running");
			tuner_thread.update_dbfe(updated_dbfe);
			tuner_thread.stop_running(true/*wait*/);
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
																				db_txn& devdb_wtxn, ssptr_t ssptr) {
	if (!ssptr)
		return;
	auto scanner = get_scanner();
	if (scanner.get()) {
		if(unsubscribe_scan(futures, devdb_wtxn, ssptr)) {
			 dtdebugf("unsubscribed scan");
		}
	}
	unsubscribe_playback_only(futures, ssptr);
	dtdebugf("calling unsubscribe_mux_and_service_only");
	unsubscribe_mux_and_service_only(futures, devdb_wtxn, ssptr);
	if((int)ssptr->get_subscription_id() >=0) {
		dtdebugf("Clearing subscription_id {}", ssptr);
		ssptr->clear_subscription_id();
	}

	dtdebugf("calling unsubscribe_mux_and_service_only -done");
}

/*
	returns either
	a stream_t (service or mux)
	or a playback_mpm_t (service)
	if called with pservice==nullptr then only a steam_t can be returned
 */
std::tuple<std::optional<devdb::stream_t>, std::unique_ptr<playback_mpm_t>>
 receiver_thread_t::subscribe_stream(
	 const chdb::any_mux_t& mux, const chdb::service_t* pservice, ssptr_t ssptr, const devdb::stream_t* stream) {
	assert(ssptr);
	bool for_streaming = !!stream;
	assert(for_streaming || pservice);
	auto subscription_id = ssptr->get_subscription_id();
	std::vector<task_queue_t::future_t> futures;
	auto tune_options = receiver.get_default_subscription_options(devdb::subscription_type_t::TUNE);
	tune_options.scan_target = for_streaming ? devdb::scan_target_t::SCAN_MINIMAL :
		devdb::scan_target_t::SCAN_FULL_AND_EPG;
	assert(!tune_options.need_spectrum);
	subscribe_ret_t sret;
	auto devdb_wtxn = receiver.devdb.wtxn();
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	pservice,
																	false /*do_not_unsubscribe_on_failure*/);
	devdb_wtxn.commit();
	ssptr->set_subscription_id(sret.subscription_id);
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			bool is_streaming = ssptr->is_streaming();
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe, is_streaming);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		if(pservice)
			user_errorf("Service reservation failed: {}", *pservice);
		else
			user_errorf("Mux reservation failed: {}", mux);
		return {};
	}
	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		bool is_streaming = ssptr->is_streaming();
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe, is_streaming);
	}

	auto* active_adapter_p = find_or_create_active_adapter(futures, sret);
	assert(active_adapter_p);
	auto& aa = *active_adapter_p;
	tune_options.tune_pars = sret.tune_pars;

	std::unique_ptr<playback_mpm_t> playback_mpm_ptr;
	if(!for_streaming) {
		std::unique_ptr<playback_mpm_t> playback_mpm_ptr;
		futures.push_back(aa.tuner_thread.push_task([&playback_mpm_ptr, &aa, &mux, pservice, &sret,
																								 &tune_options]() {
			playback_mpm_ptr = cb(aa.tuner_thread).subscribe_service_for_viewing(sret, mux, *pservice, tune_options);
			return 0;
		}));
		wait_for_all(futures); //essential
		return {std::optional<devdb::stream_t>{}, std::move(playback_mpm_ptr)};
	} else {
		devdb::stream_t streamret;
		futures.push_back(aa.tuner_thread.push_task([&aa, &mux, &sret, stream, &streamret, &tune_options]() {
			streamret = cb(aa.tuner_thread).add_stream(sret, mux, *stream, tune_options);
			return 0;
		}));
		wait_for_all(futures); //essential
		auto devdb_wtxn = receiver.devdb.wtxn();
		put_record(devdb_wtxn, streamret);
		devdb_wtxn.commit();
		return {streamret,  nullptr};
	}
  assert(false);
	return {};
}

subscription_id_t receiver_thread_t::subscribe_service_for_recording(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::any_mux_t& mux,
	recdb::rec_t& rec, ssptr_t ssptr) {
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	assert((int)subscription_id == rec.subscription_id);
	subscription_options_t tune_options(devdb::scan_target_t::SCAN_FULL_AND_EPG);
	assert(!tune_options.need_spectrum);
	subscribe_ret_t sret;
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	&rec.service,
																	false /*do_not_unsubscribe_on_failure*/);
	ssptr->set_subscription_id(sret.subscription_id);
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			bool is_streaming = ssptr->is_streaming();
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe, is_streaming);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Service reservation failed: {}", rec.service);
		return subscription_id_t::NONE;
	}
	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		bool is_streaming = ssptr->is_streaming();
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe, is_streaming);
	}
	auto* active_adapter_p = find_or_create_active_adapter(futures, sret);
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
			unsubscribe_all(futures, devdb_wtxn, ms_shared_ptr);
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
																													 ssptr_t ssptr)
{
	auto scanner = get_scanner();
	if (!scanner.get()) {
		return;
	}
	dtdebugf("Calling scanner->on_scan_mux_end: adapter={}  mux={} ssptr={}",
					finished_fe.adapter_no, finished_mux, ssptr);
	/*call scanner to start scanning new muxes and to prepare a scan report
		which will be asynchronously returned to receiver_thread by calling notify_scan_mux_end,
		which will pass the message to the GUI
	*/
	assert(ssptr);
	auto remove_scanner = scanner->on_scan_mux_end(finished_fe, finished_mux, scan_id, ssptr);
	if (remove_scanner) {
		reset_scanner();
	}
}

ssptr_t receiver_t::get_ssptr(subscription_id_t subscription_id) {
	if((int)subscription_id <0)
		return nullptr;
	auto [it, found] = find_in_safe_map_if(this->subscribers, [subscription_id](const auto& x) {
		auto& [key_, ssptr] = x;
			return ssptr->get_subscription_id() == subscription_id;
	});
	return found ? it->second : nullptr;
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
			auto ssptr = receiver_thread.receiver.get_ssptr(subscription_id);
			if(ssptr)
				cb(receiver_thread).send_scan_mux_end_to_scanner(finished_fe, finished_mux,
																												 scan_id, ssptr);
			return 0;
		});
	}
}


subscription_id_t
receiver_thread_t::subscribe_spectrum(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::sat_t& sat,
	const chdb::band_scan_t& band_scan,
	ssptr_t ssptr, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id,
	bool do_not_unsubscribe_on_failure) {
	subscribe_ret_t sret;
	assert(tune_options.need_spectrum);
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();

	sret = devdb::fe::subscribe_sat_band(devdb_wtxn, subscription_id,
																			 tune_options,
																			 sat, band_scan, do_not_unsubscribe_on_failure);

	if(sret.failed) {
		if(!do_not_unsubscribe_on_failure) {
			auto updated_old_dbfe = sret.aa.updated_old_dbfe;
			if(updated_old_dbfe) {
				dtdebugf("Subscription failed calling release_active_adapter");
				bool is_streaming = ssptr->is_streaming();
				release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe, is_streaming);
			} else {
				dtdebugf("Subscription failed: updated_old_dbfe = NONE");
			}
			ssptr->set_subscription_id(sret.subscription_id);
			user_errorf("Sat band reservation failed: {}:{}", sat, band_scan);
		}
		return subscription_id_t::RESERVATION_FAILED;
	} else
		ssptr->set_subscription_id(sret.subscription_id);
	tune_options.tune_pars = sret.tune_pars;
	dtdebugf("lnb activate subscription_id={:d}", (int) sret.subscription_id);

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		bool is_streaming = ssptr->is_streaming();
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe, is_streaming);
	}

	auto* activate_adapter_p = find_or_create_active_adapter(futures, sret);
	assert(activate_adapter_p);
	auto& aa = *activate_adapter_p;

	futures.push_back(aa.tuner_thread.push_task([&aa, subscription_id, sret, tune_options]() {
		auto ret = cb(aa.tuner_thread).lnb_spectrum_acquistion(subscription_id, sret, tune_options);
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
	ssptr_t ssptr, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id,
	bool do_not_unsubscribe_on_failure) {
	subscribe_ret_t sret;
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	assert(!tune_options.need_spectrum);
	sret = devdb::fe::subscribe_mux(devdb_wtxn, subscription_id,
																	tune_options,
																	mux,
																	(const chdb::service_t*) nullptr /*service*/,
																	do_not_unsubscribe_on_failure);

	if(sret.failed) {
		if(!do_not_unsubscribe_on_failure) {
			auto updated_old_dbfe = sret.aa.updated_old_dbfe;
			if(updated_old_dbfe) {
				dtdebugf("Subscription failed calling release_active_adapter");
				bool is_streaming = ssptr->is_streaming();
				release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe, is_streaming);
			} else {
				dtdebugf("Subscription failed: updated_old_dbfe = NONE");
			}
			ssptr->set_subscription_id(sret.subscription_id);
			user_errorf("Mux reservation failed: {}", mux);
		}
		return subscription_id_t::RESERVATION_FAILED;
	} else
		ssptr->set_subscription_id(sret.subscription_id);

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		bool is_streaming = ssptr->is_streaming();
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe, is_streaming);
	}

	auto* active_adapter_p = find_or_create_active_adapter(futures, sret);
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
																											const subscription_options_t& tune_options,
																											ssptr_t ssptr)
{
	auto s = fmt::format("SCAN[{}] {} muxes", ssptr, (int) muxes.size());
	log4cxx::NDC ndc(s);
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	std::vector<task_queue_t::future_t> futures;
	auto scanner = get_scanner();
	if ((!scanner.get() && (int)subscription_id >= 0)) {
		unsubscribe_playback_only(futures, ssptr);
		auto devdb_wtxn = receiver.devdb.wtxn();
		unsubscribe_mux_and_service_only(futures, devdb_wtxn, ssptr);
		devdb_wtxn.commit();
		bool error = wait_for_all(futures);
		if (error) {
			dterrorf("Unhandled error in scan_muxes"); // This will ensure that tuning is retried later
		}
	}
	int max_num_subscriptions = 100;
	auto ret = this->receiver_thread_t::scan_muxes(futures, muxes, tune_options,
																								 max_num_subscriptions, ssptr);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return ret;
}

subscription_id_t receiver_thread_t::cb_t::scan_spectral_peaks(
	const devdb::rf_path_t& rf_path, 	ss::vector_<chdb::spectral_peak_t>& peaks,
	const statdb::spectrum_key_t& spectrum_key, ssptr_t ssptr)
{
	auto s = fmt::format("SCAN[{}] {} peaks", ssptr, (int) peaks.size());
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;
	assert(ssptr);
	unsubscribe_playback_only(futures, ssptr);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}
	bool scan_newly_found_muxes = true;
	int max_num_subscriptions = 100;
	auto ret = this->receiver_thread_t::scan_spectral_peaks(futures, rf_path, peaks, spectrum_key,
																													scan_newly_found_muxes,
																													max_num_subscriptions, ssptr);
	error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return ret;
}

subscription_id_t receiver_thread_t::cb_t::scan_bands(
	const ss::vector_<chdb::sat_t>& sats,
	const ss::vector_<chdb::fe_polarisation_t>& pols,
	subscription_options_t tune_options, ssptr_t ssptr)
{
	auto s = fmt::format("SCAN[{}] {} sats", ssptr, (int) sats.size());
	log4cxx::NDC ndc(s);
	assert(ssptr);
	std::vector<task_queue_t::future_t> futures;
	auto scanner = get_scanner();

	if (!scanner.get()) {
		unsubscribe_playback_only(futures, ssptr);
		auto devdb_wtxn = receiver.devdb.wtxn();
		unsubscribe_mux_and_service_only(futures, devdb_wtxn, ssptr);
		devdb_wtxn.commit();
		bool error = wait_for_all(futures);
		if (error) {
			dterrorf("Unhandled error in scan_bands"); // This will ensure that tuning is retried later
		}
	}
	int max_num_subscriptions = 100;

	assert(tune_options.need_spectrum);
	auto ret =  this->receiver_thread_t::scan_bands(futures, sats, pols, tune_options,
																												max_num_subscriptions, ssptr);

	auto error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in scan_mux");
	}
	return ret;
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
receiver_thread_t::cb_t::subscribe_mux(const _mux_t& mux, ssptr_t ssptr,
																			 subscription_options_t tune_options, const chdb::scan_id_t& scan_id) {
	devdb::fe_key_t subscribed_fe_key;
	auto s = fmt::format("SUB[{}] {}",  ssptr, mux);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	assert(ssptr);
	unsubscribe_playback_only(futures, ssptr);
	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in subscribe_mux"); // This will ensure that tuning is retried later
	}

	auto devdb_wtxn = receiver.devdb.wtxn();
	auto ret_subscription_id =
		this->receiver_thread_t::subscribe_mux(futures, devdb_wtxn, mux, ssptr, tune_options,
																					 scan_id, false /*do_not_unsubscribe_on_failure*/);
	devdb_wtxn.commit();
	error = wait_for_all(futures);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(ssptr);
		set_error(saved_error); //restore error message
		return {subscription_id_t::TUNE_FAILED, {}};
	}
	else
		return {ret_subscription_id, subscribed_fe_key};
}

active_adapter_t* receiver_thread_t::find_or_create_active_adapter
(std::vector<task_queue_t::future_t>& futures, const subscribe_ret_t& sret)
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

/*
	called to manually control positioner and for spectrum_scan. The former only reserves the lnb
	but does not do anything, the latter subscribes the spectrum, but using an exclusive reservation,
	while still moving to a specific sat_pos if needed (Todo: clean up this mess)
 */
subscription_id_t receiver_thread_t::subscribe_lnb(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
																									 devdb::rf_path_t& rf_path,
																									 devdb::lnb_t& lnb, subscription_options_t tune_options,
																									 ssptr_t ssptr) {
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	bool need_spectrum = tune_options.subscription_type == devdb::subscription_type_t::SPECTRUM_ACQ ||
		tune_options.subscription_type == devdb::subscription_type_t::BAND_SCAN;
	tune_options.allowed_dish_ids = {};
	tune_options.allowed_card_mac_addresses = {};
	tune_options.need_spectrum = need_spectrum;
	tune_options.may_move_dish = true;
	std::optional<int16_t> sat_pos_to_move_to;
	if(need_spectrum)
		sat_pos_to_move_to = tune_options.spectrum_scan_options.sat.sat_pos;
	assert(tune_options.subscription_type != devdb::subscription_type_t::SPECTRUM_ACQ ||
				 tune_options.spectrum_scan_options.sat.sat_pos !=sat_pos_none);
	auto sret = devdb::fe::subscribe_rf_path(devdb_wtxn, subscription_id,
																					 tune_options,
																					 rf_path,
																					 sat_pos_to_move_to);
	ssptr->set_subscription_id(sret.subscription_id);
	if(sret.failed) {
		auto updated_old_dbfe = sret.aa.updated_old_dbfe;
		if(updated_old_dbfe) {
			dtdebugf("Subscription failed calling release_active_adapter");
			bool is_streaming = ssptr->is_streaming();
			release_active_adapter(futures, sret.subscription_id, *updated_old_dbfe, is_streaming);
		} else {
			dtdebugf("Subscription failed: updated_old_dbfe = NONE");
		}
		user_errorf("Lnb reservation failed: {}", lnb);

		return subscription_id_t::RESERVATION_FAILED;
	}
	tune_options.tune_pars = sret.tune_pars;

	dtdebugf("subscribe_lnb subscription_id={:d}", (int) sret.subscription_id);

	if(sret.aa.is_new_aa() && sret.aa.updated_old_dbfe) {
		bool is_streaming = ssptr->is_streaming();
		release_active_adapter(futures, sret.subscription_id, *sret.aa.updated_old_dbfe, is_streaming);
	}

	auto* activate_adapter_p = find_or_create_active_adapter(futures, sret);
	assert(activate_adapter_p);
	auto& aa = *activate_adapter_p;

	futures.push_back(aa.tuner_thread.push_task([&aa, subscription_id, sret, tune_options, need_spectrum]() {
		auto ret = need_spectrum
			? cb(aa.tuner_thread).lnb_spectrum_acquistion(subscription_id, sret, tune_options)
			: cb(aa.tuner_thread).lnb_activate(subscription_id, sret, tune_options);
		if (ret < 0)
			dterrorf("tune returned {:d}", ret);
		return ret;
	}));

	dtdebugf("Subscribed to: lnb={}", lnb);
	return sret.subscription_id;
}

devdb::lnb_t receiver_t::reread_lnb_lof_offsets(const devdb::lnb_t& lnb)

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
receiver_thread_t::cb_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb_, subscription_options_t tune_options,
																			 ssptr_t ssptr) {

	auto lnb = receiver.reread_lnb_lof_offsets(lnb_);
	auto s = fmt::format("SUB[{}]",  ssptr, lnb);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();
	user_error_.clear();
	auto ret_subscription_id = this->receiver_thread_t::subscribe_lnb(
		futures, devdb_wtxn, rf_path, lnb, tune_options, ssptr);
	devdb_wtxn.commit();
	bool error = wait_for_all(futures, false /*clear_errors*/) ||
		(int)ret_subscription_id <0; //the 2nd case occurs when reservation failed (no futures to wait for)
	if (error) {
		auto saved_error = get_error();
		unsubscribe(ssptr);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
	}
	return ret_subscription_id;
}

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbs_mux_t>(
	const chdb::dvbs_mux_t& mux, ssptr_t ssptr,
	subscription_options_t tune_options, const chdb::scan_id_t& scan_id);

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbc_mux_t>(
	const chdb::dvbc_mux_t& mux, ssptr_t ssptr,
	subscription_options_t tune_options, const chdb::scan_id_t& scan_id);

template
std::tuple<subscription_id_t, devdb::fe_key_t>
receiver_thread_t::cb_t::subscribe_mux<chdb::dvbt_mux_t>(
	const chdb::dvbt_mux_t& mux, ssptr_t ssptr,
	subscription_options_t tune_options, const chdb::scan_id_t& scan_id);

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
																																				ssptr_t ssptr) {

	dtdebugf("Subscribe recording {} sub={}: playback start", rec, ssptr);
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();

	auto active_playback = std::make_shared<active_playback_t>(receiver, rec);
	assert(ssptr);
	ssptr->set_active_playback(active_playback);

	return active_playback->make_client_mpm(receiver, subscription_id);
}

std::unique_ptr<playback_mpm_t>
receiver_thread_t::cb_t::subscribe_service(const chdb::service_t& service,
																					 ssptr_t ssptr) {
	auto s = fmt::format("SUB[{}] {}", ssptr, service);
	log4cxx::NDC ndc(s);
	std::vector<task_queue_t::future_t> futures;
	dtdebugf("SUBSCRIBE started");
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	auto chdb_txn = receiver.chdb.rtxn();
	auto [mux, error1] = mux_for_service(chdb_txn, service);
	if (error1 < 0) {
		user_errorf("Could not find mux for {}", service);
		if ((int) subscription_id >= 0) {
			auto devdb_wtxn = receiver.devdb.wtxn();
			unsubscribe_all(futures, devdb_wtxn, ssptr);
			devdb_wtxn.commit();
		}
		chdb_txn.abort();
		return std::unique_ptr<playback_mpm_t>{};
	}

	// Stop any playback in progress (in that case there is no suscribed service/mux
	if ((int) subscription_id >= 0) {
		assert(ssptr);
		if(ssptr->get_active_playback()) {
			// this must be playback
			unsubscribe(ssptr);
		}
	}
	chdb_txn.abort();

	dtdebugf("SUBSCRIBE - calling subscribe_service");
	// now perform the requested subscription
	auto [stream, mpmptr] = this->receiver_thread_t::subscribe_stream(mux, &service, ssptr,
																																		 nullptr /*for_streaming*/);
	/*wait_for_futures is needed because active_adapters/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/active_adapter threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/
	dtdebugf("SUBSCRIBE - returning to caller");
	return std::move(mpmptr);
}

/*
	adjust a stream to reflect desired changed in stream_, turning on/off stream as needed
	force_off: stop streaming noiw
 */
devdb::stream_t receiver_thread_t::update_and_toggle_stream(const devdb::stream_t& stream_) {
	auto turnoff = [](devdb::stream_t& stream) {
		//clean up dead stream
		stream.subscription_id = -1;
		stream.owner = -1;
		stream.streamer_pid = -1;
		stream.stream_state = devdb::stream_state_t::OFF;
		stream.mtime = system_clock_t::to_time_t(now);
	};

	auto stream = stream_;
	if (stream.stream_id <0) {
		auto devdb_rtxn = receiver.devdb.rtxn();
		stream.stream_id = devdb::make_unique_id(devdb_rtxn, stream);
		assert(stream.stream_id >= 0);
		devdb_rtxn.abort();
	}
	bool active = (stream.owner >=0 && !kill((pid_t)stream.owner, 0));
	bool ours = stream.owner == getpid();
	if(!ours) {
		if(active)
			return stream;
		if(stream.owner != -1)
			turnoff(stream);
	}
	assert(!active || stream.subscription_id>=0);
	assert(stream.stream_id >=0);
	ssptr_t ssptr;
	if(stream.subscription_id <0) {
		//create a subscriber
		//the construction below computed a unique subscription_id
		subscribe_ret_t sret{subscription_id_t::NONE, false/*failed*/};
		ssptr = subscriber_t::make(&receiver, nullptr /*window*/);
		ssptr->set_subscription_id(subscription_id_t{sret.subscription_id});
		stream.subscription_id = (int)sret.subscription_id;
	} else {
		ssptr = receiver.get_ssptr(subscription_id_t{stream.subscription_id});
		if(!ssptr.get()) {
			ssptr = receiver.get_ssptr(subscription_id_t{stream.subscription_id});
		}

	}
	std::vector<task_queue_t::future_t> futures;

	auto s = fmt::format("SUB[{}] stream {}", ssptr, stream);
	log4cxx::NDC ndc(s);
	if(active) {
		// Stop any stream in progress but keep the service running
		remove_stream(futures, subscription_id_t{stream.subscription_id});
	}
	if(stream.stream_state != devdb::stream_state_t::OFF) {
		auto* pservice = std::get_if<chdb::service_t>(&stream.content);
		chdb::any_mux_t mux;

		if(!pservice) {
			mux = std::visit(
				[&](auto &content) {
					if constexpr (is_same_type_v<chdb::service_t, decltype(content)>) {
						assert(false);
						return chdb::any_mux_t{};
					}
					else {
						return chdb::any_mux_t{content};
					}
				}, stream.content);
		} else {
			auto chdb_rtxn = receiver.chdb.rtxn();
			int error1;
			std::tie(mux, error1) = mux_for_service(chdb_rtxn, *pservice);
			chdb_rtxn.abort();
			if (error1 < 0) {
				user_errorf("Could not find mux for {}", *pservice);
				return {};
			}
		}

		if(stream.stream_state != devdb::stream_state_t::OFF) {
			auto [streamret, mpmptr] = this->receiver_thread_t::subscribe_stream(mux, pservice, ssptr,
																																					 &stream /*for_streaming*/);
			if(streamret) {
				stream = *streamret;
				ssptr->set_stream_id(stream.stream_id);
			} else {
				dterrorf("Could not create stream");
				turnoff(stream);
			}
		}
	} else {
		// Stop any playback in progress (in that case there is no suscribed service/mux
		turnoff(stream);
		auto devdb_wtxn = receiver.devdb.wtxn();
		unsubscribe_all(futures, devdb_wtxn, ssptr);
		devdb_wtxn.commit();
	}
	auto devdb_wtxn = receiver.devdb.wtxn();
	put_record(devdb_wtxn, stream);
	devdb_wtxn.commit();
	wait_for_all(futures);
	return stream;
}


std::unique_ptr<playback_mpm_t>
receiver_thread_t::cb_t::subscribe_playback(const recdb::rec_t& rec,
																												 ssptr_t ssptr) {
	auto s = fmt::format("SUB[{}] {}", ssptr, rec);
	log4cxx::NDC ndc(s);
	dtdebugf("SUBSCRIBE rec started");
	assert(ssptr);
	auto subscription_id = ssptr->get_subscription_id();
	std::vector<task_queue_t::future_t> futures;

	if ((int) subscription_id >= 0) {
		// unsubscribe existing service and/or mux if one exists
		assert(ssptr);
		auto active_playback = ssptr->get_active_playback();
		if (active_playback) {
			if (rec.epg.k == active_playback->currently_playing_recording.epg.k) {
				dtdebugf("subscribe {}: already subscribed to recording", rec);
				return active_playback->make_client_mpm(receiver, subscription_id);
			}
		}
		unsubscribe(ssptr);
	} else { //generate a unique subscription_id
		subscribe_ret_t sret{subscription_id_t::NONE, {}};
		subscription_id = sret.subscription_id;
		ssptr->set_subscription_id(subscription_id);
	}
	return subscribe_playback_(rec, ssptr);
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
	auto ssptr = subscriber_t::make(&receiver, nullptr /*window*/);
	ssptr->set_subscription_id(subscription_id_t{rec_in.subscription_id});
	this->receiver_thread_t::subscribe_service_for_recording(futures, devdb_wtxn, mux, rec_in, ssptr);
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
	subscription_id is passed by reference so that when a new subscription is created,
	the proper subscription_id is stored directly in its subscriber
 */
template <typename _mux_t>
subscription_id_t receiver_t::scan_muxes(ss::vector_<_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	subscription_id_t ret {-1};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &muxes, &tune_options, &ret, &ssptr]() {
		ret = cb(receiver_thread).scan_muxes(muxes, tune_options, ssptr);
		if((int)ret >= 0) {
			cb(receiver_thread).scan_now(); // start initial scan
		}
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return ret;
}

subscription_id_t receiver_t::scan_spectral_peaks(const devdb::rf_path_t& rf_path,
																									ss::vector_<chdb::spectral_peak_t>& peaks,
																									const statdb::spectrum_key_t& spectrum_key,
																									ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	subscription_id_t ret{-1};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &rf_path, &peaks, &spectrum_key, &ret, &ssptr]() {
		ret = cb(receiver_thread).scan_spectral_peaks(rf_path, peaks, spectrum_key, ssptr);
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return ret;
}

subscription_id_t receiver_t::scan_bands(const ss::vector_<chdb::sat_t>& sats,
																				 const ss::vector_<chdb::fe_polarisation_t>& pols,
																				 subscription_options_t tune_options,
																				 ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	subscription_id_t ret{-1};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &sats, pols, &tune_options, &ret, &ssptr]() {
		ret = cb(receiver_thread).scan_bands(sats, pols, tune_options, ssptr);
		if((int)ret >= 0) {
			cb(receiver_thread).scan_now(); // start initial scan
		}
		return 0;
	}));
	wait_for_all(futures); //essential because muxes passed by reference
	return ret;
}



/*
	main entry point
	called by code in receiver_pybind.cc
	and subscriber_pynbind.cc
 */
template <typename _mux_t>
subscription_id_t
receiver_t::subscribe_mux(const _mux_t& mux, bool blindscan, ssptr_t ssptr) {

	std::vector<task_queue_t::future_t> futures;
	auto tune_options = this->get_default_subscription_options(devdb::subscription_type_t::TUNE);
	tune_options.use_blind_tune = blindscan;
	devdb::fe_key_t subscribed_fe_key;
	subscription_id_t ret{-1};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &mux, tune_options, &ret, &ssptr, &subscribed_fe_key]() {
		std::tie(ret, subscribed_fe_key)
			= cb(receiver_thread).subscribe_mux(mux, ssptr, tune_options);
		return 0;
	}));
	wait_for_all(futures);
	return ret;
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
																	 int32_t high_freq, const chdb::sat_t& sat, ssptr_t ssptr) {
	auto lnb = reread_lnb_lof_offsets(lnb_);

	std::vector<task_queue_t::future_t> futures;
	auto tune_options = get_default_subscription_options(devdb::subscription_type_t::SPECTRUM_ACQ);
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
	tune_options.spectrum_scan_options.sat = sat;
	tune_options.need_spectrum = true;
	//call by reference ok because of subsequent wait_for_all
	subscription_id_t ret_subscription_id;
	futures.push_back(receiver_thread.push_task([this, &lnb, &rf_path, tune_options, &ret_subscription_id,
																							 &ssptr]() {
		cb(receiver_thread).abort_scan();
		ret_subscription_id = cb(receiver_thread).subscribe_lnb(rf_path, lnb, tune_options, ssptr);
		return 0;
	}));
	auto error = wait_for_all(futures, true /*clear_all_errors*/);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(ssptr);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
	}
	return ret_subscription_id;
}

subscription_id_t receiver_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																						devdb::retune_mode_t retune_mode,
																						ssptr_t ssptr) {
	lnb = reread_lnb_lof_offsets(lnb);
	std::vector<task_queue_t::future_t> futures;
	subscription_options_t tune_options;
	tune_options.subscription_type = devdb::subscription_type_t::LNB_CONTROL;
	tune_options.retune_mode = retune_mode;
	tune_options.need_spectrum = false;
	subscription_id_t ret_subscription_id;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &rf_path, &lnb, &tune_options,
																							 &ret_subscription_id, &ssptr]() {
		cb(receiver_thread).abort_scan();
		ret_subscription_id = cb(receiver_thread).subscribe_lnb(rf_path, lnb, tune_options, ssptr);
		return 0;
	}));
	auto error = wait_for_all(futures);
	if(error) {
		auto saved_error = get_error();
		unsubscribe(ssptr);
		set_error(saved_error); //restore error message
		return subscription_id_t::TUNE_FAILED;
		this->global_subscriber->notify_error(get_error());
	}
	return ret_subscription_id;
}

/*! Same as subscribe_lnb, but also subscribes to a mux in the same call
	Called from positioner dialog when tuning to specfic mux on specific rf_path
 */
subscription_id_t
receiver_t::subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
																	bool blindscan, const pls_search_range_t& pls_search_range,
																	devdb::retune_mode_t retune_mode, ssptr_t ssptr) {
	subscription_id_t ret{-1};
	devdb::fe_key_t subscribed_fe_key;
	lnb = reread_lnb_lof_offsets(lnb);
	std::vector<task_queue_t::future_t> futures;
	auto tune_options = this->get_default_subscription_options(devdb::subscription_type_t::LNB_CONTROL);
	tune_options.use_blind_tune = blindscan;
	tune_options.retune_mode = retune_mode;
	tune_options.allowed_rf_paths = {rf_path};
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task(
											[this, &mux, tune_options, &ret, &ssptr]() {
		cb(receiver_thread).abort_scan();
		devdb::fe_key_t subscribed_fe_key;
		std::tie(ret, subscribed_fe_key)
			= cb(receiver_thread).subscribe_mux(mux, ssptr, tune_options);
		return 0;
	}));
	wait_for_all(futures, true /*clear all errors*/);
	return ret;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_service_for_viewing(const chdb::service_t& service,
																															ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &ssptr, &service, &ret]() {
		ret = cb(receiver_thread).subscribe_service(service, ssptr);
		return 0;
	}));
	wait_for_all(futures);
	return ret;
}

devdb::stream_t receiver_t::update_and_toggle_stream(const devdb::stream_t& stream_) {
	std::vector<task_queue_t::future_t> futures;
	auto stream = stream_;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &stream]() mutable {
		//cb(receiver_thread).abort_scan();
		stream = cb(receiver_thread).update_and_toggle_stream(stream);
		return 0;
	}));
	wait_for_all(futures);
	return stream;
}

std::unique_ptr<playback_mpm_t> receiver_t::subscribe_playback(const recdb::rec_t& rec, ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	std::unique_ptr<playback_mpm_t> ret;
	//call by reference ok because of subsequent wait_for_all
	futures.push_back(receiver_thread.push_task([this, &ssptr, &rec, &ret]() {
		ret = cb(receiver_thread).subscribe_playback(rec, ssptr);
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
	receiver_thread.stop_running(true/*stop_running*/);
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

	startup_streams(now);
	startup_commands(now);

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
				housekeeping(now);
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
			auto mpv = ms->get_mpv();
			if(mpv) {
				mpv->notify(signal_info);
			}
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
																			const devdb::scan_stats_t& stats) {
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
	assert (spectrum_scan.start_freq !=0);
	assert (spectrum_scan.end_freq !=0);
	auto scan_id = deactivate_spectrum_scan(spectrum_scan);
	bool has_scanning_subscribers{false};
	{
		auto mss = subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;

			if(ms->is_scanning()) {
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
	assert(!has_scanning_subscribers || scanner_t::is_our_scan(scan_id));
	if(has_scanning_subscribers) {
		auto& receiver_thread = this->receiver_thread;
		//capturing by value is essential
		receiver_thread.push_task([&receiver_thread, finished_fe, spectrum_scan, scan_id, subscription_ids]() {
			cb(receiver_thread).send_spectrum_to_scanner(finished_fe, spectrum_scan, scan_id, subscription_ids);
			return 0;
		});
	}
}

//thread safe; called from fe_monitor; notify python subscribers synschronously and scanner asynchronously
void receiver_t::on_positioner_motion(const devdb::fe_t& fe, const devdb::dish_t& dish,
																			double speed, int delay,
																			const ss::vector_<subscription_id_t>& subscription_ids) {
	auto now = system_clock_t::to_time_t(::now);
	positioner_motion_report_t report{};
	report.dish = dish;
	report.start_time = now;
	report.end_time = now + delay;
	if(delay==0) {
		printf("ending dish motion\n");
		auto devdb_wtxn = devdb.wtxn();
		devdb::dish::end_move(devdb_wtxn, dish);
		devdb_wtxn.commit();
		report.end_time = report.start_time;
	}
	{
		auto mss = subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			if(ms->is_scanning()) {
				continue;
			}
			if (!subscription_ids.contains(ms->get_subscription_id()))
				continue;
			/*
				This is a spectrum or positioner dialog window. We can inform it directly
			*/
			if(subscription_ids.contains(ms->get_subscription_id()))
				ms->notify_positioner_motion(report);
		}
	}
}

void receiver_t::update_playback_info() {
		auto mss = subscribers.readAccess();
		for (auto [subsptr, ms_shared_ptr] : *mss) {
			auto* ms = ms_shared_ptr.get();
			if (!ms)
				continue;
			auto mpv = ms->get_mpv();
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
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															ssptr_t ssptr) {
	auto scanner = get_scanner();
	if (!scanner) {
		scanner = std::make_unique<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}
	auto subscription_id = ssptr->get_subscription_id();
	if((int)subscription_id<0) {
		subscribe_ret_t sret{subscription_id_t::NONE, {}}; //create new subscription_id
		subscription_id = sret.subscription_id;
		ssptr->set_subscription_id(subscription_id);
	}

	auto num_added_muxes = scanner->add_muxes(muxes, tune_options, ssptr);
	return num_added_muxes > 0 ? subscription_id : subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
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
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															ssptr_t ssptr) {
	auto scanner = get_scanner();
	if (!scanner) {
		scanner = std::make_unique<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}

	auto subscription_id = ssptr->get_subscription_id();
	if((int)subscription_id<0) {
		subscribe_ret_t sret{subscription_id_t::NONE, {}}; //create new subscription_id
		subscription_id = sret.subscription_id;
		ssptr->set_subscription_id(subscription_id);
	}

	auto num_added_bands = scanner->add_bands(sats, pols, tune_options, ssptr);
	return num_added_bands > 0 ? subscription_id : subscription_id_t::RESERVATION_FAILED_PERMANENTLY;
}

/*!
	called when complete channel scan needs to be aborted
*/
bool receiver_thread_t::unsubscribe_scan(std::vector<task_queue_t::future_t>& futures,
																				 db_txn& devdb_wtxn, ssptr_t scan_ssptr) {
	auto scanner = get_scanner();
	if(scanner.get()) {
		return scanner->unsubscribe_scan(futures, devdb_wtxn, scan_ssptr);
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
																			 const devdb::rf_path_t& rf_path,
																			 ss::vector_<chdb::spectral_peak_t>& peaks,
																			 const statdb::spectrum_key_t& spectrum_key,
																			 bool scan_found_muxes, int max_num_subscriptions,
																			 ssptr_t scan_ssptr) {
	auto scanner = get_scanner();

	if (!scanner){
		scanner = std::make_shared<scanner_t>(*this, max_num_subscriptions);
		set_scanner(scanner);
	}
	auto subscription_id = scan_ssptr->get_subscription_id();
	if((int)subscription_id<0) {
		subscribe_ret_t sret{subscription_id_t::NONE, {}}; //create new subscription_id
		subscription_id = sret.subscription_id;
		scan_ssptr->set_subscription_id(subscription_id);
	}

	scanner->add_spectral_peaks(rf_path, spectrum_key, peaks, scan_ssptr);
	bool remove_scanner = scanner->housekeeping(true); // start initial scan
	if(remove_scanner) {
		reset_scanner();
		return subscription_id_t::TUNE_FAILED;
	}
	return scan_ssptr->get_subscription_id();
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
																		ssptr_t ssptr) {
	dtdebugf("calling unsubscribe_all");
	unsubscribe_all(futures, devdb_wtxn, ssptr);
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
void receiver_thread_t::cb_t::unsubscribe(ssptr_t ssptr) {
	std::vector<task_queue_t::future_t> futures;
	auto devdb_wtxn = receiver.devdb.wtxn();
	this->receiver_thread_t::unsubscribe(futures, devdb_wtxn, ssptr);
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
void receiver_t::unsubscribe(ssptr_t ssptr) {
	receiver_thread.push_task([this, &ssptr]() {
		cb(receiver_thread).unsubscribe(ssptr);
		return 0;
	}).wait();
	//return subscription_id_t::NONE;
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

std::tuple<int, std::optional<int>>
receiver_thread_t::cb_t::positioner_cmd(ssptr_t ssptr, devdb::positioner_cmd_t cmd,
																				 int par) {
	auto subscription_id = ssptr->get_subscription_id();
	auto aa = receiver.find_active_adapter(subscription_id);
	int ret{-1};
	std::optional<int> new_usals_pos;
	if(aa) {
		aa->tuner_thread.push_task([subscription_id, &aa, cmd, par, &ret, & new_usals_pos]() {
			std::tie(ret, new_usals_pos) = cb(aa->tuner_thread).positioner_cmd(subscription_id, cmd, par);
			return 0;
		}).wait();
	}
	return {ret, new_usals_pos};
}

devdb::tune_options_t receiver_t::get_default_tune_options(devdb::subscription_type_t subscription_type) const
{
	using namespace devdb;
	tune_options_t ret;
	ret.subscription_type = subscription_type;
	bool for_scan = false;
	bool for_lnb_control = false;
	switch(subscription_type) {
	case subscription_type_t::TUNE:
		ret.scan_target =  scan_target_t::SCAN_FULL_AND_EPG;
		break;
	case subscription_type_t::BAND_SCAN:
	case subscription_type_t::SPECTRUM_ACQ:
		ret.scan_target =  scan_target_t::SCAN_FULL;
		ret.retune_mode = retune_mode_t::NEVER;
		ret.need_spectrum = true;
		for_scan = true;
		break;
	case subscription_type_t::MUX_SCAN: //for scan
		ret.scan_target =  scan_target_t::SCAN_FULL;
		ret.retune_mode = retune_mode_t::NEVER;
		for_scan=true;
		break;
	case subscription_type_t::LNB_CONTROL:
		ret.scan_target =  scan_target_t::SCAN_MINIMAL;
		for_lnb_control = true;
		break;
	default:
		assert(0);
		break;
	}
	auto r = options.readAccess();
	ret.resource_reuse_bonus = r->resource_reuse_bonus;
	ret.dish_move_penalty = r->dish_move_penalty;
	ret.use_blind_tune =  for_scan ? r->scan_use_blind_tune: r->tune_use_blind_tune;
	ret.may_move_dish = for_lnb_control ? true : (for_scan ? r->scan_may_move_dish: r->tune_may_move_dish);
	ret.scan_max_duration =  std::chrono::duration_cast<std::chrono::seconds>(r->scan_max_duration).count();

	return ret;
}

spectrum_scan_options_t
receiver_t::get_default_spectrum_scan_options(devdb::subscription_type_t subscription_type) const
{
	using namespace devdb;
	spectrum_scan_options_t ret;
	switch(subscription_type) {
	case subscription_type_t::TUNE:
		break;
	case subscription_type_t::BAND_SCAN:
	case subscription_type_t::SPECTRUM_ACQ: {
		ret.recompute_peaks = true;
		auto for_spectrum_scan = (subscription_type==subscription_type_t::BAND_SCAN);
		auto r = options.readAccess();
		ret.save_spectrum = for_spectrum_scan ? r->band_scan_save_spectrum : true;
	}
		break;
	case subscription_type_t::MUX_SCAN: //for scan
		break;
	case subscription_type_t::LNB_CONTROL:
		break;
	default:
		assert(0);
		break;
	}

	return ret;
}

subscription_options_t receiver_t::get_default_subscription_options(devdb::subscription_type_t subscription_type) const
{
	using namespace devdb;
	subscription_options_t o;
	(tune_options_t&) o = get_default_tune_options(subscription_type);
	o.constellation_options.num_samples = (subscription_type == subscription_type_t::LNB_CONTROL) ? 16*1024 : 0;
	o.spectrum_scan_options = get_default_spectrum_scan_options(subscription_type);
	auto r = options.readAccess();
	o.usals_location = r->usals_location;
	return o;
}

static chdb::scan_id_t activate_spectrum_scan_
(chdb::chdb_t&chdb, chdb::sat_t& sat, chdb::fe_polarisation_t pol,
 int start_freq, int end_freq,
 bool spectrum_obtained, devdb::lnb_pol_type_t lnb_pol_type= devdb::lnb_pol_type_t::UNKNOWN) {
	using namespace chdb;
	scan_id_t scan_id;
	auto [sat_band, sat_sub_band] = sat_band_for_freq(start_freq);
#ifndef NDEBUG
		auto [sat_band1, sat_sub_band1] = sat_band_for_freq(end_freq-1);
		assert(sat_band1 == sat_band);
		assert(sat_band == sat.sat_band);
#endif
		//update sat from db to ensure correct band_scan_status
	if(!spectrum_obtained)
		chdb::sat::clean_band_scan_pols(sat, lnb_pol_type);
	auto chdb_rtxn = chdb.rtxn();

	auto c = chdb::sat_t::find_by_key(chdb_rtxn, sat.sat_pos, sat.sat_band, find_type_t::find_eq);
	assert(c.is_valid());
	sat = c.current();
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
				dtdebugf("SET IDLE {}", band_scan);
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

void receiver_t::activate_spectrum_scan(spectrum_scan_options_t& spectrum_scan_options,
																				devdb::lnb_pol_type_t lnb_pol_type) {
	auto& sat = spectrum_scan_options.sat;
	auto pol = spectrum_scan_options.band_pol.pol;
	auto start_freq = spectrum_scan_options.start_freq;
	auto end_freq = spectrum_scan_options.end_freq;
	activate_spectrum_scan_(chdb, sat, pol, start_freq, end_freq, false/*spectrum_obtained*/,
													lnb_pol_type);
}

chdb::scan_id_t receiver_t::deactivate_spectrum_scan(const spectrum_scan_t&spectrum_scan) {
	using namespace chdb;
	auto sat = spectrum_scan.sat;
	auto pol = spectrum_scan.band_pol.pol;
	auto start_freq = spectrum_scan.start_freq;
	auto end_freq = spectrum_scan.end_freq;
	return activate_spectrum_scan_(chdb, sat, pol, start_freq, end_freq, true/*spectrum_obtained*/);
}

#ifdef set_member
#undef set_member
#endif
#define set_member(v, m, val_)																				\
	[&](auto& x)  {																											\
		if has_member(x, m)  {																						\
				auto val = val_;																							\
				bool ret = (x.m != val);																			\
				x.m =val;																											\
				return ret;																										\
			}																																\
	}(v)																																\


void receiver_thread_t::startup_streams(system_time_t now_) {
	using namespace devdb;
	int count{0};
	int startcount{0};

	bool changed{false};

  /*in pass 0 we update the database;
		in pass 1 we start streams: this requires that we do not hold a wtxn
	*/
	auto txn = receiver.devdb.rtxn();
	auto c = find_first<stream_t>(txn);
	for(auto stream: c.range())  {
		bool active = (stream.owner >=0 && !kill((pid_t)stream.owner, 0));
		if(active)
			continue;
		if(!stream.preserve) {
			auto devdb_wtxn = receiver.devdb.wtxn();
			delete_record(devdb_wtxn, stream);
			devdb_wtxn.commit();
			continue;
		}
		changed |= set_member(stream, owner, -1);
		switch(stream.stream_state) {
		case devdb::stream_state_t::OFF:
		case devdb::stream_state_t::ON:
			changed |= set_member(stream, stream_state,
														stream.autostart ? devdb::stream_state_t::ON:
														devdb::stream_state_t::OFF);
			changed |= set_member(stream, subscription_id, -1);
			changed |= set_member(stream, streamer_pid, -1);
			changed |= set_member(stream, owner, -1);
			if(stream.autostart) {
				startcount++;
				update_and_toggle_stream(stream);
				continue;
			} else if(changed) {
				auto devdb_wtxn = receiver.devdb.wtxn();
				stream.mtime = system_clock_t::to_time_t(now_);
				put_record(devdb_wtxn, stream);
				devdb_wtxn.commit();
				count++;
			}
		}
	}
	txn.commit();
	dtdebugf("Updated {:d} and started {:d} streams", count, startcount++);
}

thread_local thread_group_t thread_group{thread_group_t::unknown};

//template instantiations

template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbc_mux_t& mux,
	ssptr_t ssptr, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbt_mux_t& mux,
	ssptr_t ssptr, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);

template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbs_mux_t& mux,
	ssptr_t ssptr, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template subscription_id_t
receiver_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 ssptr_t ssptr);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 ssptr_t ssptr);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 ssptr_t ssptr);


template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux, bool blindscan,
																						ssptr_t ssptr);

template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, bool blindscan,
																						ssptr_t ssptr);

template subscription_id_t
receiver_t::subscribe_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, bool blindscan,
																						ssptr_t ssptr);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes,
																											const subscription_options_t& tune_options,
																											ssptr_t ssptr);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																											const subscription_options_t& tune_options,
																											ssptr_t ssptr);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																											const subscription_options_t& tune_options,
																											ssptr_t ssptr);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbs_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															ssptr_t ssptr);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbc_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															ssptr_t ssptr);
template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbt_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															ssptr_t ssptr);
