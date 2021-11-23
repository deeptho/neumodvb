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
#include "chdb_vector_pybind.h"
#include "neumodb/chdb/chdb_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdio.h>

namespace py = pybind11;
using namespace chdb;

extern void export_neumodb(py::module& m);

namespace chdb {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
} // namespace chdb

static void export_chdb(py::module& m) {
	py::class_<chdb::chdb_t, neumodb_t>(m, "chdb").def(py::init<>())
		;
}

static void export_mux_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("dvbs_mux"));
	mm.def("make_unique_if_template", make_unique_if_template<dvbs_mux_t>,
				 "Make the key of this mux unique, but only if network_id==mux_id==extra_id==0")
		.def("list_distinct_sats", chdb::dvbs_mux::list_distinct_sats, "List distinct sat_pos of all muxes")
		.def("find_by_fuzzy_sat_pos_network_id_ts_id", chdb::find_by_fuzzy_sat_pos_network_id_ts_id,
				 "find a dvbs mux allowing for slamm deviation in sat_pos", py::arg("txn"), py::arg("sat_pos"),
				 py::arg("network_id"), py::arg("ts_id"))
		;

	py::reinterpret_borrow<py::module>(m.attr("dvbc_mux"))
		.def("make_unique_if_template", make_unique_if_template<dvbc_mux_t>,
				 "Make the key of this mux unique, but only if network_id==mux_id==extra_id==0")
		;

	py::reinterpret_borrow<py::module>(m.attr("dvbt_mux"))
		.def("make_unique_if_template", make_unique_if_template<dvbt_mux_t>,
				 "Make the key of this mux unique, but only if network_id==mux_id==extra_id==0")
		;
}

static void export_sat_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("sat"));
	mm.attr("sat_pos_dvbc") = sat_pos_dvbc;
	mm.attr("sat_pos_dvbt") = sat_pos_dvbt;
	mm.attr("sat_pos_none") = sat_pos_none;
}

static void export_chg_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("chg"));
	mm.def("contains_service", chdb::bouquet_contains_service, "Returns True if the bouquet contains the service",
				 py::arg("rtxn"), py::arg("bouquet"), py::arg("service_key"))
		.def("toggle_service_in_bouquet", chdb::toggle_service_in_bouquet,
				 "Add service to bouquet if it is not already in there, otherwise remove it", py::arg("wtxn"), py::arg("chg"),
				 py::arg("service"))
		.def("toggle_channel_in_bouquet", chdb::toggle_channel_in_bouquet,
				 "Add channel to bouquet if it is not already in there, otherwise remove it", py::arg("wtxn"), py::arg("chg"),
				 py::arg("channel"))
		.def("make_unique_if_template", make_unique_if_template<chg_t>,
				 "Make the key of this chg unique, but only if id==bouquet_id_template")
		;
}

static void export_chgm_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("chgm"));
	mm.def("make_unique_if_template", make_unique_if_template<chgm_t>,
			 "Make the key of this chg unique, but only if id==channel_id_template")
		;
}

static void export_lnb_extra(py::module& m) {
	chdb::lnb_t new_lnb(int tuner_id, int16_t sat_pos, int dish_id = 0, chdb::lnb_type_t type = chdb::lnb_type_t::UNIV);
	auto mm = py::reinterpret_borrow<py::module>(m.attr("lnb"));
	mm.def("new_lnb", &chdb::lnb::new_lnb, "create a new lnb", py::arg("tuner_id"), py::arg("sat_pos"),
				 py::arg("dish_id") = 0, py::arg("type") = chdb::lnb_type_t::UNIV)
		.def("make_unique_if_template", make_unique_if_template<lnb_t>,
				 "Make the key of this lnb unique, but only if lnb.k.id<0")
		.def("select_reference_mux", &chdb::lnb::select_reference_mux,
				 "Select a reference mux for an lnb; use prosed_mux if suitable, else use "
				 "one which will not move positioner",
				 py::arg("rtxn"), py::arg("lnb"), py::arg("proposed_mux").none(true) = nullptr)
		.def("select_lnb", &chdb::lnb::select_lnb, "Select an lnb; whcih can tune to sat or mux; prefers positioner",
				 py::arg("rtxn"), py::arg("sat").none(true) = nullptr, py::arg("mux").none(true) = nullptr)
		.def("add_network", &chdb::lnb::add_network,
				 "Add a network to an lnb if it does not yet exist; returns true if network was added", py::arg("lnb"),
				 py::arg("lnb_network"))
		.def("lnb_frequency_range", &chdb::lnb::lnb_frequency_range,
				 "Obtain min/mid/max frequency for this lnb",  py::arg("lnb"));
		;
}

static void export_lang_extra(py::module& m) {
	m.def("lang_name", &chdb::lang_name,
				"human readable language name", py::arg("lang_code"))
		;
}

PYBIND11_MODULE(pychdb, m) {
	m.doc() = R"pbdoc(
        Pybind11 channel database
        -----------------------

        .. currentmodule:: pychdb

        .. autosummary::
           :toctree: _generate

    )pbdoc";

	using namespace chdb;
	export_ss_vector(m, fe_band_pol_t);

	m.def(
		"sat_pos_str", [](int sat_pos) { return std::string(sat_pos_str(sat_pos).c_str()); },
		"make human readable representation", py::arg("sat_pos"))
		.def(
			"to_str", [](const sat_t& sat) { return to_str(sat).c_str(); }, "make human readable representation",
			py::arg("sat"))
		.def(
			"to_str",
			[](const dvbs_mux_t& mux) {
				auto* x = to_str(mux).c_str();
				return std::string(x);
			},
			"make human readable representation", py::arg("mux"))
		.def(
			"to_str",
			[](const dvbc_mux_t& mux) {
				auto* x = to_str(mux).c_str();
				return std::string(x);
			},
			"make human readable representation", py::arg("mux"))
		.def(
			"to_str",
			[](const dvbt_mux_t& mux) {
				auto* x = to_str(mux).c_str();
				return std::string(x);
			},
			"make human readable representation", py::arg("mux"))
		.def("lnb_can_tune_to_mux", &lnb_can_tune_to_mux, "check if lnb can tune to mux", py::arg("lnb"), py::arg("mux"),
				 py::arg("disregard_networks") = false)
		;
	export_neumodb(m);
	export_chdb(m);
	export_chdb_vectors(m);
	chdb::export_enums(m);
	chdb::export_structs(m);
	export_mux_extra(m);
	export_sat_extra(m);
	export_lnb_extra(m);
	export_chg_extra(m);
	export_chgm_extra(m);
	export_lang_extra(m);

	m.attr("__version__") = version_info();
}
