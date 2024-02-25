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
#include "util/dtassert.h"
#include <dlfcn.h>
#include <errno.h>
#include <filesystem>
#include <signal.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>

#include "logger.h"
#include "util.h"

namespace fs = std::filesystem;

static const fs::path get_config_path() {
	static char mem[PATH_MAX];
	Dl_info info;
	if (!dladdr((void*)&get_config_path, &info)) {
		assert(0);
	}
	fs::path p{info.dli_fname};
	while (p != "/") {
		if (p.filename() == "build") {
			return p.parent_path() / "config";
			break;
		}
		p = p.parent_path();
	}

	return "/etc/neumodvb";
}

fs::path config_path{get_config_path()};

/*!
	Like mkdir -p; returns true if directory was created or already existed
*/
bool mkpath(const char* path) {
	std::error_code ec;
	auto ret = fs::create_directories(path, ec);
	if (ec) {
		dterrorf("mkpath {} failed: {}", path, ec.message());
	}
	return !ec;
}

/*!
	Like rm -fr ; returns true if error occurred
*/
bool rmpath(const char* path) {
	std::error_code ec;
	bool ret = std::filesystem::remove_all(path, ec);
	if (ec)
		dterrorf("Error deleting {}: {}", path, ec.message());
	return ret;
}

bool file_exists(char* fname) {
	struct stat st;
	if (stat(fname, &st)) {
		return false;
	}
	return true;
}

void event_handle_t::init() {
	_fd = ::eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
	if (_fd < 0) {
		dterrorf("error creating eventfd: {}", strerror(errno));
	}
}

void event_handle_t::unblock(uint64_t val) {
#ifdef DTDEBUG
	auto caller = gettid();
	assert(caller != owner); // needs to be called from the non owning thread
#endif
	if (val < 1) {
		// already unblocked
	}
	if (::write(_fd, &val, sizeof(uint64_t)) != sizeof(uint64_t)) {
		dterrorf("Error writing eventfd: {}", strerror(errno));
	}
}

int event_handle_t::reset() {
#ifdef DTDEBUG
	auto caller = gettid();
	if (owner == (pid_t)-1)
		owner = caller;
	assert(caller == owner); // needs to be called from owning thread
#endif
	uint64_t u = 0;
	auto ret= ::read(_fd, &u, sizeof(uint64_t));
	if (ret != sizeof(uint64_t)) {
		if(errno==EWOULDBLOCK || errno==EAGAIN) {
#if 0
			dterrorf("Spurious wakeup event fd={}", _fd);
#endif
		} else
			dterrorf("Error reading eventfd: {}", strerror(errno));
	}
	return u;
}

int event_handle_t::close() {
	int ret = -1;
	if (_fd >= 0)
		ret = ::force_close(_fd);
	_fd = -1;
	return ret;
}

void epoll_t::init() {
	static std::atomic<uint32_t> next_handle{0};
	handle= next_handle.fetch_add(1);
	_fd = epoll_create1(0); // create an epoll instance
	if (_fd < 0) {
		LOG4CXX_ERROR(logger, "Could not create epoll fd\n");
	}
}

int epoll_t::close() {
	int ret = -1;
	if (_fd >= 0)
		ret = ::force_close(_fd);
	_fd = -1;
	return ret;
}

int epoll_t::add_fd(int fd, int mask) {
#ifdef DTDEBUG
	auto caller = gettid();
	if (owner == (pid_t)-1)
		owner = caller;
	assert(caller == owner); // needs to be called from owning thread
#endif
	struct epoll_event ev = {};
	ev.data.u64 = fd | (((uint64_t)handle)<<32);
	ev.events = mask;
	int s = epoll_ctl(_fd, EPOLL_CTL_ADD, fd, &ev);
	if (s == -1) {
		auto msg= fmt::format("epoll_ctl add failed: _fd={} fd={} {}",
													(int)_fd, (int)fd, strerror(errno));
		LOG4CXX_FATAL(logger, msg);
		return -1;
	}
	return 0;
}

int epoll_t::mod_fd(int fd, int mask) {
#ifdef DTDEBUG
	auto caller = gettid();
	if (owner == (pid_t)-1)
		owner = caller;
	assert(caller == owner); // needs to be called from owning thread
#endif
	struct epoll_event ev = {};
	ev.data.u64 = fd | (((uint64_t)handle)<<32);
	ev.events = mask;
	int s = epoll_ctl(_fd, EPOLL_CTL_MOD, fd, &ev);
	if (s == -1) {
		LOG4CXX_FATAL(logger, "epoll_ctl mod failed: " << strerror(errno));
		return -1;
	}
	return 0;
}

int epoll_t::remove_fd(int fd) {
#ifdef DTDEBUG
	auto caller = gettid();
	if (owner == (pid_t)-1)
		owner = caller;
	assert(caller == owner); // needs to be called from owning thread
#endif
	int s = epoll_ctl(_fd, EPOLL_CTL_DEL, fd, NULL);
	if (s == -1) {
		LOG4CXX_FATAL(logger, "epoll_ctl remove failed: _fd=" << (int)_fd << " fd=" << (int) fd << " "  << strerror(errno));
		assert(false);
		return -1;
	}
	return 0;
}

int epoll_t::wait(struct epoll_event* events, int maxevents, int timeout) {
#ifdef DTDEBUG
	auto caller = gettid();
	if (owner == (pid_t)-1)
		owner = caller;
	assert(caller == owner); // needs to be called from owning thread
#endif
	for (;;) {
		int n = epoll_pwait(_fd, events, maxevents, timeout, // use a timeout of 0 to just check for events
												NULL);
		if (n < 0) {
			if (errno == EINTR) {
				//LOG4CXX_DEBUG(logger, "epoll_wait was interrupted");
				continue;
			} else {
				dterrorf("epoll_pwait failed: {}", strerror(errno));
				return -1;
			}
		} else
			return n;
	}
}

#ifdef DTDEBUG
void epoll_t::set_owner(pid_t pid) { owner = pid; }
#endif

/*
	Not  checking the return value of close() is a common but nevertheless serious programming error.  It is quite
	possible that errors on a previous write(2) operation are first reported at the final close().   Not  checking
	the  return value when closing the file may lead to silent loss of data.  This can especially be observed with
	NFS and with disk quota.  Note that the return value should be  used  only  for  diagnostics.   In  particular
	close()  should  not be retried after an EINTR since this may cause a reused descriptor from another thread to
	be closed.
*/
int force_close(int fd) {
	if (fd < 0)
		return -1;
	for (;;) {
		int ret = close(fd);
		if (ret == 0)
			return ret;
		else if (ret < 0 && errno != EINTR) {
			dterrorf("Error while closing fd={}: {}", fd, strerror(errno));
			return ret;
		}
	}
}

off_t filesize_fd(int fd) {
	struct stat st;
	if (fstat(fd, &st)) {
		dterrorf("stat failed: {}", strerror(errno));
		return -1;
	}
	return st.st_size;
}

int periodic_timer_create_and_start(double period_sec) {
	int fd = timerfd_create(CLOCK_MONOTONIC, 0);
	struct itimerspec new_value {};

	auto dur = std::chrono::duration<double>(period_sec);
	auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
	dur -= secs;
	new_value.it_interval.tv_sec = secs.count();
	new_value.it_interval.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();

	new_value.it_value = new_value.it_interval;
	int flags = 0;
	struct itimerspec* old_value = NULL; // returns time to expiration + period
	int ret = timerfd_settime(fd, flags, &new_value, old_value);
	if (ret < 0) {
		dterrorf("could not create timerfd: {}", strerror(errno));
		return -1;
	}
	return fd;
}

int timer_set_once(int fd, double expiration_sec) {
	struct itimerspec new_value {};

	auto dur = std::chrono::duration<double>(expiration_sec);
	auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
	dur -= secs;
	new_value.it_value.tv_sec = secs.count();
	new_value.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
	new_value.it_interval.tv_sec = 0;
	new_value.it_interval.tv_nsec = 0;
	int flags = 0;
	struct itimerspec* old_value = NULL; // returns time to expiration + period
	int ret = timerfd_settime(fd, flags, &new_value, old_value);
	if (ret < 0) {
		dterrorf("could not create timerfd: {}", strerror(errno));
	}
	return ret;
}

int timer_set_period(int fd, double period_sec) {
	struct itimerspec new_value {};

	auto dur = std::chrono::duration<double>(period_sec);
	auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
	dur -= secs;
	new_value.it_interval.tv_sec = secs.count();
	new_value.it_interval.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
	new_value.it_value = new_value.it_interval;

	int flags = 0;
	struct itimerspec* old_value = NULL; // returns time to expiration + period
	int ret = timerfd_settime(fd, flags, &new_value, old_value);
	if (ret < 0) {
		dterrorf("could not create timerfd: {}", strerror(errno));
		return -1;
	}
	return fd;
}

int timer_stop(int fd) {
	struct itimerspec new_value {};
	int flags = 0;
	struct itimerspec* old_value = NULL; // returns time to expiration + period
	int ret = timerfd_settime(fd, flags, &new_value, old_value);
	if (ret < 0) {
		dterrorf("could not stop timerfd: {}", strerror(errno));
		return -1;
	}
	while (ret) {
		ret = ::close(fd);
		if (ret != EINTR)
			break;
	}
	if (ret < 0) {
		dterrorf("could not stop timerfd: {}", strerror(errno));
		return -1;
	}
	return ret;
}

int _slowdown(time_t* last, int* count, time_t now, int maxcount) {
	(*count)++;
	if (now - *last > 1) {
		if (*last != 0 && (*count > maxcount * (now - *last))) {
			return 1;
		}
		*last = now;
		*count = 0;
	}
	return 0;
}

void assert_fail_log(const char *assertion, const char *file, unsigned line, const char *function) throw() {
	extern const char *__progname;
	char msg[256];
	snprintf(msg, sizeof(msg)-1, "%s%s%s:%u: %s%sAssertion `%s' failed.\nSUSPENDING PROGRAM",
					__progname,
					__progname[0] ? ": " : "",
					file,
					line,
					function ? function : "",
					function ? ": " : "",
					assertion
		);

	LOG4CXX_ERROR(logger, msg);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-noreturn"
void assert_fail_stop(const char *assertion, const char *file, unsigned line, const char *function) throw() {
	extern const char *__progname;
	fprintf(stderr, "%s%s%s:%u: %s%sAssertion `%s' failed.\nSUSPENDING PROGRAM",
					__progname,
					__progname[0] ? ": " : "",
					file,
					line,
					function ? function : "",
					function ? ": " : "",
        assertion
		);
	kill(getpid(), SIGSTOP); //suspend all threads
	sleep(10); // workaround: it may take a while before the calling thread itself is suspended
}
#pragma GCC diagnostic pop

int file_swap(const char* file1, const char* file2) {
#ifdef RENAMEAT_SUPPORT
	// not on zfs...
	return renameat2(AT_FDCWD, file1, AT_FDCWD, file2, RENAME_EXCHANGE);
#else

	std::error_code err;
	fs::path p1(file1);
	fs::path p2(file2);
	auto p3 = fs::canonical(p2);
	p3 += fs::path(".moved");
	auto file3 = std::string(p3);
	rename(p2, p3, err);
	if (err) {
		fprintf(stderr, "cannot rename %s to %s\n", file2, file3.c_str());
		return -1;
	}
	rename(p1, p2, err);
	if (err) {
		fprintf(stderr, "cannot rename %s to %s\n", file1, file2);
		return -1;
	}
	rename(p3, p1, err);
	if (err) {
		fprintf(stderr, "cannot rename %s to %s\n", file3.c_str(), file1);
		return -1;
	}
	return 0;
#endif
}


extern "C" {
void __dtassert_fail(const char * assertion, const char * file, unsigned int line, const char * function)
	__THROW
{
	char msg[256];
	snprintf(msg, sizeof(msg)-1, "Assert: %s failed at %s:%d in function %s", assertion, file, line, function);
	fprintf(stderr, "%s\n", msg);
	LOG4CXX_ERROR(logger, msg);
	raise(SIGTRAP);
	//__builtin_trap();
					//else
        //abort();
}

};

__thread system_time_t now{};

thread_local ss::string<256> user_error_;
