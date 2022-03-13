/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

static int scan_muxes(receiver_t& reseiver, py::list mux_list, int subscription_id) {
	ss::vector<chdb::dvbs_mux_t,1> dvbs_muxes;
	ss::vector<chdb::dvbc_mux_t,1> dvbc_muxes;
	ss::vector<chdb::dvbt_mux_t,1> dvbt_muxes;
	for(auto m: mux_list) {
		bool ok{false};
		if(!ok)
			try {
				auto* dvbs_mux = m.cast<chdb::dvbs_mux_t*>();
				dvbs_muxes.push_back(*dvbs_mux);
				ok=true;
			} catch (py::cast_error& e) {}
		if(!ok)
			try {
				auto* dvbc_mux = m.cast<chdb::dvbc_mux_t*>();
				dvbc_muxes.push_back(*dvbc_mux);
				ok=true;
			} catch (py::cast_error& e) {}
		if(!ok)
			try {
				auto* dvbt_mux = m.cast<chdb::dvbt_mux_t*>();
				dvbt_muxes.push_back(*dvbt_mux);
				ok=true;
			} catch (py::cast_error& e) {}
	}

	if(dvbs_muxes.size() > 0)
		subscription_id = reseiver.scan_muxes(dvbs_muxes, subscription_id);
	if(dvbc_muxes.size() > 0)
		subscription_id = reseiver.scan_muxes(dvbc_muxes, subscription_id);
	if(dvbt_muxes.size() > 0)
		subscription_id = reseiver.scan_muxes(dvbt_muxes, subscription_id);
	return subscription_id;
}
void export_receiver(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;

	// Setup a default log config (should be overridden by user)
	neumo_options_t options;
	auto log_path = config_path / options.logconfig;
#if 0
	set_logconfig(log_path.c_str());
#endif
	m.def("set_logconfig", &set_logconfig, py::arg("name of logfile config"));
	py::class_<receiver_t>(m, "receiver_t")
		.def(py::init<neumo_options_t*>(), py::arg("neumo_options"), "Start a NeumoDVB receiver")
		.def("dump_subs", &receiver_t::dump_subs, "Show subscriptions")
		.def("dump_all_frontends", &receiver_t::dump_all_frontends, "Show all tuners")
		//unsubscribe is needed to abort mux scan in progress
		.def("unsubscribe", &receiver_t::unsubscribe, "Unsubscribe a service or mux", py::arg("subscription_id"))
		.def("scan_muxes", scan_muxes,
				 "Scan muxes",  py::arg("muxlist"), py::arg("subscription_id"))
		.def("subscribe",
				 py::overload_cast<const chdb::dvbs_mux_t&, bool, int>(&receiver_t::subscribe_mux<chdb::dvbs_mux_t>),
				 "Subscribe to a mux", py::arg("mux"), py::arg("blindscan"), py::arg("subscription_id"))
		.def("subscribe",
				 py::overload_cast<const chdb::dvbc_mux_t&, bool, int>(&receiver_t::subscribe_mux<chdb::dvbc_mux_t>),
				 "Subscribe to a mux", py::arg("mux"), py::arg("blindscan"), py::arg("subscription_id"))
		.def("subscribe",
				 py::overload_cast<const chdb::dvbt_mux_t&, bool, int>(&receiver_t::subscribe_mux<chdb::dvbt_mux_t>),
				 "Subscribe to a mux", py::arg("mux"), py::arg("blindscan"), py::arg("subscription_id"))
#if 0
		.def("subscribe", py::overload_cast<const chdb::service_t&, int>(&receiver_t::subscribe_service),
				 "Subscribe to a service; if subscription_id is specified, then the service replaces "
				 "the current subscription."
				 "Returns thew new or existing subscription id",
				 py::arg("service"), py::arg("subscription_id") = -1)

		.def("unsubscribe", &receiver_t::unsubscribe, "Unsubscribe", py::arg("subscription_id"))
#endif
		.def("toggle_recording",
				 py::overload_cast<const chdb::service_t&, const epgdb::epg_record_t&>(&receiver_t::toggle_recording),
				 "Toggle recording of an epg event.", py::arg("service"), py::arg("epgrecord"))
		.def("toggle_recording", py::overload_cast<const chdb::service_t&>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", py::arg("service"))
		.def("toggle_recording",
				 py::overload_cast<const chdb::service_t&, time_t, int, const char*>(&receiver_t::toggle_recording),
				 "Toggle recording the current service.", py::arg("service"), py::arg("start"), py::arg("duration"),
				 py::arg("event_name"))
		.def("get_scan_stats", &receiver_t::get_scan_stats, "Return true if a statistics of current scan, in progress",
				 py::arg("subscription_id"))
		.def(
			"get_spectrum_path",
			[](receiver_t& receiver) { return std::string(receiver.options.readAccess()->spectrum_path.c_str()); },
			"Return location where spectra are stored")
#if 0
		.def("start_recording", py::overload_cast<const chdb::service_t&, time_t, int>(&receiver_t::start_recording),
				 "Start recording a service at a specific time for a specific duration (minutes)."
				 , py::arg("service")
				 , py::arg("start_time")
				 , py::arg("duration") = 120
			)
		.def("stop_recording", py::overload_cast<const chdb::service_t&, time_t>(&receiver_t::stop_recording),
				 "Stop all recordings which contain time t on service service."
				 , py::arg("service")
				 , py::arg("t")
			)
#endif

		.def("stop", &receiver_t::stop, "Cleanup before exit")
		.def_readonly("browse_history", &receiver_t::browse_history, py::return_value_policy::reference_internal)
		.def_readonly("rec_browse_history", &receiver_t::rec_browse_history, py::return_value_policy::reference_internal)
		.def_property_readonly("error_message", [](receiver_t* self) { return get_error().c_str(); })
		.def_property("options", &receiver_t::get_options, &receiver_t::set_options)
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
	export_logger(m);
	export_live_history(m);
	export_recording_history(m);
	export_scan_stats(m);
	export_options(m);
	m.attr("__version__") = version_info();

}
