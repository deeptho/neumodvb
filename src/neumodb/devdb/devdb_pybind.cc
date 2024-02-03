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
#include "devdb_vector_pybind.h"
#include "neumodb/devdb/devdb_extra.h"
#include "devdb_private.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace devdb;

extern void export_neumodb(py::module& m);
extern void export_devdb(py::module& m);

namespace devdb {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
} // namespace devdb

static inline devdb::lnb_connection_t* conn_helper
(devdb::lnb_t& lnb, devdb::rf_path_t& rf_path) {
	return connection_for_rf_path(lnb, rf_path);
}

static void export_dish_extra(py::module& m) {
	//auto mm = py::reinterpret_borrow<py::module>(m.attr("dish"));
	auto mm = m.def_submodule("dish");
	using namespace devdb;
	mm.def("list_dishes", &dish::list_dishes,
				 "Returns a list of known dish ids",
				 py::arg("devdb_rtxn"))
		;
}

static void export_lnb_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("lnb"));
	using namespace devdb;
	mm.def("update_lnb_network_from_positioner", &lnb::update_lnb_network_from_positioner,
				 "save changed lnb network"
				 , py::arg("wtxn"), py::arg("lnb"), py::arg("current_sat_pos"))
		.def("update_lnb_connection_from_positioner", &lnb::update_lnb_connection_from_positioner,
				 "save changed lnb connection"
				 , py::arg("wtxn"), py::arg("lnb"), py::arg("connection"))
		.def("update_lnb_from_lnblist", &lnb::update_lnb_from_lnblist, "save changed lnb",
				 py::arg("wtxn"), py::arg("lnb"), py::arg("save")=true)
		.def("can_move_dish", &lnb::can_move_dish,
				 "Returns true if this lnb connection can move the dish",
				 py::arg("lnb_connection"))
		.def("reset_lof_offset", &lnb::reset_lof_offset,
				 "reset the LOF offset to 0",
				 py::arg("devdb_wtxn"),
				 py::arg("lnb"))
		.def("make_unique_if_template", lnb::make_unique_if_template,
				 "Make the key of this lnb unique, but only if lnb.k.id<0")
		.def("select_lnb", &lnb::select_lnb, "Select an lnb; which can tune to sat or mux",
				 py::arg("devdb_rtxn"), py::arg("sat").none(true) = nullptr, py::arg("mux").none(true) = nullptr)
		.def("select_rf_path", &lnb::select_rf_path, "Select an rf_path for lnb", py::arg("lnb"),
				 py::arg("sat_pos")=sat_pos_none)
		.def("connection_for_rf_path", &conn_helper
				 , py::return_value_policy::reference_internal
				 , "Return lnb_connection for rf_path"
				 , py::arg("lnb")
				 , py::arg("rf_path"))
		.def("rf_path_for_connection", &devdb::rf_path_for_connection,
				 "Return rf_path for lnb_connection"
				 , py::arg("lnb_key")
				 , py::arg("lnb_connection"))
		.def("add_or_edit_network", &lnb::add_or_edit_network,
				 "Add a network to an lnb if it does not yet exist or edit it; returns true if network was added "
				 "or changed", py::arg("lnb"), py::arg("usals_location"), py::arg("lnb_network"))
		.def("add_or_edit_connection", &lnb::add_or_edit_connection,
				 "Add a connection to an lnb if it does not yet exist or edit it; returns true if connection was added "
				 "or changed",
				 py::arg("rtxn"), py::arg("lnb"), py::arg("lnb_connection"))
		.def("lnb_frequency_range", &lnb::lnb_frequency_range,
				 "Obtain min/mid/max frequency for this lnb",  py::arg("lnb"))
		.def("sat_band", &lnb::sat_band,
				 "Obtain sat_band for this lnb",  py::arg("lnb"))
		;
}

void export_subscribe_options(py::module& m) {
	py::class_<subscription_options_t, tune_options_t>(m, "subscription_options_t")
		.def(py::init<>( []() { subscription_options_t ret; ret.scan_target = scan_target_t::SCAN_FULL; return ret;}),
				 "Tune Options for neumodvb")
		.def_readwrite("spectrum_scan_options", &subscription_options_t::spectrum_scan_options)
		//.def_readwrite("subscription_type", &subscription_options_t::subscription_type)
		;

}

static void export_scan_command_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("scan_command"));
	using namespace devdb;
	mm.def("make_unique_if_template", scan_command::make_unique_if_template,
				 "Make the key of this scan_command unique, but only if id<0")
		;
}

static void export_stream_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("stream"));
	using namespace devdb;
	mm.def("make_unique_if_template", stream::make_unique_if_template,
				 "Make the key of this stream_t unique, but only if id<0")
		;
}

static std::tuple<bool, std::optional<std::string>>
lnb_can_tune_to_mux_helper(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool disregard_networks) {
	ss::string<128> error;
	bool ret= devdb::lnb_can_tune_to_mux(lnb, mux, disregard_networks, &error);
	if (ret)
		return {ret, {}};
	return {ret, error};
}

PYBIND11_MODULE(pydevdb, m) {
	m.doc() = R"pbdoc(
        Pybind11 device/frontend database
        -----------------------

        .. currentmodule:: pydevdb

        .. autosummary::
           :toctree: _generate

    )pbdoc";

	using namespace chdb;
	export_ss_vector(m, subscription_data_t);

	m.def("lnb_can_tune_to_mux", &lnb_can_tune_to_mux_helper,
				 "check if lnb can tune to mux; returns true/false and optional error string", py::arg("lnb"), py::arg("mux"),
				 py::arg("disregard_networks") = false)
		;
	export_neumodb(m);
	export_devdb(m);
	export_devdb_vectors(m);
	devdb::export_enums(m);
	devdb::export_structs(m);
	export_lnb_extra(m);
	export_scan_command_extra(m);
	export_stream_extra(m);
	export_dish_extra(m);
	export_subscribe_options(m);
	m.attr("__version__") = version_info();
}
