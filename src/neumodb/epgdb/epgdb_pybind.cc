/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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

#include "neumodb/epgdb/epgdb_db.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdio.h>

namespace py = pybind11;

extern void export_neumodb(py::module& m);
extern void export_epgdb(py::module& m);

namespace epgdb {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
} // namespace epgdb


void export_gridepg(py::module& m) {
	using namespace epgdb;
	py::class_<gridepg_screen_t>(m, "gridepg_screen")
		.def(py::init<time_t,
#ifdef USE_END_TIME
				 time_t,
#endif
				 int, uint32_t>(),
				 py::arg("start_time"),
#ifdef USE_END_TIME
				 py::arg("end_time"),
#endif
				 py::arg("num_services"), py::arg("epg_sort_order"))
		.def("add_service", &gridepg_screen_t::add_service, "add epg data for extra service to the screen",
				 py::arg("txnepg"), py::arg("service_key"), py::return_value_policy::reference_internal)
		.def("remove_service", &gridepg_screen_t::remove_service, "remove epg data for a service", py::arg("service_key"))
		.def("epg_screen_for_service", &gridepg_screen_t::epg_screen_for_service, "return epg screen for a service",
				 py::arg("service_key"), py::return_value_policy::reference_internal)
		;
}

void export_extra(py::module& m) {
	m.def("clean", &epgdb::clean, "remove old epgdb records", py::arg("txn"), py::arg("start_time"))
		.def("chepg_screen", &epgdb::chepg_screen, "channel epg sceen", py::arg("txnepg"), py::arg("service_key"),
				 py::arg("start_time"),
#ifdef USE_END_TIME
				 py::arg("end_time"),
#endif
				 py::arg("sort_order"), py::arg("tmpdb").none(true) = nullptr)
		.def("running_now", py::overload_cast<db_txn&, const chdb::service_key_t&, time_t>(&epgdb::running_now),
				 "Get currently running program on service", py::arg("txnepg"), py::arg("service_key"), py::arg("now"))
		;
}

PYBIND11_MODULE(pyepgdb, m) {
	m.doc() = R"pbdoc(
        Pybind11 channel database
        -----------------------

        .. currentmodule:: pyepgdb

        .. autosummary::
           :toctree: _generate

    )pbdoc";

	using namespace epgdb;
	export_neumodb(m);
	export_epgdb(m);
	export_extra(m);
	export_gridepg(m);
	export_ss_vector(m, epg_record_t);
	epgdb::export_enums(m);
	epgdb::export_structs(m);

	typedef screen_t<epgdb::epg_record_t> s_t;
	py::class_<epgdb::epg_screen_t, s_t>(m, "epg_screen").def("update_between", &epgdb::epg_screen_t::update_between)
		;

	m.attr("__version__") = version_info();
}
