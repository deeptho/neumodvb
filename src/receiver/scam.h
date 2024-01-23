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

#pragma once

#include "streamparser/psi.h"
//#include "service.h"
#include "linux/dvb/ca.h"
#include "dvbapi.h"
#include <linux/dvb/dmx.h>
#include "active_stream.h"
#include "active_service.h"

#define DESCRAMBLER_NONE	0
/* 64-bit keys */
#define DESCRAMBLER_CSA_CBC	1
#define DESCRAMBLER_DES_NCB	2 /* no block cipher mode! */
#define DESCRAMBLER_AES_ECB	3
/* 128-bit keys */
#define DESCRAMBLER_AES128_ECB  16

struct ecm_info_t
{
	uint32_t	operation_code{};
	uint8_t	  adapter_index{};
	uint16_t	service_id{};
	uint16_t	caid{};
	uint16_t	pid{};
	uint32_t	provider_id{};
	uint32_t	ecm_time_ms{};
	uint8_t	  hops{};
	ss::string<256> ca_system_name;
	ss::string<256> reader_name;
	ss::string<256> from_source_name;
	ss::string<256> protocol_name;
};


//todo: this is the same as dmx_sct_filter_params
struct ca_filter_t {
	dmx_filter_t dmx_filter; // the 3 sets of 16 bytes
	uint16_t pid =0;
	uint8_t demux_no = 0;
	uint32_t timeout = 0;
	uint32_t flags = 0;
	uint32_t msgid =0 ; //helps to associate ecm witk key

	void update(const ca_filter_t& other);

};



enum class cwmode_t : uint8_t {
	CAPMT_CWMODE_AUTO	= 0,
	CAPMT_CWMODE_OE22	= 1,  // CA_SET_DESCR_MODE before CA_SET_DESCR
	CAPMT_CWMODE_OE22SW	= 2,  // CA_SET_DESCR_MODE follows CA_SET_DESCR
	CAPMT_CWMODE_OE20	= 3  // DES signalled through PID index
};






class scam_t;

/*
	To avoid accidentally indexing the wrong data structure,
	we make a different type for each index which can only be converted
	using an explicit cast
 */
unconvertable_int(uint8_t, filter_no_t);
unconvertable_int(uint8_t, demux_no_t);
unconvertable_int(uint16_t, pmt_pid_t);

unconvertable_int(int16_t, decryption_index_t);

/*
TODO: currently we filtert emm and ecm streams in user space. This could be wasteful
(many unneeded read and epoll calls). On the other hand, reading using the section api
implies more file descriptors

Owned by scam thread
*/
using namespace dtdemux;
class active_scam_t final : public active_stream_t  {
	friend class scam_t;
	friend class scam_thread_t;

	ss::vector<pmt_info_t,2> pmts; //needed in case we need to reconnect
	ss::vector<std::shared_ptr<active_service_t>,2> registered_active_services;
	std::map<filter_no_t, ca_filter_t> filters; //filters indexed by filter number


	/*ca_slots (see dvbcsa.h) indexed by decryption index.
		a ca_slot stores a specific received double control word and a list of pids to which it applies
	 */
	std::map<decryption_index_t, ca_slot_t> ca_slots;
	cwmode_t cwmode = cwmode_t::CAPMT_CWMODE_AUTO;
	//void set_key(const ca_slot_t& slot);


	void set_services_key(ca_slot_t& slot, uint32_t msgid, int decryption_index);

	ca_filter_t* filter_for_slot(const active_service_t& service, const ca_slot_t& slot);

	scam_t* parent =nullptr;
	int adapter_no {};
	dtdemux::ts_stream_t stream_parser;

public:


	active_scam_t(scam_t* parent, receiver_t& receiver,
								active_service_t& active_service);
	int register_active_service(active_service_t* active_service);
	int unregister_active_service(active_service_t* active_service, int adapter_no);
	void process_ca_data();
  //add or update a filter
	void ca_set_filter(const ca_filter_t& filter, filter_no_t filter_no);
	void ca_stop_filter(filter_no_t filter_no, demux_no_t demux_no, uint16_t pid);
	int scam_send_filtered_data(uint16_t pid, const ss::bytebuffer_& buffer);
	void ca_set_pid(const ca_pid_t& ca_pid);
	void ca_set_descr(const ca_descr_t& ca_descr, uint32_t msgid);
	void ca_set_descr_aes(const ca_descr_aes_t& ca_descr_aes, uint32_t msgid);
	void ca_set_descr_mode(const ca_descr_mode_t& ca_descr_mode);
	void dvbapi_ecm_info(const dvbapi_ecm_info_t& ecm_info);
	void dmx_stop();
	void restart_decryption(uint16_t ecm_pid, system_time_t t);

	void mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t time);
	int open(int pid);


	virtual ss::string<32> name() const;
	virtual ~active_scam_t() final;
};


//typedef boost::context::execution_context<void*> fiber_t;


enum class capmt_list_management_t : uint8_t {
	more = 0,
	first = 1,
	last = 2,
	only = 3,
	add = 4,
	update =5
//all other values reserved
};



class scam_t final : public std::enable_shared_from_this<scam_t> {
	friend class scam_thread_t;
	friend class active_scam_t;

	bool write_in_progress = false;
	//bool must_exit = false;
	bool error = false;
	bool must_reconnect = false;
	int scam_fd = -1;
	ss::bytebuffer<4096> scam_read_buffer;
	int scam_read_pointer{0};

	/*
		one active_scam per tuner, but it can handle multiple services
	 */
	std::map<adapter_no_t, std::shared_ptr<active_scam_t>> active_scams; //open transponders, indexed by adapter_no
	ecm_info_t last_ecm_info; //@todo where should we store this?
	friend class scam_thread_t;
	//int flush_output();
	uint8_t* read_data(int size);
	template<typename T> T read_field();

	bool is_scam_fd(const epoll_event* event) const {
		return event->data.fd== int(scam_fd);
	}
	void reader_loop_();
	void read_greeting_reply();
	void read_filter_request();
	void read_dmx_stop_request();
	void read_ca_set_pid_request();
	void read_ca_set_descr_request(uint32_t msgid);
	void read_ca_set_descr_aes_request(uint32_t msgid);
	void read_ca_set_descr_mode_request();
	void read_ecm_info_request();
	int scam_protocol_version =  DVBAPI_PROTOCOL_VERSION;
	uint32_t scam_outgoing_msgid = 0;
	continuation_t main_fiber;
	continuation_t reader_fiber = boost::context::callcc([this](continuation_t&& invoker) {
		main_fiber = invoker.resume();
		reader_loop_();
		//reader_fiber = std::move(x);
		return std::move(main_fiber);
	});

 public:
	//LoggerPtr logger = Logger::getLogger("scam");
	receiver_thread_t& receiver;

	scam_thread_t& scam_thread; //There is only one tuner thread for the whole process

	int scam_send_filtered_data(uint8_t filter_no, uint8_t demux_no,
															const ss::bytebuffer_& buffer, uint32_t msgid);
	int scam_send_stop_decoding(uint8_t demux_no, uint32_t msgid);
private:
	int connect();
	int open();
	void close();
	int update_pmt(active_service_t* active_service,
								 int adapter_no, const pmt_info_t& pmt, bool isnext) CALLBACK;
	int register_active_service_if_needed(active_service_t* active_service, int adapter_no);
	int unregister_active_service(active_service_t* active_service, int adapter_no)  CALLBACK;

	int greet_scam();
	int scam_send_capmt(const pmt_info_t& pmt_info, capmt_list_management_t lm, int adapter_no, int demux_device);
	ss::bytebuffer<512> pending_write_buffer;

	int send_all_pmts();
	int scam_flush_write();
	int read_from_scam();

	int write_to_scam_(uint8_t* buffer, int size);
	/*!
		write size bytes in buffer to oscam. This function will
		always return 0 and will retry writing indefintely, unless
		the oscam connection needs to be closed/reopened, in which case it
		returns -1.

		If writing is delayed, the function will check for commands.
		These commands could result in more writes. In that case, the pending
		writes are stored in pending_write_buffer.


	 */
	int write_to_scam(ss::bytebuffer_& msg);
public:
	scam_t(receiver_thread_t& _receiver, scam_thread_t& scam_thread_);

	virtual ~scam_t() final;

	scam_t(scam_t&& other) = default;
	scam_t(const scam_t& other) = delete;
	scam_t operator=(const scam_t& other) = delete;

	void process_demux_fd(const epoll_event* evt);
public:

};



class ca_pmt_t {
	ss::bytebuffer_& data;
	int program_info_length =0;

	void init(capmt_list_management_t lm, const pmt_info_t& pmt_info, int adapter_no, int ca_device);
public:
 ca_pmt_t(ss::bytebuffer_& data_, capmt_list_management_t lm, const pmt_info_t& pmt_info,
					int adapter_no, int ca_device)
		: data(data_) {
			init(lm, pmt_info, adapter_no, ca_device);
		}
	void add_raw_ca_descriptor(uint8_t* start, int len);

	void add_adapter_device_descriptor(int adapter_no);
	void add_pmt_pid_descriptor(uint16_t pmt_pid);
	void add_demux_device_descriptor(uint8_t demux_device);
	void add_ca_device_descriptor(uint8_t ca_device);
};
