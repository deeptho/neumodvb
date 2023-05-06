/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
 *
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
 * Code adapted from mumudvb http://www.mumudvb.net/ and https://github.com/manio/vdr-plugin-dvbapi
 * Adaptations and modifications by DeepThought 2014
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "receiver.h"
#include <future>

#include "./dvbapi.h"
#include "scam.h"
#include "streamparser/streamparser.h"
#include "util/dtutil.h"
#include "util/logger.h"

extern "C" {
#include <dvbcsa/dvbcsa.h>
};

static bytebuffer<512> last_filtered_data;
static int last_filtered_data_count{0};


void ca_filter_t::update(const ca_filter_t& other) {
	dmx_filter = other.dmx_filter;
	assert(pid == other.pid);
	assert(demux_no == other.demux_no);
	timeout = other.timeout;
	flags = other.flags;
	// preserve request_time
	// preserve request_table_id
	// preserve msg_id
}

ss::string<32> active_scam_t::name() const {
	ss::string<32> ret;
	ret.sprintf("scam[%d]", adapter_no);
	return ret;
}

active_scam_t::active_scam_t(scam_t* parent_, receiver_t& receiver, tuner_thread_t& tuner_thread,
														 active_service_t& active_service)
	: active_stream_t(receiver, active_service.clone_stream_reader(220*dtdemux::ts_packet_t::size)) //10 sections
	, parent (parent_)
	, adapter_no(active_service.get_adapter_no())
{
	stream_parser.psi_cb = [this](uint16_t pid, const ss::bytebuffer_& buffer) {
		this->scam_send_filtered_data(pid, buffer);
	};
}

static int tcp_socket(int udp) {
	int sockfd = -1;
	if (udp)
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	else
		sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sockfd < 0) {
		dterror("socked failed: " << strerror(errno));
		return sockfd;
	}
	signal(SIGPIPE, SIG_IGN);

	if (!udp) {
		int one = 1;
		if ((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(int)) == -1) ||
				(setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&one, sizeof(int)) == -1) ||
				(setsockopt(sockfd, SOL_TCP, TCP_NODELAY, (char*)&one, sizeof(int)) == -1))
			dterror("setsockopt  failed: " << strerror(errno));
	}
	return sockfd;
}

/** Returns true on success, or false if there was an error */
static inline bool set_socket_blocking_enabled(int fd, bool blocking) {
	if (fd < 0)
		return false;

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return false;
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}

int tcp_connect(const char* hostname, int port, bool blocking, bool udp) {
	struct sockaddr_in addr;
	struct hostent* hp = gethostbyname(hostname);
	if (!hp) {
		dterror("unknown host: " << hostname);
		return -1;
	}
	addr.sin_family = hp->h_addrtype;
	addr.sin_port = htons(port);
	memset(&(addr.sin_zero), 0, 8);
	addr.sin_addr.s_addr = inet_addr(hostname);
	memcpy((char*)&addr.sin_addr, hp->h_addr, hp->h_length);
	auto sockfd = tcp_socket(udp);
	if (!udp) {
		if (connect(sockfd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
			dterror("tcp connect failed: " << strerror(errno));
			return -1;
		}
	}
	if (!blocking) {
		bool ret = set_socket_blocking_enabled(sockfd, blocking);
		if (!ret)
			dtinfo("setting non-block disabled");
	}
	return sockfd;
}

int scam_t::connect() {
	error = false;
	const bool blocking = false;
	const bool udp = false;
	auto& options = receiver.receiver.options;
	auto server = options.readAccess()->scam_server_name;
	auto port = options.readAccess()->scam_server_port;
	scam_fd = tcp_connect(server.c_str(), port, blocking, udp);
	error = (scam_fd < 0);
	if (!error)
		scam_thread.epoll_add_fd(scam_fd, EPOLLIN | EPOLLERR | EPOLLHUP);
	return error ? -1 : 1;
}

void scam_t::close() {
	if (scam_fd < 0)
		return;
	scam_thread.epoll_remove_fd(scam_fd);
	for (;;) {
		int ret = ::close(scam_fd);
		if (ret == 0)
			break;
		else if (ret < 0 && errno != EINTR) {
			dterror("Error while closing fd=" << scam_fd << " :" << strerror(errno));
		}
	}
	error = false;
	scam_fd = -1;
}

active_scam_t::~active_scam_t() { close(); }


/*!
	Connect to oscam and send greeting; returns 0 on success, -1 on error.
*/
int scam_t::open() {

	assert(scam_fd < 0);
	if (active_scams.size() == 0) {
		dtdebug("no need to connect to oscam");
		return 0;
	}

	if (connect() < 0 || greet_scam() < 0 || send_all_pmts() < 0) { // in case this is a reopen
		must_reconnect = true;
		return -1;
	}
	return 1;
}

scam_t::scam_t(receiver_thread_t& _receiver, scam_thread_t& scam_thread_)
	: receiver(_receiver), scam_thread(scam_thread_) {
}

scam_t::~scam_t() {
	dtdebugx("scam_t destroyed");
	this->close();
}

/*!
	Send initial greeting to oscam.
	returns 1 if all output was successfully written,
	0 if not everything was written, -1 on error
*/
int scam_t::greet_scam() {
	pending_write_buffer.clear();
	// int out_cursor= 0;
	ss::bytebuffer<512> out_msg; // current message, perhaps not fully written

	const char* client_name = "NeumoDVB";

	out_msg.append_raw(native_to_net((uint32_t)DVBAPI_CLIENT_INFO));
	out_msg.append_raw(native_to_net((uint16_t)scam_protocol_version));
	out_msg.append_raw(native_to_net((uint8_t)(strlen(client_name) + 1)));
	out_msg.append_raw(client_name, strlen(client_name) + 1);
	return write_to_scam(out_msg);
}

int scam_t::scam_send_capmt(const pmt_info_t& pmt_info, capmt_list_management_t lm, int adapter_no, int demux_device) {

	ss::bytebuffer<512> out_msg; // current message, perhaps not fully written
	if (scam_protocol_version >= 3) {
		out_msg.append_raw(native_to_net((uint8_t)0xa5));
		out_msg.append_raw(native_to_net((uint32_t)0));
	}

	// creating this object will prepare data and store it in out_msg
	ca_pmt_t capmt(out_msg, lm, pmt_info, adapter_no, demux_device);
	//write the data
	auto ret = write_to_scam(out_msg);
	if (ret < 0) {
		must_reconnect = true;
		return -1;
	}
	return ret;
}

int scam_thread_t::exit() {
	dtdebugx("scam exit");
	check_thread();
	return 0;
}

/*! write data to scam until the connection would bloc
	return -1 on error,
	or number of bytes written, whuch may be less than size
*/
static int write_until_block(int fd, uint8_t* buffer, int size) {
	for (int written = 0; written < size;) {
		auto ret = ::write(fd, buffer + written, size - written);
		if (ret > 0) {
			written += ret;
			continue;
		}
		if (errno == EINTR)
			continue;
		else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return written;
		} else {
			dterror("Error while writing to scam: " << strerror(errno));
			return -1;
		}
	}
	return size;
}

/*!
	Write to scam and wait for the write to complete. We count on tcp buffers being large enough
	so that writes will not stall in normal operation.

	If a stall occurs, then we start processing commands from the task queue. It is essential
	that such tasks do not write data, because writing is not possible then.
*/
int scam_t::write_to_scam_(uint8_t* buffer, int size) {
	auto n = size;
	for (;;) {
		auto ret = write_until_block(scam_fd, buffer, n);
		if (ret == size)
			return size; // success
		if (ret < 0)
			return ret; // fatal error or reconnect
		n -= ret;
		buffer += ret;
		/* output not fully written;
			 task queue commands may may place additional output in pending_write_buffer, but may not perform
			 actual writes to the network.

			 It is also essential that we do not call the dvb input stream parser, because "write_to_scam_  is
			 called by the scam dvb stream pareser and would end up calling write_to_scam_ recursively and then
			 getting blocked again because of the stalled write
		*/
		bool do_not_handle_demux_events = true;
		ret = scam_thread.wait_for_and_handle_events(do_not_handle_demux_events); // single run of the event loop
		if (ret < 0)
			return ret;
	}
	assert(0);
	return -1;
}

/*! flush all pending writes to scam.
	returns 1 if data was actually written, 0 if no data needed to be written,
	-1 on error
*/
int scam_t::scam_flush_write() {
	assert(!write_in_progress);
	write_in_progress = true;
	// process any pending write
	if (pending_write_buffer.size() > 0) {
		auto ret = write_to_scam_(pending_write_buffer.buffer(), pending_write_buffer.size());
		assert(ret < 0 || ret == pending_write_buffer.size());
		pending_write_buffer.clear();
		write_in_progress = false;
		return ret;
	}
	write_in_progress = false;
	return 0;
}

/*! write data to the scam tcp connection, but if wrting blocks, then write the data
	to abuffer instead.
 */
int scam_t::write_to_scam(ss::bytebuffer_& msg) {
	static bytebuffer<64> last;
	if (msg == last) {
		dtdebug_nice("Duplicate message sent\n");
	}
	last = msg;
	dtdebug(msg);
	if (scam_fd < 0)
		return -1;
	if (write_in_progress) {
		pending_write_buffer.append_raw(msg.buffer(), msg.size());
		return 0;
	}
	// process any pending write
	auto ret1 = scam_flush_write();
	write_in_progress = true;
	if (ret1 < 0)
		return ret1;

	auto ret = write_to_scam_(msg.buffer(), msg.size());
	assert(ret < 0 || ret == msg.size());
	pending_write_buffer.clear();
	write_in_progress = false;
	if (ret < 0) {
		return ret;
	}
	// process any new pending write created during earlier writes
	ret1 = scam_flush_write();
	if (ret1 < 0)
		return ret1;
	return ret;
}

/*!
	Check the taks queue for commands and execute them
	Returns -1 when must_exit (as asked by a command) or must_reconnect
	Returns 0 on timeout;
	Returns 1 on progress
*/

int scam_thread_t::wait_for_and_handle_events(bool do_not_handle_demux_events) {
	auto n = this->epoll_wait(2000);
	if (n < 0) {
		dterrorx("error in poll: %s", strerror(errno));
		return n;
	}
	// printf("n=%d\n", n);
	for (auto evt = next_event(); evt; evt = next_event()) {
		if (is_event_fd(evt)) {
			// an external request was received
			// run_tasks returns -1 if we must exit
			if (run_tasks(now) < 0) {
				// detach();
				return -1;
			}
		} else if (is_timer_fd(evt)) {
			// nothing...
		} else if (scam->is_scam_fd(evt)) {
			// scam input or output
			if (evt->events & EPOLLIN)
				scam->read_from_scam(); // in fiber
			if (evt->events & EPOLLOUT) {
				scam->scam_flush_write();
			}
		} else {
			if (!do_not_handle_demux_events)
				scam->process_demux_fd(evt);
		}
	}
	return 0;
}

void scam_t::process_demux_fd(const epoll_event* evt) {
	for (auto& [adapter_no, p] : active_scams) {
		if (p->reader->on_epoll_event(evt)) {
			p->process_ca_data();
		}
	}
}

int scam_thread_t::run() {
	set_name("scam");
	logger = Logger::getLogger("scam"); // override default logger for this thread

	thread_id = std::this_thread::get_id();

	timer_start();
	for (;;) {
		if (must_exit_) {
			// detach();
			return 0;
		}
		if (scam->must_reconnect) {
			scam->close();
			if (scam->open() > 0)
				scam->must_reconnect = false;
			else
				usleep(20000);
		}
		bool do_not_handle_demux_events = false;
		if (wait_for_and_handle_events(do_not_handle_demux_events) < 0)
			break;
	}
	// detach();
	return 0;
}

template <typename T> auto from_net(uint8_t* ptr) {
	T x{};
	switch (sizeof(x)) {
	case 4:
		return T((ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3]);
	case 2:
		return T((ptr[0] << 8) | ptr[1]);
	case 1:
		return T(ptr[0]);
	default:
		assert(0);
		return x;
	}
}

/*!
	Read data from the oscam connection and return a pointer to an internal buffer
	which contains that data.
	All data in the returned buffer must be used before making another call to read_data.
	At that point the data in the old buffer will be no longer valid
*/
uint8_t* scam_t::read_data(int size) {
	int available = scam_read_buffer.size() - scam_read_pointer;
	while (available < size) {
		if (scam_read_pointer > 0) {
			// move all data to the start
			if (available)
				memmove(scam_read_buffer.buffer(), scam_read_buffer.buffer() + scam_read_pointer, available);
			scam_read_buffer.resize_no_init(available);
			scam_read_pointer = 0;
		}
		auto readlen = scam_read_buffer.capacity() - scam_read_pointer;
		auto n = ::read(scam_fd, scam_read_buffer.buffer() + scam_read_buffer.size(), readlen);
		if (n < 0) {
			if (must_reconnect)
				throw std::runtime_error("reconnect");
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				assert(main_fiber);
				main_fiber = main_fiber.resume();
				continue;
			}
			if (errno == EINTR)
				continue;
			dterror("Fatal error while reading from scam: " << strerror(errno));
			throw std::runtime_error("reconnect");
		} else if (n == 0) {
			dterror("scam closed connection");
			must_reconnect = true;
			throw std::runtime_error("reconnect");
		} else {
			scam_read_buffer.resize_no_init(scam_read_buffer.size() + n);
			available += n;
			assert(available == scam_read_buffer.size());
		}
	}
	auto* ret = scam_read_buffer.buffer() + scam_read_pointer;
	scam_read_pointer += size;
	return ret;
}

template <typename T> T scam_t::read_field() {
	T val;
	auto* p = read_data(sizeof(T));
	val = from_net<T>(p);
	return val;
}

template <> ca_pid_t scam_t::read_field<ca_pid_t>() {
	ca_pid_t val;
	val.pid = read_field<uint32_t>();
	val.index = read_field<int32_t>();
	return val;
}

template <> ca_descr_t scam_t::read_field<ca_descr_t>() {
	ca_descr_t val;
	val.index = read_field<int32_t>();
	val.parity = read_field<int32_t>();
	auto* p = read_data(sizeof(val.cw));	 // temporary pointer
	memcpy(&val.cw[0], p, sizeof(val.cw)); // 8 bytes
	return val;
}

template <> ca_descr_aes_t scam_t::read_field<ca_descr_aes_t>() {
	ca_descr_aes_t val;
	val.index = read_field<int32_t>();
	val.parity = read_field<int32_t>();
	auto* p = read_data(sizeof(val.cw));	 // temporary pointer
	memcpy(&val.cw[0], p, sizeof(val.cw)); // 16 bytes
	return val;
}

template <> ca_descr_mode_t scam_t::read_field<ca_descr_mode_t>() {
	ca_descr_mode_t val;
	val.index = read_field<int32_t>();
	val.algo = (ca_descr_algo)read_field<int32_t>();
	val.cipher_mode = (ca_descr_cipher_mode)read_field<int32_t>();
	return val;
}

void scam_t::read_greeting_reply() {
	auto protocol = read_field<uint16_t>();
	auto str_size = read_field<uint8_t>();
	ss::string<256> server_reply;
	server_reply.resize_no_init(str_size + 1);
	memcpy(server_reply.buffer(), read_data(str_size), str_size);
	server_reply.buffer()[str_size] = 0;
	dtdebugx("scam server protocol=%d: %s", protocol, server_reply.buffer());
}

/*
	oscam examples:
	pmt filter: uint8_t(0x2) uint16_t(srv_id)  mask=0xffffff
	sdt filter: uint8_t(0x42) mask=0xff
	ecm filter: uint8_t(0x80) mask = 0xf0
	cat filter: uint8_t(0x01) mask = 0xff
	emm filter: uint8_t(0x01) mask = 0xff
	irdeto shared emm 0x83 xx xx 0x10 0x00 0x10 mask=0xffffffffffff
	irdeto unique emm 0x83 xx xx 0x10 xx 0x00 mask=0xffffffffffff
	bullcrypt: 4.5 bytes
*/

void scam_t::read_filter_request() {
	ca_filter_t filter;
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto demux_no = demux_no_t(read_field<uint8_t>());
	auto filter_no = filter_no_t(read_field<uint8_t>());
	/*The following data are the fields from the dmx_sct_filter_params structure (added separately to
		avoid padding problems)
	*/
	filter.pid = read_field<uint16_t>();
	filter.demux_no = uint8_t(demux_no);
	memcpy(filter.dmx_filter.filter, read_data(sizeof(filter.dmx_filter.filter)), sizeof(filter.dmx_filter.filter));
	memcpy(filter.dmx_filter.mask, read_data(sizeof(filter.dmx_filter.mask)), sizeof(filter.dmx_filter.mask));
	memcpy(filter.dmx_filter.mode, read_data(sizeof(filter.dmx_filter.mode)), sizeof(filter.dmx_filter.mode));

	filter.timeout = read_field<uint32_t>();
	filter.flags = read_field<uint32_t>();
	/*The flags can be:

		DMX_CHECK_CRC=1 - only deliver sections where the CRC check succeeded;
		DMX_ONESHOT=2 - disable the section filter after one section has been delivered;
		DMX_IMMEDIATE_START=4 - Start filter immediately without requiring a DMX_START.

		oscam returns DMX_IMMEDIATE_START
	*/
	auto it = active_scams.find(adapter_no);
	if (it == active_scams.end()) {
		dtdebugx("Received scam request for adapter %d which has stopped descrambling", int(adapter_no));
		return;
	}
	auto& active_scam = it->second;
	assert(active_scam);

	active_scam->ca_set_filter(filter, filter_no);
}

void scam_t::read_dmx_stop_request() {
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto demux_no = demux_no_t(read_field<uint8_t>());
	auto filter_no = filter_no_t(read_field<uint8_t>());
	auto pid = (read_field<uint16_t>());
	auto it = active_scams.find(adapter_no);
	if (it == active_scams.end()) {
		dtdebugx("received dmx_stop request for adapter %d which has stopped descrambling", int(adapter_no));
		return;
	}
	auto& active_scam = it->second;

	active_scam->ca_stop_filter(filter_no, demux_no, pid);
}

void scam_t::read_ca_set_pid_request() {
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto ca_pid = read_field<ca_pid_t>();

	auto it = active_scams.find(adapter_no);
	if (it == active_scams.end()) {
		dtdebugx("received ca_set_pid request for adapter %d which has stopped descrambling", int(adapter_no));
		return;
	}
	auto& active_scam = it->second;

	active_scam->ca_set_pid(ca_pid);
}

void scam_t::read_ca_set_descr_request(uint32_t msgid) {
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto cadescr = read_field<ca_descr_t>();
	cadescr.parity = !!cadescr.parity; // ca_descr.parity==1 indicates even, but we prefer odd
	auto it = active_scams.find(adapter_no);

	if (it == active_scams.end()) {
		dtdebugx("Unhandled ca_set_descr index: %d", int(adapter_no));
		return;
	}

	auto& active_scam = it->second;
	assert(active_scam);

	active_scam->ca_set_descr(cadescr, msgid);
}

void scam_t::read_ca_set_descr_aes_request(uint32_t msgid) {
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto cadescr = read_field<ca_descr_aes_t>();
	cadescr.parity = !!cadescr.parity; // ca_descr.parity==1 indicates even, but we prefer odd
	auto it = active_scams.find(adapter_no);
	if (it == active_scams.end()) {
		dtdebugx("received ca_set_descr_aes request for adapter %d which has stopped descrambling", int(adapter_no));
		return;
	}

	auto& active_scam = it->second;

	active_scam->ca_set_descr_aes(cadescr, msgid);
}

void scam_t::read_ca_set_descr_mode_request() {
	auto adapter_no = adapter_no_t(read_field<uint8_t>());
	auto camode = read_field<ca_descr_mode_t>();
	auto it = active_scams.find(adapter_no);
	if (it == active_scams.end()) {
		dtdebugx("received ca_set_descr_mode request for adapter %d which has stopped descrambling", int(adapter_no));
		return;
	}

	auto& active_scam = it->second;

	active_scam->ca_set_descr_mode(camode);
}

void scam_t::read_ecm_info_request() {
	auto& i = last_ecm_info;
	i.adapter_index = read_field<uint8_t>();
	i.service_id = read_field<uint16_t>();
	i.caid = read_field<uint16_t>();
	i.pid = read_field<uint16_t>();
	i.provider_id = read_field<uint32_t>();
	i.ecm_time_ms = read_field<uint32_t>();

	auto str_size = read_field<uint8_t>();
	ss::string<256> ca_system_name;
	ca_system_name.resize_no_init(str_size + 1);
	memcpy(ca_system_name.buffer(), read_data(str_size), str_size);
	ca_system_name.buffer()[str_size] = 0;

	str_size = read_field<uint8_t>();
	ss::string<256> reader_name;
	reader_name.resize_no_init(str_size + 1);
	memcpy(reader_name.buffer(), read_data(str_size), str_size);
	reader_name.buffer()[str_size] = 0;

	str_size = read_field<uint8_t>();
	ss::string<256> from_source_name;
	from_source_name.resize_no_init(str_size + 1);
	memcpy(from_source_name.buffer(), read_data(str_size), str_size);
	from_source_name.buffer()[str_size] = 0;

	str_size = read_field<uint8_t>();
	ss::string<256> protocol_name;
	protocol_name.resize_no_init(str_size + 1);
	memcpy(protocol_name.buffer(), read_data(str_size), str_size);
	protocol_name.buffer()[str_size] = 0;

	i.hops = read_field<uint8_t>();

	dtdebugx("ECM info for adapter=%d service=%d pid=%d: "
					 "ca_system=%s reader=%s source=%s protocol=%s",
					 i.adapter_index, i.service_id, i.pid, ca_system_name.c_str(), reader_name.c_str(), from_source_name.c_str(),
					 protocol_name.c_str());
}

void active_scam_t::ca_set_pid(const ca_pid_t& ca_pid) {
	if (ca_pid.index == -1) {
		// disable/remove pid
		for (auto& [idx, slot] : ca_slots) {
			auto it = std::find_if(slot.pids.begin(), slot.pids.end(), [&ca_pid](uint16_t pid) { return pid == ca_pid.pid; });
			if (it != slot.pids.end()) {
				dtdebugx("CA_SET_PID REMOVE PID %d", *it);
				slot.pids.erase(it - slot.pids.begin());
				dtdebugx("CA_SET_PID SLOT idx=%d now has %d entries", int16_t(idx), slot.pids.size());
				if (slot.pids.size() == 0) {
					ca_slots.erase(decryption_index_t(ca_pid.index));
				}
				break;
			}

			return;
		}
	} else {
		auto& slot = ca_slots[decryption_index_t(ca_pid.index)];
		dtdebugx("CA_SET_PID ADD PID %d", ca_pid.pid);
		if (slot.pids.size() == 0) {
			dtdebugx("CA_SET_PID SLOT %d: first entry", ca_pid.index);
		}
		slot.pids.push_back(ca_pid.pid);
	}
}

int active_scam_t::open(int pid) {
	return active_stream_t::open(pid, &parent->scam_thread.epx);
}

void active_scam_t::ca_set_filter(const ca_filter_t& filter, filter_no_t filter_no) {
	log4cxx::NDC(name());
	ss::string<32> filter_string;
	ss::string<32> mask_string;
	filter_string.sprintf("filter=");
	mask_string.sprintf("mask=");
	for (auto x : filter.dmx_filter.filter)
		filter_string.sprintf("%02x ", x);
	int n = 0;
	for (auto x : filter.dmx_filter.mask) {
		mask_string.sprintf("%02x ", x);
		if (x != 0)
			n = mask_string.size();
	}
	filter_string.resize_no_init(n);
	mask_string.resize_no_init(n);

	auto pid = filter.pid;
	bool found{false};
	bool same_pid{false};

	for(auto it = filters.begin(); it != filters.end(); ) {
		auto& [f_no, f] = *it;
		if(f_no == filter_no) {
			found =true;
			if (f.pid != filter.pid) {
				dterrorx("duplicate filter filter_no=%d old_pid=%d new_pid=%d",
								 uint8_t(filter_no), f.pid, filter.pid);
				f = filter;
				same_pid = false;
			} else {
				dtdebugx("FILTER update: demux=%d filter[%d] pid=%d: %s %s", uint8_t(filter.demux_no), uint8_t(filter_no),
								 filter.pid, filter_string.c_str(), mask_string.c_str());
				f.update(filter);
				same_pid = true;
			}
		} else if (f.pid == filter.pid) {
				dtdebugx("SCAM would register same pid multiple times; unregistering filter_no=%d ecm_pid=%d\n",
								 (int)filter_no, (int)f.pid);
				same_pid = true;
				it = filters.erase(it);
				continue;
		}
		++it;
	}

	if(!found) {
		filters[filter_no] = filter;
		dtdebugx("FILTER NEW: demux=%d filter[%d] pid=%d is_ca=%d: %s %s", uint8_t(filter.demux_no), uint8_t(filter_no),
						 filter.pid, pmts[0].is_ecm_pid(filter.pid), filter_string.c_str(), mask_string.c_str());
	}

	bool is_ecm = true;
	//@todo todo("when to register emm pid instead of ecm_pid?");
	if (!reader->is_open()) {
		open(pid);
		assert(reader->is_open());
	} else if (!same_pid)
		add_pid(pid);
	if (!same_pid) {
		stream_parser.register_psi_pid(pid, is_ecm ? "ECM" : "EMM");
	}
}

void active_scam_t::ca_stop_filter(filter_no_t filter_no, demux_no_t demux_no, uint16_t ecm_pid) {
	log4cxx::NDC(name());
	dtdebugx("STOP FILTER: demux=%d filter[%d] ecm_pid=%d", uint8_t(filter_no), uint8_t(demux_no), ecm_pid);
	auto it = filters.find(filter_no);
	if (it != filters.end()) {
		auto& filter = it->second;
#pragma unused(filter)
		assert(filter.pid == ecm_pid);
		assert(demux_no_t(filter.demux_no) == demux_no);
		remove_pid(ecm_pid);
		filters.erase(filter_no);
		stream_parser.unregister_psi_pid(ecm_pid);
		restart_decryption(ecm_pid, system_clock_t::now());
	}

	last_filtered_data.clear();
	last_filtered_data_count=0;

}

/*!
	transfer a key to all relevant service threads (set key on all active_services matching slots)
	called by scam thread
*/
void active_scam_t::set_services_key(ca_slot_t& slot, uint32_t msgid, int decryption_index) {
	for (auto& active_service_p : registered_active_services) {
		auto& active_service = *active_service_p;
		active_service.set_services_key(slot, decryption_index);
	}
}

/*
	called by scam thread to signal to active_service_threads
	that decryption has been interrupted
	@todo: as oscam sets multiple filters at once, this function will
	be called several times (more than is needed)
*/
void active_scam_t::restart_decryption(uint16_t ecm_pid, system_time_t t) {
	for (auto& active_service_p : registered_active_services) {
		auto& active_service = *active_service_p;
		active_service.restart_decryption(ecm_pid, t);
	}
}

void active_scam_t::ca_set_descr(const ca_descr_t& ca_descr, uint32_t msgid) {
	log4cxx::NDC(name());
	auto& slot = ca_slots[decryption_index_t(ca_descr.index)];
	if (ca_descr.parity != 0 && ca_descr.parity != 1) {
		dterrorx("illegal parity: %d", ca_descr.parity);
		return;
	}
#ifndef NDEBUG
	ss::string<32> s;
	s.sprintf("slot=%p key[%d]=", &slot, ca_descr.parity);
	for (auto x : ca_descr.cw)
		s.sprintf("%02x", x);
	dtdebug(s.c_str());
#endif

	slot.last_key.parity = ca_descr.parity;
	slot.last_key.receive_time = system_clock_t::now();
	memcpy(slot.last_key.cw, ca_descr.cw, sizeof(ca_descr.cw));
	if (cwmode == cwmode_t::CAPMT_CWMODE_OE22SW)
		slot.last_key.valid = false; /* wait for CA_SET_DESCR_MODE */
	else {
		slot.last_key.valid = true;

		// advance the counter
		//@todo: this assumes that no two ecm requests (for odd and even key) occur before key is returned
		parent->scam_outgoing_msgid++;
		if (parent->scam_outgoing_msgid % 2 != ca_descr.parity) // could happen at start
			parent->scam_outgoing_msgid++;

		set_services_key(slot, msgid, ca_descr.index);
	}
}

void active_scam_t::ca_set_descr_aes(const ca_descr_aes_t& ca_descr_aes, uint32_t msgid) {
	log4cxx::NDC(name());
	auto& slot = ca_slots[decryption_index_t(ca_descr_aes.index)];
	if (ca_descr_aes.parity != 0 && ca_descr_aes.parity != 1) {
		dterrorx("illegal parity: %d", ca_descr_aes.parity);
		return;
	}
	slot.algo = ca_algo_t::CA_ALGO_AES128;
#ifndef NDEBUG
	ss::string<32> s;
	s.sprintf("key[%d]=", ca_descr_aes.parity);
	for (auto x : ca_descr_aes.cw)
		s.sprintf("%2x", x);
	dtdebug(s.c_str());
#endif

	slot.last_key.parity = ca_descr_aes.parity;
	slot.last_key.receive_time = system_clock_t::now();
	memcpy(slot.last_key.cw, ca_descr_aes.cw, sizeof(ca_descr_aes.cw));
	slot.last_key.valid = true;

	// advance the counter
	//@todo: this assumes that no two ecm requests (for odd and even key) occur before key is returned
	parent->scam_outgoing_msgid++;
	if (parent->scam_outgoing_msgid % 2 != ca_descr_aes.parity) // could happen at start
		parent->scam_outgoing_msgid++;
	set_services_key(slot, msgid, ca_descr_aes.index);
}

void active_scam_t::ca_set_descr_mode(const ca_descr_mode_t& ca_descr_mode) {
	log4cxx::NDC(name());
	auto& slot = ca_slots[decryption_index_t(ca_descr_mode.index)];
	if (ca_descr_mode.cipher_mode < 0 || ca_descr_mode.cipher_mode > 1) {
		dterrorx("illegal cipher_mode: %d", ca_descr_mode.cipher_mode);
		return;
	}
	if (ca_descr_mode.algo < 0 || ca_descr_mode.algo > 2) {
		dterrorx("illegal algo: %d", ca_descr_mode.algo);
		return;
	}

	dtdebugx("CA_SET_DESCR_MODE algo=%d cipher=%d", ca_descr_mode.algo, ca_descr_mode.cipher_mode);

	slot.cipher_mode = (ca_cipher_mode_t)ca_descr_mode.cipher_mode;
	slot.algo = (ca_algo_t)ca_descr_mode.algo;
	if (cwmode == cwmode_t::CAPMT_CWMODE_OE22SW)
		slot.last_key.valid = true;
}

/*!
returns true if filter matches
*/
static bool filter_match(const dmx_filter_t& filter, const uint8_t* data, int len) {
	for (int i = 0, j = 0; i < DMX_FILTER_SIZE && j < len; ++i, ++j) {
		auto xor_ = data[j] ^ filter.filter[i];
		auto nomatch = filter.mask[i] & xor_;

		if (nomatch)
			return false;
		/*
			section consists of uint8_t(table_id) uint16_t(sect_len) .....
			oscam does not pas filter bytes for sec_len
		*/
		if (j == 0)
			j += 2;
	}
	return true;
}

int scam_t::scam_send_stop_decoding(uint8_t demux_no, uint32_t msgid) {
	ss::bytebuffer<32> out_msg; // current message, perhaps not fully written
	if (scam_protocol_version >= 3) {
		out_msg.append_raw(native_to_net((uint8_t)0xa5));
		out_msg.append_raw(native_to_net((uint32_t)msgid));
	}
	out_msg.append_raw(native_to_net((uint32_t)DVBAPI_AOT_CA_STOP));
	out_msg.append_raw(native_to_net((uint8_t)0x83));
	out_msg.append_raw(native_to_net((uint8_t)0x02));
	out_msg.append_raw(native_to_net((uint8_t)0x00));
	out_msg.append_raw(native_to_net((uint8_t)demux_no));
	return write_to_scam(out_msg);
}

int scam_t::scam_send_filtered_data(uint8_t filter_no, uint8_t demux_no, const ss::bytebuffer_& buffer,
																		uint32_t msgid) {
	ss::bytebuffer<4096 + 100> out_msg; // current message, perhaps not fully written
	slowdown(20, "");
	if (scam_protocol_version >= 3) {
		out_msg.append_raw(native_to_net((uint8_t)0xa5));
		out_msg.append_raw(native_to_net((uint32_t)msgid));
	}
	out_msg.append_raw(native_to_net((uint32_t)DVBAPI_FILTER_DATA));
	out_msg.append_raw(native_to_net((uint8_t)demux_no));
	out_msg.append_raw(native_to_net((uint8_t)filter_no));
	out_msg.append_raw(buffer.buffer(), buffer.size());
	if(out_msg == last_filtered_data) {
		dtdebugx("Sending duplicate filtered data filter=%d demux_no=%d count=%d",
						 filter_no, demux_no, last_filtered_data_count);
		last_filtered_data_count++;
	} else
		last_filtered_data_count =0;
	last_filtered_data = out_msg;
	return write_to_scam(out_msg);
}

/*!
	called by scam thread to signal to active_service_threads
	that ecm was sent to oscam
	@todo: as oscam sets multiple filters at once, this function might
	be called several times (more than is needed)
*/
void active_scam_t::mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t t) {
	for (auto& active_service_p : registered_active_services) {
		auto& active_service = *active_service_p;
		active_service.mark_ecm_sent(odd, ecm_pid, t);
	}
}

/*!
	returns >0 if data was written
	0 if no data was written
	-1 on error
*/
int active_scam_t::scam_send_filtered_data(uint16_t pid, const ss::bytebuffer_& data) {
	log4cxx::NDC(name());
	ss::bytebuffer<4096 + 100> out_msg; // current message, perhaps not fully written
	int matchcount = 0;
	auto t = system_clock_t::now();
	int ret = 0;
	for (auto& [filter_no, filter] : filters) {
		if (filter.pid != pid)
			continue;

		if (!filter_match(filter.dmx_filter, data.buffer(), data.size()))
			continue; // this filter does not match
		dtdebugx("ecm_request_time set for %s filter[%p]=%d pid=%d time=%ld", (data[0] == 0x80) ? "even" : "odd", &filter,
						 uint8_t(filter_no), filter.pid, system_clock_t::to_time_t(t));

		filter.msgid = parent->scam_outgoing_msgid;

		mark_ecm_sent(data[0] == 0x81, pid, t);

		ret = parent->scam_send_filtered_data(uint8_t(filter_no), uint8_t(filter.demux_no), data, filter.msgid);
		if (ret < 0) {
			dterrorx("scam_send_filtered_data failed: ret=%d", ret);
			return ret;
		}
		matchcount++;
	}
	if (matchcount > 1)
		dterror("Unexpected: more than one filter matches: " << matchcount);
	return matchcount > 0 ? 1 : 0;
}

void active_scam_t::process_ca_data() {
	auto start = steady_clock_t::now();
	for (;;) {
		if (steady_clock_t::now() - start > 500ms) {
			return;
		}

		auto [buffer, ret] = reader->read();

		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EOVERFLOW) {
				dtdebug_nice("OVERFLOW");
				continue;
			}
			if (errno == EAGAIN) {
				break; // no more data
			} else {
				dterror("error while reading: " << strerror(errno));
				break;
			}
		}
		if (ret > 0) {
			auto delta = ret % dtdemux::ts_packet_t::size;
			stream_parser.set_buffer(buffer, ret - delta);
			reader->discard(ret - delta);
			while (stream_parser.current_range.available() > 0)
				stream_parser.parse();
		} else {
		}
	}
}

void scam_t::reader_loop_() {
	dtdebug("Reader fiber starting");
	for (;;) {
		if (must_reconnect) {
			assert(main_fiber);
			main_fiber = main_fiber.resume();
		}
		try {
			uint32_t msgid = 0;
			if (scam_protocol_version >= 3) {
				auto opcode = read_field<uint8_t>();
				if (opcode != 0xa5) { // message start
					dterrorx("did not get message start byte: %d", opcode);
				} else {
					msgid = read_field<uint32_t>();
				}
			}

			auto opcode = read_field<uint32_t>();
			switch (opcode) {
			case DVBAPI_SERVER_INFO:
				read_greeting_reply();
				break;

			case DVBAPI_DMX_SET_FILTER:
				read_filter_request();
				break;

			case DVBAPI_CA_SET_PID:
				read_ca_set_pid_request();
				break;

			case DVBAPI_CA_SET_DESCR:
				read_ca_set_descr_request(msgid);
				break;

			case DVBAPI_CA_SET_DESCR_AES:
				read_ca_set_descr_aes_request(msgid);
				break;

			case DVBAPI_CA_SET_DESCR_MODE:
				read_ca_set_descr_mode_request();
				break;

			case DMX_STOP:
				read_dmx_stop_request();
				break;

			case DVBAPI_ECM_INFO:
				read_ecm_info_request();
				break;

			default:
				dterrorx("Unknown scam opcode: %d", opcode);
			}
		} catch (std::runtime_error) {
			must_reconnect = true;
			continue; // reader fiber must be kept alive even after scam reconnect, otherwise a new one would have to be
								// started
		}
	}
}

int scam_t::read_from_scam() {
	assert(reader_fiber);
	reader_fiber = reader_fiber.resume();
	return 0;
}

int scam_t::register_active_service_if_needed(active_service_t* active_service, int adapter_no) {

	auto& active_scam = active_scams[adapter_no_t(adapter_no)];

	if (!active_scam) {
		active_scam = std::make_shared<active_scam_t>(this, active_service->receiver, active_service->receiver.tuner_thread,
																									*active_service);
	}

	active_scam->register_active_service(active_service);
	if (scam_fd < 0) {
		dtdebug("Registering first scam service");
		open();
	}
	return 0;
}

int active_scam_t::register_active_service(active_service_t* active_service) {
	std::shared_ptr<active_service_t>* freeslot = nullptr;
	for (auto& active_service_p : registered_active_services) {
		if (!active_service_p.get() && !freeslot)
			freeslot = &active_service_p;
		else if (active_service == active_service_p.get()) {
			dtdebugx("active_service already registered (ok)");
			return -1;
		}
	}
	if (freeslot)
		*freeslot = active_service->shared_from_this();
	else
		registered_active_services.push_back(active_service->shared_from_this());
	return 0;
}

int scam_t::unregister_active_service(active_service_t* active_service, int adapter_no) {
	auto it = active_scams.find(adapter_no_t(adapter_no));
	assert(it != active_scams.end());
	auto active_scam = it->second;
	active_scam->unregister_active_service(active_service, adapter_no);
	auto num_active_services = active_scam->pmts.size();
  // @todo do we ever have more than one pmt in active_scam->pmts?
	if (num_active_services == 0) {
		dtdebug("unregistered last active service");
		scam_send_stop_decoding(0xff, scam_outgoing_msgid = 0); // stop all decoding, but do not close
		active_scams.erase(it);
	} else
		send_all_pmts();
	return 0;
}

int active_scam_t::unregister_active_service(active_service_t* active_service, int adapter_no) {
	dterrorx("SCAM: unregister_active_service %s", active_service->get_current_service().name.c_str());
	int use_count = 0;
	int num_active_services = 0;
	for (int i = registered_active_services.size() - 1; i >= 0; --i) {
		auto& active_service_p = registered_active_services[i];
		assert(active_service_p.get());
		num_active_services++;
		if (active_service_p->current_pmt_pid == active_service->current_pmt_pid)
			use_count++;
		if (active_service_p.get() == active_service) {
			active_service_p.reset(); // not needed?
			registered_active_services.erase(i);
		}
	}

	if (use_count == 0) {
		dterror("Could not unregister active_service");
		return -1;
	}
	if (use_count == 1) {
		dtdebugx("Unregistering pmt pid %d", active_service->current_pmt_pid);
		auto pmt_pid = active_service->current_pmt_pid;
		int demux_no = 0;
		// assert(pmts.size()<=1); //@todo do we ever have more than one pmt?
		for (auto& x : pmts) {
			if (x.pmt_pid == pmt_pid) {
				for (auto& [idx, f] : filters) {
					if (f.demux_no == demux_no) {
						filters.erase(idx);
						break;
					}
				}
				num_active_services--;
				pmts.erase(&x - &pmts[0]);
				break;
			}
			demux_no++;
		}
	}
	return 0;
}

/*!
	replace the current pmt list with a new one, e.g. after detuning a service
*/
int scam_t::send_all_pmts() {
	if (!scam_fd)
		if (open() < 0)
			return -1;

	int count = 0;
	for (auto& [adapter_no, active_scam] : active_scams)
		count += active_scam->pmts.size();

	auto lm = capmt_list_management_t::first;
	for (auto& [adapter_no, active_scam] : active_scams) {
		int demux_no = 0;
		assert(active_scam->pmts.size() <= 1); //@todo do we ever have more than one pmt?
		for (auto& pmt_info : active_scam->pmts) {
			if (--count == 0)
				lm = (capmt_list_management_t)((uint8_t)lm | (uint8_t)capmt_list_management_t::last);
			auto ret = scam_send_capmt(pmt_info, lm, active_scam->adapter_no, demux_no);
			if (ret < 0)
				return ret;
			lm = capmt_list_management_t::more;
			demux_no++;
		}
	}
	return 1;
}

/*!
	@todo: do not call scam_send_capmt if CA has not changed
 */
int scam_t::update_pmt(active_service_t* active_service, int adapter_no, const pmt_info_t& pmt_info, bool isnext) {
	// auto& tuner = active_service->tuner;
	auto& active_scam_ptr = active_scams[adapter_no_t(adapter_no)];
	register_active_service_if_needed(active_service, adapter_no);
	assert(active_scam_ptr);
	auto& active_scam = *active_scam_ptr;
	bool is_update = false;
	int demux_device = 0;
	// assert(active_scam.pmts.size()<=1); //@todo do we ever have more than one pmt?
	for (auto& x : active_scam.pmts) {
		if (x.pmt_pid == pmt_info.pmt_pid) {
			is_update = true;
			x = pmt_info;
			break;
		}
		demux_device++;
	}
	if (!is_update) {
		// active_scam.pmts.clear(); //@TDOO: this is a test for 27.5W
		active_scam.pmts.push_back(pmt_info);
	}
	if (!scam_fd) {
		if (open() < 0)
			return -1;
	}

	/*
		need to find out CA_SET_PID has only adapter_index
		CA_SET_DESCR (control words) has only adapter_index
	*/
	return scam_send_capmt(pmt_info, is_update ? capmt_list_management_t::update : capmt_list_management_t::add,
												 adapter_no, demux_device);

	// send pmt to scam
};

int scam_thread_t::cb_t::update_pmt(active_service_t* active_service, int adapter_no, const pmt_info_t& pmt,
																		bool isnext) {
	/*service thread has received a new or updated pid and we need to notify scam */
	return scam->update_pmt(active_service, adapter_no, pmt, isnext);
};

int scam_thread_t::cb_t::unregister_active_service(active_service_t* active_service, int adapter_no) {
	return scam->unregister_active_service(active_service, adapter_no);
}


scam_thread_t::scam_thread_t(receiver_thread_t& receiver_) : task_queue_t(thread_group_t::scam) {
	scam = std::make_shared<scam_t>(receiver_, *this);
}
