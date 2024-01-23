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

#include "linux/dvb/ca.h"
#include "dvbapi.h"
#include <linux/dvb/dmx.h>
#include "active_stream.h"

inline const char* odd_even_str(bool odd)
{
	return odd ? "odd": "even";
}

enum class ca_algo_t : uint8_t
{
	CA_ALGO_DVBCSA,
	CA_ALGO_DES,
	CA_ALGO_AES,
	CA_ALGO_AES128
};

enum class ca_cipher_mode_t : uint8_t {
	CA_MODE_ECB,
	CA_MODE_CBC,
};



struct ca_key_t {
	int8_t parity = -1; //invalid
	bool valid = false;
	uint8_t cw[16]{};
	int64_t request_bytepos{-1};
	int64_t receive_bytepos{-1};
	system_time_t request_time{};
	system_time_t receive_time{};
	int restart_count = 0 ; //number of times scam was restarted before receving this key
	ss::string<32> to_str() const;
};


/*
	for each pid store index of last used decryption key
 */
struct descrambling_context_t {
	enum class key_validy_t {
							UNKNOWN,  /*The key is being used,  but we don't know if the key came during the current parity period
													or during the next other-parity period (unlikely) and may turn out invalid later;
													this state can only be entered for the first key of a given parity received after an
													scam restart;
												*/
							VALID, 	/* The key is being used and we know that it arrived before the transition to the current
												 parity */
							EXPIRED, /*The key was used but we encountered a parity transition, and therefore we know that
												 they has been experied cannot be used in future. We still did not load a new key
												 for the parity to which the key applies
											 */
	}; //applies only to a key which was installed for use
	int scrambling_control_packet{0}; //parity of last decrypted packet for this pid
	int packets_since_last_transition{0}; //for debugging
	int last_used_key_idxs[2]{-1,-1};
	int64_t last_used_key_request_bytepos[2]{-1,-1};
	int last_used_key_restart_count{-1};
	key_validy_t last_used_key_validity[2]{key_validy_t::UNKNOWN ,key_validy_t::UNKNOWN};
	int cc = -1; //continuity counter
	descrambling_context_t () = default;

};


struct ca_slot_t {
	static constexpr int MAX_PIDS=16;
	ss::vector<uint16_t, MAX_PIDS> pids; //service pids
	ca_algo_t algo;
	ca_cipher_mode_t cipher_mode;
	ca_key_t last_key;
	ca_slot_t() {
		for(auto & pid: pids)
			pid = null_pid;
	}
};


struct dvbcsa_bs_key_s;


struct decrypt_cache_t {
	int batch_size{0};
	ss::vector_<struct dvbcsa_bs_batch_s>  batches[2];
	ss::vector_<unsigned char *>  scnt_fields[2];
	int batch_idx[2]={0,0};
	std::array<struct dvbcsa_bs_key_s*, 2> active_keys{{nullptr, nullptr}};
	std::array<int, 2> active_key_indexes{-1, -1};

	decrypt_cache_t();
	~decrypt_cache_t();

	void decrypt_all_pending(const char*debug_msg);
	void add_packet(bool odd, uint8_t* packet);
};



/*
	owned by service streaming thread
 */
struct dvbcsa_t {
	system_time_t start;
	std::mutex key_mutex;
	/*This dvbcsa is specific per actice_service
	here we need either a key queue per pid
	or a key_seqno per pid

	we cannot keep the key per id in an scam global data structure
	because scan could be handling pids on different tuners
	the service thread must also keep a per pid
	detection of parity transitions (concretely: last_parity = parity for previous
																	 packet). At parity transsition it must read a new key.
	The first time it can skp the first key if it is the wrong parity, from then on not.



	Alternative solution could be to keep the keys per adapterno in the scam
	global datastructure. In that case the service thread must also keep a per pid
	detection of parity transsitions. At parity transsition it must read a new key.
	The first time it can skp the first key if it is the wrong parity, from then on not.
	*/
	int last_installed_key_idxs[2] = {0, 0}; //last installed key (per parity)
	int last_received_key_idx = -1; //last received key
	constexpr static int num_keys = 256; //2 should be enough
	ca_key_t keys[num_keys];
	int64_t  num_bytes_decrypted{}; //total number of bytes decrypted from service start
	int64_t  num_bytes_received{}; //total number of bytes read from service start
	int64_t skip_non_decryptable_last_scanned_bytepos{}; /*when no keys arrive the code
																												 starts scanning for parity changes
																												 in the data; this variable
																												 remebers the progress
																											 */

	int restart_count{0}; //number of times scam has restarted
	int64_t last_key_request_bytepos{-1};
	int64_t last_restart_decryption_bytepos{-1};
	system_time_t last_key_request_time{};

	int decryption_index = -1;

	decrypt_cache_t cache;


	std::map<uint16_t, descrambling_context_t> descrambling_contexts;

	system_time_t start_wait_for_key_time;
	const int wait_for_key_timeout_ms = 6000;

	void restart_decryption(system_time_t t);
	void add_key(const ca_slot_t& slot, int decryption_index, system_time_t t);

	void set_cw(const descrambling_context_t& context, bool odd);
	int next_key(descrambling_context_t& context, uint16_t pid, bool odd);
	void mark_ecm_sent(bool odd, system_time_t t);
	bool waiting_for_keys{false};



	dvbcsa_t();

	~dvbcsa_t();
	int decrypt_buffer(uint8_t* buffer, int buffer_size);

private:
	int get_key(int key_idx, int parity, bool allow_future_key);
	int skip_non_decryptable(uint8_t* buffer, int buffer_size);
	int confirm_parity_change(uint8_t* buffer, int buffer_size, int pid, int scrambling_control_packet,
														int threshold=3);
	int handle_parity_change(descrambling_context_t* context, int* idx,
													 int pid, bool& odd, int& packet_start,
													 int& scrambling_control_packet,
													 uint8_t* buffer, int buffer_size);
};
