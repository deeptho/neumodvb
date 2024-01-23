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

extern "C" {
#include <dvbcsa/dvbcsa.h>
}
#include "mpm.h"
#include "receiver.h"
#include "scam.h"

ss::string<32> ca_key_t::to_str() const {
	ss::string<32> ret;
	ret.format("key [{:s}]:", parity ? "odd" : "even");
	for (int i = 0; i < 8; ++i)
		ret.format(" {:02x}", cw[i]);
	return ret;
}

/** @brief Getting ts payload starting point
 *
 *
 */
unsigned char ts_packet_get_payload_offset(uint8_t* ts_packet) {
	if (ts_packet[0] != 0x47)
		return 0;

	unsigned char adapt_field = (ts_packet[3] & ~0xDF) >> 5;	 // 11x11111
	unsigned char payload_field = (ts_packet[3] & ~0xEF) >> 4; // 111x1111

	if (!payload_field) // No payload
		return 0;

	if (adapt_field) {
		unsigned char adapt_len = ts_packet[4];

		if (adapt_len > 182) // validity check
			return 0;

		return 4 + 1 + adapt_len; // ts header + adapt_field_len_byte + adapt_field_len
	} else {
		return 4; // No adaptation, data starts directly after TS header
	}
}

decrypt_cache_t::decrypt_cache_t() : batch_size(dvbcsa_bs_batch_size()) {
	batches[0].resize_no_init(batch_size + 1);
	batches[1].resize_no_init(batch_size + 1);
	scnt_fields[0].resize_no_init(batch_size + 1);
	scnt_fields[1].resize_no_init(batch_size + 1);
	active_keys[0] = dvbcsa_bs_key_alloc();
	active_keys[1] = dvbcsa_bs_key_alloc();
}

decrypt_cache_t::~decrypt_cache_t() {
	dvbcsa_bs_key_free(active_keys[0]);
	dvbcsa_bs_key_free(active_keys[1]);
}

void decrypt_cache_t::add_packet(bool odd, uint8_t* packet) {
	int offset = ts_packet_get_payload_offset(packet);
	if (!offset)
		return;
	int len = ts_packet_t::size - offset;
	assert(active_key_indexes[odd] >= 0);
	auto& batch = batches[odd];
	auto& scnt_field = scnt_fields[odd];
	int& idx = batch_idx[odd];
	batch[idx].data = packet + offset;
	batch[idx].len = len;
	scnt_field[idx] = packet + 3;
	++idx;
	if (idx == batch_size) {
		decrypt_all_pending("(full buffer)");
		assert(idx == 0);
	}
}

void decrypt_cache_t::decrypt_all_pending(const char* debug_msg) {

	for (int odd = 0; odd < 2; ++odd) {
		auto& batch = batches[odd];
		int& idx = batch_idx[odd];
		auto& scnt_field = scnt_fields[odd];
		if (idx > 0) {
#if 1
			dtdebugf("Decrypting {:d} packets with parity={:d} {}", idx, odd, debug_msg);
#endif
			assert(idx < batch_size + 1);
			batch[idx].data = 0;
			dvbcsa_bs_decrypt(active_keys[odd], batch.buffer(), 184);
			// We zero the scrambling control field to mark stream as unscrambled.
			for (int i = 0; i < idx; ++i) {
				*scnt_field[i] &= 0x3f;
			}
			idx = 0;
		}
	};
}


/*!
	Checks the first few packets for specific stream pid, starting at position 0 in buffer
	to determine if a detected parity change is not a accidentally caused by
	a bit error in the stream or a duplicate packet

	returns 1 if a parity transition (to a different scrambling_control_packet value) has definitely occured.
	returns 0 if a parity transition candidate is a false positive (packet error) and should be disregared
	returns -1 if no decision can be reached yet (insufficient data)
*/
int dvbcsa_t::confirm_parity_change(uint8_t* buffer, int buffer_size, int pid, int scrambling_control_packet,
																		int threshold) {
	int packet_start = ts_packet_t::size; // skip first packet
	int matches = 0;
	for (; packet_start + ts_packet_t::size <= buffer_size; packet_start += ts_packet_t::size) {
		auto matching_pid = (pid == (0x1fff & (((int)buffer[packet_start + 1]) << 8 | buffer[packet_start + 2])));
		if (!matching_pid)
			continue;
		auto scrambling_control_packet_ = ((buffer[packet_start + 3] & 0xc0) >> 6);
		matches += (scrambling_control_packet_ == scrambling_control_packet);
		if (matches >= threshold)
			return 1;
		else if ((scrambling_control_packet_ ^ 0x1) == scrambling_control_packet)
			return 0; // wrong parity found
	}
	return -1;
}


/*!
	Returns the number of packets that can be skipped because it will be
	impossible to decrypt them
*/
int dvbcsa_t::skip_non_decryptable(uint8_t* buffer, int buffer_size) {
	assert(waiting_for_keys);
	int64_t pos;
	{
		std::unique_lock lck(key_mutex);
		pos = last_restart_decryption_bytepos;
	}

	if (pos <= num_bytes_decrypted) {
		return 0; /*restart was in the past, so it is possible that it will arrive later
							 */
	}
	/*
		Important: it is not enough to test for num_bytes_decrypted < last_restart_decryption_bytepos
		as this will not use the initial key pair which may be received later.
		We really must wait for a parity transition to make a decision
	*/

	skip_non_decryptable_last_scanned_bytepos = std::max(skip_non_decryptable_last_scanned_bytepos, num_bytes_decrypted);
	int packet_start = skip_non_decryptable_last_scanned_bytepos - num_bytes_decrypted;
	assert(packet_start >= 0);
	for (; packet_start + ts_packet_t::size <= buffer_size; packet_start += ts_packet_t::size) {
		int pid = 0x1fff & (((int)buffer[packet_start + 1]) << 8 | buffer[packet_start + 2]);
		int scrambling_control_packet = ((buffer[packet_start + 3] & 0xc0) >> 6);
		auto odd = (scrambling_control_packet != 2);
		auto* context = &descrambling_contexts[pid];
		switch (scrambling_control_packet) {
		case 2:		// scrambled with even key
		case 3: { // scrambled with odd key
			// we look for the next transition to the NEXT parity (hence == instead of !=)
			if (context->scrambling_control_packet == scrambling_control_packet) {
				auto parity_check = confirm_parity_change(buffer + packet_start, buffer_size - packet_start, pid,
																									context->scrambling_control_packet);
				if (parity_check < 0) {
					/*Not enough data in buffer to confirm parity change/
						Decrypt data before the parity change and return to caller until more data is available.
					*/
					skip_non_decryptable_last_scanned_bytepos += packet_start; // remember where to continue next time
					return 0;
				} else if (parity_check == 0) {
					/*false alarm; packet should be decoded with different parity
						or may be corrupt
					*/
					odd = !odd;
					scrambling_control_packet ^= 0x1; // we look for the next transition to the NEXT parity (hence ^0x1)
				} else {
					/*parity change occurs for this pid at packet_start.
					 */
					skip_non_decryptable_last_scanned_bytepos += packet_start; // remember where to continue next time
					// test if we should skip data
					if (pos >= skip_non_decryptable_last_scanned_bytepos) {
						/*It is impossible to decrypt the packets up to packet_start (a phase transition) as
							scam was restarted after the end of the next phase. So
							we can skip the whole next parity phase.*/
						dtdebugf("packets[0-{:d}] UNDECRYPTABLE pid={:d} scrambling_control_packet={:d}", packet_start, pid,
										 scrambling_control_packet);
						descrambling_contexts.clear();
						waiting_for_keys = false; // needed
						return packet_start;
					}
					return 0;
				}
			}
		}
		}
	}
	return 0;
}

/*!
	@brief
	Decrypt a batch of transportstream packets.

	This assumes that correct keys
	are available. It is not possible (?) to detect decryption errors.
	TODO: what happens when the batch size is too large? We have to avoid using more than
	2 keys of the same parity (odd or even)
	TODO: we must be able to mark the position at which keys become known in the recorded transport
	stream. Perhaps we should record all or some ecm and emm packets in the stream. The keys
	could be recorded in the database; invalid keys (not found) could be recorded as well. They roughly
	indicate where non-decrypted packets are present in the recorded stream. The alternative is to record
	this at a finergrained level.
	TODO: find out if oscam can descramble historically recorded streams. How exactly does it cache
	keys? Based on ecm pid or based on content of ecm?
*/
int dvbcsa_t::decrypt_buffer(uint8_t* buffer, int buffer_size) {
	int packet_start = 0;
	// bool late_key = false;
	// unsigned int last_scrambling_control_packet = 0;

	{
		std::unique_lock lck(key_mutex);
		num_bytes_received = num_bytes_decrypted + buffer_size; // used to mark keys
	}
	dtdebugf("buffer_size={:d}", buffer_size);
	descrambling_context_t* context = nullptr;
	// static descrambling_context_t * last_pid_context  = nullptr; //only for debugging
	for (; packet_start + ts_packet_t::size <= buffer_size; packet_start += ts_packet_t::size) {
		int pid = 0x1fff & (((int)buffer[packet_start + 1]) << 8 | buffer[packet_start + 2]);
		int scrambling_control_packet = ((buffer[packet_start + 3] & 0xc0) >> 6);
		int cc = ((buffer[packet_start + 3] & 0x0f) >> 6);
		bool payload = ((int)buffer[packet_start + 3] & 0x10);
		context = &descrambling_contexts[pid];
		if (payload) {
			if ((16 + cc - context->cc) % 16 != 0 && context->cc >= 0)
				dterrorf("pid={:d}: cc error {:d}/{:d}", pid, context->cc, cc);
			context->cc = cc;
		}

		auto odd = (scrambling_control_packet != 2);
		switch (scrambling_control_packet) {
		case 2:		// scrambled with even key
		case 3: { // scrambled with odd key
			int* idx = &cache.batch_idx[odd];
			if (context->scrambling_control_packet != scrambling_control_packet) {
				/*
					Note that this function may change packet_start, scrambling_control_packet and odd
				*/
				if (handle_parity_change(context, idx, pid, odd, packet_start, scrambling_control_packet, buffer, buffer_size) <
						0) {
					// cache.decrypt_all_pending("(waiting for keys)");
					num_bytes_decrypted += packet_start;
					return packet_start;
				}
			}
			if (waiting_for_keys) {
				cache.decrypt_all_pending("(waiting for keys)");
				num_bytes_decrypted += packet_start;
				return packet_start;
			}

			assert(!waiting_for_keys);
		}
			/*add packets to a list to decrypt them later
			 */

			cache.add_packet(odd, &buffer[packet_start]);
			context->packets_since_last_transition++;
			// dtdebugf("packet[{:d}] pid={:d} even_batch_idx={:d} odd_batch_idx={:d}", packet_start, pid, even_batch_idx,
			// odd_batch_idx); we handled a parity switch now
			break;
		default:
			break;
		}
	}
	cache.decrypt_all_pending("(no more data)"); // decrypt all even
	num_bytes_decrypted += packet_start;
	return packet_start;
}

/*!
	Returns -1 if not enough data is available, or if we must wait for a key.
	Returns 0 if parity change was successful and decryption can continue
*/
int dvbcsa_t::handle_parity_change(descrambling_context_t* context, int* idx, int pid, bool& odd, int& packet_start,
																	 int& scrambling_control_packet, uint8_t* buffer, int buffer_size) {
	/*possible parity transition detected for this pid, but this can also be caused by a single
		packet error
	*/

	if (pid == 0)
		dtdebugf("called with 0 pid\n");
	auto parity_check =
		confirm_parity_change(buffer + packet_start, buffer_size - packet_start, pid, scrambling_control_packet);
	int ret = -1;
	if (parity_check < 0) {
		/*Not enough data in buffer to confirm parity change/
			Decrypt data before the parity change and return to caller until more data is available.
		*/
		cache.decrypt_all_pending("(cannot check parity yet)");
		start_wait_for_key_time = system_clock_t::now();
		dtdebugf("KEY pid={:d}: failed parity transition to parity={:d}: control {:d} => {:d} (after {:d}/{:d} packets)", pid, odd,
						 context->scrambling_control_packet, scrambling_control_packet, packet_start / ts_packet_t::size,
						 buffer_size / ts_packet_t::size);
		if (packet_start > 0)
			dtdebugf("KEY pid ={:d} parity={:d}: Decrypted or skipped {:d}/{:d} packets in total "
							 "(waiting for parity check data)",
							 pid, odd, packet_start / ts_packet_t::size, buffer_size / ts_packet_t::size);
		return -1;
	} else if (parity_check == 0) {
		/*false alarm; packet should be decoded with different parity
			or may be corrupt
		*/
		dtdebugf("KEY pid={:d} Packet with wrong parity detected; detected parity={:d}", pid, odd);
		dtdebugf("KEY pid={:d}: aborting parity transition to parity={:d}: control {:d} => {:d} (after {:d}/{:d} packets)", pid, odd,
						 context->scrambling_control_packet, scrambling_control_packet, packet_start / ts_packet_t::size,
						 buffer_size / ts_packet_t::size);
		odd = !odd;
		ret = 0;
		scrambling_control_packet = context->scrambling_control_packet;
		idx = &cache.batch_idx[odd];
	} else {
		dtdebugf("KEY pid={:d}: parity transition to parity={:d}:  {:d} => {:d} (after {:d}/{:d} packets)", pid, odd,
						 context->scrambling_control_packet, scrambling_control_packet, packet_start / ts_packet_t::size,
						 buffer_size / ts_packet_t::size);

		ret = next_key(*context, pid, odd);
		if (ret >= 0)
			dtdebugf("KEY pid={:d}: called next_key ret={:d} for parity transition to parity={:d}: {:d} => {:d}", pid, ret, odd,
							 context->scrambling_control_packet, scrambling_control_packet);
		context->packets_since_last_transition = 0;
	}
	if (ret < 0) { // we need a key which has not yet been received from scam
		/*we failed to load the key (it has not been received)
			so we have to abort for now; or we have processed a lot of data and return early
		*/
		start_wait_for_key_time = system_clock_t::now();
		cache.decrypt_all_pending("(waiting for key)");
		dtdebugf("Decrypted or skipped {:d} packets in total (waiting for key)", packet_start);
		/*
			Check if we have to skip data
		*/
		if (buffer_size > packet_start + cache.batch_size * ts_packet_t::size) {

			auto non_decryptable = skip_non_decryptable(buffer + packet_start, buffer_size - packet_start);
			packet_start += non_decryptable;
		}
		return -1; // we must wait for a key update or for data
	} else if (ret == 1) {
		/*new key needs to be and can be installed; first decrypt any existing data with the new parity;
			there should be none in practice*/
		if (*idx) {
			dterrorf("Unexpected: old {:s} data ({:d} packets) detected while transitioning to {:s} parity",
							 odd ? "odd" : "even",
							 *idx, odd ? "odd" : "even");
			cache.decrypt_all_pending("(before changing key - UNEXPECTED)"); /*decrypt all old data with new parity,
																																				 before updating key for that new parity*/
		}
		set_cw(*context, odd); // install the key
	} else {
		assert(ret == 0); // some other pid already installed the needed key
	}
	if (context->scrambling_control_packet != 0 && ret != 0)
		dtdebugf("KEY pid={:d}: finished parity transition to parity={:d}: {:d} => {:d}", pid, odd,
						 context->scrambling_control_packet, scrambling_control_packet);

	context->scrambling_control_packet = scrambling_control_packet; // remember that we made the parity transition
	assert(!waiting_for_keys);
	return 0;
}

/*!
	The "keys" array contains typically 2 keys, an odd and an even one.
	When a pid transitions from even to odd, it requests to install the odd key (set_cw).
	At initialisation time, two keys are sent by scam (odd and even) and they need not occur
	in the right order. For this reason we must always search at least two slots to find a key of the
	proper parity

	order of keys received: odd even odd even ...
	but could also be : even odd odd even ....
	This is because stream could contain: odd odd(*) odd(*)  even odd(*) even
	where (*) is a repeated key, not sent by scam

	key_idx = the index of the last returned key for this parity

	Returns index of the next key with the correct parity
	Or: -1 no key is available yet
	Or: -2 next key is available but of the wrong parity

*/
int dvbcsa_t::get_key(int key_idx, int parity, bool allow_future_key) {
	assert(parity == 0 || parity == 1);
	std::unique_lock lck(key_mutex);
	auto& last_installed_key_idx = last_installed_key_idxs[parity]; // info only
	auto start = key_idx % num_keys;																// we may need to reuse the last key
	auto end = (last_received_key_idx + 1) % num_keys;
	/// loop over all keys, starting with the oldest (circular buffer)

	if (start == end) {
		dtdebugf("KEY LOAD {:s} key {:d}: no key yet. start={:d} end={:d}", parity ? "odd" : "even", key_idx, start, end);
		waiting_for_keys = true;
		return -1; // no key received yet
	}

	for (int idx = start; idx != end; idx = (idx + 1) % num_keys) {
		auto& key = keys[idx];
		if (!key.valid || parity != key.parity) {
			if (key.request_bytepos > num_bytes_decrypted && !allow_future_key) {
				// This can also happen at start, if keys are wrongly ordered
				dtdebugf("KEY found for other parity which was requested in the future; cannot use yet (key "
								 "we want will also be in the future)");
				/*@todo we could skip some data (but this is handled elsewehere)
				 */
				break;
			}
			continue; // we expect that this happens once on each call
		}
		if (key.valid && parity == key.parity) {
			if (key.request_bytepos > num_bytes_decrypted && !allow_future_key) {
				// This can also happen at start, if keys are wrongly ordered
				dtdebugf("KEY found for current parity which was requested in the future; cannot use it yet");
				/*@todo we could skip some data (but this is handled elsewehere)
				 */
				break;
			}
			last_installed_key_idx = idx;
			dtdebugf("Switch to {:s} key idx {:d} => {:d} start={:d} end={:d}", parity ? "odd" : "even", key_idx, idx, start, end);
			waiting_for_keys = false;
			return idx;
		}
	}

	waiting_for_keys = true;
	return -2;
}

/*!
	switch to the next key with the desired parity for one specific pid.
	Three possible outcomes:
	-returns 0; the new key was already installed by a parity transition for another pid
	-returns 1; a new key should be installed now; the caller should do that, after checking
	that any existing data for the same parity is first decrypted (normally no such
	data should exist anymore)
	-returns -1; a new key is not yet available
*/
int dvbcsa_t::next_key(descrambling_context_t& context, uint16_t pid, bool odd) {
	auto& idx = context.last_used_key_idxs[odd];
	/*
		Mark the key for the the other parity as having expired; at the start of a stream,
		we may not yet have installed a key for the other parity, but this is fine.
	*/

	auto saved_idx = idx;
	auto& otheridx = context.last_used_key_idxs[!odd]; // idx for the other parity
	if (otheridx >= 0) {
		/* First expire key for other parity (except in rare cases)
		 */
		auto& otherkey = keys[otheridx];
		if (otherkey.request_bytepos < num_bytes_decrypted) {
			/*usual case: otherkey has expired;
				note that the other key may not yet have been installed
			*/
			context.last_used_key_validity[!odd] = descrambling_context_t::key_validy_t::EXPIRED;
		} else {
			/*unusual case: otherkey has perhaps been used, but it was actually requested after the current
				parity transition;
			*/

			switch (context.last_used_key_validity[!odd]) {
			case descrambling_context_t::key_validy_t::UNKNOWN:
				dtdebugf("KEY pid {:d}: desired key[{:d}] for parity {:d} applies to a later parity phase", pid, otheridx, !odd);
				break;
			case descrambling_context_t::key_validy_t::VALID:
				dtdebugf("KEY pid {:d}: UNEXPECTED: desired key[{:d}] for parity {:d} was VALID before, but seems to be VALID "
								 "in a later phase as well)",
								 pid, otheridx, !odd);
				break;

			case descrambling_context_t::key_validy_t::EXPIRED:
				dtdebugf("KEY pid {:d}: UNEXPECTED: desired key[{:d}] for parity {:d} was EXPIRED before, but seems to be VALID "
								 "in a later phase as well)",
								 pid, otheridx, !odd);
				break;
			}
			context.last_used_key_validity[odd] = descrambling_context_t::key_validy_t::UNKNOWN;
		}
	}
	/*
		Now check for rare cases in which the last used key is valid now (and was therefore erroneously treated as
		valid earlier)
	*/
	bool allow_future_key = false;
	if (idx >= 0) {
		auto& key = keys[idx];
		switch (context.last_used_key_validity[odd]) {
		case descrambling_context_t::key_validy_t::UNKNOWN:
			/*
				This state was entered after a restart; there has been no transition to the other parity yet (or the
				state would be EXPIRED)
			*/
			if (key.request_bytepos < num_bytes_decrypted) {
				dtdebugf("KEY pid {:d}: desired key[{:d}] for parity {:d} was UNKNOWN; is now VALID", pid, idx, odd);
				context.last_used_key_validity[odd] = descrambling_context_t::key_validy_t::VALID;
				// assert(!waiting_for_keys);
				waiting_for_keys = false;
				return 0; // continue to use this key
			}
			allow_future_key = true;
			break;
		case descrambling_context_t::key_validy_t::VALID:
			dtdebugf("KEY pid {:d}: UNEXPECTED desired key[{:d}] for parity {:d} in VALID state visited twice", pid, idx, odd);
			break;
		case descrambling_context_t::key_validy_t::EXPIRED:
			assert(key.request_bytepos < num_bytes_decrypted);
			allow_future_key = true; // if the last key was expired some gap must have occurred
			break;
		}
	} else
		allow_future_key = true;
	/*next key could be found 1 or 2 slots in the future: in normal use, the next key will be
		for a different parity, but after an scam restart, 2 successive keys could have the same
		parity
	*/

	idx = (idx + 1) % num_keys;
	if (idx == cache.active_key_indexes[odd]) {
		dtdebugf("KEY pid {:d}: desired key[{:d}] for parity {:d} already installed", pid, idx, odd);
		assert(!waiting_for_keys);
		return 0;
	}

	// retrieve key from scam
	auto ret = get_key(idx, odd, allow_future_key);
	if (ret < 0) {
		/*
			no key available yet; undo the change to idx so we can later resume at the same state
		*/
		idx = saved_idx;
		assert(waiting_for_keys);
		return -1;
	}
	idx = ret;
	auto& key = keys[idx];

	switch (context.last_used_key_validity[odd]) {
	case descrambling_context_t::key_validy_t::UNKNOWN:
		// preserve the unknown state
		break;
	case descrambling_context_t::key_validy_t::VALID:
		dtdebugf("KEY pid {:d}: UNEXPECTED last key before key[{:d}] for parity {:d} is still VALID", pid, idx, odd);

		// preserve the unknown state
		break;
	case descrambling_context_t::key_validy_t::EXPIRED:
		context.last_used_key_validity[odd] = (key.restart_count != context.last_used_key_restart_count)
			? descrambling_context_t::key_validy_t::UNKNOWN
			: // first key of this parity after a restart
		descrambling_context_t::key_validy_t::VALID;
		break;
	}

	if (key.restart_count != context.last_used_key_restart_count) {
		dtdebugf("KEY pid {:d}: key[{:d}] for parity {:d} is FIRST AFTER RESTART restart_count={:d}/{:d}", pid, idx, odd,
						 key.restart_count, context.last_used_key_restart_count);
		context.last_used_key_restart_count = key.restart_count;
	}
	dtdebugf("KEY pid={:d}: Switched from key[{:d}] to key[{:d}] for parity {:d}", pid, cache.active_key_indexes[odd], ret, odd);

	// This test needs to be done once more
	if (idx == cache.active_key_indexes[odd]) {
		dtdebugf("KEY pid {:d}: desired key with idx={:d} and parity {:d} already installed", pid, idx, odd);
		assert(!waiting_for_keys);
		return 0;
	}
	assert(!waiting_for_keys);
	return 1;
}

/*!
	called by scam thread to transfer a received key to a specific
	service thread
*/
void dvbcsa_t::add_key(const ca_slot_t& slot, int decryption_index, system_time_t t) {
	std::unique_lock lck(key_mutex);
	auto idx = (last_received_key_idx + 1) % num_keys;
	if (this->decryption_index < 0)
		this->decryption_index = decryption_index;
	if (decryption_index != this->decryption_index) {
		dterrorf("Unexpected: received keys from multiple slots: {:d} and {:d}", decryption_index, this->decryption_index);
	};
	auto k = slot.last_key.to_str();
	ss::string<64> tt;
	tt.format("{}", std::chrono::duration_cast<std::chrono::seconds>(t - start).count());
	dtdebugf("ADD CW {:s} [{:d}] at bytepos={:d} t={:s}: {:s}", odd_even_str(slot.last_key.parity), idx, num_bytes_received,
					 tt.c_str(), k.c_str());
	auto& key = keys[idx];
	key = slot.last_key;
	key.receive_time = t;
	key.receive_bytepos = num_bytes_received;
	key.request_time = last_key_request_time;
	key.request_bytepos = last_key_request_bytepos;
	key.restart_count = restart_count;
	/*tag the key with the point in the byte stream where it was approximately received
		Because multiple threads are involved, this position is approximate
	*/
	// key.valid_from_byte_pos = num_bytes_received;
	last_received_key_idx = idx;
}

void dvbcsa_t::restart_decryption(system_time_t t) {
	ss::string<64> tt;
	tt.format("{}", std::chrono::duration_cast<std::chrono::seconds>(t - start).count());
	dtdebugf("DECRYPTION restarted at bytepos {:d} t={:s}", num_bytes_received, tt.c_str());
	std::unique_lock lck(key_mutex);
	restart_count++;
	last_restart_decryption_bytepos = num_bytes_received;
}

void dvbcsa_t::mark_ecm_sent(bool odd, system_time_t t) {
	ss::string<64> tt;
	tt.format("{}", std::chrono::duration_cast<std::chrono::seconds>(t - start).count());
	dtdebugf("ECM {:s} sent to scam at bytepos={:d} t={:s}", odd_even_str(odd), num_bytes_received, tt.c_str());
	std::unique_lock lck(key_mutex);
	last_key_request_bytepos = num_bytes_received;
	last_key_request_time = t;
}

void dvbcsa_t::set_cw(const descrambling_context_t& context, bool odd) {
	auto& idx = context.last_used_key_idxs[odd];
	auto& key = keys[idx];
	assert(key.parity == odd);
	assert(key.parity == 0 || key.parity == 1);
	dvbcsa_bs_key_set(key.cw, cache.active_keys[key.parity]);
	auto k = key.to_str();
	cache.active_key_indexes[key.parity] = idx;
	dtdebugf("SET CW {:s}[{:d}]: {:s}", key.parity ? "odd" : "even", idx, k.c_str());
}

dvbcsa_t::dvbcsa_t()  {
	start = system_clock_t::now();
}

dvbcsa_t::~dvbcsa_t() {
}
