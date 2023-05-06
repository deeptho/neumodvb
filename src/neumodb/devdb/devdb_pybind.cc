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
#include "devdb_vector_pybind.h"
#include "neumodb/devdb/devdb_extra.h"
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

static void export_lnb_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("lnb"));
	using namespace devdb;
	mm.def("update_lnb_from_positioner", &lnb::update_lnb_from_positioner, "save changed lnb"
				 , py::arg("wtxn"), py::arg("lnb"), py::arg("usals_location")
				 , py::arg("current_sat_pos")=sat_pos_none
				 , py::arg("current_conn")= nullptr
				 ,py::arg("save")=true)
		.def("update_lnb_from_lnblist", &lnb::update_lnb_from_lnblist, "save changed lnb",
				 py::arg("wtxn"), py::arg("lnb"), py::arg("save")=true)
		.def("can_move_dish", &lnb::can_move_dish,
				 "Returns true if this lnb connection can move the dish",
				 py::arg("lnb_connection"))
		.def("on_positioner", &lnb::on_positioner,
				 "Returns true if this lnb is on a positioner",
				 py::arg("lnb"))
		.def("reset_lof_offset", &lnb::reset_lof_offset,
				 "reset the LOF offset to 0",
				 py::arg("devdb_wtxn"),
				 py::arg("lnb"))
		.def("make_unique_if_template", make_unique_if_template,
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
#if 0
		.def("current_sat_pos", &devdb::lnb::current_sat_pos,
				 "Obtain the direction in which the lnb currently points",
				 py::arg("lnb"), py::arg("usals_location"))
#endif
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
	export_ss_vector(m, fe_band_pol_t);

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

	m.attr("__version__") = version_info();
}
