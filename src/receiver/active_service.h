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
#include "stackstring.h"
#include "active_stream.h"
#include "receiver.h"
#include "mpm.h"
#include <functional>

class service_reservation_t;

using namespace dtdemux;


/*
	There is only one active_service_t for a given streamed service, even if the service
	is subscribed to multiple times.

 */

class active_service_t final : public std::enable_shared_from_this<active_service_t>
											 , public active_stream_t  {


	friend class service_thread_t;
	friend class open_channel_parser_t;
	friend class active_mpm_t;
	mutable std::mutex mutex;
	//the following fields can be modified and should not be accessed/modified without locikng a mutex
	chdb::service_t current_service; //current channel
	pmt_info_t current_pmt;
	ss::bytebuffer<256> pmt_sec_data;
	std::shared_ptr<dtdemux::pmt_parser_t> pmt_parser; /*we save this in order to be able to control it
																											 but currently this is unused*/
	std::shared_ptr<dtdemux::pat_parser_t> pat_parser; /*we save this in order to be able to control it
																											 but currently this is unused*/
	recdb::stream_descriptor_t current_streams;

	bool is_encrypted{false}; //set by pmt or by increases in stream_parser.num_encrypted_packets
	bool have_pat{false};
	bool pmt_is_encrypted{false};
	bool have_pmt{false}; /*we can only turn decryption on after having received a pmt and  having
													registered video and audio streams. Otherwise the decryption code will
													take a lot of time to fill its buffers due to posibly low data rate*/

	bool have_scam{false}; //we started scam

 public:
	inline std::shared_ptr<stream_reader_t> clone_stream_reader(ssize_t buffer_size) const {
		return reader->clone(buffer_size);
	}


  volatile uint16_t current_pmt_pid = null_pid;// the pmt_pid which is currently requested from the stream

	inline chdb::service_t get_current_service() const  {
		std::scoped_lock lck(mutex);
		return current_service;
	}

	playback_info_t get_current_program_info() const;
	service_thread_t service_thread;
private:
	int channel_status=0; //composed of bitflags channel_status_t
	FILE* fp_out = NULL; //file in which stream is saved
	off64_t byteswritten =  0;
	ss::string<128> filename;
	ss::string<128> idx_filename;

	bool registered_scam =false; //we must not exit as long as we have registered with scam

	int key_index = -1; // the decryption slot for this channel (-1 means unscramnled or no key found yet)

	active_mpm_t mpm;
	periodic_t periodic;
	void destroy();
	int create_recording_in_filesystem(const recdb::rec_t& rec);
	void update_audio_languages(const pmt_info_t& pmt);
	void update_subtitle_languages(const pmt_info_t& pmt);
	void update_pmt_pid(int new_pmt_pid);

	int deactivate();
	//int run();
	void update_pmt(const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data);
	void save_pmt(system_time_t now, const pmt_info_t& pmt_info);
 public:

	int open();
	void close();

		//void process_psi(int pid, unsigned char* payload, int payload_size);
	active_service_t(receiver_t& receiver, active_adapter_t& active_adapter,
									 const std::shared_ptr<stream_reader_t>& reader);

	active_service_t(receiver_t& receiver, active_adapter_t& active_adapter,
									 const chdb::service_t& ch, const std::shared_ptr<stream_reader_t>& reader);

	~active_service_t() final;

	virtual  ss::string<32> name() const;
	void housekeeping(system_time_t now); //periodically called to remove old data in timeshift buffer
	void restart_decryption(uint16_t ecm_pid, system_time_t t);
	void set_services_key(ca_slot_t& slot, int decryption_index);
	void mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t t);
	system_time_t get_creation_time() const {
		return mpm.creation_time;
	}

	recdb::live_service_t get_live_service(subscription_id_t subscription_id) const;
	std::unique_ptr<playback_mpm_t> make_client_mpm(subscription_id_t subscription_id);

	bool need_decryption();

	std::optional<recdb::rec_t>
	start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec);
};
