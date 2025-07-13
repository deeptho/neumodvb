/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "active_adapter.h"
#include "neumodb/chdb/chdb_extra.h"
#include "util/dtassert.h"
#include <memory>
#include <atomic>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <mutex>
#pragma once
class embedded_stream_reader_t;
class stream_filter_t;

class stream_filter_t {
protected:
	std::mutex m;
	friend class embedded_stream_reader_t;
	constexpr static int dmx_buffer_size{32*1024L*1024L};
	//data for the master stream
	active_adapter_t& active_adapter;

	chdb::any_mux_t embedded_mux;
	epoll_t * epoll{nullptr};
	int epoll_flags = (int) (EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET);
	ss::vector<std::shared_ptr<embedded_stream_reader_t>, 4> stream_readers;
	bool error{false};
	pid_t command_pid{-1};

	//struct subscription_t;

	const int buff_size{16777120}; //approx 16*1024*1024, multiple of 188
	std::unique_ptr<uint8_t[]> bufferp; /*data we received from dvb device and which will send to the external
																		 command but which has  not been fully tranitted*/

	//needs to be atomic to ensure that threads see the latest value; (TODO: a weaker form of barrier would suffice)
	std::atomic_int write_pointer{0};

	int data_ready{false}; //external command has returned additional data

	int data_fd{-1}; //where external commands returns its data to


protected:
	virtual bool on_epoll_event(const epoll_event* evt, embedded_stream_reader_t* reader, event_handle_t& notifier) =0;
	virtual bool read_and_process_data() =0;
	virtual void open() = 0;
	virtual void close() = 0;

	std::tuple<std::unique_ptr<dvb_stream_reader_t>, int> open_dvb_reader();

	int read_data();

protected:

	stream_filter_t(active_adapter_t& active_adapter, const chdb::any_mux_t& embedded_mux,
									epoll_t* epoll, int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET)
		: active_adapter(active_adapter)
		, embedded_mux(embedded_mux)
		, epoll(epoll)
		, epoll_flags(epoll_flags)
		,	bufferp(std::make_unique<uint8_t[]>(dmx_buffer_size)) {
	}

	~stream_filter_t() {
		assert (stream_readers.size()==0);
	}

	inline int available_for_write();


	pid_t start();

	virtual inline bool is_open() const {
		bool ret = data_fd >=0;
		assert (ret? (command_pid>0) : (command_pid<0));
		return ret;
	}

	void register_reader(embedded_stream_reader_t* reader);
	void unregister_reader(embedded_stream_reader_t* reader);
	void notify_other_readers(embedded_stream_reader_t* reader);
};


class t2mi_stream_filter_t : public stream_filter_t {
public:
	t2mi_stream_filter_t(active_adapter_t& active_adapter, const chdb::any_mux_t& embedded_mux,
											 epoll_t* epoll, int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET)
		: stream_filter_t(active_adapter, embedded_mux, epoll, epoll_flags) {
  }

	virtual bool on_epoll_event(const epoll_event* evt, embedded_stream_reader_t* reader,
															event_handle_t& notifier) override;
	virtual bool read_and_process_data() final;
	virtual void open() final;
	virtual void close() final;

	virtual ~t2mi_stream_filter_t() {
		close();
	}
};

class ts_in_ts_stream_filter_t : public stream_filter_t {
	chdb::service_t embedding_service;
	std::shared_ptr<active_service_t> active_servicep; //must be a shared_ptr because shared_from_this used elsewhere
public:
	ts_in_ts_stream_filter_t(active_adapter_t& active_adapter,
													 const chdb::any_mux_t& embedded_mux,
													 const chdb::service_t& embedding_service,
													 epoll_t* epoll, int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET)
		: stream_filter_t(active_adapter, embedded_mux, epoll, epoll_flags),
			embedding_service(embedding_service) {}

	virtual ~ts_in_ts_stream_filter_t() {
		close();
	}

	virtual bool on_epoll_event(const epoll_event* evt, embedded_stream_reader_t* reader,
															event_handle_t& notifier) override;
	virtual bool read_and_process_data() final{
		assert(false);
		return false;
	};

	virtual inline bool is_open() const override {
		return true;
	}


	virtual void open() final;
		 virtual void close() final;
		 void read_data(uint8_t* buffer, int num_bytes);
};


//external command sending an ip stream
class streamer_t {
	friend class active_adapter_t;
	int fd{-1};
	devdb::stream_t stream;
public:
	streamer_t(int fd_, const devdb::stream_t& stream_)
		: fd(fd_)
		, stream(stream_)
		{}
	inline const chdb::service_t* get_service() const {
		return std::get_if<chdb::service_t>(&stream.content);
	}
	inline int get_t2mi_pid() const {
		return std::visit([](auto& record) -> int16_t {
			if constexpr (is_same_type_v<chdb::service_t, decltype(record)>) {
				return record.k.mux.t2mi_pid;
			} else {
				return record.k.t2mi_pid;
			}
			return -1;
		}, stream.content);
	}
	int start();
	void stop();
	pid_t get_streamer_pid() const {
		return stream.streamer_pid;
	}
};
