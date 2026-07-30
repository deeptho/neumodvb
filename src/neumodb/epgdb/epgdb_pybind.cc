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
#include <nanobind/stl/optional.h>
#include "neumodb/epgdb/epgdb_db.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <stdio.h>

namespace nb = nanobind;

extern void export_neumodb(nb::module_& m);
extern void export_epgdb(nb::module_& m);

namespace epgdb {
	extern void export_enums(nb::module_& m);
	extern void export_structs(nb::module_& m);
} // namespace epgdb


void export_gridepg(nb::module_& m) {
	using namespace epgdb;
	nb::class_<gridepg_screen_t>(m, "gridepg_screen")
		.def(nb::init<time_t,
#ifdef USE_END_TIME
				 time_t,
#endif
				 int, uint32_t>(),
				 nb::arg("start_time"),
#ifdef USE_END_TIME
				 nb::arg("end_time"),
#endif
				 nb::arg("num_services"), nb::arg("epg_sort_order"))
		.def("add_service", &gridepg_screen_t::add_service, "add epg data for extra service to the screen",
				 nb::arg("txnepg"), nb::arg("service_key"), nb::rv_policy::reference_internal)
		.def("remove_all", &gridepg_screen_t::remove_all, "remove all epg data")
		.def("epg_screen_for_service", &gridepg_screen_t::epg_screen_for_service, "return epg screen for a service",
				 nb::arg("service_key"), nb::rv_policy::reference_internal)
		;
}

static std::unique_ptr<epgdb::epg_screen_t> chepg_screen(db_txn& txnepg,
																												 uint32_t sort_order,
																												 const chdb::service_key_t& service_key,
																												 time_t start_time,
#ifdef USE_END_TIME
																												 time_t end_time,
#endif
																												 const ss::vector_<field_matcher_t>* field_matchers_,
																												 const epgdb::epg_record_t* match_data_,
																												 const ss::vector_<field_matcher_t>* field_matchers2_,
																												 const epgdb::epg_record_t* match_data2_
	) {
	return epgdb::chepg_screen(txnepg, {}, sort_order,
														 service_key,
														 start_time,
#ifdef USE_END_TIME
														 time_end_time,
#endif
														 field_matchers_,
														 match_data_,
														 field_matchers2_,
														 match_data2_
		);
}

void export_extra(nb::module_& m) {
	m.def("clean", &epgdb::clean, "remove old epgdb records", nb::arg("txn"), nb::arg("start_time"))
		.def("chepg_screen", &chepg_screen, "channel epg sceen",
				 nb::arg("txnepg"),
				 nb::arg("sort_order"),
				 nb::arg("service_key"),
				 nb::arg("start_time"),
#ifdef USE_END_TIME
				 nb::arg("end_time"),
#endif
				 nb::arg("field_matchers") = nullptr, nb::arg("match_data") = nullptr,
				 nb::arg("field_matchers2") = nullptr, nb::arg("match_data2") = nullptr
			)
		.def("running_now", nb::overload_cast<db_txn&, const chdb::service_key_t&, time_t>(&epgdb::running_now),
				 "Get currently running program on service"
				 , nb::arg("txnepg")
				 , nb::arg("service_key")
				 , nb::arg("now")=-1)
		;
}

NB_MODULE(pyepgdb, m) {
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
	nb::class_<epgdb::epg_screen_t, s_t>(m, "epg_screen").def("update_between", &epgdb::epg_screen_t::update_between)
		;

	m.attr("__version__") = version_info();
}
