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
#include "active_playback.h"
#include "mpm.h"
#include "receiver.h"
#include "util/logger.h"
#include "util/util.h"
#include <atomic>
#include <errno.h>
#include <filesystem>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

std::unique_ptr<playback_mpm_t> active_playback_t::make_client_mpm(receiver_t& receiver, subscription_id_t subscription_id) {
	auto mpm = std::make_unique<playback_mpm_t>(receiver, subscription_id);
	const recdb::rec_t& rec = currently_playing_recording;
	auto d = fs::path(receiver.options.readAccess()->recordings_path.c_str()) / rec.filename.c_str();
	mpm->open_recording(d.c_str());
	return mpm;
}
