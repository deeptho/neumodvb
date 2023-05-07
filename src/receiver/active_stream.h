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
#include <vector>
#include <variant>
#include <atomic>
#include <optional>
#include <unistd.h>
#include <sys/epoll.h>

#include "stackstring.h"
#include "util/logger.h"
#include "util/util.h"

#ifndef null_pid
#define null_pid (0x1fff)
#endif

namespace chdb {
	struct dvbs_mux_t;
	struct dvbc_mux_t;
	struct dvbt_mux_t;
	struct mux_key_t;
	typedef std::variant<chdb::dvbs_mux_t, chdb::dvbc_mux_t, chdb::dvbt_mux_t> any_mux_t;
}

namespace devdb {
	struct lnb_key_t;
}


struct db_txn;
struct tune_confirmation_t;
class stream_filter_t;

struct pid_with_use_count_t {
	uint16_t pid{null_pid};
	int16_t use_count{1};
	pid_with_use_count_t(uint16_t pid_):
		pid(pid_)
		{}

};

struct tune_options_t;
class active_adapter_t;
class epoll_tx1;

class stream_reader_t : public std::enable_shared_from_this<stream_reader_t> {
	constexpr static  std::chrono::duration data_timeout = 10000ms; //in ms
	steady_time_t last_data_time{};

public:
	ssize_t num_read{0};
	uint16_t embedded_pid{0};
	active_adapter_t& active_adapter;

	epoll_t* epoll{nullptr};
	int epoll_flags;

protected:
	stream_reader_t(active_adapter_t& active_adapter)
		: last_data_time(steady_clock_t::now())
		, active_adapter(active_adapter)
		{}

public:

		bool no_data() const {
			return num_read==0  &&  (steady_clock_t::now() - last_data_time) >= data_timeout;
		}

		void data_tick() {
			last_data_time = steady_clock_t::now();
		}


	virtual bool is_open() const {
		return epoll != nullptr;
	}

	virtual int open(uint16_t initial_pid, epoll_t* epoll, int epoll_flags) {
		return -1;
	}

	virtual void close() {
	}

	virtual void reset() final {
		num_read = 0;
		data_tick();
	}


	virtual ~stream_reader_t() {
	}

	virtual chdb::any_mux_t stream_mux() const = 0;
	const tune_options_t& tune_options() const;

	virtual inline void on_stream_mux_change(const chdb::any_mux_t& mux) =0;
	virtual inline void update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad) =0;

	virtual inline void set_current_tp(const chdb::any_mux_t& stream_mux) const = 0;
	void  update_stream_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation);

	//stream_mux is the currently active mux, which is the embedded mux for t2mi and the tuned_mux in other cases
	virtual void update_stream_mux_nit(const chdb::any_mux_t& stream_mux) = 0;

	virtual inline bool on_epoll_event(const epoll_event* evt) = 0;

	virtual inline std::tuple<uint8_t*, ssize_t> read(ssize_t size=-1)  = 0;
	virtual inline void discard(ssize_t bytes)  =0;
	virtual ssize_t read_into(uint8_t* p, ssize_t to_read, const std::vector<pid_with_use_count_t>* pids = nullptr) = 0;


	virtual inline int add_pid(int pid) {
		assert(0);
		return -1;
	}

	virtual inline int remove_pid(int pid) {
		assert(0);
		return -1;
	}

	virtual std::shared_ptr<stream_reader_t> clone(ssize_t buffer_size=-1) const {
		assert(0);
		return nullptr;
	}

	int16_t get_sat_pos() const;
	virtual int embedded_stream_pid() const {
		return -1;
	}
};


struct dvb_stream_reader_t final : public stream_reader_t {
	const ssize_t dmx_buffer_size;
	int demux_fd = -1; /*file descriptor used with  DMX_OUT_TSDEMUX_TAP (all pids in a single transport stram
											 can be read from this fd */

	int read_pointer{0}; //location in buffer where client will read next
	std::unique_ptr<uint8_t[]> bufferp{nullptr};

	dvb_stream_reader_t(active_adapter_t & active_adapter, ssize_t dmx_buffer_size_ = -1)
		: stream_reader_t(active_adapter)
		, dmx_buffer_size(dmx_buffer_size_ <0 ?  32*1024L*1024 : dmx_buffer_size_)
		{}

	virtual ~dvb_stream_reader_t() {
		close();
	}
	inline virtual bool is_open() const {
		return demux_fd >= 0;
	}

	virtual inline bool on_epoll_event(const epoll_event* evt) {
		return demux_fd == (evt->data.u64 & 0xffffffff);
	}
	virtual int open(uint16_t initial_pid, epoll_t* epoll,
									 int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET);
	virtual void close();

	virtual inline void set_current_tp(const chdb::any_mux_t& mux) const;
	virtual chdb::any_mux_t stream_mux() const;
	virtual inline void on_stream_mux_change(const chdb::any_mux_t& mux);
	virtual inline void update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad);

	//stream_mux is the currently active mux, which is the embedded mux for t2mi and the tuned_mux in other cases
	virtual void update_stream_mux_nit(const chdb::any_mux_t& stream_mux);
/*
		retuns a buffer range which has valid data, or the return value ret of the read call
		Alwas return a multiple of the packet size
	 */
	virtual inline std::tuple<uint8_t*, ssize_t> read(ssize_t size=-1) {
		assert(read_pointer < 188);
		auto *p = bufferp.get();
		if(!p) {
			bufferp =std::make_unique<uint8_t[]>(dmx_buffer_size);
			p = bufferp.get();
		}
		ssize_t toread = dmx_buffer_size - read_pointer;
		if(size >0)
			toread = std::min(toread, size);
		ssize_t ret= ::read(demux_fd, p + read_pointer, toread);
		if(ret>0) {
			ret += read_pointer;
			read_pointer = ret;
		}
		if(ret>0)
			num_read+=ret;
		return  {p, ret};
	}

	virtual inline ssize_t read_into(uint8_t* p, ssize_t toread, const std::vector<pid_with_use_count_t>* pids = nullptr) {
		ssize_t ret = ::read(demux_fd, p, toread);
		return ret;
	}

	virtual inline void discard(ssize_t num_bytes) {
		assert(num_bytes<= read_pointer);
		auto delta = read_pointer - num_bytes;
		if(delta>0)
			memmove(bufferp.get(), bufferp.get() + num_bytes, delta);
		read_pointer = 0;
	}

	virtual int add_pid(int pid);


	virtual inline int remove_pid(int pid);

	virtual std::shared_ptr<stream_reader_t> clone(ssize_t buffer_size = -1) const {
		return std::make_shared<dvb_stream_reader_t>(active_adapter,
																								 buffer_size < 0 ? dmx_buffer_size : buffer_size);
	}

};


class embedded_stream_reader_t final : public stream_reader_t {
	friend class stream_filter_t;
	std::shared_ptr<stream_filter_t> stream_filter;
	int master_read_count{0}; //for debugging
	//needs to be atomic to ensure that threads see the latest value; a weaker form would suffice
	std::atomic_int read_pointer{0}; //location in buffer where client will read next
	int last_range_end_pointer{0}; //location in buffer where last read() ends

	event_handle_t notifier;

	/*
		parent receives two file descriptors to epoll_wait on.
		It as to call on_epoll_event() when one of them is
		in the epoll list and then read
	 */


public:
	embedded_stream_reader_t(active_adapter_t& adapter,
													 const std::shared_ptr<stream_filter_t>& stream_filter);

	virtual int embedded_stream_pid() const;

	virtual bool is_open() const {
		return epoll != nullptr;
	}

	virtual int open(uint16_t initial_pid, epoll_t* epoll,
									 int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET);

	virtual void close();


	virtual bool on_epoll_event(const epoll_event* evt);

	virtual inline int add_pid(int pid) {
		return 0; //we don't care
	}

	virtual inline int remove_pid(int pid) {
		return 0;
	}


	virtual std::shared_ptr<stream_reader_t> clone(ssize_t buffer_size=-1) const {
		auto ret = std::make_shared<embedded_stream_reader_t>(active_adapter, stream_filter);
		return ret;
	}



	/*
		obtain a memory address and a size in which data can be read
	 */
	virtual std::tuple<uint8_t*, ssize_t> read(ssize_t size=-1);
	virtual ssize_t read_into(uint8_t* p, ssize_t toread, const std::vector<pid_with_use_count_t>* pids = nullptr);

	virtual inline void discard(ssize_t num_bytes);

	virtual ~embedded_stream_reader_t() {
		notifier.close();
		close();
	}

	virtual chdb::any_mux_t stream_mux() const;
	virtual void set_current_tp(const chdb::any_mux_t& mux) const;

	//stream_mux is the currently active mux, which is the embedded mux for t2mi and the tuned_mux in other cases
	virtual void update_stream_mux_nit(const chdb::any_mux_t& stream_mux);

	virtual void on_stream_mux_change(const chdb::any_mux_t& mux);
	virtual void update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad);

};


class receiver_t;
class tuner_thread_t;


/*
	partial transport stream, with functionality to add/remove pids and such
 */
class active_stream_t  {
	//helper class; public members should be avoided
public:
	receiver_t & receiver;
protected:
	std::shared_ptr<stream_reader_t> reader;

	std::vector<pid_with_use_count_t> open_pids; //list of opened pids; should not contain duplicates


	virtual ss::string<32> name() const;
	//virtual void log4cxx::NDC(name()) const;
	int open(uint16_t initial_pid, epoll_t* epoll,
					 int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET);
	void close();

	//!register a pid; may_exist supresses warning if pid was already registered
	int add_pid(uint16_t pid);

	void remove_pid(uint16_t pid);
	void remove_all_pids();
	int deactivate();
	inline bool is_open() const {
		return reader->is_open();
	}

public:
	active_stream_t(active_stream_t&& other) :
		receiver(other.receiver) {
		reader = std::move(other.reader);
		open_pids = std::move(other.open_pids);
	}

	inline active_adapter_t& active_adapter() const {
		assert(reader);
		return reader->active_adapter;
	}

	inline ssize_t dmx_buffer_size() const {
		auto p = std::dynamic_pointer_cast<dvb_stream_reader_t>(reader);
		assert(p.get());
		return p.get() ? p->dmx_buffer_size : -1;
	}

	int get_adapter_no() const; //thread safe because it only accesses constant members
	int64_t get_adapter_mac_address() const; //thread safe because it only accesses constant members
	devdb::lnb_key_t get_adapter_lnb_key() const; //thread safe because it only accesses constant members

	//void process_psi(int pid, unsigned char* payload, int payload_size);
	active_stream_t(receiver_t& receiver, const std::shared_ptr<stream_reader_t>& reader)
		: receiver (receiver)
		, reader(std::move(reader))
		{ //TODO make tuner const; prevented by simgr.h
		}

	virtual ~active_stream_t() {
		if(is_open())
			close();
		dtdebug("~active_stream_t\n");
	}
};
