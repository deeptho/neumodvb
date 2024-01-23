/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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

#include "neumodb/cursors.h"
#include "neumodb/neumodb.h"
#include "receiver/streamparser/streamparser.h"
#include "stackstring/stackstring.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include "neumotime.h"
#include <pybind11/pybind11.h>
#include <stdio.h>
namespace py = pybind11;
void export_find_type(py::module& m) {
	static int called = false;
	if (called)
		return;
	called = true;
	py::enum_<find_type_t>(m, "find_type_t", py::arithmetic())
		.value("find_eq", find_type_t::find_eq)
		.value("find_geq", find_type_t::find_geq)
		.value("find_leq", find_type_t::find_leq)
		;
}

void export_field_matcher_t(py::module& m) {
	static int called = false;
	if (called)
		return;
	called = true;

	auto mm = m.def_submodule("field_matcher");
	typedef field_matcher_t::match_type_t m_t;

	py::enum_<m_t>(mm, "match_type", py::arithmetic())
		.value("EQ", m_t::EQ)
		.value("GEQ", m_t::GEQ)
		.value("LEQ", m_t::LEQ)
		.value("GT", m_t::GT)
		.value("LT", m_t::LT)
		.value("STARTSWITH", m_t::STARTSWITH)
		.value("CONTAINS", m_t::CONTAINS)
		;

	py::class_<field_matcher_t>(mm, "field_matcher")
		.def(py::init<int8_t, field_matcher_t::match_type_t>())
		.def("__repr__",
				 [](field_matcher_t matcher) {
					 return std::string(fmt::format("{}", matcher));
				 })
		.def_readwrite("field_id", &field_matcher_t::field_id)
		.def_readwrite("match_type", &field_matcher_t::match_type)
		;
}

void export_milli_seconds_t(py::module& m) {
	static int called = false;
	if (called)
		return;
	called = true;

	py::class_<milliseconds_t>(m, "milli_seconds")
		.def(py::init<int64_t>())
		.def("__repr__",
				 [](milliseconds_t s) {
					 return fmt::format("{}", s);
				 })
		.def("__int__", [](milliseconds_t s) { return s.ms; })
		;
}

EXPORT void export_neumodb(py::module& m) {
	static bool called = false;
	m.attr("neumo_schema_version") = neumo_schema_version; //needs to be before the if(called)
	if (called)
		return;
	called = true;
	export_find_type(m);
	export_field_matcher_t(m);
	export_ss_vector(m, field_matcher_t);

	export_ss_vector(m, int32_t);
	export_ss_vector(m, uint32_t);
	export_ss_vector(m, uint16_t);
	export_ss_vector(m, int16_t);
	export_ss_vector(m, int8_t);
	export_ss_vector(m, uint8_t);
	export_ss_vector(m, int64_t);

	export_milli_seconds_t(m);
	py::class_<db_txn>(m, "db_txn")
		.def("commit", &db_txn::commit, "Commit transaction")
		.def(
			"abort", [](db_txn& self) { self.abort(); }, "Abort transaction")
		.def(
			"child_txn", [](db_txn& self, neumodb_t& db) { return self.child_txn(db); }, "child transaction")
		;
	py::class_<neumodb_t>(m, "neumodb")
		.def("open", &neumodb_t::open, "Open database file", py::arg("dbpath"), py::arg("allow_degraded_mode") = false,
				 py::arg("table_name") = nullptr, py::arg("use_log") = true, py::arg("mapsize") = 128 * 1024u * 1024u)
		.def("open_secondary", &neumodb_t::open_secondary, "Open a second table in an already open datase",
				 py::arg("table_name"), py::arg("allow_degraded_mode") = false)
		// py::keep_alive<0,1>() => ensure that the result of wtxn and rtxn os destroyed before teh database
		.def("wtxn", &neumodb_t::wtxn, py::keep_alive<0, 1>())
		.def("rtxn", &neumodb_t::rtxn, py::keep_alive<0, 1>())
		.def_readonly("db_version", &neumodb_t::db_version)
		.def("stats", &stats_db)
		;
}
