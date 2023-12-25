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

/*
	Compute the next time t>=now_ such
	that the hour of the day is a multiple of interval hours after midnight
	interval is in hours

	DST behaviour:
	-when transitioning to winter time, and when now_>= 2 AM and now_ < 3AM, the code
	 will pick the earliest matching time
	-when transitioning to summer time, if intverval==2, and if now_ is before 2AM, then
	 the code will retunrn 3AM, because 2AM does not exist. We could also have opted to return 4AM

 */
static time_t next_hourly(time_t now_, int interval)
{
	interval *= 3600;
	using namespace std::chrono;
	auto tz = current_zone();
	auto nowsys = system_clock::from_time_t(now_);
	auto nowloc = tz->to_local(nowsys);
	auto dayloc = floor<days>(nowloc);
	auto next= dayloc + seconds(((duration_cast<seconds>(nowloc -dayloc).count()+interval-1)/interval)*interval);
	auto t = tz->to_sys(next, std::chrono::choose::earliest);

	//A DST transition to winter time may have set t not to the next possible time, but the 2nd next
	auto t2 = t - seconds(interval);
	if (t2 >= nowsys)
		t= t2;

	return system_clock::to_time_t(t);
}

/*
	Add an integer number of weeks to start_time such that start_time>= now_

 */
static time_t next_weekly(time_t now_, time_t start_time)
{
	using namespace std::chrono;
	auto tz = current_zone();
	auto nowloc = tz->to_local(system_clock::from_time_t(now_));
	auto startloc = tz->to_local(system_clock::from_time_t(start_time));
	auto delta = duration_cast<weeks>(nowloc - startloc);
	auto next = startloc +delta;
	if(next < nowloc)
		next += weeks(1);
	auto nextsys = tz->to_sys(next, std::chrono::choose::earliest);
	return system_clock::to_time_t(nextsys);
}

/*
	Recomputes next_time;
	returns tow bools:
	1. true if anything changed
	2. true if command must be started after this call
 */
std::tuple<int, int> update_command(devdb::scan_command_t& cmd, system_time_t now_)
{
	using namespace devdb;
	auto now = system_clock_t::to_time_t(now_);
	bool ret{false};
	bool must_start{false};
	if(cmd.next_time > now)
		return {false, false};
	switch(cmd.run_type) {
	case run_type_t::NEVER:
		ret |= set_member(cmd, next_time, -1);
		ret |= set_member(cmd, run_status,run_status_t::NONE);
		break;
	case run_type_t::ONCE:
		if(cmd.next_time < now) {
			if(cmd.run_status == run_status_t::PENDING ||
				 cmd.run_status == run_status_t::NONE) {
				dtdebugf("Detected skipped command {}", cmd.id);
				ret |= set_member(cmd, run_status,
													(cmd.next_time - now < cmd.max_duration) ? run_status_t::NONE:
													run_status_t::RUNNING);
				if(!must_start)
					ret |= set_member(cmd, run_result, run_result_t::SKIPPED);

			}

	}
		break;
	case run_type_t::HOURLY: {
		auto next_time=next_hourly(now, cmd.interval);
		ret |= set_member(cmd, next_time, next_time);
	}
		break;
	case run_type_t::DAILY:
		break;
	case run_type_t::WEEKLY: {
		auto next_time=next_weekly(now, cmd.start_time);
		ret |= set_member(cmd, next_time, next_time);
	}
		break;
	case run_type_t::MONTHLY:

		break;
	}

	return {ret, must_start};
}

/*
	https://fzco.wackymango.net/date-time-manipulations/
	https://stackoverflow.com/questions/16773285/how-to-convert-stdchronotime-point-to-stdtm-without-using-time-t
	https://lunarwatcher.github.io/til/cpp/incomplete-time-parsing.html
	https://stackoverflow.com/questions/52238978/creating-a-stdchronotime-point-from-a-calendar-date-known-at-compile-time

*/
