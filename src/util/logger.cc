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

#include "logger.h"
#include "time_util.h"
#include <limits>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <fmt/chrono.h>
#include "log4cxx/consoleappender.h"
#include "log4cxx/propertyconfigurator.h"
#include "log4cxx/xml/domconfigurator.h"
#include "util/dtassert.h"
#include <chrono>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/fileappender.h>
#include <log4cxx/helpers/pool.h>
#include <log4cxx/logger.h>
#include <log4cxx/patternlayout.h>

using namespace log4cxx;
using namespace log4cxx::xml;
using namespace log4cxx::helpers;

#include <ctime>
#include <iomanip>
#include <iostream>
#include <time.h>

LoggerPtr create_log(const char* loggername) {
	const char* pattern = "%d %.40F:%L\n  %m %n";
	auto l = LayoutPtr(new PatternLayout(pattern));
	ConsoleAppender* consoleAppender = new ConsoleAppender(l);
	helpers::Pool p;
	BasicConfigurator::configure(AppenderPtr(consoleAppender));
	// TRACE < DEBUG < INFO < WARN < ERROR < FATAL.
	Logger::getRootLogger()->setLevel(Level::getDebug());
	LoggerPtr logger = Logger::getLogger("logger");
	logger->setLevel(Level::getDebug());
	return logger;
}

void set_logconfig(const char* logconfig) {
	// Block signals so that the main process inherits them...
	sigset_t sigset, old;
	sigfillset(&sigset);
	sigprocmask(SIG_BLOCK, &sigset, &old);
	if (!logconfig || !logconfig[0]) {
		const char* pattern = "%d %.40F:%L\n  %m %n";
		auto l = LayoutPtr(new PatternLayout(pattern));
		ConsoleAppender* consoleAppender = new ConsoleAppender(l);
		helpers::Pool p;
		BasicConfigurator::configure(AppenderPtr(consoleAppender));
	} else {
		DOMConfigurator::configureAndWatch(logconfig, 1000);
	}
	sigprocmask(SIG_SETMASK, &old, &sigset);
}

template <class... Durations, class DurationIn> std::tuple<Durations...> break_down_durations(DurationIn d) {
	std::tuple<Durations...> retval;
	using discard = int[];
	(void)discard{0, (void(((std::get<Durations>(retval) = std::chrono::duration_cast<Durations>(d)),
													(d -= std::chrono::duration_cast<DurationIn>(std::get<Durations>(retval))))),
										0)...};
	return retval;
}

LoggerPtr mainlogger = create_log("main");
thread_local LoggerPtr logger = Logger::getLogger("main");
thread_local log4cxx::NDC global_ndc("");
