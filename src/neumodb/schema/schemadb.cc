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
#include "neumodb/schema/schema_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace nb = nanobind;
using namespace schema;

extern void export_neumodb(nb::module_& m);

namespace schema {
	extern void export_enums(nb::module_& m);
	extern void export_structs(nb::module_& m);
}; // end namespace  schema

static void export_schema(nb::module_& m) {
	nb::class_<schema::schema_t, neumodb_t>(m, "schema").def(nb::init<>())
		;
}

NB_MODULE(pyschemadb, m) {
	m.doc() = R"pbdoc(
        Pybind11 channel database
        -----------------------

        .. currentmodule:: pyschemadb

        .. autosummary::
           :toctree: _generate

    )pbdoc";

	using namespace schema;
	export_schema(m);

	m.attr("__version__") = version_info();
}
