/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "receiver/neumo-frontend.h"
#include "receiver/receiver.h"
#include "receiver/active_si_stream.h"
#include "receiver/scan.h"
#include "receiver/subscriber.h"
#include "stackstring/stackstring_pybind.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include "viewer/wxpy_api.h"
#include "scan.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <wx/window.h>

namespace py = pybind11;

void export_pls_search_range(py::module& m) {
	py::class_<pls_search_range_t>(m, "pls_search_range_t")
		.def(py::init())
		.def_readwrite("start", &pls_search_range_t::start)
		.def_readwrite("end", &pls_search_range_t::end)
		.def_readwrite("timeoutms", &pls_search_range_t::timeoutms)
		.def_readwrite("pls_mode", &pls_search_range_t::pls_mode)
		;
}


void export_playback_info(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<playback_info_t>(m, "playback_info_t")
		.def(py::init())
		.def_readonly("service", &playback_info_t::service)
		.def_readonly("epg", &playback_info_t::epg)
#if 0
		.def_readonly("audio_language", &playback_info_t::audio_language)
		.def("subtitle_language", &playback_info_t::subtitle_language)
 		.def("start_time", &playback_info_t::start_time)
		.def("end_time", &playback_info_t::end_time)
 		.def("play_time{}", &playback_info_t::play_time)
		.def("is_recording", &playback_info_t::is_recording)
		.def("is_timeshifted", &playback_info_t::is_timeshifted)
		.def("stream_status", &playback_info_t::stream_status)
#endif
		;
};

static py::object constellation_helper(const ss::vector_<dtv_fe_constellation_sample> samples) {
	if (samples.size() == 0)
		return py::none();
	int width{2};
	int height{samples.size()};
	py::array::ShapeContainer shape{width, height};
	py::array_t<int> ret(shape);
	py::buffer_info info = ret.request();
	int* p = (int*)info.ptr;
	int stride0 = info.strides[0] / sizeof(p[0]);
	int stride1 = info.strides[1] / sizeof(p[1]);
	int i = 0;

	for (const auto& s : samples) {
		p[i * stride1] = s.real;
		p[i * stride1 + stride0] = s.imag;
		++i;
	}

	return std::move(ret);
}

void export_signal_info(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<signal_info_t>(m, "signal_info_t")
		.def(py::init())
		.def_property_readonly("has_carrier", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_CARRIER) ? 1 : 0;
		})
		.def_property_readonly("has_timing_lock", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_TIMING_LOCK) ? 1 : 0;
		})
		.def_property_readonly("has_fec", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_VITERBI) ? 1 : 0;
		})
		.def_property_readonly("has_sync", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_SYNC) ? 1 : 0;
		})
		.def_property_readonly("has_lock", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_HAS_LOCK) ? 1 : 0;
		})
		.def_property_readonly("has_fail", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_TIMEDOUT) ? 1 : 0;
		})
		.def_property_readonly("has_tempfail", [](const signal_info_t& i) {
			return (i.lock_status.fe_status & FE_OUT_OF_RESOURCES) ? 1 : 0;
		})
		.def_property_readonly("sat_pos_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE
				&& (mux_common_ptr(i.driver_mux)->tune_src ==  tune_src_t::NIT_TUNED
						|| mux_common_ptr(i.driver_mux)->tune_src ==  tune_src_t::NIT_CORRECTED);
		})
		.def_property_readonly("on_wrong_sat", [](const signal_info_t& i) {
			return i.tune_confirmation.on_wrong_sat;
		})
		.def_property_readonly("network_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.network_id_by != confirmed_by_t::NONE;
		})
		.def_property_readonly("ts_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.ts_id_by != confirmed_by_t::NONE;
		})
		.def_property_readonly("nit_received", [](const signal_info_t& i) {
			return i.tune_confirmation.nit_actual_received;
		})
		.def_property_readonly("sdt_received", [](const signal_info_t& i) {
			return i.tune_confirmation.sdt_actual_received;
		})
		.def_property_readonly("pat_received", [](const signal_info_t& i) {
			return i.tune_confirmation.pat_received;
		})
		.def_property_readonly("has_nit", [](const signal_info_t& i) {
			return i.tune_confirmation.nit_actual_seen;
		})
		.def_property_readonly("has_sdt", [](const signal_info_t& i) {
			return i.tune_confirmation.sdt_actual_seen;
		})
		.def_property_readonly("has_pat", [](const signal_info_t& i) {
			return i.tune_confirmation.pat_seen;
		})
		.def_property_readonly("has_si_done", [](const signal_info_t& i) {
			return i.tune_confirmation.si_done;
		})
		.def_property_readonly("has_no_dvb", [](const signal_info_t& i) {
			return  (i.lock_status.matype >= 0 && i.lock_status.matype<256) && (i.lock_status.matype >>6) != 3;
		})
		.def_readonly("stat", &signal_info_t::stat)
		.def_property_readonly("signal_strength", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)-60000; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.signal_strength;
		})
		.def_property_readonly("snr", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)0; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.snr;
		})
		.def_property_readonly("ber", [](const signal_info_t& i) {
			if(i.stat.stats.size()==0)
				return (float)0; //should not happen
			auto& e = i.stat.stats[i.stat.stats.size()-1];
			return e.ber;
		})
		.def_readonly("lnb_lof_offset",&signal_info_t::lnb_lof_offset)
 		.def_property_readonly("isi_list", [](const signal_info_t& i) {
			return  &(ss::vector_<int16_t>&)i.isi_list;
		})
 		.def_property_readonly("matype_list", [](const signal_info_t& i) {
			return  &(ss::vector_<uint16_t>&)i.matype_list;
		})
		.def_property_readonly("matype", [](const signal_info_t& i) {
			auto *dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&i.driver_mux);
			auto ret = chdb::matype_str(i.lock_status.matype, dvbs_mux ? (int)dvbs_mux->rolloff : -1);
			return  std::string(ret.c_str());
		})
		.def_property_readonly("locktime", [](const signal_info_t& i) {
			return i.locktime_ms;
		})
		.def_property_readonly("bitrate", [](const signal_info_t& i) {
			return i.bitrate;
		})
		.def_property_readonly("has_matype", [](const signal_info_t& i) {
			return i.lock_status.matype >=0;
		})
		.def_property_readonly("mis_mode", [](const signal_info_t& i) {
			return !((i.lock_status.matype >>5)&1);
		})
		.def_property_readonly("constellation_samples", [](const signal_info_t& i) {
			return constellation_helper(i.constellation_samples);
		})
		.def_property_readonly("driver_mux", [](const signal_info_t& i) { //tuned mux
			return &i.driver_mux;
		}
			, "Information received from driver"
		)
		.def_property_readonly("received_si_mux", [](const signal_info_t& i) {
			return &i.received_si_mux;
		}
			, "NIT info as received from the current stream"
			)
		.def_property_readonly("received_si_mux_is_bad", [](const signal_info_t& i) {
			return &i.received_si_mux_is_bad;
		}
			, "NIT info as received from the current stream is considered incorrect"
			)
		.def_property_readonly("min_snr", [](const signal_info_t& i) {
			return (int)(chdb::min_snr(i.driver_mux)*1000);
		})
		;
}

void export_sdt_data(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<sdt_data_t>(m, "sdt_data_t")
		.def(py::init())
		.def_readwrite("mux_key", &sdt_data_t::mux_key)
		.def_readwrite("network_id", &sdt_data_t::actual_network_id)
		.def_readwrite("ts_id", &sdt_data_t::actual_ts_id)
		.def_property_readonly("services", [](sdt_data_t& sdt_data) {
			return (ss::vector_<chdb::service_t>&) sdt_data.actual_services;})
		;
}

void export_position_motion_report(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<positioner_motion_report_t>(m, "positioner_motion_report_t",
																				 "Tune Options for neumodvb")
		.def_readwrite("dish", &positioner_motion_report_t::dish)
		.def_readwrite("start_time", &positioner_motion_report_t::start_time)
		.def_readwrite("end_time", &positioner_motion_report_t::end_time)
		;
}

void export_scan_report(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<scan_mux_end_report_t>(m, "scan_mux_end_report_t")
		.def(py::init())
		.def_readwrite("spectrum_key", &scan_mux_end_report_t::spectrum_key)
		.def_readwrite("peak", &scan_mux_end_report_t::peak)
		.def_readwrite("mux", &scan_mux_end_report_t::mux)
		.def_readwrite("fe_key", &scan_mux_end_report_t::fe_key)
		;
	py::class_<peak_to_scan_t>(m, "peak_to_scan_t")
		.def(py::init())
		.def_readwrite("peak", &peak_to_scan_t::peak)
		.def_readwrite("scan_id", &peak_to_scan_t::scan_id)
		;
}
