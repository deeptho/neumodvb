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
#include <string>
#include <mutex>
#include <condition_variable>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <filesystem>
#include <sys/epoll.h>
#include "time_util.h"
#include <sys/timerfd.h>
#include "stackstring.h"

extern "C" {
off_t filesize_fd(int fd);
}

extern std::filesystem::path config_path;

int timer_start(double period_sec=2.0);
int timer_stop(int fd);
int timer_set_period(int fd, double period_sec);

//std::string absolute_path(const char*program, const char*filename);

#if 0
inline std::string absolute_path(const char*program, std::string filename) {
	return absolute_path(program, filename.c_str());
}
#endif

extern bool mkpath(const char* path);
inline bool mkpath(std::string path) {
	return mkpath(path.c_str());
}


bool rmpath(const char* path);
inline bool rmpath(std::string path) {
	return rmpath(path.c_str());
}



inline unsigned int count_one_bits(uint32_t n) {
	unsigned masks[] = { 0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF };
	for(unsigned i=0; i<5; i++) { n = (n & masks[i]) + ((n >> (1 << i)) & masks[i]); }
	return n;
}



bool file_exists(char*fname);
inline bool file_exists(std::string fname) {
	return file_exists((char*)fname.c_str());
}

#if 0
extern pid_t gettid();
#endif

int force_close(int fd);

/*
	Wrapper for eventfd.
	Operations:
	 -unblock: write to the event fd such that threads blocked on reading it will be unblocked
	 -reset: read from the event fd such that threads block again
	 event_handle_t is owned by a single thread which can then block on it
 */
class event_handle_t {
	int _fd=-1;
#ifdef DTDEBUG
	pid_t owner = (pid_t)-1;
#endif
	void init();

 public:
 event_handle_t() {
		init();
	}

	operator int() const {
		return _fd;
	}

	void unblock(uint64_t val=1);
	int reset();
	int close();

	~event_handle_t() {
		close();
	}
};


class epoll_t {
	int _fd{-1};
#ifdef DTDEBUG
	pid_t owner = (pid_t)-1;
#endif
	int32_t handle{0};

	void init();

 public:
	epoll_t() {
		init();
	}

	epoll_t(epoll_t&& other)  = default;
	epoll_t(const epoll_t& other)  = delete;
	epoll_t& operator=(const epoll_t& other)  = delete;

	~epoll_t() {
		close();
	}
	operator int() const {
		return _fd;
	}

	bool matches(const epoll_event* event, int fd) const {
		bool fd_matches = (event->data.u64 & 0xffffffff) == fd;
		bool handle_matches = (event->data.u64 >>32) == handle;
		if(fd_matches && ! handle_matches)
			printf("Ignored bad match\n");
		return (event->data.u64 & 0xffffffff) == fd && (event->data.u64 >>32) == handle;
	}

	int close();

	int remove_fd(int fd);
	int add_fd(int fd, int mask);
	int mod_fd(int fd, int mask);
	int wait(struct epoll_event* events, int maxevents=16, int timeout=-1);
#ifdef DTDEBUG
	void set_owner(pid_t pid);
	void set_owner() {
		set_owner(gettid());
	}
#endif
};

#define unconvertable_int(base_name, type_name)	\
	enum class type_name : base_name {}


extern thread_local ss::string<256> user_error_;

inline const ss::string_& get_error() {
	return user_error_;
}

inline void set_error(const ss::string_& old) {
	user_error_ =old;
}


#ifdef NODEBUG
#define slowdown(maxcount, msg)   {}
#else
extern int _slowdown(time_t*last,int *count, time_t now, int maxcount);

#define slowdown(maxcount, msg)											\
	{																																 \
		time_t now=time(NULL);																						\
		static time_t last=0;																								\
		static int count=0;																									\
		if(_slowdown(&last, &count, now, maxcount)){												\
			dtdebugx("SLOW %s: Too many calls: %d/s (%d in %d s)\n",					\
							 msg,(int)(count/(now-last)),count,(int)(now-last));			\
		}																																		\
}


enum class subscription_id_t : int {
	NONE = -1, // unspecified failure or no subscription
	RESERVATION_FAILED = -2,
	RESERVATION_FAILED_PERMANENTLY = -3,
	TUNE_FAILED = -4
};

inline subscription_id_t& operator++(subscription_id_t& x) {
	x= (subscription_id_t) (1+(int)x);
	return x;
}

inline subscription_id_t operator++(subscription_id_t& x, int) {
	auto ret = x;
	x= (subscription_id_t) (1+(int)x);
	return ret;
}

void assert_fail_stop(const char *assertion, const char *file, unsigned line, const char *function) throw();


#endif

int file_swap(const char* file1, const char* file2);

/** @brief Wait msec miliseconds
 */
inline void msleep(uint32_t msec) {
	struct timespec req = {msec / 1000, 1000000 * (msec % 1000)};
	while (nanosleep(&req, &req))
		;
}
