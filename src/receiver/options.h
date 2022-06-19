/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include <string>
#include "stackstring.h"
#include "stackstring.h"
#include "neumodb/chdb/chdb_extra.h"

#define CALLBACK

class receiver_t;

struct neumo_options_t {
	//receiver_t& receiver;
	bool inplace_upgrade = false;
	bool dont_backup = false;
	bool force_overwrite = false;
	//::chdb::options_t options;

	std::string live_path{"/mnt/neumo/live"};
	std::string recordings_path{"/mnt/neumo/recordings"};
	std::string spectrum_path{"/mnt/neumo/spectrum"};
	std::string scam_server_name{"streacom.mynet"};
	int scam_server_port{9000};
	std::string chdb{"/mnt/neumo/db/chdb.mdb"};
	std::string statdb{"/mnt/neumo/db/statdb.mdb"};
	std::string epgdb{"/mnt/neumo/db/epgdb.mdb"};
	std::string recdb{"/mnt/neumo/db/recdb.mdb"};
	std::string logconfig{"neumo.xml"};
	std::string osd_svg{"osd.svg"};
	std::string radiobg_svg{"radiobg.svg"};
	std::string mpvconfig{"mpv"};

	chdb::usals_location_t usals_location;
	int32_t dish_move_penalty{100}; //penalty for having to move dish; reduces priority


	std::chrono::seconds pre_record_time{1min}; //extra seconds to record before a program start
	std::chrono::seconds post_record_time{5min}; //extra seconds to record after a program ends
	std::chrono::seconds max_pre_record_time{1h}; //upper limit on prerecord time. Needed by start_recordings to efficiently search
	std::chrono::seconds default_record_time{2h};

	std::chrono::seconds timeshift_duration{2h}; //ow far can user rewind?
	std::chrono::seconds livebuffer_retention_time{5min}; //how soon is an inactive timehsift buffer removed

	std::chrono::seconds livebuffer_mpm_part_duration{300s}; //how quickly live buffers are deleted after they become inactive



	neumo_options_t()
		{}
};
