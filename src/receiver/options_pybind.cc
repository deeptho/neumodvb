/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
#include <pybind11/chrono.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include <stdio.h>

#include "options.h"
#include "neumodb/devdb/tune_options.h"
#include "stackstring/stackstring_pybind.h"
namespace py = pybind11;


void export_options(py::module& m) {
	py::class_<neumo_options_t>(m, "options_t")
		.def(py::init<>(), "Options for neumodvb; set before use")
		.def("load_from_db", &neumo_options_t::load_from_db, py::arg("devdb_rtxn"), py::arg("user_id")=0)
		.def("save_to_db", &neumo_options_t::save_to_db, py::arg("devdb_wtxn"), py::arg("user_id")=0)
		.def_readwrite("upgrade_dir", &neumo_options_t::upgrade_dir)
		.def_readwrite("db_dir", &neumo_options_t::db_dir)
		.def_readwrite("live_path", &neumo_options_t::live_path)
		.def_readwrite("recordings_path", &neumo_options_t::recordings_path)
		.def_readwrite("spectrum_path", &neumo_options_t::spectrum_path)
		.def_readwrite("softcam_server", &neumo_options_t::softcam_server)
		.def_readwrite("softcam_port", &neumo_options_t::softcam_port)
		.def_readwrite("softcam_enabled", &neumo_options_t::softcam_enabled)
		.def_readwrite("devdb", &neumo_options_t::devdb)
		.def_readwrite("chdb", &neumo_options_t::chdb)
		.def_readwrite("statdb", &neumo_options_t::statdb)
		.def_readwrite("epgdb", &neumo_options_t::epgdb)
		.def_readwrite("recdb", &neumo_options_t::recdb)
		.def_readwrite("logconfig", &neumo_options_t::logconfig)
		.def_readwrite("osd_svg", &neumo_options_t::osd_svg)
		.def_readwrite("radiobg_svg", &neumo_options_t::radiobg_svg)
		.def_readwrite("mpvconfig", &neumo_options_t::mpvconfig)
		.def_readwrite("usals_location", &neumo_options_t::usals_location)

		.def_readwrite("default_record_time", &neumo_options_t::default_record_time)
		.def_readwrite("pre_record_time", &neumo_options_t::pre_record_time)
		.def_readwrite("max_pre_record_time", &neumo_options_t::max_pre_record_time,
									 "upper limit on prerecord time. Needed by start_recordings to efficiently search")
		.def_readwrite("post_record_time", &neumo_options_t::post_record_time,
									 "extra seconds to record after a program ends")
		.def_readwrite("timeshift_duration", &neumo_options_t::timeshift_duration, "how far can user rewind?")
		.def_readwrite("livebuffer_retention_time", &neumo_options_t::livebuffer_retention_time,
									 "how soon is an inactive timehsift buffer removed")
		.def_readwrite("livebuffer_mpm_part_duration", &neumo_options_t::livebuffer_mpm_part_duration,
									 "how quickly live buffers are deleted after they become inactive")
		.def_readwrite("tune_use_blind_tune", &neumo_options_t::tune_use_blind_tune)
		.def_readwrite("tune_may_move_dish", &neumo_options_t::tune_may_move_dish)
		.def_readwrite("dish_move_penalty", &neumo_options_t::dish_move_penalty)
		.def_readwrite("resource_reuse_bonus", &neumo_options_t::resource_reuse_bonus)
		.def_readwrite("positioner_dialog_use_blind_tune", &neumo_options_t::positioner_dialog_use_blind_tune)
		.def_readwrite("scan_max_duration", &neumo_options_t::scan_max_duration)
		.def_readwrite("scan_use_blind_tune", &neumo_options_t::scan_use_blind_tune)
		.def_readwrite("scan_may_move_dish", &neumo_options_t::scan_may_move_dish)
		.def_readwrite("band_scan_save_spectrum", &neumo_options_t::band_scan_save_spectrum)
		;
}
