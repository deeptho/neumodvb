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
#include "neumodb/schema/schema_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace schema;

extern void export_neumodb(py::module& m);

namespace schema {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
}; // end namespace  schema

static void export_schema(py::module& m) {
	py::class_<schema::schema_t, neumodb_t>(m, "schema").def(py::init<>())
		;
}

PYBIND11_MODULE(pyschemadb, m) {
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
