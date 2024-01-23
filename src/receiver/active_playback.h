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

class active_playback_t final : public std::enable_shared_from_this<active_playback_t> {

	typedef std::function<void(const chdb::language_code_t& lang)> callback_t;
	mutable std::mutex mutex;
	receiver_t& receiver;
public:
	recdb::rec_t currently_playing_recording;
	std::unique_ptr<playback_mpm_t> make_client_mpm(receiver_t& receiver, subscription_id_t subscription_id);


	inline chdb::service_t get_current_service() const  {
		std::scoped_lock lck(mutex);
		return currently_playing_recording.service;
	}
	playback_info_t get_current_program_info() const;


public:
		//void process_psi(int pid, unsigned char* payload, int payload_size);
	active_playback_t(receiver_t& receiver_, const recdb::rec_t& rec_)
		: receiver(receiver_)
		, currently_playing_recording(rec_) {}

	virtual ~active_playback_t() final {
		dtdebugf("destructor\n");
	}

	virtual  ss::string<32> name() const {
		ss::string<32>  ret;
		ret.format("{}", currently_playing_recording);
		return ret;
	}

	/*also during playback we need to monitor pmt for disappearing/appearing audio anguages.
		In this case, we have to tell the mpv thread to change to another track according to the user
		preferences.
		This also needs to be implemented in timeshift mode

	*/
};
