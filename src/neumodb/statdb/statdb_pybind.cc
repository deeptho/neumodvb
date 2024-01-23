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
#include "neumodb/statdb/statdb_extra.h"
#include "neumodb/chdb/chdb_extra.h"
#include "util/identification.h"
#include "stackstring/stackstring_pybind.h"
#include "statdb_vector_pybind.h"
#include <pybind11/pybind11.h>
#include <stdio.h>

namespace py = pybind11;

extern void export_neumodb(py::module& m);
extern void export_statdb(py::module& m);

namespace statdb {
	extern void export_enums(py::module &m);
	extern void export_structs(py::module &m);
}

static void export_statdb_extra(py::module& m) {
	auto mm = m.def_submodule("signal_stat");
	mm.def("get_by_mux_fuzzy", &statdb::signal_stat::get_by_mux_fuzzy,
				"Retrieve signal_stat data for a specific sat, pol and freq",
				 py::arg("devdb_rtxn"), py::arg("sat_pos"), py::arg("pol"),
				 py::arg("frequency"), py::arg("start_time")=0, py::arg("tolerance")=500)
		;
}

PYBIND11_MODULE(pystatdb, m) {
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
