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
#include <pybind11/pybind11.h>
namespace py = pybind11;

#include "neumodb/{{dbname}}/{{dbname}}_db.h"

using namespace {{dbname}};

namespace {{dbname}} {
//forward declarations and data type helpers
	void export_enums(py::module& m) {

	{%for enum in enums %}
		py::enum_<{{enum.name}}>(m, "{{enum.name}}", py::arithmetic())
    {%for f in enum.values %}
		     .value("{{f.short_name}}", {{enum.name}}::{{f.name}})
    {% endfor %}
		;

		{%endfor%}

	}
} //end namespace {{dbname}}
