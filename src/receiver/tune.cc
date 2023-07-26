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
#include "active_adapter.h"

#include <stdio.h>
#include <stdlib.h>
//#include <ctype.h>
#include <sys/time.h>
//#include <sys/poll.h>
#include <sys/epoll.h>
//#include <sys/stat.h>
#include "util/template_util.h"
#include "active_service.h"
#include "active_si_stream.h"
#include "receiver.h"
#include "util/neumovariant.h"
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <resolv.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <values.h>
using namespace chdb;

/** @brief Print the status
 * Print the status contained in festatus, this status says if the card is lock, sync etc.
 *
 * @param festatus the status to display
 */
void print_tuner_status(fe_status_t festatus) {
	dtdebug("FE_STATUS:");
	if (festatus & FE_HAS_SIGNAL)
		dtdebug("     FE_HAS_SIGNAL : found something above the noise level");
	if (festatus & FE_HAS_CARRIER)
		dtdebug("     FE_HAS_CARRIER : found a DVB signal");
	if (festatus & FE_HAS_VITERBI)
		dtdebug("     FE_HAS_VITERBI : FEC is stable");
	if (festatus & FE_HAS_SYNC)
		dtdebug("     FE_HAS_SYNC : found sync bytes");
	if (festatus & FE_HAS_LOCK)
		dtdebug("     FE_HAS_LOCK : everything's working...");
	if (festatus & FE_TIMEDOUT)
		dtdebug("     FE_TIMEDOUT : no lock within the last about 2 seconds");
	if (festatus & FE_HAS_TIMING_LOCK)
		dtdebug("     FE_REINIT : frontend has timing loop locked");
	dtdebug("---");
}

ss::string<128> dump_caps(chdb::fe_caps_t caps) {
	ss::string<128> ret;
	for (int i = 0; i < 32; ++i) {
		auto mask = ((uint32_t)1) << i;
		if (mask & (uint32_t)caps) {
			ret << enum_to_str((chdb::fe_caps_t)mask) << " ";
		}
	}
	return ret;
}

void tuner_thread_t::clean_dbs(system_time_t now, bool at_start) {
	{
		auto wtxn = receiver.chdb.wtxn();
		chdb::chdb_t::clean_log(wtxn);
		if(at_start)
			chdb::clean_scan_status(wtxn);
		if(at_start) {
			//ancient ubuntu does not know std::chrono::months{1}; hence use hours
			clean_expired_services(wtxn, std::chrono::hours{24*30});
		}
		wtxn.commit();
	}
	{
		auto wtxn = receiver.recdb.wtxn();
		recdb::recdb_t::clean_log(wtxn);
		wtxn.commit();
	}
	{
		auto wtxn = receiver.statdb.wtxn();
		statdb::statdb_t::clean_log(wtxn);
		wtxn.commit();
	}

	if (now > next_epg_clean_time) {
		auto wtxn = receiver.epgdb.wtxn();
		epgdb::clean(wtxn, now - 4h); // preserve last 4 hours
		wtxn.commit();
		next_epg_clean_time = now + 12h;
	}
}

int tuner_thread_t::cb_t::on_pmt_update(active_adapter_t& active_adapter, const dtdemux::pmt_info_t& pmt) {
	auto mux = active_adapter.current_tp();

	bool changed = pmt.has_freesat_epg ? add_epg_type(mux, chdb::epg_type_t::FREESAT)
		: remove_epg_type(mux, chdb::epg_type_t::FREESAT);

	changed |=
		pmt.has_skyuk_epg ? add_epg_type(mux, chdb::epg_type_t::SKYUK) : remove_epg_type(mux, chdb::epg_type_t::SKYUK);

	if (changed) {
		dtdebug("freesat/skyuk epg flag changed on mux " << mux);
		active_adapter.set_current_tp(mux);
		auto txn = receiver.chdb.wtxn();
		namespace m = chdb::update_mux_preserve_t;
		chdb::update_mux(txn, mux, now, m::flags{m::ALL & ~m::EPG_TYPES}, /*false ignore_key,*/
										 false /*ignore_t2mi_pid*/, true /*must_exist*/);
		txn.commit();
		dtdebug("committed");
	}
	return 0;
}

int tuner_thread_t::cb_t::update_service(const chdb::service_t& service) {
	auto txn = receiver.chdb.wtxn();
	put_record(txn, service);
	txn.commit();
	dtdebug("committed");
	return 0;
}

inline std::shared_ptr<active_adapter_t> tuner_thread_t::make_active_adapter(const devdb::fe_t& dbfe) {
	auto dvb_frontend = receiver.fe_for_dbfe(dbfe.k);
	dvb_frontend->update_dbfe(dbfe);
	return std::make_shared<active_adapter_t>(receiver, recmgr, dvb_frontend);
}

int tuner_thread_t::cb_t::lnb_activate(subscription_id_t subscription_id, const subscribe_ret_t& sret,
																			 tune_pars_t tune_pars) {
	// check_thread();
	dtdebugx("lnb activate subscription_id=%d", (int) subscription_id);
	bool failed = sret.subscription_failed();
	if(failed) {
		release_active_adapter(subscription_id);
		return -1;
	}
	assert((int) sret.sub_to_reuse  < 0  || sret.sub_to_reuse == sret.subscription_id);
	auto [it, found] = find_in_map(this->active_adapters, sret.subscription_id);
	if(!found) {
		assert(!sret.retune);
		assert(sret.newaa);
		auto& aa = *sret.newaa;
		auto active_adapter = make_active_adapter(aa.fe);
		this->active_adapters[sret.subscription_id] = active_adapter;
		return active_adapter->lnb_activate(aa.rf_path, aa.lnb, tune_pars);
	}
	if(sret.newaa)  {
		release_active_adapter(sret.subscription_id);
		assert(sret.newaa);
		auto& aa = *sret.newaa;
		auto& active_adapter = it->second;
		active_adapter->fe->update_dbfe(aa.fe);
		return active_adapter->lnb_activate(aa.rf_path, aa.lnb, tune_pars);
	}
	assert(sret.sub_to_reuse == sret.subscription_id);
	auto& active_adapter = it->second;
	return active_adapter->lnb_activate(active_adapter->current_rf_path(),
																			active_adapter->current_lnb(), tune_pars);
}


/*
	Called from tune_mux when our own subscription is a normal tune, but mux is resubscribed, e.g.,
	to set the scan status. The newly subscribed mux could be for a not yet running embedded t2mi stream

	In this case, an embedded stream is added through prepare_si.
	if scan_id >0, prepare_si also adds the subscription to the list of subscriptions to notify
	when scanning finishes. In case scanning already finished, this notification is sent immediately

	Finally, add_si also sets the tune options:
	@todo: various tune requests may have conflicting tune options (such as propagate_scan)
 */

void tuner_thread_t::add_si(active_adapter_t& active_adapter,
																	const chdb::any_mux_t& mux, const tune_options_t& tune_options ,
																	subscription_id_t subscription_id) {
	// check_thread();
#if 0
	ss::string<128> prefix;
	prefix << "TUN" << active_adapter.get_adapter_no() << "-ADD-SI";
#endif
	dtdebugx("tune restart_si");
	active_adapter.prepare_si(mux, true /*start*/, subscription_id, true /*add_to_running_mux*/);
	active_adapter.fe->set_tune_options(tune_options);
}

int tuner_thread_t::tune(std::shared_ptr<active_adapter_t> active_adapter,
												 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
												 const chdb::dvbs_mux_t& mux_, tune_pars_t tune_pars,
												 subscription_id_t subscription_id) {
	// check_thread();
	chdb::dvbs_mux_t mux{mux_};
	/*
		The new scan status must be written to the database now.
		Otherwise there may be problems on muxes which fail to scan: their status would remain
		pending, and when parallel tuners are in use, the second tuner might decide to scan
			the mux again
	*/
	ss::string<128> prefix;
	prefix << "TUN" << active_adapter->get_adapter_no() << "-TUNE";
	log4cxx::NDC::pop();
	log4cxx::NDC ndc(prefix.c_str());

	dtdebug("tune mux action " << mux);

	this->active_adapters[subscription_id] = active_adapter;
	bool user_requested = true;
	return active_adapter->tune(rf_path, lnb, mux, tune_pars, user_requested, subscription_id);
}

template <typename _mux_t>
int tuner_thread_t::tune(std::shared_ptr<active_adapter_t> active_adapter, const _mux_t& mux_,
															 tune_pars_t tune_pars,
															 subscription_id_t subscription_id) {
	_mux_t mux{mux_};

	assert( mux.c.scan_status != scan_status_t::ACTIVE);
	/*
		The new scan status must be written to the database now.
		Otherwise there may be problems on muxes which fail to scan: their status would remain
		pending, and whne parallel tuners are in use, the second tuner might decide to scan
		the mux again
		*/

	ss::string<128> prefix;
	prefix << "TUN" << active_adapter->get_adapter_no() << "-TUNE";
	log4cxx::NDC::pop();
	log4cxx::NDC ndc(prefix.c_str());

	dtdebugx("tune mux action");
	this->active_adapters[subscription_id] = active_adapter;
	bool user_requested = true;
	auto ret = active_adapter->tune(mux, tune_pars, user_requested, subscription_id);
	assert (ret>=0 || active_adapter->tune_state == active_adapter_t::TUNE_FAILED);
	return ret;
}

template int tuner_thread_t::cb_t::tune<chdb::dvbc_mux_t>(std::shared_ptr<active_adapter_t> active_adapter,
																													const chdb::dvbc_mux_t& mux, tune_pars_t tune_pars,
																													subscription_id_t subscription_id);

template int tuner_thread_t::cb_t::tune<chdb::dvbt_mux_t>(std::shared_ptr<active_adapter_t> active_adapter,
																													const chdb::dvbt_mux_t& mux, tune_pars_t tune_pars,
																													subscription_id_t subscription_id);

int tuner_thread_t::exit() {
	dtdebugx("tuner exit");
	this->active_adapters.clear();
	return 0;
}


void tuner_thread_t::stop_recording(recdb::rec_t rec) // important that this is not a reference (async called)
{

	if(rec.owner != getpid()) {
		dterror("Error stopping recording we don't own: " << rec);
		return;
	}

	assert(rec.subscription_id >= 0);
	if (rec.subscription_id < 0)
		return;

	auto subscription_id = subscription_id_t{rec.subscription_id};
	auto active_adapter = active_adapter_for_subscription(subscription_id);

	mpm_copylist_t copy_list;
	auto ret = active_adapter->stop_recording(subscription_id, rec, copy_list);
	if (ret >= 0) {
		assert(copy_list.rec.epg.rec_status == epgdb::rec_status_t::FINISHED);
		copy_list.run();
	}

	auto active_servicep = active_adapter->active_service_for_subscription(subscription_id);
	assert(active_servicep);

	recmgr.update_recording(copy_list.rec);
	auto& as = *active_servicep;
	as.service_thread //call by reference safe because of subsequent .wait
		.push_task([&as, &rec]() {
			cb(as.service_thread).forget_recording(rec);
			return 0;
		}).wait();

	auto& receiver_thread = receiver.receiver_thread;
	receiver_thread.push_task([&receiver_thread, subscription_id]() {
		cb(receiver_thread).unsubscribe(subscription_id);
		return 0;
	});
}


int tuner_thread_t::cb_t::release_active_adapter(subscription_id_t subscription_id) {

	auto [it, found] = find_in_map(this->active_adapters, subscription_id);
	if (!found) {
		dterrorx("Request to remove active_adapter for subscription_id=%d which was already removed",
			subscription_id);
		return -1;
	}
	auto& active_adapter = *it->second;
	active_adapter.remove_service(subscription_id);
	this->active_adapters.erase(it);
	return 0;
}


/*
	called whenever  a new or updated epg record is found to update information shown
	on live screen.
	Also sets recording status on epg_record
*/

void tuner_thread_t::on_epg_update(db_txn& txnepg, system_time_t now,
																	 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																																		to true or false*/)
{
	auto timeshift_duration = receiver.options.readAccess()->timeshift_duration;

	for (auto& it : active_adapters) {
		if(!it.second)
			continue;
		auto& aa = *it.second;
		for (auto& [subscription_id, active_service_p] : aa.subscribed_active_services) {
			if (epg_record.k.start_time > system_clock_t::to_time_t(now + timeshift_duration))
				break; // record too far in the future
			auto service = active_service_p->get_current_service();
			if (epg_record.k.service == service.k) {
				// send request to service_thread
				active_service_p->service_thread.push_task([now, active_service_p = active_service_p,
																										epg_record]() { // epg_record passed by value
					cb(active_service_p->service_thread).on_epg_update(now, epg_record);
					return 0;
				});
			}
		}
	}
	return recmgr.on_epg_update(txnepg, epg_record);
}

/*
	Called periodically to store update time for still active live buffers
	and clean old ones.

	Todo: do we need the update time? If not, this could be simplified by only updating
	at service switch time
 */
void tuner_thread_t::livebuffer_db_update_(system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	// constexpr int tolerance = 15*60; //we look 15 minutes in to the future (and past
	auto txn = receiver.recdb.wtxn();
	// first update modification time of live_service records
	for (auto& it : active_adapters) {
		if(!it.second)
			continue;
		auto& aa = *it.second;
		for (auto& [subscription_id, active_servicep] : aa.subscribed_active_services) {
			auto& active_service = *active_servicep;
			auto creation_time = system_clock_t::to_time_t(active_service.get_creation_time());
			auto c = recdb::live_service_t::find_by_key(txn, creation_time, aa.get_adapter_no(), find_type_t::find_eq,
																									recdb::live_service_t::partial_keys_t::all);
			if (c.is_valid()) {
				auto live_service = c.current();
				live_service.update_time = now;
				dtdebug("Updating live service adapter " << aa.get_adapter_no() << " create=" << creation_time
								<< " update=" << now);
#if 0
				recdb::put_record_at_key(c, c.current_serialized_primary_key(), live_service);
#else
				recdb::update_record_at_cursor(c, live_service);
#endif
			}
		}
	}

	// now check for expired live_service records

	auto c = recdb::live_service::find_first_sorted_by_update_time(txn);

	auto retention_time = receiver.options.readAccess()->livebuffer_retention_time.count();

	for (const auto& live_service : c.range()) {

		if (live_service.update_time >= now - retention_time)
			break; // we have reached too recent record
		ss::string<128> dirname;
		{
			auto r = receiver.options.readAccess();
			dirname.sprintf("%s/%s", r->live_path.c_str(), live_service.dirname.c_str());
		}
		dtdebugx("DELETING TIMESHIFT DIR %s\n", dirname.c_str());
		rmpath(dirname.c_str());
		recdb::delete_record_at_cursor(c.maincursor); //@todo: doublecheck if this cfile cursor point to the current "file"?
	}
	txn.commit();
}

int tuner_thread_t::run() {
	thread_id = std::this_thread::get_id();

	/*TODO: The timer below is used to gather signal strength, cnr and ber.
		When used from a gui, it may be better to let the gui handle this asynchronously
	*/
	set_name("tuner");
	logger = Logger::getLogger("tuner"); // override default logger for this thread
	double period_sec = 1.0;
	timer_start(period_sec);
	now = system_clock_t::now();
	clean_dbs(now, true);
	recmgr.startup(now);
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorx("error in poll: %s", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		// printf("n=%d\n", n);
		for (auto evt = next_event(); evt; evt = next_event()) {
			bool needs_remove{false};
			if (is_event_fd(evt)) {
				ss::string<128> prefix;
				prefix << "TUN-CMD";
				log4cxx::NDC ndc(prefix.c_str());
				// an external request was received
				// run_tasks returns -1 if we must exit
				if (run_tasks(now) < 0) {
					// detach();
					return 0;
				}
			} else if (is_timer_fd(evt)) {
					// time to do some housekeeping (check tuner)
				for (auto& it : active_adapters) {
					if(!it.second) {
						needs_remove = true;
						continue;
					}
					auto& active_adapter = *it.second;
					if (!active_adapter.fe) {
						dterror_nice("Implementation error\n");
						continue;
					}
					dttime_init();
					ss::string<128> prefix;
					prefix << "TUN" << active_adapter.get_adapter_no() << "-MON";
					log4cxx::NDC ndc(prefix.c_str());
					active_adapter.monitor();
					auto delay = dttime(-1);
					if (delay >= 500)
						dterrorx("monitor cycle took too long delay=%d", delay);
					run_tasks(now, false); //prioritize tuning commands
				}
				dttime_init();
				clean_dbs(now, false);
				dttime(100);
				recmgr.housekeeping(now);
				dttime(100);
				livebuffer_db_update.run([this](system_time_t now) { livebuffer_db_update_(now); }, now);
				dttime(100);
				auto delay = dttime(-1);
				if (delay >= 500)
					dterrorx("clean cycle took too long delay=%d", delay);
			} else {
				// this must be a si event
				for (auto& it : active_adapters) {
					if(!it.second) {
						needs_remove = true;
						continue;
					}
					auto& active_adapter = *it.second;
					// active_adapter will return if fd is for other active_adapter
					// The following call returns true if fd was for this active_adapter
					ss::string<128> prefix;
					prefix << "TUN" << active_adapter.get_adapter_no() << "-SI";
					log4cxx::NDC ndc(prefix.c_str());
					dttime_init();

					if (evt->events & EPOLLERR) {
						dterrorx("ERROR in epoll event for fd=%d", evt->data.fd);
					}
					if (evt->events & EPOLLIN) {
						if (active_adapter.read_and_process_data_for_fd(evt)) {
							// printf("processed using new si interface\n");
							auto delay = dttime(300);
							if (delay >= 200)
								dterrorx("si cycle took too long delay=%d", delay);
							break;
						}
					}
				}
				ss::string<128> prefix;
				prefix << "TASK";
				log4cxx::NDC ndc(prefix.c_str());
				run_tasks(now, false); //prioritize tuning commands
			}
			if(needs_remove) {
				for(auto it = active_adapters.begin(); it != active_adapters.end();) {
					if(it->second)
						++it;
					else {
						dtdebug("Erasing empty aa pointer");
						it = active_adapters.erase(it);
					}
				}
			}
		}
	}
	return 0;
}

int tuner_thread_t::cb_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	return recmgr.toggle_recording(service, epg_record);
}

void tuner_thread_t::cb_t::update_autorec(recdb::autorec_t& autorec)
{
	recmgr.update_autorec(autorec);
}

void tuner_thread_t::cb_t::delete_autorec(const recdb::autorec_t& autorec)
{
	recmgr.delete_autorec(autorec);
}

#if 0
void tuner_thread_t::cb_t::update_recording(const recdb::rec_t& rec) {
	recmgr.update_recording(rec);
}
#endif

tuner_thread_t::tuner_thread_t(receiver_t& receiver_)
	: task_queue_t(thread_group_t::tuner)
	, receiver(receiver_)
	, livebuffer_db_update(60)
	, recmgr(receiver_) {
}

tuner_thread_t::~tuner_thread_t() {
}

int tuner_thread_t::set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options) {
	return active_adapter.fe ? active_adapter.fe->set_tune_options(tune_options) : -1;
}

int tuner_thread_t::cb_t::set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options) {
	return this->tuner_thread_t::set_tune_options(active_adapter, tune_options);
}

int tuner_thread_t::cb_t::positioner_cmd(subscription_id_t subscription_id, devdb::positioner_cmd_t cmd,
																				 int par) {
	auto active_adapter = active_adapter_for_subscription(subscription_id);
	return (active_adapter && active_adapter->fe) ? active_adapter->fe->positioner_cmd(cmd, par) : -1;
}

int tuner_thread_t::cb_t::update_current_lnb(subscription_id_t subscription_id, const devdb::lnb_t& lnb) {
	auto active_adapter = active_adapter_for_subscription(subscription_id);
	if(active_adapter)
		active_adapter->update_current_lnb(lnb);
	return 0;
}

std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
tuner_thread_t::subscribe_mux(const subscribe_ret_t& sret,
															const chdb::any_mux_t& mux,
															const tune_pars_t& tune_pars) {
	/*In case of failure, release the resources associated with tjis subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	if(sret.failed && sret.was_subscribed) {
		release_all(sret.subscription_id);
		return {subscription_id_t::NONE, {}};
	}

	/*
		Perform all actions needed to tune to the proper mux, which we have been able to subscribe;
		In case of failure, release the resources assosciated with tjis subscription (active_adapter and
		active_service).
		This will also release any resources alreay in use (active_mux and active_service) if they are no
		longer needed.
	 */
	auto [ret1, active_adapter] = this->tuner_thread_t::tune_mux(sret, mux, tune_pars);
	if((int)ret1 < 0) {
		release_all(sret.subscription_id);
		return {subscription_id_t::NONE, {}};
	}
	return {ret1, active_adapter};
}

std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
tuner_thread_t::cb_t::subscribe_mux(const subscribe_ret_t& sret,
															const chdb::any_mux_t& mux,
															const tune_pars_t& tune_pars) {
	return this->tuner_thread_t::subscribe_mux(sret, mux, tune_pars);
}


subscription_id_t
tuner_thread_t::cb_t::subscribe_service_for_recording(const subscribe_ret_t& sret,
																				const chdb::any_mux_t& mux, recdb::rec_t& rec,
																				const tune_pars_t& tune_pars) {
	/*In case of failure, release the resources assosciated with this subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	auto [subscription_id, active_adapter] = this->subscribe_mux(sret, mux, tune_pars);
	assert(subscription_id == sret.subscription_id);
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback
	*/

	assert(active_adapter);
	auto recnew = active_adapter->tune_service_for_recording(sret, mux, rec);
	assert(recnew);
	recmgr.update_recording(*recnew);
	recnew->owner = getpid();
	recnew->subscription_id = (int) sret.subscription_id;
	return sret.subscription_id;
}


std::unique_ptr<playback_mpm_t>
tuner_thread_t::cb_t::subscribe_service(const subscribe_ret_t& sret,
																				const chdb::any_mux_t& mux, const chdb::service_t& service,
																				const tune_pars_t& tune_pars) {
	/*In case of failure, release the resources assosciated with this subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	auto [subscription_id, active_adapter] = this->subscribe_mux(sret, mux, tune_pars);

	assert(subscription_id == sret.subscription_id);
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback
	*/

	assert(active_adapter);
	return active_adapter->tune_service_for_viewing(sret, mux, service);
}


/*
	called by
     receiver_thread_t::subscribe_service_
		 receiver_thread_t::cb_t::tune_mux

	-subscribe new mux, and unsubscribe the old one, taking into account they may be the same;
	release active_adapters as needed as a side effect, in three steps
	 1. check if the new mux is the same as the one active on the subscription. In this case
	   restart si processing and/or retune
   2. else check if the the mux is already active on some other subscription. If so, increament use count
	    of that new frontend, and decrease it on the old frontend
   3. else reserve an fe  for the new mux. If so, increament use count
	    of that new frontend, and decrease it on the old frontend; new and old frontend can turn
			out to be the same, in which case use_count does not change
  Then, if the oild fe's use_count has dropped to 0, release the old active adapter

	-Before this call, any active service should be removed


 */
std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
tuner_thread_t::tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
												 const tune_pars_t& tune_pars) {
	assert(sret.subscription_id != subscription_id_t::NONE);
	if(sret.failed && sret.was_subscribed) {
		release_all(sret.subscription_id);
		return {subscription_id_t::NONE, {}};
	}

	auto old_active_adapter = active_adapter_for_subscription(sret.subscription_id);
	/*If the requested mux happens to be already the active mux for this subscription, simply return,
		after restarting si processing or retuning
	*/
	if(sret.sub_to_reuse == sret.subscription_id)  {
		assert(old_active_adapter);
		dtdebug("subscribe " << mux << ": already subscribed to mux");
		if(tune_pars.tune_options.subscription_type == subscription_type_t::NORMAL) {
			add_si(*old_active_adapter, mux, tune_pars.tune_options, sret.subscription_id);
		} else {
			/// during DX-ing and scanning retunes need to be forced
			old_active_adapter->request_retune(mux, tune_pars.tune_options, sret.subscription_id);
		}
		return { sret.subscription_id, old_active_adapter};
	}

	/*We now know that the old unsubscribed mux is different from the new one
		or current_lnb != lnb
		We do not unsubscribe it yet, because that would cause the asssociated frontend
		to be released, which is not optimal if we decide to just reuse this frontend
		and tune it to a different mux
	*/
	if((int)sret.sub_to_reuse >=0)  {
		auto other_active_adapter = active_adapter_for_subscription(sret.sub_to_reuse);
		assert(other_active_adapter);
		/*the following assignment may cause old_active_adapter to redduce in use count,
			and then it will be deactivarted
		*/
		active_adapters[sret.subscription_id] = other_active_adapter;
		add_si(*other_active_adapter, mux, tune_pars.tune_options, sret.subscription_id);
		dtdebug("subscribe " << mux << ": reused activate_adapter from subscription_id=" <<
						(int)sret.sub_to_reuse);
		return {sret.subscription_id, other_active_adapter};
	}

	/*
		we must retune old_active_adapter or create a new active_adapter
	 */
	std::shared_ptr<active_adapter_t> active_adapter;
	int ret{-1};

	if(sret.newaa) {
		auto& aa = *sret.newaa;
		active_adapter = make_active_adapter(aa.fe);
		this->active_adapters[sret.subscription_id] = active_adapter;
		dtdebugx("New active_adapter %p: subscription_id=%d adapter_no=%d", this, sret.subscription_id, (int)active_adapter->get_adapter_no());
		visit_variant(mux,
									[&](const chdb::dvbs_mux_t& mux) {
										ret = this->tune(active_adapter, aa.rf_path, aa.lnb, mux, tune_pars,
																	 sret.subscription_id);
									},
									[&](const chdb::dvbc_mux_t& mux) {
										ret = this->tune(active_adapter, mux, tune_pars, sret.subscription_id);
									},
									[&](const chdb::dvbt_mux_t& mux) {
										ret = this->tune(active_adapter, mux, tune_pars, sret.subscription_id);
									}
			);
		if (ret < 0)
			dterrorx("tune returned %d", ret);
	} else {
		active_adapter = this->active_adapters[sret.subscription_id];
		dtdebugx("Reusing active_adapter %p: subscription_id=%d adapter_no=%d", this, sret.subscription_id, (int)active_adapter->get_adapter_no());
		visit_variant(mux,
									[&](const chdb::dvbs_mux_t& mux) {
										ret = this->tune(active_adapter, active_adapter->current_rf_path(),
																	 active_adapter->current_lnb(), mux, tune_pars,
																	 sret.subscription_id);
									},
									[&](const chdb::dvbc_mux_t& mux) {
										ret = this->tune(active_adapter, mux, tune_pars, sret.subscription_id);
									},
									[&](const chdb::dvbt_mux_t& mux) {
										ret = this->tune(active_adapter, mux, tune_pars, sret.subscription_id);
									}
			);
		if (ret < 0)
			dterrorx("tune returned %d", ret);
	}
#if 0
	auto adapter_no =  active_adapter->get_adapter_no();
	dtdebug("Subscribed: subscription_id=" << (int) sret.subscription_id << " adapter " <<
					adapter_no << " " << mux);
#endif
	//Destructor of old_active_adapter can call deactivate at this point

	return {sret.subscription_id, active_adapter};
}


std::tuple<subscription_id_t, devdb::fe_key_t>
tuner_thread_t::cb_t::tune_mux(
	const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
	const tune_pars_t& tune_pars) {
	auto [subscription_id, active_adapter] =
		this->tuner_thread_t::tune_mux(sret, mux, tune_pars);

	if(!active_adapter.get())  {
		assert(subscription_id == subscription_id_t::NONE);
		return {subscription_id, {}};
	}
	return {subscription_id, active_adapter->fe->dbfe().k};
}

std::shared_ptr<active_adapter_t>
tuner_thread_t::active_adapter_for_subscription(subscription_id_t subscription_id) {
	auto [it, found] = find_in_map(active_adapters, subscription_id);
	return found ? it->second : std::shared_ptr<active_adapter_t>{};
}


void tuner_thread_t::release_all(subscription_id_t subscription_id) {
	if ((int)subscription_id < 0)
		return;
	active_adapters.erase(subscription_id);
}
