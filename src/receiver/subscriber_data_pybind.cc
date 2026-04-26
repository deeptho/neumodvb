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
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/devmanager.h"
#include "receiver/modcods.h"
#include "receiver/neumo-frontend.h"
#include "receiver/receiver.h"
#include "receiver/active_si_stream.h"
#include "receiver/scan.h"
#include "receiver/subscriber.h"
#include "stackstring/stackstring_pybind.h"
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include "viewer/wxpy_api.h"
#include "scan.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <wx/window.h>
#include <numeric>
#include <array>

namespace nb = nanobind;
using numpy_int_array = nb::ndarray<int, nb::numpy, nb::ndim<1>, nb::c_contig>;

template <typename T, typename Compare>
std::vector<std::size_t> sort_permutation(
    const std::vector<T>& vec,
    Compare&& compare)
{
    std::vector<std::size_t> p(vec.size());
    std::iota(p.begin(), p.end(), 0);
    std::sort(p.begin(), p.end(),
        [&](std::size_t i, std::size_t j){ return compare(vec[i], vec[j]); });
    return p;
}

template <typename T>
std::vector<T> apply_permutation(
    const std::vector<T>& vec,
    const std::vector<std::size_t>& p)
{
    std::vector<T> sorted_vec(vec.size());
    std::transform(p.begin(), p.end(), sorted_vec.begin(),
        [&](std::size_t i){ return vec[i]; });
    return sorted_vec;
}

void export_pls_search_range(nb::module_& m) {
	nb::class_<pls_search_range_t>(m, "pls_search_range_t")
		.def(nb::init())
		.def_rw("start", &pls_search_range_t::start)
		.def_rw("end", &pls_search_range_t::end)
		.def_rw("timeoutms", &pls_search_range_t::timeoutms)
		.def_rw("pls_mode", &pls_search_range_t::pls_mode)
		;
}


void export_playback_info(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	nb::class_<playback_info_t>(m, "playback_info_t")
		.def(nb::init())
		.def_ro("service", &playback_info_t::service)
		.def_ro("epg", &playback_info_t::epg)
		;
};

static std::optional<numpy_int_array> constellation_helper(const ss::vector_<dtv_fe_constellation_sample> samples) {
	if (samples.size() == 0)
		return {};
	std::array<size_t, 2> shape{2, (size_t)samples.size()};
	std::array<int64_t, 2> stride{1, 2};
	numpy_int_array ret(nullptr /*let nanobind allocate*/, (size_t) 2 /*ndim*/, &shape[0], nullptr /*owner*/, &stride[0]);
	auto* p = ret.data();
	int i = 0;

	for (const auto& s : samples) {
		p[i * stride[1]] = s.real;
		p[i * stride[1] + stride[0]] = s.imag;
		++i;
	}

	return ret;
}

void export_signal_info(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	nb::class_<signal_info_t>(m, "signal_info_t")
		.def(nb::init())
		.def_prop_ro("has_carrier", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_CARRIER) ? 1 : 0;
		})
		.def_prop_ro("has_timing_lock", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_TIMING_LOCK) ? 1 : 0;
		})
		.def_prop_ro("has_fec", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_VITERBI) ? 1 : 0;
		})
		.def_prop_ro("has_sync", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_SYNC) ? 1 : 0;
		})
		.def_prop_ro("has_lock", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_LOCK) ? 1 : 0;
		})
		.def_prop_ro("has_fail", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_TIMEDOUT) ? 1 : 0;
		})
		.def_prop_ro("has_tempfail", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_OUT_OF_RESOURCES) ? 1 : 0;
		})
		.def_prop_ro("sat_pos_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE
				&& (mux_common_ptr(i.driver_mux)->tune_src ==  tune_src_t::NIT_TUNED
						|| mux_common_ptr(i.driver_mux)->tune_src ==  tune_src_t::NIT_CORRECTED);
		})
		.def_prop_ro("on_wrong_sat", [](const signal_info_t& i) {
			return i.tune_confirmation.on_wrong_sat;
		})
		.def_prop_ro("network_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.network_id_by != confirmed_by_t::NONE;
		})
		.def_prop_ro("ts_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.ts_id_by != confirmed_by_t::NONE;
		})
		.def_prop_ro("nit_received", [](const signal_info_t& i) {
			return i.tune_confirmation.nit_actual_received;
		})
		.def_prop_ro("sdt_received", [](const signal_info_t& i) {
			return i.tune_confirmation.sdt_actual_received;
		})
		.def_prop_ro("pat_received", [](const signal_info_t& i) {
			return i.tune_confirmation.pat_received;
		})
		.def_prop_ro("has_nit", [](const signal_info_t& i) {
			return i.tune_confirmation.nit_actual_seen;
		})
		.def_prop_ro("has_sdt", [](const signal_info_t& i) {
			return i.tune_confirmation.sdt_actual_seen;
		})
		.def_prop_ro("has_pat", [](const signal_info_t& i) {
			return i.tune_confirmation.pat_seen;
		})
		.def_prop_ro("has_si_done", [](const signal_info_t& i) {
			return i.tune_confirmation.si_done;
		})
		.def_prop_ro("has_no_dvb", [](const signal_info_t& i) {
			return  (i.lock_status.matype >= 0 && i.lock_status.matype<256) && (i.lock_status.matype >>6) != 3;
		})
		.def_ro("stat", &signal_info_t::stat)
		.def_prop_ro("signal_strength", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)-60000; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.signal_strength;
		})
		.def_prop_ro("snr", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)0; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.snr;
		})
		.def_prop_ro("ber", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)0; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.ber;
		})
		.def_ro("lnb_lof_offset",&signal_info_t::lnb_lof_offset)
 		.def_prop_ro("isi_list", [](const signal_info_t& i) {
			return  &(ss::vector_<int16_t>&)i.isi_list;
		})
		.def_prop_ro("modcod_list", [](const signal_info_t& i) {
			std::vector<int16_t> modcod;
			std::vector<float> perc;
			std::vector<std::string> ret;
			for(auto & e: i.modcod_list) {
				modcod.push_back(e.modcod);
				perc.push_back(e.frac/10.);
			}

			auto p = sort_permutation(perc,
																[](float const& a, float const& b){
																	return a > b;});

			modcod= apply_permutation(modcod, p);
			perc = apply_permutation(perc, p);
			for(int i=0 ; i < (int)modcod.size(); ++i) {
				ss::string<16> s;
				auto *desc = get_modcod_desc(modcod[i]);
				if(perc[i] <1.) {
					s.format("{} {} ({}%)", desc->modulation, desc->code_rate, perc[i]);
				} else {
					s.format("{} {} ({}%)", desc->modulation, desc->code_rate, (int)round(perc[i]));
				}
				ret.push_back(s);
			}
			return ret;
		})
 		.def_prop_ro("modcod_frac_list", [](const signal_info_t& i) {
			return  &(ss::vector_<int16_t>&)i.modcod_list;
		})
 		.def_prop_ro("matype_list", [](const signal_info_t& i) {
			return  &(ss::vector_<uint16_t>&)i.matype_list;
		})
		.def_prop_ro("matype", [](const signal_info_t& i) {
			ss::string<128> ret;
			auto *dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&i.driver_mux);
			char * start ="";
			if(i.matype_list.size()==0) {
				ret = chdb::matype_str(i.lock_status.matype, dvbs_mux ? (int)dvbs_mux->rolloff : -1);
			}
			for (auto matype_: i.matype_list) {
				int isi = matype_ & 0xff;
				int matype = (matype_ >> 8);
				if(dvbs_mux->k.stream_id ==  (matype&0xff)) {
					auto r = chdb::matype_str(matype, dvbs_mux ? (int)dvbs_mux->rolloff : -1);
					ret.format("{}{:d}:{}", start, isi, r);
					start ="; ";
				}
			}
			return  std::string(ret.c_str());
		})
		.def_prop_ro("locktime", [](const signal_info_t& i) {
			return i.locktime_ms;
		})
		.def_prop_ro("bitrate", [](const signal_info_t& i) {
			return i.bitrate;
		})
		.def_prop_ro("has_matype", [](const signal_info_t& i) {
			return i.lock_status.matype >=0;
		})
		.def_prop_ro("mis_mode", [](const signal_info_t& i) {
			return !((i.lock_status.matype >>5)&1);
		})
		.def_prop_ro("constellation_samples", [](const signal_info_t& i) {
			return constellation_helper(i.constellation_samples);
		})
		.def_prop_ro("driver_mux", [](const signal_info_t& i) { //tuned mux
			return &i.driver_mux;
		}
			, "Information received from driver"
		)
		.def_prop_ro("received_si_mux", [](const signal_info_t& i) {
			return &i.received_si_mux;
		}
			, "NIT info as received from the current stream"
			)
		.def_prop_ro("received_si_mux_is_bad", [](const signal_info_t& i) {
			return &i.received_si_mux_is_bad;
		}
			, "NIT info as received from the current stream is considered incorrect"
			)
		.def_prop_ro("min_snr", [](const signal_info_t& i) {
			return (int)(chdb::min_snr(i.driver_mux)*1000);
		})
		;
}

void export_sdt_data(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	nb::class_<sdt_data_t>(m, "sdt_data_t")
		.def(nb::init())
		.def_rw("mux_key", &sdt_data_t::mux_key)
		.def_rw("network_id", &sdt_data_t::actual_network_id)
		.def_rw("ts_id", &sdt_data_t::actual_ts_id)
		.def_prop_ro("services", [](sdt_data_t& sdt_data) {
			return (ss::vector_<chdb::service_t>&) sdt_data.actual_services;})
		;
}

void export_position_motion_report(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	nb::class_<positioner_motion_report_t>(m, "positioner_motion_report_t",
																				 "Tune Options for neumodvb")
		.def_rw("dish", &positioner_motion_report_t::dish)
		.def_rw("start_time", &positioner_motion_report_t::start_time)
		.def_rw("end_time", &positioner_motion_report_t::end_time)
		;
}

void export_scan_report(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	nb::class_<scan_mux_end_report_t>(m, "scan_mux_end_report_t")
		.def(nb::init())
		.def_rw("spectrum_key", &scan_mux_end_report_t::spectrum_key)
		.def_rw("peak", &scan_mux_end_report_t::peak)
		.def_rw("mux", &scan_mux_end_report_t::mux)
		.def_rw("fe_key", &scan_mux_end_report_t::fe_key)
		;
	nb::class_<peak_to_scan_t>(m, "peak_to_scan_t")
		.def(nb::init())
		.def_rw("peak", &peak_to_scan_t::peak)
		.def_rw("scan_id", &peak_to_scan_t::scan_id)
		;
}
