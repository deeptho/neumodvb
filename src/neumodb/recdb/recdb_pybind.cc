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
#include "neumodb/recdb/recdb_db.h"
#include "neumodb/recdb/recdb_extra.h"
#include "stackstring/stackstring.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <stdio.h>

namespace nb = nanobind;

extern void export_neumodb(nb::module_& m);
extern void export_recdb(nb::module_& m);

namespace recdb {
	extern void export_enums(nb::module_& m);
	extern void export_enums(nb::module_& m);
	extern void export_structs(nb::module_& m);
} // namespace recdb

NB_MODULE(pyrecdb, m) {
	m.doc() = R"pbdoc(
        Pybind11 channel database
        -----------------------

        .. currentmodule:: pyrecdb

        .. autosummary::
           :toctree: _generate

    )pbdoc";
	// m.def("make_filename",  &recdb::rec::make_filename);
	m.def("make_filename", [](const chdb::service_t& s, const epgdb::epg_record_t& e) {
		ss::string<512> ret;
		recdb::rec::make_filename(ret, s, e);
		return std::string(ret.c_str());
	});

	using namespace recdb;
	export_neumodb(m);
	export_recdb(m);
	export_ss_vector(m, rec_fragment_t);
	export_ss_vector(m, marker_t);
	export_ss_vector(m, pmt_marker_t);
	export_ss_vector(m, rec_t);
	export_ss_vector(m, file_t);
	export_ss_vector(m, live_service_t);
	export_enums(m);
	export_structs(m);

	m.attr("__version__") = version_info();
}
