/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
 *
 * Copyright notice:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the sGNU General Public License as published by
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
#include "setproctitle.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>
#include <wx/window.h>
#include "viewer/wxpy_api.h"
#include "receiver/receiver.h"
#include "receiver/scan.h"
#include "neumodb/chdb/chdb_extra.h"
#include "subscriber_pybind.h"
#include "util/identification.h"
#include <sys/prctl.h>

namespace nb = nanobind;

static void export_live_history(nb::module_& m) {
	using namespace chdb;
	nb::class_<history_mgr_t>(m, "browse_history")
		.def_rw("h", &history_mgr_t::h)
		.def("save", nb::overload_cast<>(&history_mgr_t::save))
		.def("save", nb::overload_cast<const chdb::service_t&>(&history_mgr_t::save), nb::arg("service"))
		.def("save", nb::overload_cast<const chdb::chgm_t&>(&history_mgr_t::save), nb::arg("chgm"))
		.def("last_service", &history_mgr_t::last_service)
		.def("last_chgm", &history_mgr_t::last_chgm)
		.def("next_service", &history_mgr_t::next_service)
		.def("next_chgm", &history_mgr_t::next_chgm)
		.def("prev_service", &history_mgr_t::prev_service)
		.def("prev_chgm", &history_mgr_t::prev_chgm)
		.def("recall", &history_mgr_t::recall_service)
		.def("recall_chgm", &history_mgr_t::recall_chgm)
		.def("clear", &history_mgr_t::clear)
		;
}

static void export_recording_history(nb::module_& m) {
	using namespace recdb;
	nb::class_<rec_history_mgr_t>(m, "rec_browse_history")
		.def_rw("h", &rec_history_mgr_t::h)
		.def("save", nb::overload_cast<>(&rec_history_mgr_t::save))
		.def("save", nb::overload_cast<const recdb::rec_t&>(&rec_history_mgr_t::save), nb::arg("recording"))
		.def("last_recording", &rec_history_mgr_t::last_recording)
		.def("next_recording", &rec_history_mgr_t::next_recording)
		.def("prev_recording", &rec_history_mgr_t::prev_recording)
		.def("recall_recording", &rec_history_mgr_t::recall_recording)
		.def("clear", &rec_history_mgr_t::clear)
		;
}

static void set_process_name(const char* name)
{
	setproctitle(name);
#if 0
	char **argv=nullptr;
	int argc =0;
	int i=0;
	get_argc_argv(&argc, &argv);
	printf("argc={:d}\n", argc);
	auto l = strlen(argv[0]);
	if (strlen(name) > l) {
		dterrorf("String too long: {}\n", name);
	}
	strncpy(argv[0], name, l+1);
	for(i=1; i <argc; ++i)
		argv[i]=0;
	if (prctl(PR_SET_NAME, (unsigned long)argv[0], 0, 0, 0) < 0) {
		dterrorf("prctl failed: {}", strerror(errno));
	}
	pthread_setname_np(pthread_self(), argv[0]);
#else
	pthread_setname_np(pthread_self(), name);
#endif
}

static void export_db_upgrade_info(nb::module_& m) {
	nb::class_<db_upgrade_info_t>(m, "db_upgrade_info_t")
		.def_ro("stored_db_version", &db_upgrade_info_t::stored_db_version)
		.def_ro("current_db_version", &db_upgrade_info_t::current_db_version)
		;
}

static void export_receiver(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	export_db_upgrade_info(m);
	// Setup a default log config (should be overridden by user)
	neumo_options_t options;
	auto log_path = config_path / options.logconfig;
#if 0
	set_logconfig(log_path.c_str());
#endif
	m.def("set_logconfig", &set_logconfig, nb::arg("name of logfile config"))
		.def("set_process_name", &set_process_name, "Set process name",
				 nb::arg("name"))
		;
	nb::class_<receiver_t>(m, "receiver_t")
		.def(nb::init<neumo_options_t*>(), nb::arg("neumo_options"), "Start a NeumoDVB receiver")
		//unsubscribe is needed to abort mux scan in progress
		.def("init", &receiver_t::init, "Re-initialize a receiver if creating it failed")
		.def("renumber_card", &receiver_t::renumber_card, "Renumber a card",
				 nb::arg("old_number"), nb::arg("new_number"))
		.def("update_autorec",
				 (&receiver_t::update_autorec),
				 "Create or update an auto rec", nb::arg("autorec"))
		.def("delete_autorec",
				 (&receiver_t::update_autorec),
				 "Delete an auto rec", nb::arg("autorec"))
		.def("toggle_recording",
				 nb::overload_cast<const chdb::service_t&, const epgdb::epg_record_t&>(&receiver_t::toggle_recording),
				 "Toggle recording of an epg event.", nb::arg("service"), nb::arg("epgrecord"))
		.def("toggle_recording", nb::overload_cast<const chdb::service_t&>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", nb::arg("service"))
		.def("toggle_recording",
				 nb::overload_cast<const chdb::service_t&, time_t, int, const char*>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", nb::arg("service"), nb::arg("start"), nb::arg("duration"),
				 nb::arg("event_name"))
		.def("update_and_toggle_stream"
				 , &receiver_t::update_and_toggle_stream
				 , "Add or update a stream in the database and start or stop the stream accordingly"
				 , nb::arg("stream")
			)

		.def("get_default_tune_options", &receiver_t::get_default_tune_options,
				 nb::arg("subscription_type"))
		.def("get_api_type", &receiver_t::get_api_type)
		.def("get_options", &receiver_t::get_options)
		.def("set_options", &receiver_t::set_options, nb::arg("options"))
		.def(
			"get_spectrum_path",
			[](receiver_t& receiver) { return std::string(receiver.options.readAccess()->spectrum_path.c_str()); },
			"Return location where spectra are stored")

		.def("stop", &receiver_t::stop, "Cleanup before exit")
		.def_ro("browse_history", &receiver_t::browse_history, nb::rv_policy::reference_internal)
		.def_ro("rec_browse_history", &receiver_t::rec_browse_history, nb::rv_policy::reference_internal)
		.def_prop_ro("error_message", [](receiver_t* self) { return get_error().c_str(); })
		.def_ro("devdb", &receiver_t::devdb)
		.def_ro("chdb", &receiver_t::chdb)
		.def_ro("epgdb", &receiver_t::epgdb)
		.def_ro("recdb", &receiver_t::recdb)
		.def_ro("statdb", &receiver_t::statdb)
		.def_ro("db_upgrade_info", &receiver_t::db_upgrade_info)
		;
}

extern void export_logger(nb::module_& m);
extern void export_options(nb::module_& m);
NB_MODULE(pyreceiver, m) {
	m.doc() = R"pbdoc(
        Receiver control functions for neumoDVB
    )pbdoc";	// export_find_type(m);
	export_receiver(m);
	export_subscriber(m);
	export_logger(m);
	export_live_history(m);
	export_recording_history(m);
	export_options(m);
	m.attr("__version__") = version_info();
}
