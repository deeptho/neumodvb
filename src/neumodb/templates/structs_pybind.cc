/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include <pybind11/stl.h>
namespace py = pybind11;

#include "neumodb/{{dbname}}/{{dbname}}_db.h"
#include "neumodb/{{dbname}}/{{dbname}}_extra.h"


namespace {{dbname}} {
//forward declarations and data type helpers
	void export_{{struct.name}}(py::module& mm) {
		using namespace {{dbname}};
    {% if struct.is_table %}
    mm.def("subfield_from_name",
				 static_cast<uint32_t(*)(const char*)>(&{{struct.class_name}}::subfield_from_name),
				 py::arg("subfield index for a column name in dot format"))
		.def("key_for_sort_order",
				 static_cast<{{struct.class_name}}::keys_t (*)(const dynamic_key_t&)>(&{{struct.class_name}}::key_for_sort_order), py::arg("sort_order"));
		{% endif %}
		py::class_<{{struct.class_name}}>(mm, "{{struct.name}}")
    .def(py::init<>())//default constructor
		//full constructor
    .def(py::init<{%for f in struct.fields %}
				{{f['type'] }} {%-if not loop.last  %}, {%-endif%}
    {% endfor %}>(),
			{%for f in struct.fields %}
			py::arg("{{f.name }}") {%-if not loop.last  %}, {%-endif%}
    {% endfor %}
			)
		.def("copy", [](const {{struct.class_name}}& self){ return self;})
    .def("__repr__", [](const {{struct.class_name}}& self){ auto x = to_str(self); return std::string(x.c_str());})
    {%for f in struct.fields %}
		{%if f.is_string %}
		.def_property("{{f.name}}",
									[](const {{struct.class_name}}&x) {return x.{{f.name}}.c_str();},
									[]({{struct.class_name}}&x, const std::string val) {x.{{f.name}}= val.c_str();}
			)
		{%elif f.is_vector_of_strings %}
		.def_property("{{f.name}}",
									[](const {{struct.class_name}}&x) {
										std::vector<std::string> out;
										out.reserve(x.{{f.name}}.size());
										for(int i=0; i<  (int) x.{{f.name}}.size(); ++i)
											out.push_back(x.{{f.name}}[i]);
										return out;},
									[]( {{struct.class_name}}&x, const std::vector<std::string> val)
										{/* Todo: this does not allow setting individual elements
												e.g. q.zorro[3]='adsds' will not work, as it sets the copy
												instead of the result
										 */
											x.{{f.name}}.clear();
											for(const auto& v: val)
												x.{{f.name}}.push_back(v.c_str());
										})
		{%elif f.is_vector %}
		.def_property("{{f.name}}",
									[](const {{struct.class_name}}&x) {
										return &(ss::vector_<{{f.scalar_type}}>&) x.{{f.name}};
									},
									[]( {{struct.class_name}}&x, const ss::vector_<{{f.scalar_type}}> val)
										{ x.{{f.name}} = val;})
			              //{ x.{{f.name}} = {{f.type}}(&val[0], val.size()); })

		{%else%}
		.def_readwrite("{{f.name}}", &{{struct.class_name}}::{{f.name}})
		{%endif%}
    {% endfor %}
		;
	}
} //end namespace {{dbname}}


{% if struct.is_table %}
namespace {{dbname}} {
	void export_{{struct.name}}_screen(py::module& mm) {
		typedef screen_t<{{dbname}}::{{struct.class_name}}> s_t;
		py::class_<s_t>(mm, "screen")
			.def(py::init<db_txn&, uint32_t, {{struct.class_name}}::partial_keys_t,
					 const {{struct.class_name}}*, const {{struct.class_name}}*,
					 const ss::vector_<field_matcher_t>*,
					 const {{struct.class_name}}*>(),
					 py::arg("db_txn"),
					 py::arg("sort_order"),
					 py::arg("key_prefix_type")={{struct.class_name}}::partial_keys_t::none,
					 py::arg("key_prefix_data") = nullptr,
					 py::arg("lower_limit") = nullptr,
					 py::arg("field_matchers") = nullptr,
					 py::arg("match_data") = nullptr
				)
#ifdef NEWXXX
			.def(py::init<db_txn&, uint32_t, const ss::vector_<field_matcher_t>*,
					 const {{struct.class_name}}*>(),
					 py::arg("db_txn"),
					 py::arg("sort_order"),
					 py::arg("field_matchers") = nullptr,
					 py::arg("match_data") = nullptr)
#endif
			.def("update", &s_t::update)
			.def("record_at_row", &s_t::record_at_row, py::arg("row_number"))
			.def("set_reference", py::overload_cast<const {{struct.class_name}}&>(&s_t::set_reference), py::arg("record"))
			.def_readonly("pos_top", &s_t::pos_top)
			.def_property_readonly("list_size", &s_t::list_size)
			.def_readonly("sort_order", &s_t::sort_order)
			.def_property("field_matchers",
          [](const s_t &x) {
            return &(ss::vector_<field_matcher_t> &)x.field_matchers;
          },
          [](s_t &x, const ss::vector_<field_matcher_t>&val) {
						x.field_matchers=val;
					}
				)
			.def_readwrite("match_data", &s_t::match_data)
    ;
	}
} //end namespace {{dbname}}
{% endif %}

{% if struct.is_table %}
namespace {{dbname}} {
	void export_{{struct.name}}_lists(py::module& mm) {
{%for key in struct.keys%}
{%if key.full %}
{% if key.primary %}
  /**
   * List all by given PRIMARY key.
   */
{% else %}
  /**
   * List all by given SECONDARY key.
   */
{% endif %}
mm.def("list_all_by_{{key.index_name}}",
			 py::overload_cast<db_txn&>(
				 &{{struct.name}}::list_all_by_{{key.index_name}}),
			"list of all {{struct.name}} records, ordered by {{key.index_name}} index",
			py::arg("db_txn"));

mm.def("list_all",
			 py::overload_cast<db_txn&, uint32_t, bool>(
				 &{{struct.name}}::list_all),
			"list of all {{struct.name}} records, ordered by columns specified in order (uint32_t, each byte is one column)",
			 py::arg("db_txn"), py::arg("order"), py::arg("use_index")=false);
mm.def("index_in_vector",
			 &{{struct.name}}::index_in_vector,
			"in a vector haystack of {{struct.name}} records, find the location of needle, by comparing primary key",
			 py::arg("haystack"), py::arg("needle"));

{% endif %}
	{% endfor  %}



/////////////////


	{% if struct.is_table %}
	py::enum_<{{dbname}}::{{struct.class_name}}::partial_keys_t>(mm, "{{struct.name}}_prefix", py::arithmetic())
																					 .value("none", {{dbname}}::{{struct.class_name}}::partial_keys_t::none)
		 {% for prefix_name in struct.key_prefixes %}
	.value("{{prefix_name}}", {{dbname}}::{{struct.class_name}}::partial_keys_t::{{prefix_name}})
			{% endfor %}
	.value("all", {{dbname}}::{{struct.class_name}}::partial_keys_t::all)
		;
	{%endif%}

	{% if struct.is_table %}
	py::enum_<{{dbname}}::{{struct.class_name}}::subfield_t>(mm, "column", py::arithmetic())
	.value("none", {{dbname}}::{{struct.class_name}}::subfield_t::none)
	{% for field in struct.subfields %}
	.value("{{field.name.replace('.','_')}}",
				 {{dbname}}::{{struct.class_name}}::subfield_t::{{field.name.replace('.','_')}})
	 {% endfor %}
		;
	{% endif %}

	{% if struct.is_table %}
	py::enum_<{{dbname}}::{{struct.class_name}}::keys_t>(mm, "{{struct.name}}_order", py::arithmetic())
			{% for key in struct.keys %}
	.value("{{key.index_name}}", {{dbname}}::{{struct.class_name}}::keys_t::{{key.index_name}})
    		{% endfor %}
		;
	{% endif %}

	{% if struct.filter_fields|length %}
	py::enum_<{{dbname}}::{{struct.class_name}}::filter_fields_t>(mm, "{{struct.name}}_filter", py::arithmetic())
																					 .value("none", {{dbname}}::{{struct.class_name}}::filter_fields_t::none)
			{% for field in struct.filter_fields %}
	.value("{{field.name}}", {{dbname}}::{{struct.class_name}}::filter_fields_t::{{field.name}})
			{% endfor %}
	;
  {% endif %}



/////////////////
	{%if false and struct.is_table %}
	mm.def("list",
				py::overload_cast<db_txn&,
				 {{struct.class_name}}::keys_t,
				 {{struct.class_name}}::partial_keys_t,
				 {{struct.class_name}}*,
				 uint64_t, {{struct.class_name}}*, int, int>
				 (&{{struct.name}}::list),
				"lists records in the dabase. "
				 "The records are returned in a selected order, which must match"
				 "one of the indexes in the database:"
				 "   {% for master, variants in struct.keys_by_master.items() -%}{% for key in variants -%}  {{key.index_name}}, {%- endfor %} {%- endfor %}"
				 "Records are returned only if they are in the database range defined by key_prefix:"
				 "   {% for prefix in struct.key_prefixes -%} {{prefix}}, {%- endfor %}"
				 "and if they match with the reference record ref_fields if one is provided in fields indicated "
				 "by	field_filter, which is a logical or of:"
				 "   {% for f in struct.filter_fields -%} {{f.name}},  {%- endfor %}"
				 "At most num_records records are returned if num_records>0."
				 "The first returned one is ref if ref!=nullptr, else it is the first record in the database range"
				 "If offset!=0 then the returned records are those with index offset, offset+1.... where offset==0"
				 "corresponds to ref if ref!=nullptr, else to the start of the range",
				 py::arg("db_txn"),
				 py::arg("order"),
				 py::arg("key_prefix") = {{struct.class_name}}::partial_keys_t::none,
				 py::arg("ref") = nullptr,
				 py::arg("field_filter") = (uint64_t)0,
				 py::arg("ref_filter") = nullptr,
				 py::arg("num_records") = (int)-1,
				 py::arg("offset") = (int)0
		);

	{% endif %}


	} //end export_{{struct.name}}_lists
} //end namespace {{dbname}}
{% endif %}

{% if struct.is_table %}
namespace {{dbname}} {
	void export_{{struct.name}}_find(py::module& mm) {
		{% for key in struct.keys %}
		{% for prefix in key.key_prefixes %}
		{% if not  prefix.duplicate %}
		{%set num_fields = loop.index %}
		{
			auto fn = [](db_txn& txn,
				{%- for field in prefix.fields %}const {{field.namespace}}{{field.scalar_type}}& {{field.short_name}},
				{%- endfor -%}
				find_type_t find_type, {{struct.class_name}}::partial_keys_t key_prefix) {
									auto c = {{struct.class_name}}::find_by_{{key.index_name}}(txn,
										{%- for field in prefix.fields %}{{field.short_name}},
										{%- endfor -%}
										find_type, key_prefix);
						        return record_at_cursor<{{struct.class_name}}, decltype(c)>(c);
								};
			mm.def("find_by_{{key.index_name}}", fn,
						 "Find a {{struct.name}} using an index"
						 , py::arg("db_txn")
						 {%- for field in prefix.fields %}
						 , py::arg("{{field.short_name}}")
						 {%endfor -%}
						 , py::arg("find_type") = find_type_t::find_eq
						 , py::arg("key_prefix") =  {{struct.class_name}}::partial_keys_t::none
				);
	  }
{% endif %}
{% endfor %}
{% endfor %}
}
}; //end namespace  {{dbname}}
{% endif %}
