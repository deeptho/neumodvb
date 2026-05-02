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

//needed because nb::overload_cast does not seem to work
template <typename record_t> inline void xxx_put_record(db_txn& txn, const record_t& record) {
	put_record(txn, record, 0);
}

//needed because nb::overload_cast does not seem to work
	template <typename record_t> inline void xxx_delete_record
	(db_txn& txn, const record_t& record) {
		delete_record(txn, record);
}

void export_{{dbname}}(nb::module_& m) {
	nb::class_<{{dbname}}::{{dbname}}_t, neumodb_t>(m, "{{dbname}}")
																						//.def(nb::init<>())
     .def(nb::init<const neumodb_t&>(),
				 nb::arg("maindb")
			)
		.def(nb::init<bool, bool, bool, bool>(),
				 nb::arg("readonly") = false,
				 nb::arg("is_temp") = false,
				 nb::arg("autoconvert") = true,
				 nb::arg("autoconvert_major_version") = false)
		;
}


namespace {{dbname}} {
	{%for struct in structs%}
	extern void export_{{struct.name}}(nb::module_& m);
	extern void export_{{struct.name}}_lists(nb::module_& m);
	  {% if struct.is_table %}
		extern void export_{{struct.name}}_screen(nb::module_& m);
		extern void export_{{struct.name}}_find(nb::module_& m);
		{% endif %}
	{%endfor%}

	void export_structs(nb::module_& m) {
		{%for struct in structs%}
		{
			{% if struct.is_table %}
 			m.def("put_record",
						nb::overload_cast<db_txn&, const {{struct.class_name}}&>(xxx_put_record<{{struct.class_name}}>),
		  	    "Save record in db", nb::arg("txn"), nb::arg("{{struct.name}}"))
				.def("delete_record",
						 nb::overload_cast<db_txn&, const {{struct.class_name}}&>(xxx_delete_record<{{struct.class_name}}>),
		  	    "Delete record from db", nb::arg("txn"), nb::arg("{{struct.name}}"))
			;
			{% endif %}
			auto mm = m.def_submodule("{{struct.name}}");

			export_{{struct.name}}(mm);
			{% if struct.is_table %}
			export_{{struct.name}}_lists(mm);
			export_{{struct.name}}_screen(mm);
			export_{{struct.name}}_find(mm);
			{% endif %}
		}
		{%endfor%}
	}

} //end namespace {{dbname}}
