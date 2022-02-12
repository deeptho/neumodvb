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

#include "adapter.h"
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

std::shared_ptr<fe_monitor_thread_t> fe_monitor_thread_t::make(receiver_t& receiver, dvb_frontend_t* fe) {
	auto t = fe->ts.writeAccess();
	assert(t->fefd < 0);
	auto p = std::make_shared<fe_monitor_thread_t>(receiver, fe);
	fe->set_monitor_thread(p);
	fe->open_device(*t);
	dtdebugx("starting frontend_monitor %p: fefd=%d\n", fe, t->fefd);
	p->epoll_add_fd(t->fefd,
									EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET); // will be used to monitor the frontend edge triggered!
	p->start_running();
	return p;
}

void fe_monitor_thread_t::monitor_signal() {
	auto m = fe->ts.readAccess()->tune_mode;
	if (m != tune_mode_t::NORMAL)
		return;

	bool get_constellation{true};

	chdb::signal_info_t info;
	fe->get_signal_info(info, get_constellation);
	bool verbose = false;
	if (verbose) {
		dtdebug("------------------------------------------------------");
	}

	if (verbose) {
		auto &e = info.last_stat();
		dtdebug("Signal strength: " << std::fixed << std::setprecision(1) << (e.signal_strength * 1e-3) << "dB"
						<< " CNR: " << (e.snr * 1e-3) << "dB");
	}
	dttime_init();
	receiver.notify_signal_info(info);
	dttime(200);
	{
		auto ts = fe->signal_monitor.writeAccess();
		auto &signal_monitor = *ts;
		signal_monitor.update_stat(receiver, info.stat);
	}
}

chdb::signal_info_t fe_monitor_thread_t::cb_t::get_signal_info() {
	chdb::signal_info_t signal_info;
	fe->get_signal_info(signal_info, false);
	return signal_info;
}

void fe_monitor_thread_t::handle_frontend_event() {
	struct dvb_frontend_event event {};
	auto fefd = fe->ts.readAccess()->fefd;
	int r = ioctl(fefd, FE_GET_EVENT, &event);
	if (r < 0) {
		dtdebugx("FE_GET_EVENT stat=0x%x err=%s\n", event.status, strerror(errno));
		return;
	}

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
	dtdebugx("SIGNAL: signal=%d carrier=%d viterbi=%d sync=%d lock=%d timedout=%d\n", signal, carrier, viterbi, has_sync,
					 has_lock, timedout);
	bool done = signal && carrier && viterbi && has_sync && has_lock;

	switch (fe->ts.readAccess()->tune_mode) {
	case tune_mode_t::SPECTRUM: {
		if (!done) {
			return;
		}
		ss::string<128> spectrum_path = receiver.options.readAccess()->spectrum_path.c_str();
		auto result = fe->get_spectrum(spectrum_path);
		if (result) {
			auto txn = receiver.statdb.wtxn();
			auto& spectrum = *result;
			auto c = statdb::spectrum_t::find_by_key(txn, spectrum.k);
			put_record(txn, spectrum);
			txn.commit();
			receiver.notify_spectrum_scan(spectrum);
		}
	}

		break;
	case tune_mode_t::IDLE:
	case tune_mode_t::NORMAL:
	case tune_mode_t::SCAN_BLIND:
	case tune_mode_t::POSITIONER_CONTROL:
	case tune_mode_t::UNCHANGED:
		fe->set_lock_status(event.status);
		if (fe->api_type == api_type_t::NEUMO)
			monitor_signal();
		break;
	}
}

int fe_monitor_thread_t::run() {
	ss::string<64> fe_name;
	fe_name.sprintf("fe %d.%d", (int)fe->adapter->adapter_no, (int)fe->frontend_no);
	set_name(fe_name.c_str());
	log4cxx::MDC::put("thread_name", fe_name.c_str());

	dtdebugx("frontend_monitor run: %p: fefd=%d\n", fe, fe->ts.readAccess()->fefd);
	auto save = shared_from_this(); // prevent ourself from being deleted until thread exits;

	if (fe->api_type != api_type_t::NEUMO)
		timer_start(1); // NEUMO api activates heartbeat mode
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorx("error in poll: %s", strerror(errno));
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
			} else if (is_timer_fd(evt)) { //only for non-neumo mode
				monitor_signal();
			}
		}
	}
exit_:
	dtdebugx("frontend_monitor end: %p: fefd=%d\n", fe, fe->ts.readAccess()->fefd);
	fe->close_device(*fe->ts.writeAccess());
	save.reset();
	{
		auto ts = fe->signal_monitor.writeAccess();
		auto &signal_monitor = *ts;
		signal_monitor.end_stat(receiver);
	}

	return 0;
}

void signal_monitor_t::update_stat(receiver_t& receiver, const statdb::signal_stat_t& update) {
	//it is possible that max_key changes without tuning
	bool save_old =  stat.stats.size()>0 && (stat.k.mux != update.k.mux || stat.k.lnb != update.k.lnb);
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
