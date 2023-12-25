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

#include "neumo.h"
#include "receiver.h"
#include "subscriber.h"
#include "scan.h"
#include "spectrum.h"
#include "util/dtutil.h"



/*
	For each command in progress, check if it should be stopped
	and stop if needed
*/
void receiver_thread_t::stop_commands(db_txn& devdb_rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace devdb;
	auto c = scan_command_t::find_by_run_status_next_time
		(devdb_rtxn, run_status_t::RUNNING, find_type_t::find_geq, scan_command_t::partial_keys_t::run_status);
	for (auto cmd : c.range()) {
		if (now - cmd.run_time >= cmd.max_duration) {
			next_command_event_time = std::min(next_command_event_time, cmd.run_time + cmd.max_duration);
#ifdef TODO
			stop_command(rec);
#endif
		}
	}
}

void receiver_thread_t::start_commands(db_txn& rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace devdb;

	auto cc = scan_command_t::find_by_run_status_next_time(rtxn, run_status_t::PENDING, find_type_t::find_geq,
																												 scan_command_t::partial_keys_t::run_status);
	time_t last_start_time = 0;
	for (auto cmd : cc.range()) {
		dterrorf("CANDIDATE CMD: {}", cmd.id);
		if (cmd.next_time <= now) {
			if (cmd.next_time + cmd.interval <= now) {
				dtdebugf("TOO LATE; skip CMD: {}", cmd.id);
				auto wtxn = receiver.devdb.wtxn();
				cmd.mtime = now;
				put_record(wtxn, cmd);
				wtxn.commit();
			} else {
#ifdef TODO
				{
					auto txn = receiver.epgdb.rtxn();
					epg_key_t epg_key = rec.epg.k;
					auto ec =
						epgdb::epg_record_t::find_by_key(txn, epg_key, find_type_t::find_eq, epg_record_t::partial_keys_t::all);
					if (ec.is_valid())
						rec.epg = ec.current();
				}
				{
					auto txn = receiver.chdb.rtxn();
					// rec.service = epgdb::service_for_epg_record(txn, epg_record);
					auto ec = chdb::service_t::find_by_key(txn, rec.service.k.mux, rec.service.k.service_id);
					if (ec.is_valid())
						rec.service = ec.current();
				}

				rec.epg.rec_status = rec_status_t::IN_PROGRESS;
				rec.owner = getpid();
				subscribe_ret_t sret{subscription_id_t::NONE, false/*failed*/};
				rec.subscription_id = (int)sret.subscription_id;
				update_recording(rec); // must be done now to avoid starting the same recording twice
				receiver.start_recording(rec);
#endif
			}
		} else {
			next_command_event_time =
				std::min(next_command_event_time, cmd.next_time);
			// command is in the future; nothing todo
			if (cmd.next_time > now)
				break; //@todo we can break here, because records should be ordered according to next_time
			else {
				last_start_time = cmd.next_time;
			}
		}
	}
}


int receiver_thread_t::housekeeping(system_time_t now) {
	auto prefix =fmt::format("-RECEIVER-HOUSEKEEPING");
	log4cxx::NDC ndc(prefix.c_str());
	if (system_clock_t::to_time_t(now) > next_command_event_time) {
		auto parent_txn = receiver.devdb.rtxn();
		// check for any command to stop
		stop_commands(parent_txn, now);
		// check for any recordings to start
		start_commands(parent_txn, now);
	}
	return 0;
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
	subscription_id_t subscription_id, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbt_mux_t& mux,
	subscription_id_t subscription_id, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);

template
subscription_id_t
receiver_thread_t::subscribe_mux(
	std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::dvbs_mux_t& mux,
	subscription_id_t subscription_id, subscription_options_t tune_options,
	const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);


template subscription_id_t
receiver_t::scan_muxes<chdb::dvbs_mux_t>(ss::vector_<chdb::dvbs_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 subscriber_t& subscriber);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 subscriber_t& subscriber);

template subscription_id_t
receiver_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																				 const subscription_options_t& tune_options,
																				 subscriber_t& subscriber);


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
																											const subscription_options_t& tune_options,
																											subscription_id_t subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbc_mux_t>(ss::vector_<chdb::dvbc_mux_t>& muxes,
																											const subscription_options_t& tune_options,
																											subscription_id_t subscription_id);

template subscription_id_t
receiver_thread_t::cb_t::scan_muxes<chdb::dvbt_mux_t>(ss::vector_<chdb::dvbt_mux_t>& muxes,
																											const subscription_options_t& tune_options,
																											subscription_id_t subscription_id);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbs_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);


template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbc_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);
template subscription_id_t
receiver_thread_t::scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbt_mux_t>& muxes,
															const subscription_options_t& tune_options,
															int max_num_subscriptions,
															subscription_id_t subscription_id);
