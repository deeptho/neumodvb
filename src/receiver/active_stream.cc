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

//classes to manage a single tuner
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <stdint.h>
#include <resolv.h>
#include <fcntl.h>
#include <signal.h>
#include <values.h>
#include <string.h>
#include <syslog.h>
//#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <linux/dvb/version.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <linux/limits.h>
#include <sys/sendfile.h>
#include <pthread.h>
#include <linux/dvb/dmx.h>
#include <dirent.h>
#include <algorithm>
#include <set>

#include "util/dtutil.h"
#include "receiver.h"

#include "active_adapter.h"
#include "active_service.h"
#include "neumo.h"
#include "filemapper.h"
#include "streamparser/packetstream.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

int active_stream_t::get_adapter_no() const {
	return active_adapter().get_adapter_no();
}

int64_t active_stream_t::get_adapter_mac_address() const {
	return active_adapter().get_adapter_mac_address();
}

devdb::lnb_key_t active_stream_t::get_adapter_lnb_key() const {
	return active_adapter().get_lnb_key();
}

ss::string<32> active_stream_t::name() const
{
	ss::string<32> ret;
	ret.format("stream[{:d}]-{:p}: ", get_adapter_no(), fmt::ptr(this));
	return ret;
}



//return -1 on error
/*
  @brief open the demux, create a pes filter and ask to read the pat;
	PAT will be received as transport stream
  TODO: if we know pmt_pid we could also read the pmt
 */
int dvb_stream_reader_t::open(uint16_t initial_pid, epoll_t* epoll, int epoll_flags)
{
	this->epoll = epoll;
	this->epoll_flags = epoll_flags;
	if(demux_fd>=0) {
		dterrorf("Implementation error: multiple opens");
		return demux_fd;
	}

	demux_fd = active_adapter.open_demux();
	if(demux_fd<0) {
		dterrorf("Cannot open demux: {}", strerror(errno));
		return demux_fd;
	}
	dtdebugf("OPEN DEMUX_FD={}", demux_fd);

	epoll->add_fd(demux_fd, epoll_flags);

	uint16_t pid= initial_pid;
	dtdebugf("Adding pid={}", pid);
	struct dmx_pes_filter_params pesFilterParams;
	memset(&pesFilterParams,0,sizeof(pesFilterParams));
	pesFilterParams.pid = pid;
	pesFilterParams.input = DMX_IN_FRONTEND;
	pesFilterParams.output = DMX_OUT_TSDEMUX_TAP;//DMX_OUT_TS_TAP;
	pesFilterParams.pes_type = DMX_PES_OTHER;
	pesFilterParams.flags = 0; //DMX_IMMEDIATE_START;
	if(ioctl(demux_fd, DMX_SET_BUFFER_SIZE, dmx_buffer_size)) {
		dterrorf("DMX_SET_BUFFER_SIZE failed: {}", strerror(errno));
	}
	if (ioctl(demux_fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0) {
		dterrorf("DMX_SET_PES_FILTER  pid={} failed: {}", pid, strerror(errno));
		return -1;
	}
	if(ioctl (demux_fd, DMX_START)<0) {
		dterrorf("DMX_START FAILED: {}", strerror(errno));
	}

	return demux_fd;
}



int active_stream_t::open(uint16_t initial_pid, epoll_t* epoll, int epoll_flags)
{
	log4cxx::NDC(name());
	if(reader->open(initial_pid, epoll, epoll_flags) >=0) {
		//note that pat pid has already been activated!
		open_pids.push_back(pid_with_use_count_t(initial_pid));
		return 0;
	}
	return -1;
}

void dvb_stream_reader_t::close() {
	if(demux_fd<0) {

		return;
	}
	dtdebugf("closing demux_fd={:d}", demux_fd);
	epoll->remove_fd(demux_fd);
	if(::close(demux_fd)<0) {
		dterrorf("Cannot close demux: {}", strerror(errno));
	} else {
		dtdebugf("Closed demux_fd");
	}
	demux_fd = -1;
}

void active_stream_t::close() {
	log4cxx::NDC(name());
	reader->close();
	open_pids.clear();
}


/**
	 @brief Add pid to the transport stream for this channel. The file descriptor has to be
	 opened before. I.e., it will ask the card for this PID.
	 In case the pid is already opened, the existing one is returned
	 Pid will be received as a transport stream on a single file descriptor shared with other pids

	 @param pid the pid for the filter
	 Output is sent to /dev/dvb/adapter{:d}/demux{:d}
	 Returns 0 or -1
*/
int active_stream_t::add_pid(uint16_t pid)
{
	log4cxx::NDC(name());
	if(pid==null_pid) //dummy pid
		return -1;
	assert(reader->is_open());

	for(auto& x: open_pids) {
		if(x.pid == pid) {
			assert(x.use_count>0);
			x.use_count++;
			dtdebugf("registering duplicate pid={}", pid);
			return 0;
		}
	}
	dtdebugf("Adding pid={} to channel transport stream", pid);
	open_pids.push_back(pid_with_use_count_t(pid));
	if(reader->add_pid(pid)<0) {
		dterrorf("DMX_ADD_PID {} FAILED: {}", pid, strerror(errno));
		return -1;
	}

	return 0;
}

/**
 * @brief Remove a single pid from a transport stream
*/
void active_stream_t::remove_pid(uint16_t pid)
{
	log4cxx::NDC(name());
	if(pid==0x1fff)
		return;
	if(!reader->is_open()) {
		dterrorf("remove_pid with demux_fd<0");
		return;
	}

	for(auto& x: open_pids) {
		if(x.pid == pid) {
			assert(x.use_count >0);
			if(--x.use_count == 0) {
				if(reader->remove_pid(pid)<0) {
					dterrorf("DMX_REMOVE_PID {} FAILED: {}", pid, strerror(errno));
				} else {
					dtdebugf("DMX_REMOVE_PID {}", pid );
				}
				int idx  = &x - &open_pids[0];
				open_pids.erase(open_pids.begin() + idx);
			}
			return;
		}
	}
}

/**
 * @brief Remove all pids from a transport stream
*/
void active_stream_t::remove_all_pids()
{
	log4cxx::NDC(name());
	if(!reader->is_open()) {
		//dterrorf("remove_pid with reader->demux_fd<0");
		return;
	}

	for(auto &x: open_pids) {
		assert(x.use_count>0);
		if(reader->remove_pid(x.pid)<0) {
			dterrorf("DMX_REMOVE_PID {} FAILED: {}", x.pid, strerror(errno));
		}
	}
	open_pids.clear();
}


int active_stream_t::deactivate()
{
	log4cxx::NDC(name());
	remove_all_pids();
	close();
	return 0;
}


chdb::any_mux_t dvb_stream_reader_t::stream_mux() const {
	auto mux =active_adapter.current_tp();
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::scan_in_progress(chdb::mux_common_ptr(mux)->scan_id));

	return active_adapter.current_tp();
}


int16_t stream_reader_t::get_sat_pos() const
{
	auto stream_mux = this->stream_mux();
	auto* stream_mux_key = mux_key_ptr(stream_mux);
	return stream_mux_key->sat_pos;
}


void dvb_stream_reader_t::set_current_tp(const chdb::any_mux_t& mux) const
{
	active_adapter.set_current_tp(mux);
}

const subscription_options_t& stream_reader_t::tune_options() const
{
	auto& tune_options = active_adapter.fe->ts.readAccess()->tune_options;
	return tune_options;
}

void dvb_stream_reader_t::on_stream_mux_change(const chdb::any_mux_t& stream_mux)
{
	active_adapter.on_tuned_mux_change(stream_mux); //for active_adapter stream_mux == tuned_mux
}

void dvb_stream_reader_t::update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad)
{
	active_adapter.update_received_si_mux(mux, is_bad);
}



void  stream_reader_t::update_stream_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation)
{
	//for active_adapter stream_mux == tuned_mux
	active_adapter.update_tuned_mux_tune_confirmation(tune_confirmation);
}


void dvb_stream_reader_t::update_stream_mux_nit(const chdb::any_mux_t& stream_mux)
{
	//for active_adapter stream_mux == tuned_mux
	active_adapter.update_tuned_mux_nit(stream_mux);
}



int dvb_stream_reader_t::add_pid(int pid) {
	if(ioctl (demux_fd, DMX_ADD_PID, &pid)<0) {
		dterrorf("DMX_ADD_PID {} FAILED: ", pid, strerror(errno));
		return -1;
	}
	return 0;
}


int dvb_stream_reader_t::remove_pid(int pid) {
	if(ioctl (demux_fd, DMX_REMOVE_PID, &pid)<0) {
		dterrorf("DMX_REMOVE_PID {} FAILED: {}", pid, strerror(errno));
		return -1;
	} else
		dtdebugf("DMX_REMOVE_PID {}",  pid);
	return 0;
}
