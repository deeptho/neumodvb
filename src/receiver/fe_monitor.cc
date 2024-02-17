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

#include "devmanager.h"
#include "neumodb/cursors.h"
#include "neumodb/statdb/statdb_extra.h"
#include "receiver.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/neumovariant.h"
#include <dirent.h>
#include <errno.h>
#include <functional>
#include <iomanip>
#include <map>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

std::shared_ptr<fe_monitor_thread_t> fe_monitor_thread_t::make(receiver_t& receiver,
																															 std::shared_ptr<dvb_frontend_t>& fe) {
	auto fefd = fe->open_device();
	auto p = std::make_shared<fe_monitor_thread_t>(receiver, fe);
	dtdebugf("starting frontend_monitor {:p}: fefd={:d}\n", fmt::ptr(fe.get()), fefd);
	p->epoll_add_fd(fefd,
									EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET); // will be used to monitor the frontend edge triggered!
	p->start_running();
	return p;
}

void fe_monitor_thread_t::update_lock_status_and_signal_info(fe_status_t fe_status) {

	bool get_constellation{true};
	auto info_ = fe->update_lock_status_and_signal_info(fe_status, get_constellation);
	if(!info_)
		return;
	auto& info = *info_;
	bool verbose = false;
	if (verbose) {
		dtdebugf("------------------------------------------------------");
	}

	if (verbose) {
		auto &e = info.last_stat();
		dtdebugf("Signal strength: {:.1f}dB  CNR:  {:1.f}dN",
						 (e.signal_strength * 1e-3), (e.snr * 1e-3));
	}
	dttime_init();
	receiver.on_signal_info(info, fe->get_subscription_ids());
	dttime(200);
	{
		auto w = fe->signal_monitor.writeAccess();
		auto &signal_monitor = *w;
		signal_monitor.update_stat(receiver, info);
	}
}

void fe_monitor_thread_t::handle_frontend_event() {
	using namespace devdb;

	struct dvb_frontend_event event {};
	auto fefd = fe->ts.readAccess()->fefd;
	int r = ioctl(fefd, FE_GET_EVENT, &event);
	if (r < 0) {
		dtdebugf("FE_GET_EVENT stat=0x{:x} errno={:d} err={:s}\n", (int)event.status, errno, strerror(errno));
		return;
	}

	if(is_paused) //we receive the event but do not actually process it
		return;
	/**
	 * enum fe_status - enumerates the possible frontend status
	 * @FE_HAS_SIGNAL:	found something above the noise level
	 * @FE_HAS_CARRIER:	found a DVB signal
	 * @FE_HAS_VITERBI:	FEC is stable
	 * @FE_HAS_SYNC:	found sync bytes
	 * @FE_HAS_LOCK:	everything's working
	 * @FE_TIMEDOUT:	no lock within the last ~2 seconds
	 * @FE_REINIT:		frontend was reinitialized, application is recommended
	 *			to reset DiSEqC, tone and parameters
	 */

	bool signal = event.status & FE_HAS_SIGNAL;
	bool carrier = event.status & FE_HAS_CARRIER;
	bool viterbi = event.status & FE_HAS_VITERBI;
	bool has_sync = event.status & FE_HAS_SYNC;
	bool has_lock = event.status & FE_HAS_LOCK;
	bool timedout = event.status & FE_TIMEDOUT;
#pragma unused(timedout)
#if 0
	dtdebugf("SIGNAL: signal={:d} carrier={:d} viterbi={:d} sync={:d} lock={:d} timedout={:d}\n", signal, carrier, viterbi, has_sync,
					 has_lock, timedout);
#endif
	bool done = signal && carrier && viterbi && has_sync && has_lock;

	switch (fe->ts.readAccess()->tune_mode) {
	case tune_mode_t::SPECTRUM: {
		if (!done) {
			return;
		}
		ss::string<128> spectrum_path = receiver.options.readAccess()->spectrum_path.c_str();
		auto scan = fe->get_spectrum(spectrum_path);
		if (scan && scan->spectrum && fe->ts.readAccess()->tune_options.spectrum_scan_options.save_spectrum) {
			auto txn = receiver.statdb.wtxn();
			auto& spectrum = *scan->spectrum;
			auto c = statdb::spectrum_t::find_by_key(txn, spectrum.k);
			put_record(txn, spectrum);
			txn.commit();
		}
		assert(scan);
		auto finished_fe = fe->dbfe();
		assert (scan->start_freq !=0);
		assert (scan->end_freq !=0);

		receiver.on_spectrum_scan_end(finished_fe, *scan, fe->get_subscription_ids());
	}

		break;
	case tune_mode_t::IDLE:
	case tune_mode_t::NORMAL:
	case tune_mode_t::BLIND:
	case tune_mode_t::POSITIONER_CONTROL:
	case tune_mode_t::UNCHANGED:
		if (fe->api_type == api_type_t::NEUMO)
			update_lock_status_and_signal_info(event.status);
		else
			fe->set_lock_status(event.status);
		break;
	}
}

int fe_monitor_thread_t::cb_t::pause() {
	ss::string<64> fe_name;
	fe_name.format("fe {:d}.{:d}", (int)fe->adapter_no, (int)fe->frontend_no);
	set_name(fe_name.c_str());
	log4cxx::MDC::put("thread_name", fe_name.c_str());
	this->is_paused = true;
	dtdebugf("frontend_monitor pause: {:p}: fefd={:d}", fmt::ptr(fe.get()), fe->ts.readAccess()->fefd);

	return 0;
}


int fe_monitor_thread_t::cb_t::unpause() {
	ss::string<64> fe_name;
	fe_name.format("fe {:d}.{:d}", (int)fe->adapter_no, (int)fe->frontend_no);
	set_name(fe_name.c_str());
	log4cxx::MDC::put("thread_name", fe_name.c_str());
	this->is_paused = false;
	dtdebugf("frontend_monitor unpause: {:p}: fefd={:d}\n", fmt::ptr(fe.get()), fe->ts.readAccess()->fefd);

	return 0;
}


int fe_monitor_thread_t::run() {
	ss::string<64> fe_name;
	fe_name.format("fe {:d}.{:d}", (int)fe->adapter_no, (int)fe->frontend_no);
	set_name(fe_name.c_str());
	log4cxx::MDC::put("thread_name", fe_name.c_str());
	dtdebugf("frontend_monitor run: {:p}: fefd={:d}\n", fmt::ptr(fe.get()), fe->ts.readAccess()->fefd);
	auto save = shared_from_this(); // prevent ourself from being deleted until thread exits;

	if (fe->api_type != api_type_t::NEUMO)
		timer_start(1); // NEUMO api activates heartbeat mode
	for (;;) {

		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorf("error in poll: {:s}", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				if (run_tasks(now) < 0) {
					goto exit_;
				}
			} else if (fe->is_fefd(evt->data.fd)) {
				handle_frontend_event();
			} else if (is_timer_fd(evt)) {
        /*only for non-neumo mode. In this case events are only reported when a lock status
					bit changes. In neumo mode, events are sent periodically.
				*/
				if(!is_paused && fe->api_type != api_type_t::NEUMO) {
					auto state = fe->get_lock_status();
					update_lock_status_and_signal_info(state.fe_status);
				}
			}
		}
	}
exit_:
	save.reset();
	dtdebugf("frontend_monitor end: {:p}: fefd={:d}\n", fmt::ptr(fe.get()), fe->ts.readAccess()->fefd);
	return 0;
}

int fe_monitor_thread_t::exit() {

	fe->close_device();
	dtdebugf("frontend_monitor end: {:p}: closed device\n", fmt::ptr(fe.get()));
	auto ts = fe->signal_monitor.writeAccess();
	auto &signal_monitor = *ts;
	signal_monitor.end_stat(receiver);
		return 0;
}

void signal_monitor_t::update_stat(receiver_t& receiver, const signal_info_t& info) {
	auto& update = info.stat;
	bool reset = tune_count!=info.tune_count;
	tune_count = info.tune_count;
	bool save_old =  stat.stats.size()>0 && reset;
	if (save_old ) {
		auto wtxn = receiver.statdb.wtxn();
		if(stat.stats.size() > 0 ) {
			assert (stat.k.live);
			delete_record(wtxn, stat); //key will change, so we remove the record at the old key
			stat.k.live = false;
			put_record(wtxn, stat);
		} else {
		}
		stat = update;
		assert (stat.k.live);
		put_record(wtxn, stat);
		wtxn.commit();
		return;
	}

	auto t = update.k.time;
	assert(t > 0);
	assert(stat.k.live);
	assert(update.stats.size()>0);

	int idx = (t - stat.k.time)/300; //new record every 5 minutes
	auto v = update.stats[update.stats.size()-1];
	if (stat.stats.size() == 0 ) {
		stat = update; //copy everything, including key
		snr_sum = v.snr;
		signal_strength_sum = v.signal_strength;
		ber_sum = v.ber;
		stats_count=1;
	} else if (idx + 1 > stat.stats.size()) {
		//replace current slot with statistics
		auto& w = stat.stats[stat.stats.size()-1];
		w.snr = snr_sum / stats_count;
		w.signal_strength = signal_strength_sum / stats_count;
		w.ber = ber_sum / stats_count;

		//make new slot for the live measurement
		stat.stats.push_back(v);
		snr_sum = v.snr;
		signal_strength_sum = v.signal_strength;
		ber_sum = v.ber;
		stats_count=1;
	} else {
		snr_sum += v.snr;
		signal_strength_sum += v.signal_strength;
		ber_sum += v.ber;
		stats_count++;

		//update live measurement
		auto& w = stat.stats[stat.stats.size()-1];
		w = v;
	}
	auto wtxn = receiver.statdb.wtxn();
	assert (stat.k.live);
	put_record(wtxn, stat);
	wtxn.commit();
}

void signal_monitor_t::end_stat(receiver_t& receiver) {
	//it is possible that max_key changes without tuning
	auto wtxn = receiver.statdb.wtxn();
	if(stat.stats.size() > 0 ) {
		assert (stat.k.live);
		delete_record(wtxn, stat); //key will change, so we remove the record at the old key
		stat.k.live = false;
		put_record(wtxn, stat);
		stat.stats.clear();
		stat.k = {};
	}
	wtxn.commit();
}

/*
	TODO:  in current code, upon tune, active_adapter first sends DTV_STOP to put frontend in IDLE
	mode -- immediately --. It then starts tuning, but the calls involved race with the parallel
	calls made by fe_monitor: almost all ioctls lock the semaphore "fepriv->sem", which prevents concurrent
	access from any thread using any file descriptor

	The new code should run tuning and monitoring ioctls only from fe_monitor thread.
	tuner_thread should tune as follows:
	1. set a flag, or run a task in fe_monitor asking to go to idle mode prior to tuning
	2. call DTV_STOP ioctl, so that frontend stops its internal tuen tasks and will therefore
	   be able to execute any ioctl on which an fe_monitor thread might be blocked
	3. run the actual tune_task in the fe_monitor thread
Note that it is essential that the DTV_STOP happens before tuning. Step 1 force fe_monitor thread
to wait for the DTV_STOP call to be actually made

Another approach could be the following:
	1. ask fe_monitor ti run a task composed of two parts: a) wait to be notified in DTV_STOP.
	   This notification has to come from tune_thread
	2. call DTV_STOP ioctl and a message that tuning can proceed

Ideally the kernel should also set a flag showing "idle mode", but this is not the case

 */
