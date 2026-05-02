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
#include "neumodb/statdb/statdb_extra.h"
#include "neumodb/chdb/chdb_extra.h"
#include "util/identification.h"
#include "stackstring/stackstring_pybind.h"
#include "statdb_vector_pybind.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <stdio.h>

namespace nb = nanobind;

extern void export_neumodb(nb::module_& m);
extern void export_statdb(nb::module_& m);

namespace statdb {
	extern void export_enums(nb::module_ &m);
	extern void export_structs(nb::module_ &m);
}

static void export_statdb_extra(nb::module_& m) {
	auto mm = m.def_submodule("signal_stat");
	mm.def("get_by_mux_fuzzy", &statdb::signal_stat::get_by_mux_fuzzy,
				"Retrieve signal_stat data for a specific sat, pol and freq",
				 nb::arg("devdb_rtxn"), nb::arg("sat_pos"), nb::arg("pol"),
				 nb::arg("frequency"), nb::arg("start_time")=0, nb::arg("tolerance")=500)
		;
}

NB_MODULE(pystatdb, m) {
	m.doc() = R"pbdoc(
        Pybind11 stat database
        -----------------------

        .. currentmodule:: pystatdb

        .. autosummary::
           :toctree: _generate

    )pbdoc";

	using namespace statdb;
	export_neumodb(m);
	export_statdb(m);
	export_statdb_extra(m);
	export_statdb_vectors(m);
	export_ss_vector(m, signal_stat_entry_t);
	statdb::export_enums(m);
	statdb::export_structs(m);

	m.attr("__version__") = version_info();
}
