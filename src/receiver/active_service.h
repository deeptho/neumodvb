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

#pragma once
#include "stackstring.h"
#include "active_stream.h"
#include "receiver.h"
#include "mpm.h"
#include <functional>
#include <utility>

class service_reservation_t;

using namespace dtdemux;

class active_ts_t : public stream_buffer_t {
	int totcount{0};
	int count{0};
	int bytes{0};
	static constexpr int buffer_size{1024*188};
	std::unique_ptr<uint8_t[]> storage{new uint8_t[buffer_size]};
	uint8_t* buffer{nullptr};
	int decrypt_pointer{0}; //start of already decrypted data
	int write_pointer{0}; //start of already decrypted data
	stream_filter_t* output_filter{nullptr};
	bool needs_parsing{false};

public:
	inline recdb::live_service_t make_live_service() const;
	virtual void close() override;

	//get a range in the buffer in which encrypted data can be written
	inline virtual int get_write_buffer(uint8_t*& buffer_ret) override {
		assert(write_pointer>=0);
		assert(write_pointer <= buffer_size);
		buffer_ret = & buffer[write_pointer];
		return buffer_size -write_pointer;
	}

	//grow the size of the buffer and return the new size (usually not needed)
	inline virtual int advance()  override {
		assert(decrypt_pointer <= write_pointer);
		auto n = write_pointer - decrypt_pointer;
		if (n+ 188*100 > buffer_size) { //90% of the buffer could not be decrypted yet
			dtdebug_nicef("Discarding some encrypted data");
			decrypt_pointer += ((n+188)/(2*188))*188; //skip first half of the unencrypted data
			n = write_pointer - decrypt_pointer;
			assert(n>=0);
		}
		if(n>0) {
			bytes +=n;
			count++;
#if 0
			dtdebug_nicef("Moving some encrypted data {} {}/{}", bytes/(double)count, count, totcount);
#endif
			memmove(&buffer[0], &buffer[decrypt_pointer], n);
		}
		write_pointer -= decrypt_pointer;
		assert(write_pointer==n);
		decrypt_pointer = 0;
		assert(write_pointer>=0);
		return buffer_size -write_pointer;
	}

//make write_pointer point past the end of last written encrypted data
	inline virtual void advance_write_pointer(int extra) override {
		write_pointer+=extra;
		assert(write_pointer <= buffer_size);
	}

	//position decrypt_pointer at start of range to decrypt and return length of range to decrypt
	inline virtual int bytes_to_decrypt(uint8_t*& buffer_ret) override {
		buffer_ret = buffer + decrypt_pointer;
		assert (decrypt_pointer>= 0);
		assert (decrypt_pointer<= buffer_size);
		return ((write_pointer - decrypt_pointer)/188)*188;

	}

	//inform stream_parser of range of decrypted data to parse next
	inline virtual void set_buffer(int num_bytes_decrypted_now) override {
		assert(num_bytes_decrypted_now + decrypt_pointer <= buffer_size);
		this->stream_parser.set_buffer(&buffer[decrypt_pointer], num_bytes_decrypted_now);
	}

	//position decrypt_pointer past the range of decrypted data
	inline virtual void advance_decrypt_pointer(int extra) override {
		//there should never be more decrypted than encrypted data
		assert(decrypt_pointer+extra <= write_pointer);
		decrypt_pointer+=extra;
		assert(decrypt_pointer <= write_pointer);
		assert(decrypt_pointer <= buffer_size);
		totcount++;
		/*
			at this stage, decrypted data has already been processed
		 */
		if(write_pointer == decrypt_pointer) {
			write_pointer -= decrypt_pointer;
			decrypt_pointer = 0;
		}
	}

	inline virtual void housekeeping(system_time_t now) override {
		return;
	}

	void data_cb(uint8_t* buffer, int num_bytes);
	virtual bool process_service_data(int num_bytes_decrypted_now) override;

	virtual void register_parser_pid(int service_id, const dtdemux::pid_info_t& pidinfo) final;

	active_ts_t(active_service_t* active_service, stream_filter_t* output_filter, bool needs_parsing)
		: stream_buffer_t(active_service, nullptr)
		, buffer(storage.get())
		, output_filter (output_filter)
		, needs_parsing(needs_parsing)
		{}

	virtual ~active_ts_t() = default;
};


/*
	There is only one active_service_t for a given streamed service, even if the service
	is subscribed to multiple times.

 */

class active_service_t final : public std::enable_shared_from_this<active_service_t>
											 , public active_stream_t  {
	enum type_t {
		MPM,
		TS
	};

	type_t type;

	friend class service_thread_t;
	friend class open_channel_parser_t;
	friend class active_mpm_t;
	friend class buffert_t;


	mutable std::mutex mutex;
	system_time_t last_payload_data;
	system_time_t last_decrypted_data;

	//the following fields can be modified and should not be accessed/modified without locikng a mutex
	chdb::service_t current_service; //current channel
	pmt_info_t current_pmt;
	ss::bytebuffer<256> pmt_sec_data;
	std::shared_ptr<dtdemux::pmt_parser_t> pmt_parser; /*we save this in order to be able to control it
																											 but currently this is unused*/
	std::shared_ptr<dtdemux::pat_parser_t> pat_parser; /*we save this in order to be able to control it
																											 but currently this is unused*/
	bool is_encrypted{false}; //set by pmt or by increases in stream_parser.num_encrypted_packets
	bool have_pat{false};
	bool pmt_is_encrypted{false};
	bool have_pmt{false}; /*we can only turn decryption on after having received a pmt and  having
													registered video and audio streams. Otherwise the decryption code will
													take a lot of time to fill its buffers due to posibly low data rate*/

	bool have_scam{false}; //we started scam
	int channel_status=0; //composed of bitflags channel_status_t
	FILE* fp_out = NULL; //file in which stream is saved
	off64_t byteswritten =  0;
	ss::string<128> filename;
	ss::string<128> idx_filename;

	bool registered_scam =false; //we must not exit as long as we have registered with scam

	int key_index = -1; // the decryption slot for this channel (-1 means unscramnled or no key found yet)

	mutable std::unique_ptr<stream_buffer_t> stream_buffer;

	inline active_mpm_t* mpm()  const {
		return dynamic_cast<active_mpm_t*>(stream_buffer.get());
	}

	inline active_ts_t* active_ts() const {
		return dynamic_cast<active_ts_t*>(stream_buffer.get());
	}

	void service_status_message(stream_status_t status);
	void process_service_data();
public:
  volatile uint16_t current_pmt_pid = null_pid;// the pmt_pid which is currently requested from the stream
	service_thread_t service_thread;

 public:
	inline std::shared_ptr<stream_reader_t> clone_stream_reader(ssize_t buffer_size) const {
		return reader->clone(buffer_size);
	}

	inline chdb::service_t get_current_service() const  {
		std::scoped_lock lck(mutex);
		return current_service;
	}

private:
	void update_aa_pmt_(const dtdemux::pmt_info_t& pmt, bool isnext, bool service_changed);
	void update_scam_pmt_(const dtdemux::pmt_info_t& pmt, bool isnext, bool service_changed, bool ca_changed);
	void destroy();
	int create_recording_in_filesystem(const recdb::rec_t& rec);
	void update_audio_languages(const dtdemux::pmt_info_t& pmt);
	void update_subtitle_languages(const dtdemux::pmt_info_t& pmt);
	void update_pmt_pid(int new_pmt_pid);

	int deactivate();
	//int run();
	void update_pmt(const dtdemux::pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data);

 public:

	int open();

	active_service_t(active_adapter_t& active_adapter, const chdb::service_t& service,
									 const recdb::live_service_t& live_service,
									 const std::shared_ptr<stream_reader_t>& reader);
	active_service_t(active_adapter_t& active_adapter, ts_in_ts_stream_filter_t* filter,
									 const chdb::service_t& service,
									 const recdb::live_service_t& live_service,
									 const std::shared_ptr<stream_reader_t>& reader);
	active_service_t(active_adapter_t& active_adapter, t2mi_stream_filter_t* filter,
									 const chdb::service_t& service,
									 const recdb::live_service_t& live_service,
									 const std::shared_ptr<stream_reader_t>& reader);

	virtual ~active_service_t() final;

	virtual  ss::string<32> name() const;

	/*!
	periodically called to remove old data in timeshift bufferl so that it does not grow larger than
	what user wants
*/
	inline void housekeeping(system_time_t now) {
		stream_buffer->housekeeping(now);
	}

	void restart_decryption(uint16_t ecm_pid, system_time_t t);
	void set_services_key(ca_slot_t& slot, int decryption_index);
	void mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t t);
	recdb::live_service_t make_live_service() const;
	inline std::unique_ptr<playback_mpm_t> make_playback_mpm(subscription_id_t subscription_id) {
		return mpm()->make_playback_mpm(subscription_id);
	}

	bool need_decryption();

	std::optional<recdb::rec_t> start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec);
	void add_pat_and_pmt_parsers();
};
