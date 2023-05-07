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
#include "active_adapter.h"
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

	//needs to be atomic to ensure that threads see the latest value; a weaker form would suffice
	std::atomic_int write_pointer{0};

	int data_ready{false}; //external command has returned additional data

	int data_fd{-1}; //where external commands returns its data to
	std::unique_ptr<uint8_t> send; /*data we received from dvb device and which will send to the external
																					command but which has  not been fully tranitted*/
	bool read_and_process_data();
public:

	stream_filter_t(active_adapter_t& active_adapter, const chdb::any_mux_t& mux,
									epoll_t* epoll, int epoll_flags = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET);

	inline int available_for_write();


	~stream_filter_t()
		{
			close();
			assert (stream_readers.size()==0);
		}

	int open();
	void close();
	int start();
	void stop();
	inline bool is_open() const {
		bool ret = data_fd >=0;
		assert (ret? (command_pid>0) : (command_pid<0));
		return ret;
	}
	inline int read_external_data();

	template<typename... Args>
	int start_command(int stream_fd, const char* pathname, Args...args);


	void register_reader(embedded_stream_reader_t* reader);
	void unregister_reader(embedded_stream_reader_t* reader);
	void notify_other_readers(embedded_stream_reader_t* reader);
};
