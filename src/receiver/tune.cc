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
#include "active_adapter.h"

#include <stdio.h>
#include <stdlib.h>
//#include <ctype.h>
#include <sys/time.h>
//#include <sys/poll.h>
#include <sys/epoll.h>
//#include <sys/stat.h>
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
	if (festatus & FE_REINIT)
		dtdebug("     FE_REINIT : frontend was reinitialized");
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
		chdb::update_mux(txn, mux, now, m::flags{m::ALL & ~m::EPG_TYPES});
		txn.commit();
	}
	return 0;
}

int tuner_thread_t::cb_t::update_service(const chdb::service_t& service) {
	auto txn = receiver.chdb.wtxn();
	put_record(txn, service);
	txn.commit();
	return 0;
}

int tuner_thread_t::cb_t::lnb_scan(std::shared_ptr<active_adapter_t> active_adapter, const chdb::lnb_t& lnb,
																	 tune_options_t tune_options) {
	// check_thread();
	dtdebugx("lnb scan action");
	auto fefd = active_adapter->current_fe->ts.readAccess()->fefd;
	this->active_adapters[frontend_fd_t(fefd)] = active_adapter;
	return active_adapter->lnb_scan(lnb, tune_options);
}

int tuner_thread_t::cb_t::tune(std::shared_ptr<active_adapter_t> active_adapter, const chdb::lnb_t& lnb,
															 const chdb::dvbs_mux_t& mux_, tune_options_t tune_options) {
	// check_thread();
	dtdebugx("tune mux action");
	chdb::dvbs_mux_t mux{mux_};
	if(mux.c.scan_status == scan_status_t::PENDING || mux.c.scan_status == scan_status_t::ACTIVE) {
		/*
			The new scan status must be written to the database now.
			Otherwise there may be problems on muxes which fail to scan: their status would remain
			pending, and whne parallel tuners are in use, the second tuner might decide to scan
			the mux again
		*/
		auto wtxn = receiver.chdb.wtxn();
		namespace m = chdb::update_mux_preserve_t;
		mux.c.scan_status = scan_status_t::ACTIVE;
		chdb::update_mux(wtxn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS});
		wtxn.commit();
		assert(mux.c.scan_status == scan_status_t::ACTIVE);
	}
	auto fefd = active_adapter->current_fe->ts.readAccess()->fefd;
	this->active_adapters[frontend_fd_t(fefd)] = active_adapter;
	bool user_requested = true;
	return active_adapter->tune(lnb, mux, tune_options, user_requested);
}

template <typename _mux_t>
int tuner_thread_t::cb_t::tune(std::shared_ptr<active_adapter_t> active_adapter, const _mux_t& mux_,
															 tune_options_t tune_options) {
	_mux_t mux{mux_};
	assert( mux.c.scan_status != scan_status_t::ACTIVE);
	if(mux.c.scan_status == scan_status_t::PENDING) {
		/*
			The new scan status must be written to the database now.
			Otherwise there may be problems on muxes which fail to scan: their status would remain
			pending, and whne parallel tuners are in use, the second tuner might decide to scan
			the mux again
		*/
		auto wtxn = receiver.chdb.wtxn();
		namespace m = chdb::update_mux_preserve_t;
		mux.c.scan_status = scan_status_t::ACTIVE;
		chdb::update_mux(wtxn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS});
		wtxn.commit();
		assert(mux.c.scan_status == scan_status_t::ACTIVE);
	}
	dtdebugx("tune mux action");
	auto fefd = active_adapter->current_fe->ts.readAccess()->fefd;
	this->active_adapters[frontend_fd_t(fefd)] = active_adapter;
	bool user_requested = true;
	return active_adapter->tune(mux, tune_options, user_requested);
}

template int tuner_thread_t::cb_t::tune<chdb::dvbc_mux_t>(std::shared_ptr<active_adapter_t> active_adapter,
																													const chdb::dvbc_mux_t& mux, tune_options_t tune_options);

template int tuner_thread_t::cb_t::tune<chdb::dvbt_mux_t>(std::shared_ptr<active_adapter_t> active_adapter,
																													const chdb::dvbt_mux_t& mux, tune_options_t tune_options);

int tuner_thread_t::cb_t::remove_service(active_adapter_t& active_adapter, active_service_t& channel) {
	dtdebugx("tune deactivate_channel action: %s", channel.get_current_service().name.c_str());
	recmgr.remove_live_buffer(channel);
	return active_adapter.remove_service(channel);
}

int tuner_thread_t::cb_t::add_service(active_adapter_t& tuner, active_service_t& channel) {
	dtdebugx("tune add ch action");
	recmgr.add_live_buffer(channel);
	return tuner.add_service(channel);
}

int tuner_thread_t::exit() {
	dtdebugx("tuner exit");
	for (auto& it : active_adapters) {
		auto& active_adapter = *it.second;
		active_adapter.remove_all_services(recmgr);
		ss::string<128> prefix;
		prefix << "SI" << active_adapter.get_adapter_no() << "-STOP";
		log4cxx::NDC ndc(prefix.c_str());
		active_adapter.deactivate();
	}
	return 0;
}

int tuner_thread_t::cb_t::remove_active_adapter(active_adapter_t& active_adapter) {
	auto [it, found] = find_in_map_if(this->active_adapters, [&active_adapter](const auto& x) {
			auto [fd_, ptr] = x;
			return ptr.get() == &active_adapter;
		});
	if (!found) {
		dterrorx("Request to remove active_adapter");
		return -1;
	}

	assert(&active_adapter == it->second.get());
	dtdebug("calling deactivate");
	active_adapter.deactivate();
	dtdebug("calling deactivate done");
	active_adapters.erase(it);
	return 0;
}

active_adapter_t* tuner_thread_t::find_active_adapter_for_event(const epoll_event* event) const {
	auto it = active_adapters.find(frontend_fd_t(event->data.fd));
	if (it == active_adapters.end())
		return NULL;
	return it->second.get();
}

int tuner_thread_t::cb_t::on_lnb_lof_offset_update(int fefd, const ss::vector<int32_t, 2>& lof_offsets) {
	auto it = active_adapters.find(frontend_fd_t(fefd));
	if (it == active_adapters.end())
		return -1;

	auto& active_adapter = *it->second;
	active_adapter.update_lof(lof_offsets);
	return 0;
}

/*
	called whenever  a new or updated epg record is found
	also sets recording status on epg_record
*/

void tuner_thread_t::on_epg_update(db_txn& txnepg, system_time_t now,
																	 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																																		to true or false*/)
{
	auto timeshift_duration = receiver.options.readAccess()->timeshift_duration;

	for (auto& it : active_adapters) {
		auto& tuner = *it.second;
		for (auto& it : tuner.active_services) {
			auto& [current_pmt_pid, tu] = it;
			auto& [pmt_pid, active_servicep] = tu;
			if (epg_record.k.start_time > system_clock_t::to_time_t(now + timeshift_duration))
				break; // record too far in the future
			auto service = active_servicep->get_current_service();
			if (epg_record.k.service.sat_pos == service.k.mux.sat_pos && epg_record.k.service.ts_id == service.k.mux.ts_id &&
					epg_record.k.service.network_id == service.k.mux.network_id &&
					epg_record.k.service.service_id == service.k.service_id) {
				// send request to service_thread
				auto& active_service = *active_servicep;
				active_service.service_thread.push_task([now, &active_service, epg_record]() { // epg_record passed by value
					cb(active_service.service_thread).on_epg_update(now, epg_record);
					return 0;
				});
			}
		}
	}
	return recmgr.on_epg_update(txnepg, epg_record);
}

void tuner_thread_t::livebuffer_db_update_(system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	// constexpr int tolerance = 15*60; //we look 15 minutes in to the future (and past
	auto txn = receiver.recdb.wtxn();
	// first update modification time of live_service records
	for (auto& it : active_adapters) {
		auto& tuner = *it.second;
		for (auto& it : tuner.active_services) {
			auto& [current_pmt_pid, tu] = it;
			auto& [pmt_pid, active_servicep] = tu;
			auto& active_service = *active_servicep;
			auto creation_time = system_clock_t::to_time_t(active_service.get_creation_time());
			auto c = recdb::live_service_t::find_by_key(txn, creation_time, tuner.get_adapter_no(), find_type_t::find_eq,
																									recdb::live_service_t::partial_keys_t::all);
			if (c.is_valid()) {
				auto live_service = c.current();
				live_service.update_time = now;
				dtdebug("Updating live service adap=" << tuner.get_adapter_no() << " create=" << creation_time
								<< " update=" << now);
				recdb::put_record_at_key(c, c.current_serialized_primary_key(), live_service);
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
				dttime_init();
				{
					// time to do some housekeeping (check tuner)
					dttime_init();
					for (auto& it : active_adapters) {
						auto& active_adapter = *it.second;
						if (!active_adapter.current_fe) {
							dterror_nice("Implementation error\n");
							continue;
						}
						ss::string<128> prefix;
						prefix << "TUN" << active_adapter.get_adapter_no() << "-MON";
						log4cxx::NDC ndc(prefix.c_str());
						dttime_init();
						active_adapter.monitor();
						dttime(200);
					}
					dttime(150);
					clean_dbs(now, false);
					dttime(100);
					recmgr.housekeeping(now);
					dttime(100);
					livebuffer_db_update.run([this](system_time_t now) { livebuffer_db_update_(now); }, now);
					dttime(100);
				}
				auto delay = dttime(-1);
				if (delay >= 500)
					dterrorx("timer cycle took too long delay=%d", delay);
			} else {
				// this must be a si event
				for (auto& it : active_adapters) {
					auto& active_adapter = *it.second;
					// active_adapter will return if fd is for other active_adapter
					// The following call returns true if fd was for this active_adapter
					ss::string<128> prefix;
					prefix << "TUN" << active_adapter.get_adapter_no() << "-SI";
					log4cxx::NDC ndc(prefix.c_str());
					dttime_init();
					if (evt->events & EPOLLERR) {
						dterrorx("ERROR in epoll event for fd=%d", evt->data.fd);
					} else if (evt->events & EPOLLIN) {
						if (active_adapter.read_and_process_data_for_fd(evt->data.fd)) {
							// printf("processed using new si interface\n");
							auto delay = dttime(300);
							if (delay >= 200)
								dterrorx("si cycle took too long delay=%d", delay);
							break;
						}
					}
				}
			}
		}
	}
	return 0;
}

int tuner_thread_t::cb_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record,
																					 bool insert, bool remove) {
	return recmgr.toggle_recording(service, epg_record, insert, remove);
}

void tuner_thread_t::cb_t::update_recording(const recdb::rec_t& rec) {
	recmgr.update_recording(rec);
}

chdb::lnb_t active_adapter_reservation_t::lnb() const {
	assert(active_adapter);
	return active_adapter->get_lnb();
}

chdb::any_mux_t active_adapter_reservation_t::mux() const {
	assert(active_adapter);
	return active_adapter->current_mux();
}

template <typename mux_t> bool active_adapter_reservation_t::is_tuned_to(const mux_t& mux) const {
	assert(active_adapter);
	return active_adapter->is_tuned_to(mux);
}

template <> bool active_adapter_reservation_t::is_tuned_to(const dvbs_mux_t& mux) const;
template <> bool active_adapter_reservation_t::is_tuned_to(const dvbc_mux_t& mux) const;
template <> bool active_adapter_reservation_t::is_tuned_to(const dvbt_mux_t& mux) const;
template <> bool active_adapter_reservation_t::is_tuned_to(const chdb::any_mux_t& mux) const;

int active_adapter_reservation_t::release() {
	auto ret = use_count.unregister_subscription();
#if 0 //leads to race condition
	if (ret == 0) {
		auto* fe = active_adapter->current_fe.get();
		auto fefd = fe->ts.readAccess()->fefd;
		dtdebugx("Release fe_fd=%d", fefd);
		fe->adapter->release_fe();
	}
#endif
	return ret;
}

int active_adapter_reservation_t::reserve(const chdb::lnb_t& lnb, int sat_pos) {
	auto* fe = active_adapter->current_fe.get();
	fe->adapter->reserve_fe(fe, lnb, sat_pos);
	return check_consistency(use_count.register_subscription());
}

int active_adapter_reservation_t::reserve(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
	auto* fe = active_adapter->current_fe.get();
	fe->adapter->reserve_fe(fe, lnb, mux);
	return check_consistency(use_count.register_subscription());
}

int active_adapter_reservation_t::reserve(const chdb::dvbc_mux_t& mux) {
	auto* fe = active_adapter->current_fe.get();
	fe->adapter->reserve_fe(fe, mux);
	return check_consistency(use_count.register_subscription());
}

int active_adapter_reservation_t::reserve(const chdb::dvbt_mux_t& mux) {
	auto* fe = active_adapter->current_fe.get();
	fe->adapter->reserve_fe(fe, mux);
	return check_consistency(use_count.register_subscription());
}

int active_adapter_reservation_t::reserve_current() {
	return check_consistency(use_count.register_subscription());
}

tuner_thread_t::tuner_thread_t(receiver_t& receiver_)
	: task_queue_t(thread_group_t::tuner)
	, receiver(receiver_)
	, livebuffer_db_update(60)
	, recmgr(receiver_) {
}

tuner_thread_t::~tuner_thread_t() {
}

int tuner_thread_t::set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options) {
	auto tune_mode = (tune_options.tune_mode == tune_mode_t::UNCHANGED) ? active_adapter.tune_options.tune_mode
		: tune_options.tune_mode;
	active_adapter.tune_options = tune_options;
	active_adapter.tune_options.tune_mode = tune_mode;
	active_adapter.retune_count = 0;
	return 0;
}

int tuner_thread_t::request_retune(active_adapter_t& active_adapter) {
	active_adapter.restart_tune();
	return 0;
}

int tuner_thread_t::cb_t::set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options) {
	return this->tuner_thread_t::set_tune_options(active_adapter, tune_options);
}

int tuner_thread_t::cb_t::request_retune(active_adapter_t& active_adapter) {
	return this->tuner_thread_t::request_retune(active_adapter);
}

int tuner_thread_t::cb_t::positioner_cmd(std::shared_ptr<active_adapter_t> active_adapter, chdb::positioner_cmd_t cmd,
																				 int par) {
	return active_adapter->positioner_cmd(cmd, par);
}

int tuner_thread_t::cb_t::update_current_lnb(active_adapter_t& active_adapter, const lnb_t& lnb) {
	active_adapter.set_current_lnb(lnb);
	return 0;
}
