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
namespace nb = nanobind;

#include "neumodb/{{dbname}}/{{dbname}}_db.h"

using namespace {{dbname}};

namespace {{dbname}} {
//forward declarations and data type helpers
	void export_enums(nb::module_& m) {

	{%for enum in enums %}
		nb::enum_<{{enum.name}}>(m, "{{enum.name}}", nb::is_arithmetic())
    {%for f in enum.values %}
		     .value("{{f.short_name}}", {{enum.name}}::{{f.name}})
    {% endfor %}
		;

		{%endfor%}

	}
} //end namespace {{dbname}}
