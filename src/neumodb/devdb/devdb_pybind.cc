/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

namespace devdb {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
} // namespace devdb

static void export_devdb(py::module& m) {
	py::class_<devdb::devdb_t, neumodb_t>(m, "devdb").def(py::init<>())
		;
}


static void export_lnb_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("lnb"));
	using namespace devdb;
	mm
		.def("update_lnb", &lnb::update_lnb, "save changed lnb, while checking tune string",
				 py::arg("wtxn"), py::arg("lnb"), py::arg("save")=true)
		.def("reset_lof_offset", &lnb::reset_lof_offset,
				 "reset the LOF offset to 0",
				 py::arg("lnb"))
		.def("make_unique_if_template", make_unique_if_template,
				 "Make the key of this lnb unique, but only if lnb.k.id<0")
		.def("select_reference_mux", &lnb::select_reference_mux,
				 "Select a reference mux for an lnb; use prosed_mux if suitable, else use "
				 "one which will not move positioner",
				 py::arg("rtxn"), py::arg("lnb"), py::arg("proposed_mux").none(true) = nullptr)
		.def("select_lnb", &lnb::select_lnb, "Select an lnb; whcih can tune to sat or mux; prefers positioner",
				 py::arg("rtxn"), py::arg("sat").none(true) = nullptr, py::arg("mux").none(true) = nullptr)
		.def("add_network", &lnb::add_network,
				 "Add a network to an lnb if it does not yet exist; returns true if network was added", py::arg("lnb"),
				 py::arg("lnb_network"))
		.def("lnb_frequency_range", &lnb::lnb_frequency_range,
				 "Obtain min/mid/max frequency for this lnb",  py::arg("lnb"));
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
