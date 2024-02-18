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
#include <sys/prctl.h>

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

/*
	start an external command, connect its stdin to stream_fd
	and connect its stdout to a pipe.
	Returns the file descriptor of the pipe so that we can read from it
*/
std::tuple< int , pid_t>
start_command(int stream_fd, const char* pathname, ss::vector_<const char*>& args) {
	pid_t command_pid{pid_t(-1)};
	int childToParent[2];

	// int status;

	/*  The O_NONBLOCK and FD_CLOEXEC  flags  shall  be
			clear  on  both  file descriptors
			fildes[0] = read end
			fildes[1] = write end

	*/

	if (pipe(childToParent) != 0) {
		dterrorf("pipe failed\n");
		::exit(1);
	}

	switch (command_pid = fork()) {
	case -1:
		dterrorf("Fork failed\n");
		::exit(-1);

	case 0: /* Child */
		/*rename file descriptors to standard ones so that the external command can read from fd=0
			and write to fd=1*/
		if (dup2(stream_fd, STDIN_FILENO) < 0 || dup2(childToParent[WRITE_FD], STDOUT_FILENO) < 0 ||
				::close(childToParent[READ_FD]) != 0) {
			dterrorf("error occured");
			::exit(1);
		}
		set_blocking(STDIN_FILENO, true);
		signal(SIGINT, SIG_IGN); //avoid interrupt by gdb
		setpgid(0, 0);
		prctl(PR_SET_PDEATHSIG, SIGHUP); //ask to be killed when parent dies
		/*     file, arg0, arg1,  arg2 */
		execvp(pathname,  const_cast<char* const*>(args.buffer()));

		// note that we cannot use dterror....
		fprintf(stderr, "This line should never be reached!!!\n");
		::exit(-1);

	default: /* Parent */
		dtdebugf("Child process {:d} running...\n", command_pid);

		if (::close(childToParent[WRITE_FD]) != 0) {
			dterrorf("error closing pipe fd");
		}
		set_blocking(childToParent[READ_FD], false);

		auto flags = fcntl(childToParent[READ_FD], F_GETFD);
		if (flags < 0) {
			dterrorf("fcntl failed: {}", strerror(errno));
			return {-1,-1};
		}

		if (fcntl(childToParent[READ_FD], F_SETFD, flags | FD_CLOEXEC) < 0) {
			dterrorf("Could not set FD_CLOEXEC: {}", strerror(errno));
			return {-1, -1};
		}

		return {childToParent[READ_FD], command_pid};
	}
	return {-1, -1};
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
		dterrorf("Error in close: {}", strerror(errno));
	}
	data_fd = -1;
	stop();
}

void stream_filter_t::stop() {
	assert(command_pid > 0);
	if (kill(command_pid, SIGHUP) < 0) {
		dterrorf("Error while sending signal: {}", strerror(errno));
	}
	if (waitpid(command_pid, nullptr, 0) < 0) {
		dterrorf("Error during wait: {}", strerror(errno));
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
	ndc.format("PID[{:d}]", stream_pid);
	log4cxx::NDC(ndc.c_str());
	int dmx_buffer_size = 32 * 1024 * 1024;
	dvb_stream_reader_t dvb_reader(active_adapter, dmx_buffer_size);
	int stream_fd = dvb_reader.open(stream_pid, epoll, epoll_flags);
	if (stream_fd < 0)
		return -1;

	auto flags = fcntl(stream_fd, F_GETFD);
	if (flags < 0) {
		dterrorf("fcntl failed: {}", strerror(errno));
		return -1;
	}
	if (fcntl(stream_fd, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
		dterrorf("Could not clear FD_CLOEXEC: {}", strerror(errno));
		return -1;
	}
	ss::string<32> pid_;
	pid_.format("{:d}", stream_pid);
	const char* cmd ="tsp";
	ss::vector<const char*,16> args = {{cmd,
			"--realtime", "--initial-input-packets", "256", "-P", "t2mi", "--pid", pid_.c_str(),
													// @todo: "--plp", plp.cstr()
			(char*)nullptr}};
	std::tie(data_fd, command_pid) = start_command(stream_fd, cmd, args);
	if (data_fd < 0) {
		dterrorf("Could not start command");
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
			dterrorf("end stream closed\n");
			return -1;
		} else if (ret < 0) {
			if (errno == EAGAIN) {
				data_ready = false;
				break;
			}
			if (errno == EINTR)
				continue;
			else {
				dterrorf("read from command failed: {}", strerror(errno));
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
			dterrorf("Reader already registered");
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
				 chdb::scan_in_progress(chdb::mux_common_ptr(mux)->scan_id));

	return stream_filter->embedded_mux; }

void embedded_stream_reader_t::on_stream_mux_change(const chdb::any_mux_t& mux) {
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::scan_in_progress(chdb::mux_common_ptr(mux)->scan_id));
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
				 chdb::scan_in_progress(chdb::mux_common_ptr(mux)->scan_id));
	stream_filter->embedded_mux = embedded_mux;
}

void embedded_stream_reader_t::update_stream_mux_nit(const chdb::any_mux_t& stream_mux) {
	auto & mux = stream_mux;
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::scan_in_progress(chdb::mux_common_ptr(mux)->scan_id));
	stream_filter->embedded_mux = stream_mux;
}

pid_t streamer_t::start() {
	auto * pservice = get_service();
	ss::string<32> service_id_;
	ss::string<32> sid_pmt_;
	ss::string<32> dest;
	dest.format("{}:{:d}", stream.dest_host, stream.dest_port);
	const char* cmd ="tsp";
	ss::vector<const char*, 16> args = {cmd};
	auto t2mi_pid = this->get_t2mi_pid();
	if(t2mi_pid >=0) {
		ss::string<32> t2mi_pid_;
		t2mi_pid_.format("{:d}", t2mi_pid);
		for(auto& a: {"-P", "t2mi", "--pid", (const char*)t2mi_pid_.c_str()}) {
			args.push_back(a);
		}
	}
	if(pservice) {
		service_id_.format("{:d}", pservice->k.service_id);
		sid_pmt_.format("{:d}/{:d}", pservice->k.service_id, pservice->pmt_pid);
		//TODO: pat rewriting (requires service_id)

		for(auto& a: {
				"--realtime", "--initial-input-packets", "256",
				"-P", "filter", "--pid" , "0",
				"--service", (const char*)service_id_.c_str(),
				"-P", "filter", "--pid", "0", "--negate", "--stuffing", // replace PAT will null packets
				"-P", "pat", "--create",
				"--add-service", (const char*)sid_pmt_.c_str(), //created new single service PAT
				//"--inter-packet", "200",
				"-O", "ip", "--enforce-burst", //"--rtp",
				"--packet-burst", "128",
				(const char*) dest.c_str(), (const char*)nullptr}) {
			args.push_back(a);
		}
	} else {
		for(auto& a: {
				"--realtime", "--initial-input-packets", "256",
				"-O", "ip", "--enforce-burst", //"--rtp",
				"--packet-burst", "128",
				//"--buffer-size", "33554432",
				(const char*) dest.c_str(),
				//"-O" , "file", "/tmp/test.ts",
				(const char*)nullptr}) {
			args.push_back(a);
		}
	}
	auto [data_fd, command_pid] = start_command(fd, cmd, args); //stream
	stream.streamer_pid = command_pid;
	stream.owner = getpid();
	stream.mtime = system_clock_t::to_time_t(now);
	assert(stream.subscription_id >=0);
	assert(stream.stream_state == devdb::stream_state_t::ON);
	if (data_fd < 0) {
		dterrorf("Could not start command");
		return -1;
	}
	::close(fd);
	fd = -1;
	return command_pid;
}

void streamer_t::stop() {
	assert(stream.streamer_pid > 0);
	if (kill(stream.streamer_pid, SIGHUP) < 0) {
		dterrorf("Error while sending signal: {}", strerror(errno));
	}
	if (waitpid(stream.streamer_pid, nullptr, 0) < 0) {
		dterrorf("Error during wait: {}", strerror(errno));
	}
	stream.streamer_pid = -1;
	stream.owner = -1;
	stream.mtime = system_clock_t::to_time_t(now);
	assert(stream.subscription_id >=0);
	stream.stream_state = devdb::stream_state_t::OFF;
}
