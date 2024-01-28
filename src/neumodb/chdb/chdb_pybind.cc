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
#include "chdb_vector_pybind.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "stackstring/stackstring_pybind.h"
#include "util/identification.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace chdb;

extern void export_neumodb(py::module& m);
extern void export_chdb(py::module& m);
namespace chdb {
	extern void export_enums(py::module& m);
	extern void export_structs(py::module& m);
} // namespace chdb

static void export_chdb_extra(py::module& m) {
	m.def("select_sat_and_reference_mux", &chdb::select_sat_and_reference_mux,
				"Select a sat and reference mux for an lnb; use prosed_mux if suitable, else use "
				"one which will not move positioner",
				py::arg("rtxn"), py::arg("lnb"), py::arg("proposed_mux").none(true) = nullptr)
		.def("select_reference_mux", &chdb::select_reference_mux,
				"Select a reference mux for an lnb; choose a mux from the database or mux with good defaults",
				 py::arg("chdb_rtxn"), py::arg("lnb"), py::arg("sat_pos").none(true) = nullptr)
		.def("select_sat_for_sat_band", &chdb::select_sat_for_sat_band,
				"Select a sat for a specific sat_band, prefering one with a close sat_pos",
				 py::arg("chdb_rtxn"), py::arg("sat_band"), py::arg("sat_pos") = sat_pos_none)
		;
}

static void export_mux_extra(py::module& m) {
	auto mm = py::reinterpret_borrow<py::module>(m.attr("dvbs_mux"));
	mm.def("make_unique_if_template", make_unique_if_template<dvbs_mux_t>,
				 "Make the key of this mux unique, but only if network_id==mux_id==extra_id==0")
		.def("list_distinct_sats", chdb::dvbs_mux::list_distinct_sats, "List distinct sat_pos of all muxes")
		.def("find_by_sat_pos_freq_pol_fuzzy",
				 [](db_txn& txn, int16_t sat_pos, int frequency, chdb::fe_polarisation_t pol, int stream_id, int t2mi_pid)
				 -> std::optional<chdb::dvbs_mux_t> {
					 using namespace chdb;
					 dvbs_mux_t mux;
					 mux.k.sat_pos = sat_pos;
					 mux.frequency = frequency;
					 mux.k.stream_id = stream_id;
					 mux.k.t2mi_pid = t2mi_pid;
					 mux.pol = pol;
					 auto c = find_by_mux_fuzzy(txn, mux, false /*ignore_stream_id*/, false /*ignore_t2mi_pid*/);
					 if(c.is_valid())
						 return record_at_cursor<dvbs_mux_t, decltype(c)>(c);
					 else
						 return {};
				 },
				 "Find a mux by sat_pos, frequency and pol, allowing small differences in frequency and sat_pos",
				 py::arg("chdb_rtxn"), py::arg("sat_pos"), py::arg("frequency"), py::arg("pol"),
				 py::arg("stream_id")=-1, py::arg("t2mi_pid") = -1)
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
	mm.attr("sat_pos_dvbs") = sat_pos_dvbs;
	mm.attr("sat_pos_none") = sat_pos_none;
	mm.def("freq_bounds", [](chdb::sat_band_t& sat_band) {
		return chdb::sat_band_freq_bounds(sat_band, chdb::sat_sub_band_t::NONE);
	}, "Frequency bounds for a satellite band",
		py::arg("sat_band"));
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

	m.def(
		"sat_pos_str", [](int sat_pos) { return std::string(sat_pos_str(sat_pos).c_str()); },
		"make human readable representation", py::arg("sat_pos"))
		.def("sat_band_for_freq", &chdb::sat_band_for_freq,
				 "Sat band and low/high for frequency",
				 py::arg("freq")
			)
		.def(
			"key_src_str", [](key_src_t key_src) { return std::string(fmt::format("{}",key_src)); },
			"make human readable representation", py::arg("key_src"))
		.def(
			"tune_src_str", [](tune_src_t tune_src) { return std::string(fmt::format("{}",tune_src)); },
			"make human readable representation", py::arg("tune_src"))
		.def("matype_str", [](int matype) { return std::string(matype_str(matype).c_str()); },
				 "make human readable representation", py::arg("matype"))
		.def(
			"to_str", [](const sat_t& sat) { return std::string(fmt::format("{}", sat)); },
			"make human readable representation",
			py::arg("sat"))
		.def(
			"to_str",
			[](const dvbs_mux_t& mux) {
				return std::string(fmt::format("{}", mux));
			},
			"make human readable representation", py::arg("mux"))
		.def(
			"to_str",
			[](const dvbc_mux_t& mux) {
				return std::string(fmt::format("{}", mux));
			},
			"make human readable representation", py::arg("mux"))
		.def(
			"to_str",
			[](const dvbt_mux_t& mux) {
				return std::string(fmt::format("{}", mux));
			},
			"make human readable representation", py::arg("mux"))
		.def("delsys_to_type", &chdb::delsys_to_type)
		;
	export_neumodb(m);
	export_chdb(m);
	export_chdb_extra(m);
	export_chdb_vectors(m);
	chdb::export_enums(m);
	chdb::export_structs(m);
	export_mux_extra(m);
	export_sat_extra(m);
	export_chg_extra(m);
	export_chgm_extra(m);
	export_lang_extra(m);

	m.attr("__version__") = version_info();
}
