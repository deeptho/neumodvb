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

#include "options.h"

void neumo_options_t::load_from_db(db_txn& devdb_wtxn, int32_t user_id)
{
	auto c = devdb::user_options_t::find_by_key(devdb_wtxn, user_id);
	if(c.is_valid()) {
		const auto & u = c.current();
		this->softcam_server = u.softcam_server;
		this->softcam_port = u.softcam_port;
		this->softcam_enabled = u.softcam_enabled;

		this->usals_location = u.usals_location;

		this->tune_use_blind_tune = u.tune_use_blind_tune;
		this->tune_may_move_dish = u.tune_may_move_dish;
		this->dish_move_penalty = u.dish_move_penalty;
		this->resource_reuse_bonus = u.resource_reuse_bonus;

		this->scan_use_blind_tune = u.scan_use_blind_tune;
		this->scan_may_move_dish = u.scan_may_move_dish;
		this->scan_max_duration = std::chrono::seconds(u.scan_max_duration);
		this->band_scan_save_spectrum = u.band_scan_save_spectrum;

		this->positioner_dialog_use_blind_tune = u.positioner_dialog_use_blind_tune;

		this->default_record_time = std::chrono::seconds(u.default_record_time);
		this->pre_record_time = std::chrono::seconds(u.pre_record_time);
		this->max_pre_record_time = std::chrono::seconds(u.max_pre_record_time);
		this->post_record_time = std::chrono::seconds(u.post_record_time);

		this->timeshift_duration = std::chrono::seconds(u.timeshift_duration);
		this->livebuffer_retention_time = std::chrono::seconds(u.livebuffer_retention_time);
		this->livebuffer_mpm_part_duration = std::chrono::seconds(u.livebuffer_mpm_part_duration);

	} else {
		save_to_db(devdb_wtxn, user_id);
	}
}



void neumo_options_t::save_to_db(db_txn& devdb_wtxn, int32_t user_id)
{
	devdb::user_options_t u;
	u.user_id = user_id;
	u.mtime = system_clock_t::to_time_t(now);

	u.softcam_server = this->softcam_server.c_str();
	u.softcam_port = this->softcam_port;
	u.softcam_enabled =	this->softcam_enabled;

	u.usals_location = this->usals_location;

	u.tune_use_blind_tune = this->tune_use_blind_tune;
	u.tune_may_move_dish = this->tune_may_move_dish;
	u.dish_move_penalty = this->dish_move_penalty;
	u.resource_reuse_bonus = this->resource_reuse_bonus;

	u.scan_use_blind_tune = this->scan_use_blind_tune;
	u.scan_may_move_dish = this->scan_may_move_dish;
	u.scan_max_duration = this->scan_max_duration.count();
	u.band_scan_save_spectrum = this->band_scan_save_spectrum;

	u.positioner_dialog_use_blind_tune = this->positioner_dialog_use_blind_tune;

	u.default_record_time = default_record_time.count();
	u.pre_record_time = this->pre_record_time.count();
	u.max_pre_record_time = this->max_pre_record_time.count();
	u.post_record_time  = this->post_record_time.count();

	u.timeshift_duration = this->timeshift_duration.count();
	u.livebuffer_retention_time = this->livebuffer_retention_time.count();
	u.livebuffer_mpm_part_duration = this->livebuffer_mpm_part_duration.count();

	put_record(devdb_wtxn, u);
}
