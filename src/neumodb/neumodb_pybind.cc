/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <stdio.h>
namespace nb = nanobind;
void export_find_type(nb::module_& m) {
	static int called = false;
	if (called)
		return;
	called = true;
	nb::enum_<find_type_t>(m, "find_type_t", nb::is_arithmetic())
		.value("find_eq", find_type_t::find_eq)
		.value("find_geq", find_type_t::find_geq)
		.value("find_leq", find_type_t::find_leq)
		;
}

void export_field_matcher_t(nb::module_& m) {
	static int called = false;
	if (called)
		return;
	called = true;

	auto mm = m.def_submodule("field_matcher");
	typedef field_matcher_t::match_type_t m_t;

	nb::enum_<m_t>(mm, "match_type", nb::is_arithmetic())
		.value("EQ", m_t::EQ)
		.value("GEQ", m_t::GEQ)
		.value("LEQ", m_t::LEQ)
		.value("GT", m_t::GT)
		.value("LT", m_t::LT)
		.value("STARTSWITH", m_t::STARTSWITH)
		.value("CONTAINS", m_t::CONTAINS)
		;

	nb::class_<field_matcher_t>(mm, "field_matcher")
		.def(nb::init<int8_t, field_matcher_t::match_type_t>())
		.def("__repr__",
				 [](field_matcher_t matcher) {
					 return std::string(fmt::format("{}", matcher));
				 })
		.def_rw("field_id", &field_matcher_t::field_id)
		.def_rw("match_type", &field_matcher_t::match_type)
		;
}

void export_milli_seconds_t(nb::module_& m) {
	static int called = false;
	if (called)
		return;
	called = true;

	nb::class_<milliseconds_t>(m, "milli_seconds")
		.def(nb::init<int64_t>())
		.def("__repr__",
				 [](milliseconds_t s) {
					 return fmt::format("{}", s);
				 })
		.def("__int__", [](milliseconds_t s) { return s.ms; })
		;
}

EXPORT void export_neumodb(nb::module_& m) {
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
	nb::class_<db_txn>(m, "db_txn")
		.def("commit", &db_txn::commit, "Commit transaction")
		.def(
			"abort", [](db_txn& self) { self.abort(); }, "Abort transaction")
		.def(
			"child_txn", [](db_txn& self, neumodb_t& db) { return self.child_txn(db); }, "child transaction")
		;
	nb::class_<neumodb_t>(m, "neumodb")
		.def("open", &neumodb_t::open, "Open database file", nb::arg("dbpath"), nb::arg("allow_degraded_mode") = false,
				 nb::arg("table_name") = nullptr, nb::arg("use_log") = true, nb::arg("mapsize") = 128 * 1024u * 1024u)
		.def("open_secondary", &neumodb_t::open_secondary, "Open a second table in an already open datase",
				 nb::arg("table_name"), nb::arg("allow_degraded_mode") = false)
		.def("wtxn", &neumodb_t::wtxn, nb::keep_alive<0, 1>())
		.def("rtxn", &neumodb_t::rtxn, nb::keep_alive<0, 1>())
		.def_ro("db_version", &neumodb_t::db_version)
		.def("stats", &stats_db)
		;
}
