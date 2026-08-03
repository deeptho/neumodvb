/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
 *
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
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/optional.h>

#include "options.h"
#include "neumodb/devdb/tune_options.h"
#include "stackstring/stackstring_pybind.h"
namespace nb = nanobind;


void export_options(nb::module_& m) {
	nb::class_<neumo_options_t>(m, "options_t")
		.def(nb::init<>(), "Options for neumodvb; set before use")
		.def("load_from_db", &neumo_options_t::load_from_db, nb::arg("devdb_rtxn"), nb::arg("user_id")=0)
		.def("save_to_db", &neumo_options_t::save_to_db, nb::arg("devdb_wtxn"), nb::arg("user_id")=0)
		.def_rw("upgrade_dir", &neumo_options_t::upgrade_dir)
		.def_rw("db_dir", &neumo_options_t::db_dir)
		.def_rw("live_path", &neumo_options_t::live_path)
		.def_rw("recordings_path", &neumo_options_t::recordings_path)
		.def_rw("spectrum_path", &neumo_options_t::spectrum_path)
		.def_rw("softcam_server", &neumo_options_t::softcam_server)
		.def_rw("softcam_port", &neumo_options_t::softcam_port)
		.def_rw("softcam_enabled", &neumo_options_t::softcam_enabled)
		.def_rw("devdb", &neumo_options_t::devdb)
		.def_rw("chdb", &neumo_options_t::chdb)
		.def_rw("statdb", &neumo_options_t::statdb)
		.def_rw("epgdb", &neumo_options_t::epgdb)
		.def_rw("recdb", &neumo_options_t::recdb)
		.def_rw("logconfig", &neumo_options_t::logconfig)
		.def_rw("osd_svg", &neumo_options_t::osd_svg)
		.def_rw("radiobg_svg", &neumo_options_t::radiobg_svg)
		.def_rw("mpvconfig", &neumo_options_t::mpvconfig)
		.def_rw("usals_location", &neumo_options_t::usals_location)

		.def_rw("default_record_time", &neumo_options_t::default_record_time)
		.def_rw("pre_record_time", &neumo_options_t::pre_record_time)
		.def_rw("max_pre_record_time", &neumo_options_t::max_pre_record_time,
									 "upper limit on prerecord time. Needed by start_recordings to efficiently search")
		.def_rw("post_record_time", &neumo_options_t::post_record_time,
									 "extra seconds to record after a program ends")
		.def_rw("timeshift_duration", &neumo_options_t::timeshift_duration, "how far can user rewind?")
		.def_rw("livebuffer_retention_time", &neumo_options_t::livebuffer_retention_time,
									 "how soon is an inactive timehsift buffer removed")
		.def_rw("livebuffer_mpm_part_duration", &neumo_options_t::livebuffer_mpm_part_duration,
									 "how quickly live buffers are deleted after they become inactive")
		.def_rw("tune_use_blind_tune", &neumo_options_t::tune_use_blind_tune)
		.def_rw("tune_may_move_dish", &neumo_options_t::tune_may_move_dish)
		.def_rw("tune_use_bbframes", &neumo_options_t::tune_use_bbframes)
		.def_rw("dish_move_penalty", &neumo_options_t::dish_move_penalty)
		.def_rw("resource_reuse_bonus", &neumo_options_t::resource_reuse_bonus)
		.def_rw("positioner_dialog_use_blind_tune", &neumo_options_t::positioner_dialog_use_blind_tune)
		.def_rw("scan_max_duration", &neumo_options_t::scan_max_duration)
		.def_rw("scan_use_blind_tune", &neumo_options_t::scan_use_blind_tune)
		.def_rw("scan_may_move_dish", &neumo_options_t::scan_may_move_dish)
		.def_rw("band_scan_save_spectrum", &neumo_options_t::band_scan_save_spectrum)
		;
}
