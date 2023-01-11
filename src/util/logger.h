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

#pragma once

#include <sys/timeb.h>
#include <log4cxx/logger.h>
#include <log4cxx/helpers/pool.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/fileappender.h>
#include <log4cxx/patternlayout.h>
#include "log4cxx/consoleappender.h"
#include "log4cxx/ndc.h"
#include "time_util.h"
#include "stackstring.h"

using namespace log4cxx;
extern thread_local LoggerPtr logger;
extern thread_local log4cxx::NDC global_ndc;
//extern LoggerPtr indexer_logger;

extern LoggerPtr create_log(const char* filename=NULL);
extern void set_logconfig(const char* logfile);

struct audio_language_t;
struct audio_stream_t;
struct subtitle_stream_t;

namespace std { //needed ot log4cxx creates errors
std::ostream& operator<<(std::ostream& os, const ss::string_& a);

std::ostream& operator<<(std::ostream& os, system_time_t a);

};




namespace dtdemux {
	struct pts_dts_t;
	struct pcr_t;

	std::ostream& operator<<(std::ostream& os,  const pts_dts_t& a);
	std::ostream& operator<<(std::ostream& os,  const pcr_t& a);
}



#define dterror(text, args...)			\
		LOG4CXX_ERROR(logger, text, ##args)

#define dtinfo(text, args...)			\
		LOG4CXX_INFO(logger, text, ##args)

#define dtdebug(text, args...)			\
		LOG4CXX_DEBUG(logger, text, ##args)

#define dtthrow(text, args...)											\
	do {																							\
	std::stringstream ss;															\
	ss << text  ##args;																\
	throw std::runtime_error(ss.str());								\
	} while(0)

#define dterror_nice(text, args...)			\
	do {																																	\
		static int ___count=0; static time_t ___last=0; time_t ___now=time(NULL);	\
		if(___now-___last<1) {___count++;break;}														\
		LOG4CXX_ERROR(logger, text, ##args);								\
		if(___count) 		LOG4CXX_ERROR(logger, "Last message repeated " << ___count << " times"); \
		___count=0;___last=___now;																					\
	} while(0)


#define dtdebug_nice(text, args...)			\
	do {																																	\
		static int ___count=0; static time_t ___last=0; time_t ___now=time(NULL);	\
		if(___now-___last<1) {___count++;break;}														\
		LOG4CXX_DEBUG(logger, text, ##args);																\
		if(___count) LOG4CXX_DEBUG(logger, "Last message repeated " << ___count << " times"); \
		___count=0;___last=___now;																					\
	} while(0)


#define dtdebug_nicex(fmt, args...)			\
	do {																																	\
		static int ___count=0; static time_t ___last=0; time_t ___now=time(NULL);	\
		if(___now-___last<1) {___count++;break;}														\
		char msg[256];																											\
		snprintf(msg, 255, fmt, ##args);																		\
		LOG4CXX_DEBUG(logger, msg);																					\
		if(___count) LOG4CXX_DEBUG(logger, "Last message repeated " << ___count << " times"); \
		___count=0;___last=___now;																					\
	} while(0)


#define dtdebugx(fmt, args...)																					\
	do {																																	\
		char msg[256];																											\
		snprintf(msg, 255, fmt, ##args);																		\
		LOG4CXX_DEBUG(logger, msg);																					\
	} while(0)

#define dtthrowx(fmt, args...)																					\
	do {																																	\
		char msg[256];																											\
		snprintf(msg, 255, fmt, ##args);																		\
		throw std::runtime_error(std::string(msg));													\
	} while(0)

#define dterrorx(fmt, args...)																					\
	do {																																	\
		char msg [256];																											\
		snprintf(msg, 255, fmt, ##args);																		\
		LOG4CXX_ERROR(logger, msg);																					\
	} while(0)

//error which should be reported to the user
#define user_errorx(fmt, args...)																				\
	do {																																	\
		user_error_.clear();																								\
		user_error_.sprintf(fmt, ##args);																		\
		LOG4CXX_ERROR(logger, user_error_.c_str());													\
	} while(0)

//error which should be reported to the user
#define user_error(text)														\
	do {																							\
		user_error_.clear();														\
		ss::accu_t(user_error_) << text;								\
		LOG4CXX_ERROR(logger, user_error_.c_str());			\
	} while(0)

inline int dttime_(steady_time_t& dt_timer,int timeout,const char*func,int line)
{
	auto now = steady_clock_t::now();

	int ret =  std::chrono::duration_cast<std::chrono::milliseconds>(now -dt_timer).count();
	dt_timer = now;
	if(timeout>=0&&ret>=timeout) {
		dtdebugx("%s_%d TIME: %d",func,line,ret);
	}
	return ret;
}

#define dttime_init()\
	steady_time_t dt_timer = steady_clock_t::now();

#define dttime(timeout) \
	dttime_(dt_timer, timeout,__FILE__,__LINE__)


inline void log4cxx_store_threadname()
{
	char thread_name[32];
	pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
	assert(strlen(thread_name)>0);
	log4cxx::MDC::put( "thread_name", thread_name);
}
