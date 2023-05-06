/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include <wx/window.h>
#include <sip.h>
#include "viewer/wxpy_api.h"
#include "receiver/receiver.h"
#include "receiver/scan.h"
#include "neumodb/chdb/chdb_extra.h"
#include "subscriber_pybind.h"
#include "util/identification.h"
#include <sys/prctl.h>

namespace py = pybind11;

static void export_live_history(py::module& m) {
	using namespace chdb;
	py::class_<history_mgr_t>(m, "browse_history")
		.def_readwrite("h", &history_mgr_t::h)
		.def("save", py::overload_cast<>(&history_mgr_t::save))
		.def("save", py::overload_cast<const chdb::service_t&>(&history_mgr_t::save), py::arg("service"))
		.def("save", py::overload_cast<const chdb::chgm_t&>(&history_mgr_t::save), py::arg("chgm"))
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

static void export_recording_history(py::module& m) {
	using namespace recdb;
	py::class_<rec_history_mgr_t>(m, "rec_browse_history")
		.def_readwrite("h", &rec_history_mgr_t::h)
		.def("save", py::overload_cast<>(&rec_history_mgr_t::save))
		.def("save", py::overload_cast<const recdb::rec_t&>(&rec_history_mgr_t::save), py::arg("recording"))
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
	printf("argc=%d\n", argc);
	auto l = strlen(argv[0]);
	if (strlen(name) > l) {
		dterrorx("String too long: %s\n", name);
	}
	strncpy(argv[0], name, l+1);
	for(i=1; i <argc; ++i)
		argv[i]=0;
	if (prctl(PR_SET_NAME, (unsigned long)argv[0], 0, 0, 0) < 0) {
		dterrorx("prctl failed: %s", strerror(errno));
	}
	pthread_setname_np(pthread_self(), argv[0]);
#else
	pthread_setname_np(pthread_self(), name);
#endif
}

static void export_db_upgrade_info(py::module& m) {
	py::class_<db_upgrade_info_t>(m, "db_upgrade_info_t")
		.def_readonly("stored_db_version", &db_upgrade_info_t::stored_db_version)
		.def_readonly("current_db_version", &db_upgrade_info_t::current_db_version)
		;
}

static void export_receiver(py::module& m) {
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
	m.def("set_logconfig", &set_logconfig, py::arg("name of logfile config"))
		.def("set_process_name", &set_process_name, "Set process name",
				 py::arg("name"))
		;
	py::class_<receiver_t>(m, "receiver_t")
		.def(py::init<neumo_options_t*>(), py::arg("neumo_options"), "Start a NeumoDVB receiver")
		//unsubscribe is needed to abort mux scan in progress
		.def("init", &receiver_t::init, "Re-initialize a receiver if creating it failed")
		.def("renumber_card", &receiver_t::renumber_card, "Renumber a card",
				 py::arg("old_number"), py::arg("new_number"))
		.def("unsubscribe", &receiver_t::unsubscribe, "Unsubscribe a service or mux", py::arg("subscription_id"))
		.def("toggle_recording",
				 py::overload_cast<const chdb::service_t&, const epgdb::epg_record_t&>(&receiver_t::toggle_recording),
				 "Toggle recording of an epg event.", py::arg("service"), py::arg("epgrecord"))
		.def("toggle_recording", py::overload_cast<const chdb::service_t&>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", py::arg("service"))
		.def("toggle_recording",
				 py::overload_cast<const chdb::service_t&, time_t, int, const char*>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", py::arg("service"), py::arg("start"), py::arg("duration"),
				 py::arg("event_name"))
		.def("get_api_type", &receiver_t::get_api_type)
		.def("get_options", &receiver_t::get_options)
		.def("set_options", &receiver_t::set_options, py::arg("options"))
		.def("get_scan_stats", &receiver_t::get_scan_stats, "Return true if a statistics of current scan, in progress",
				 py::arg("subscription_id"))
		.def(
			"get_spectrum_path",
			[](receiver_t& receiver) { return std::string(receiver.options.readAccess()->spectrum_path.c_str()); },
			"Return location where spectra are stored")

		.def("stop", &receiver_t::stop, "Cleanup before exit")
		.def_readonly("browse_history", &receiver_t::browse_history, py::return_value_policy::reference_internal)
		.def_readonly("rec_browse_history", &receiver_t::rec_browse_history, py::return_value_policy::reference_internal)
		.def_property_readonly("error_message", [](receiver_t* self) { return get_error().c_str(); })
		.def_readonly("devdb", &receiver_t::devdb)
		.def_readonly("chdb", &receiver_t::chdb)
		.def_readonly("epgdb", &receiver_t::epgdb)
		.def_readonly("recdb", &receiver_t::recdb)
		.def_readonly("statdb", &receiver_t::statdb)
		.def_readonly("db_upgrade_info", &receiver_t::db_upgrade_info)
		;
}

extern void export_logger(py::module& m);
extern void export_options(py::module& m);

PYBIND11_MODULE(pyreceiver, m) {
	m.doc() = R"pbdoc(
        Receiver control functions for neumoDVB
    )pbdoc";	// export_find_type(m);
	export_receiver(m);
	export_subscriber(m);
	export_signal_info(m);
	export_sdt_data(m);
	export_scan_report(m);
	export_logger(m);
	export_live_history(m);
	export_recording_history(m);
	export_options(m);
	m.attr("__version__") = version_info();

}
