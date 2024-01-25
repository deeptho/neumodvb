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
#include <string>
#include "stackstring.h"
#include "stackstring.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"

#define CALLBACK

class receiver_t;

struct neumo_options_t {
	//receiver_t& receiver;
	bool inplace_upgrade = false;
	bool dont_backup = false;
	bool force_overwrite = false;
	//::chdb::options_t options;
	std::string upgrade_dir{}; // set  from python
	std::string db_dir{"~/neumo/db"};
	std::string live_path{"~/neumo/live"};
	std::string recordings_path{"~/neumo/recordings"};
	std::string spectrum_path{"~/neumo/spectrum"};
	std::string logconfig{"neumo.xml"};
	std::string osd_svg{"osd.svg"};
	std::string devdb{"~/neumo/db/devdb.mdb"};
	std::string chdb{"~/neumo/db/chdb.mdb"};
	std::string statdb{"~/neumo/db/statdb.mdb"};
	std::string epgdb{"~/neumo/db/epgdb.mdb"};
	std::string recdb{"~/neumo/db/recdb.mdb"};
	std::string radiobg_svg{"radiobg.svg"};
	std::string mpvconfig{"mpv"};

	std::string softcam_server{"192.168.2.254"};
	int softcam_port{9000};
	bool softcam_enabled{true};
	devdb::usals_location_t usals_location;
	bool tune_use_blind_tune{false};
	bool positioner_dialog_use_blind_tune{false};
	bool scan_use_blind_tune{false};
	bool band_scan_save_spectrum{false};

	bool tune_may_move_dish{false};
	bool scan_may_move_dish{false};

	int32_t dish_move_penalty{100}; //penalty for having to move dish; reduces priority
	int32_t resource_reuse_bonus{1000}; //penalty for having to move dish; increases priority


	std::chrono::seconds pre_record_time{1min}; //extra seconds to record before a program start
	std::chrono::seconds post_record_time{5min}; //extra seconds to record after a program ends
	std::chrono::seconds max_pre_record_time{1h}; //upper limit on prerecord time. Needed by start_recordings to efficiently search
	std::chrono::seconds default_record_time{2h};

	std::chrono::seconds timeshift_duration{2h}; //ow far can user rewind?
	std::chrono::seconds livebuffer_retention_time{5min}; //how soon is an inactive timehsift buffer removed

	std::chrono::seconds livebuffer_mpm_part_duration{300s}; //duration of an mpm part

	std::chrono::seconds scan_max_duration{180s}; /*after this time, scan will be forcefull ended*/

	neumo_options_t()
		{}

	EXPORT void load_from_db(db_txn& devdb_wtxn, int32_t user_id=0);
	EXPORT void save_to_db(db_txn& devdb_wtxn, int32_t user_id=0);
};
