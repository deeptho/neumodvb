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
	that the hour of the day is a multiple of interval timeperiods after start_time,
	where this interval remains syncrhonized with the local time of day (i.e., the
	multiple is adjusted slightly to achieve the same start hour)

	DST behaviour:
	-when transitioning to winter time, and when now_>= 2 AM and now_ < 3AM, the code
	 will pick the earliest matching time with the correct hour of day. The  main exception
	 is when 2 AM of DST changeover happens to be the desired time if no DST chnage woyld occur
	 In that case, the code runs at 3AM post-DST which equals 2AM pre-DST
	-when transitioning to summer time, if intverval==2, and if now_ is before 2AM, then
	 the code will return 3AM, because 2AM does not exist. We could also have opted to return 4AM
 */
template<typename period_t>
requires (! is_same_type_v<std::chrono::months, period_t>)
time_t next_periodic(time_t now_, time_t start_time, int interval=2)
{
	now_ = std::max(start_time, now_);
	using namespace std::chrono;
	auto tz = current_zone();
	auto nowsys = system_clock::from_time_t(now_);
	auto nowloc = tz->to_local(nowsys);
	auto startloc = tz->to_local(system_clock::from_time_t(start_time));
	auto delta = ((duration_cast<period_t>(nowloc - startloc).count()+interval -1)/interval)*interval;
	auto next = startloc + period_t(delta);
	if(next < nowloc)
		next += period_t(interval);

	auto nextsys = tz->to_sys(next, std::chrono::choose::earliest);
#if 0
	auto t2 = nextsys - period_t(interval);
	if (t2 > nowsys) {
		fmt::print("t2={} nextsys={}\n", t2, nextsys);
		nextsys= t2;
		next =  tz->to_local(nextsys);
	}
#endif
	return system_clock::to_time_t(nextsys);
}


static time_t next_monthly(time_t now_, time_t start_time, int interval=2)
{
	const char* msg="montly";
	now_ = std::max(start_time, now_);
	using namespace std::chrono;
	auto tz = current_zone();
	auto nowsys = system_clock::from_time_t(now_);
	auto nowloc = tz->to_local(nowsys);
	auto startloc = tz->to_local(system_clock::from_time_t(start_time));
	// Get a days-precision chrono::time_point
	auto nowdayloc = floor<days>(nowloc);
	auto startdayloc = floor<days>(startloc);

	// Record the local time of day
	auto start_tod = startloc - startdayloc;
	auto now_tod = nowloc - nowdayloc;
	// Convert to a y/m/d calendar data structure
	year_month_day now_ymd{nowdayloc};
	year_month_day start_ymd{startdayloc};

	auto y = now_ymd.year();
	auto m = now_ymd.month();
	auto d = start_ymd.day();
	if( d < now_ymd.day()
			|| (d == now_ymd.day() && now_tod > start_tod)) {
		if(m==month{12}) {
			m=month{1};
			y++;
		} else {
			m+= months{1};
		}
	}

	year_month_day next_ymd{y, m, d};
	if (!next_ymd.ok())
		next_ymd = next_ymd.year()/next_ymd.month()/std::chrono::last;

	// Convert back to local time
	auto nextloc = local_days{next_ymd} + start_tod;
	// Convert back to system_clock::time_point
	auto nextsys = tz->to_sys(nextloc, std::chrono::choose::earliest);
	auto s = std::format("next      {}={}\n", msg, nextloc);
	printf("%s",s.c_str());
	return system_clock::to_time_t(nextsys);
}


static inline time_t compute_next_time(devdb::scan_command_t& cmd, time_t now)
{
	using namespace devdb;

	switch(cmd.run_type) {
	case run_type_t::NEVER:
		return -1;
		break;
	case run_type_t::ONCE:
		return -1;
		break;
	case run_type_t::HOURLY:
		return next_periodic<std::chrono::hours>(now, cmd.start_time, cmd.interval);
		break;
	case run_type_t::DAILY:
		return next_periodic<std::chrono::days>(now, cmd.start_time, 1);
		break;
	case run_type_t::WEEKLY:
		return next_periodic<std::chrono::weeks>(now, cmd.start_time, 1);
		break;
	case run_type_t::MONTHLY:
		return next_monthly(now, cmd.start_time, 1);
		break;
	}
	return -1;
}

/*
	Updates next_time, run_status, run_time, run_result and run_status in case
	the current record values indicate that something is incorrect
	-next_time is no longer uptodate
	-command is not running but should now be running
	-command is running but should now be not running

	returns two bools:
	1. true if anything changed and database record should be updated
	2. true if command must be actually started now
	3. true if command must be actually be stopped now
 */
std::tuple<int, int, int> update_command(devdb::scan_command_t& cmd, system_time_t now_, bool force)
{
	using namespace devdb;
	auto now = system_clock_t::to_time_t(now_);
	bool ret{false};
	bool must_start{false};
	bool must_stop{false};
	if(cmd.next_time > now && !force)
		return {false, false, false};
	time_t new_next_time = compute_next_time(cmd, now);
	if(cmd.run_type == run_type_t::NEVER)
		ret |= set_member(cmd, run_status,run_status_t::NONE);

	if(cmd.run_status == run_status_t::RUNNING) {
		must_stop = (now - cmd.next_time  > cmd.max_duration && cmd.max_duration>0);
		ret |= set_member(cmd, run_result, run_result_t::ABORTED);
		ret |= set_member(cmd, run_end_time, now);
		if(new_next_time >=0) { //will run again
			ret |= set_member(cmd, run_status, run_status_t::PENDING);
			ret |= set_member(cmd, run_end_time, -1);
		} else {
			ret |= set_member(cmd, run_status, run_status_t::FINISHED);
		}
	} else {
		bool delayed_start = (new_next_time>=0 && new_next_time <= now);
		auto next_time = delayed_start ? new_next_time : cmd.next_time;
		if(next_time <= now) {
			if(cmd.run_status == run_status_t::PENDING ||
				 cmd.run_status == run_status_t::NONE ||
				 cmd.run_status == run_status_t::FINISHED) {
				must_start = cmd.catchup || (now - next_time  < cmd.max_duration || cmd.max_duration <= 0);
				if(must_start)
					ret |= set_member(cmd, run_start_time, now);
				if(!must_start &&!cmd.catchup)  {
					dtdebugf("Detected skipped command {}", cmd.id);
					ret |= set_member(cmd, run_result, run_result_t::SKIPPED);
					ret |= set_member(cmd, run_status, run_status_t::NONE);
				} else {
					ret |= set_member(cmd, run_result, run_result_t::NONE);
					ret |= set_member(cmd, run_status, run_status_t::RUNNING);
				}
			}
		}
	}
	ret |= set_member(cmd, next_time, new_next_time);
	if(ret) {
		dtdebugf("Changed CMD {}: start={} stop={} "
						 "run_status={} run_result={} "
						 "next_time={} run_start={} run_end={} "
						 "start_time={} run_type={} ",
						 cmd.id, must_start, must_stop,
						 cmd.run_status, cmd.run_result,
						 cmd.next_time, cmd.run_start_time, cmd.run_end_time,
						 cmd.start_time, cmd.run_type);

	}
	return {ret, must_start, must_stop};
}

/*
	returns true if cmd is changed
 */
bool receiver_thread_t::start_command(devdb::scan_command_t& cmd, time_t now)
{
	using namespace devdb;
	cmd.run_start_time = now;
	switch(cmd.tune_options.subscription_type) {
	case subscription_type_t::TUNE:
		break;
	case subscription_type_t::MUX_SCAN:
		break;
	case subscription_type_t::BAND_SCAN:
		break;
	case subscription_type_t::SPECTRUM_ACQ:
		break;
	default:
		assert(false);
		break;
	}
	return false;
}

/*
	returns true if cmd is changed
 */
bool receiver_thread_t::stop_command(devdb::scan_command_t& cmd, time_t now)
{
	cmd.run_end_time = now;
	return false;
}


void receiver_thread_t::start_stop_commands(auto& cursor, db_txn& devdb_rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace devdb;
	for (auto cmd : cursor.range()) {
		auto [changed, must_start, must_stop]= update_command(cmd, now_, false/*force*/);
			assert(must_stop || must_start || ! changed);
			assert(!(must_start && must_stop));
			assert(!must_stop || cmd.run_start_time > 0);
			if(must_start)
				next_command_event_time = std::min(next_command_event_time, cmd.run_start_time + cmd.max_duration);
			next_command_event_time = std::min(next_command_event_time, cmd.next_time);
			if(must_start)
				changed |= start_command(cmd, now);
			if(must_stop)
				changed |= stop_command(cmd, now);

			if(changed) {
				auto wtxn = receiver.devdb.wtxn();
				cmd.mtime = now;
				put_record(wtxn, cmd);
				wtxn.commit();
			}
		}
}


/*
	For each command in progress, check if it should be stopped
	and stop if needed
*/
void receiver_thread_t::stop_commands(db_txn& devdb_rtxn, system_time_t now_) {
	using namespace devdb;
	auto c = scan_command_t::find_by_run_status_next_time
		(devdb_rtxn, run_status_t::RUNNING, find_type_t::find_geq, scan_command_t::partial_keys_t::run_status);
	start_stop_commands(c, devdb_rtxn, now_);
}

void receiver_thread_t::start_commands(db_txn& devdb_rtxn, system_time_t now_) {
	using namespace devdb;
	auto c = scan_command_t::find_by_run_status_next_time(devdb_rtxn, run_status_t::PENDING, find_type_t::find_geq,
																												 scan_command_t::partial_keys_t::run_status);
	start_stop_commands(c, devdb_rtxn, now_);
}


void receiver_thread_t::startup(system_time_t now_)
{
	using namespace devdb;
	auto devdb_rtxn = receiver.devdb.rtxn();
	auto c = find_first<scan_command_t>(devdb_rtxn);
	start_stop_commands(c, devdb_rtxn, now_);
	devdb_rtxn.commit();
}


int receiver_thread_t::housekeeping(system_time_t now) {
	return 0;
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



/*
	https://fzco.wackymango.net/date-time-manipulations/
	https://stackoverflow.com/questions/16773285/how-to-convert-stdchronotime-point-to-stdtm-without-using-time-t
	https://lunarwatcher.github.io/til/cpp/incomplete-time-parsing.html
	https://stackoverflow.com/questions/52238978/creating-a-stdchronotime-point-from-a-calendar-date-known-at-compile-time

*/
