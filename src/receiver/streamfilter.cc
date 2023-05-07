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
#include "streamfilter.h"
#include "active_stream.h"
#include "util/logger.h"
#include "util/util.h"
#include "util/dtassert.h"
#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

enum PIPE_FILE_DESCRIPTERS
{
	READ_FD  = 0,
	WRITE_FD = 1
};

enum CONSTANTS
{
	BUFFER_SIZE = 100
};

/** Returns true on success, or false if there was an error */
bool set_blocking(int fd, bool on) {
	if (fd < 0)
		return false;

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return false;
	flags = on ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}

stream_filter_t::stream_filter_t(active_adapter_t& active_adapter, const chdb::any_mux_t& embedded_mux,
																 epoll_t* epoll, int epoll_flags)
	: active_adapter(active_adapter)
	, embedded_mux(embedded_mux)
	, epoll(epoll)
	, epoll_flags(epoll_flags)
	,	bufferp(std::make_unique<uint8_t[]>(dmx_buffer_size)) {
}

int stream_filter_t::open() {
	//this->tuned_mux = active_adapter.current_mux();
#ifndef NDEBUG
	auto* k = chdb::mux_key_ptr(this->embedded_mux);
#endif
	assert(k->sat_pos != sat_pos_none);
	start();
	return error ? -1 : 0;
}

void stream_filter_t::close() {
	if (!is_open())
		return;
	assert(data_fd >= 0);
	if (::close(data_fd) < 0) {
		dterrorx("Error in close: %s", strerror(errno));
	}
	data_fd = -1;
	stop();
}

void stream_filter_t::stop() {
	assert(command_pid > 0);
	if (kill(command_pid, SIGHUP) < 0) {
		dterrorx("Errror while sending signal: %s", strerror(errno));
	}
	if (waitpid(command_pid, nullptr, 0) < 0) {
		dterrorx("Errror during wait: %s", strerror(errno));
	}
	command_pid = -1;
}

int stream_filter_t::start() {
#if 0
	thread_id = std::this_thread::get_id();
	set_name("t2mi");
	logger = Logger::getLogger("t2mi"); //override default logger for this thread
#endif
	ss::string<64> ndc;
	auto stream_pid = chdb::mux_key_ptr(embedded_mux)->t2mi_pid;
	ndc.sprintf("PID[%d]", stream_pid);
	log4cxx::NDC(ndc.c_str());
	int dmx_buffer_size = 32 * 1024 * 1024;
	dvb_stream_reader_t dvb_reader(active_adapter, dmx_buffer_size);
	int stream_fd = dvb_reader.open(stream_pid, epoll, epoll_flags);
	if (stream_fd < 0)
		return -1;

	auto flags = fcntl(stream_fd, F_GETFD);
	if (flags < 0) {
		dterrorx("fcntl failed: %s", strerror(errno));
		return -1;
	}
	if (fcntl(stream_fd, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
		dterrorx("Could not clear FD_CLOEXEC: %s", strerror(errno));
		return -1;
	}
	ss::string<32> pid_;
	pid_.sprintf("%d", stream_pid);
#if 1
	data_fd = start_command(stream_fd, "tsp", "--realtime", "--initial-input-packets", "256", "-P", "t2mi", "--pid", pid_.c_str(),
													// @todo: "--plp", plp.cstr()
													(char*)nullptr);
#else
	data_fd = start_command(stream_fd, "/tmp/saver", "saver",
													// @todo: "--plp", plp.cstr()
													(char*)nullptr);
#endif
	if (data_fd < 0) {
		dterrorx("Could not start command");
		return -1;
	}
	return 0;
}

bool stream_filter_t::read_and_process_data() {
	if (error)
		return false;
	data_ready = true;
	error |= (read_external_data() < 0);
	// rearm
	return error;
}

inline int stream_filter_t::available_for_write() {
	int ret = buff_size;
	// std::scoped_lock lck(m);
	for (auto& s : stream_readers) {
		auto toread = write_pointer - s->read_pointer;
		if (toread < 0) // wrap around
			toread += buff_size;
		if (buff_size - toread < ret)
			ret = buff_size - toread;
	}
	return ret;
}

inline int stream_filter_t::read_external_data() {
	auto lck = std::scoped_lock(m);
	if (!data_ready)
		return 0;
	int toread = available_for_write();
	for (;;) {
		assert(data_fd>=0);
		auto size = std::min(toread, buff_size - write_pointer);
		assert ( write_pointer + size <= buff_size);
		auto ret = read(data_fd, bufferp.get() + write_pointer, size);
		if (ret == 0) {
			dterrorx("end stream closed\n");
			return -1;
		} else if (ret < 0) {
			if (errno == EAGAIN) {
				data_ready = false;
				break;
			}
			if (errno == EINTR)
				continue;
			else {
				dterrorx("read from command failed: %s\n", strerror(errno));
				return -1;
			}
		}
		data_ready = (ret == size);
		assert(ret > 0);
		assert(ret <= size);
		{
#if 0
			static FILE* fp = fopen("/tmp/out1.ts", "w");
			fwrite( bufferp.get() + write_pointer, 1, ret, fp);
#endif
		}
		assert(ret <= toread);
		toread -= ret;
		write_pointer += ret;
		assert(write_pointer <= buff_size);
		if (write_pointer == buff_size)
			write_pointer = 0; // wrap around
		break;
	}
	return 0;
}

/*
	start an external command, connect its stdin to stream_fd
	and connect its stdout to a pipe.
	Returns teh file descriptor of the pipe so that we can read from it
*/
template <typename... Args> int stream_filter_t::start_command(int stream_fd, const char* pathname, Args... args) {
	int childToParent[2];

	// int status;

	/*  The O_NONBLOCK and FD_CLOEXEC  flags  shall  be
			clear  on  both  file descriptors
			fildes[0] = read end
			fildes[1] = write end

	*/

	if (pipe(childToParent) != 0) {
		dterrorx("pipe failed\n");
		::exit(1);
	}

	switch (command_pid = fork()) {
	case -1:
		dterrorx("Fork failed\n");
		::exit(-1);

	case 0: /* Child */
		/*rename filedesriptors to standard ones so that the external command can read from fd=0
			and write to fd=1*/
		if (dup2(stream_fd, STDIN_FILENO) < 0 || dup2(childToParent[WRITE_FD], STDOUT_FILENO) < 0 ||
				::close(childToParent[READ_FD]) != 0) {
			dterror("error occured\n");
			::exit(1);
		}
		set_blocking(STDIN_FILENO, true);
		/*     file, arg0, arg1,  arg2 */
		execlp(pathname, pathname, args...);

		// note that we cannot use dterror....
		fprintf(stderr, "This line should never be reached!!!\n");
		::exit(-1);

	default: /* Parent */
		dtdebugx("Child process %d running...\n", command_pid);

		if (::close(childToParent[WRITE_FD]) != 0) {
			dterror("error closing pipe fd\n");
		}
		set_blocking(childToParent[READ_FD], false);

		auto flags = fcntl(childToParent[READ_FD], F_GETFD);
		if (flags < 0) {
			dterrorx("fcntl failed: %s", strerror(errno));
			return -1;
		}

		if (fcntl(childToParent[READ_FD], F_SETFD, flags | FD_CLOEXEC) < 0) {
			dterrorx("Could not set FD_CLOEXEC: %s", strerror(errno));
			return -1;
		}

		return childToParent[READ_FD];
	}
	return -1;
}

inline void embedded_stream_reader_t::discard(ssize_t num_bytes) {
	assert(num_bytes + read_pointer <= last_range_end_pointer);
	last_range_end_pointer = read_pointer + num_bytes;
	if (last_range_end_pointer == stream_filter->buff_size)
		last_range_end_pointer = 0;
	assert((last_range_end_pointer % dtdemux::ts_packet_t::size)==0);
	read_pointer = last_range_end_pointer;
}

inline std::tuple<uint8_t*, ssize_t> embedded_stream_reader_t::read(ssize_t size) {
	assert(size != 0);
	assert((size<0 || (size % dtdemux::ts_packet_t::size) ==0));
	auto* ptr = stream_filter->bufferp.get() + read_pointer;
	auto toread = stream_filter->write_pointer - read_pointer;
	toread -= toread % dtdemux::ts_packet_t::size;
	assert((toread % dtdemux::ts_packet_t::size) ==0);
	if (toread < 0) { // wrap around
		toread += stream_filter->buff_size;
	}
	assert((toread % dtdemux::ts_packet_t::size) ==0);
	// never read past end of buffer (called can call again to get next part)
	toread = std::min(toread, stream_filter->buff_size - read_pointer);
	assert((toread % dtdemux::ts_packet_t::size) ==0);
	if (size >= 0)
		toread = std::min((int)size, toread);
	assert((toread % dtdemux::ts_packet_t::size) ==0);
	if (toread == 0) {
		// attempt to read some more data
		stream_filter->read_external_data();
		toread = (stream_filter->buff_size + stream_filter->write_pointer - read_pointer)%stream_filter->buff_size;
		toread -= toread % dtdemux::ts_packet_t::size;
		assert((toread % dtdemux::ts_packet_t::size) ==0);
		toread = std::min(toread, stream_filter->buff_size - read_pointer);
		assert((toread % dtdemux::ts_packet_t::size) ==0);
		if(size >0)
			toread = std::min((int)size, toread); // never read more than requested
		assert((toread % dtdemux::ts_packet_t::size) ==0);
	}

	if (toread > 0) {
		// wil be used by release_range
		last_range_end_pointer = read_pointer + toread;
	}
	if (toread > 0)
		num_read += toread;
 	assert((toread % dtdemux::ts_packet_t::size) ==0);
	return {ptr, toread};
}

inline ssize_t embedded_stream_reader_t::read_into(uint8_t* p, ssize_t toread, const std::vector<pid_with_use_count_t>* pids) {
	ssize_t num_read{0};
	while (toread >= dtdemux::ts_packet_t::size) {
		auto [ptr, ret] = this->read(toread);
		if (ret <= 0)
			return num_read > 0 ? num_read : ret;
		assert(ret <= toread);
		assert((ret % dtdemux::ts_packet_t::size)==0);

		auto *ptr_end = ptr + ret;
		while (ptr < ptr_end) {
			int pid = (((uint16_t)(ptr[1] & 0x1f)) << 8) | ptr[2];
			for(const auto& x : *pids) {
				if (x.pid == pid) {
					memcpy(p, ptr, dtdemux::ts_packet_t::size);
					p += dtdemux::ts_packet_t::size;
					num_read += dtdemux::ts_packet_t::size;
					break;
				}
			}
			ptr += dtdemux::ts_packet_t::size;
		}
		discard(ret);
		toread -= ret;
	}
	return num_read;
}


inline bool embedded_stream_reader_t::on_epoll_event(const epoll_event* evt) {
	if ((evt->data.u64 & 0xffffffff) == stream_filter->data_fd) {
		/*each of the subscribers will randomly receive this event
			and then process incoming data
		*/
		master_read_count++; // for debugging
		stream_filter->read_and_process_data();
		stream_filter->notify_other_readers(this);
#if 0
		int mask = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
		epoll->mod_fd(stream_filter->data_fd, mask|EPOLLONESHOT); //EPOLLEXCLUSIVE not allowed!
#endif
		return true;
	} else if (epoll && epoll->matches(evt, (int) notifier)) {
		if(stream_filter->data_fd <0)
			return false;
		notifier.reset();
		return true;
	}
	return false;
}

embedded_stream_reader_t::embedded_stream_reader_t(active_adapter_t& active_adapter,
																									 const std::shared_ptr<stream_filter_t>& stream_filter)
	: stream_reader_t(active_adapter), stream_filter(stream_filter) {}

int embedded_stream_reader_t::embedded_stream_pid() const {
	return mux_key_ptr(stream_filter->embedded_mux)->t2mi_pid; }

int embedded_stream_reader_t::open(uint16_t initial_pid, epoll_t* epoll, int epoll_flags) {
	this->epoll = epoll;
	this->epoll_flags = epoll_flags;
	stream_filter->register_reader(this);
	// ensure that exactly one thread receives a wakeup call for data_fd
	assert(stream_filter->data_fd >= 0);
	epoll->add_fd(stream_filter->data_fd, epoll_flags | EPOLLEXCLUSIVE);
	epoll->add_fd((int)notifier, epoll_flags);

	// initial_pid not used becaue we get all pids anyway
	return 0;
}

void embedded_stream_reader_t::close() {
	if (is_open()) {
		epoll->remove_fd((int)notifier);
		stream_filter->unregister_reader(this);
		if(stream_filter->data_fd>=0)
			epoll->remove_fd(stream_filter->data_fd);
		epoll = nullptr;
	}
}

void stream_filter_t::register_reader(embedded_stream_reader_t* reader) {
	std::scoped_lock lck(m);
	if (stream_readers.size() == 0)
		this->open();
	for (int i = 0; i < stream_readers.size(); ++i) {
		if (stream_readers[i].get() == reader) {
			dterrorx("Reader already registered");
			return;
		}
	}
	auto p = reader->shared_from_this();
	auto q = std::static_pointer_cast<embedded_stream_reader_t>(p);
	stream_readers.push_back(q);
}

void stream_filter_t::unregister_reader(embedded_stream_reader_t* reader) {
	std::scoped_lock lck(m);
	for (int i = 0; i < stream_readers.size(); ++i) {
		if (stream_readers[i].get() == reader) {
			stream_readers.erase(i);
			break;
		}
	}
	if (stream_readers.size() == 0)
		close();
}

void stream_filter_t::notify_other_readers(embedded_stream_reader_t* reader) {
	std::scoped_lock lck(m);
	for (int i = 0; i < stream_readers.size(); ++i) {
		auto& r = stream_readers[i];
		if (r.get() != reader) {
			r->notifier.unblock();
		}
	}
}

chdb::any_mux_t embedded_stream_reader_t::stream_mux() const {
	auto & mux = stream_filter->embedded_mux;
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);

	return stream_filter->embedded_mux; }

void embedded_stream_reader_t::on_stream_mux_change(const chdb::any_mux_t& mux) {
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	stream_filter->embedded_mux = mux;
}

void embedded_stream_reader_t::update_received_si_mux(const std::optional<chdb::any_mux_t>& mux,
																											bool is_bad) {
//noop
}

void embedded_stream_reader_t::set_current_tp(const chdb::any_mux_t& embedded_mux) const {
	assert(mux_key_ptr(embedded_mux)->sat_pos != sat_pos_none);
	auto& mux = embedded_mux;
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	stream_filter->embedded_mux = embedded_mux;
}

void embedded_stream_reader_t::update_stream_mux_nit(const chdb::any_mux_t& stream_mux) {
	auto & mux = stream_mux;
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	stream_filter->embedded_mux = stream_mux;
}
