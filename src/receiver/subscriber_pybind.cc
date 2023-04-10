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
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/devmanager.h"
#include "receiver/neumofrontend.h"
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
#include <sip.h>
#include <stdio.h>
#include <wx/window.h>

namespace py = pybind11;

template <typename T> T* wxLoad(py::object src, const wxString& inTypeName) {
	/* Extract PyObject from handle */
	PyObject* source = src.ptr();

	T* obj = nullptr;

	bool success = wxPyConvertWrappedPtr(source, (void**)&obj, inTypeName);
	wxASSERT_MSG(success, _T("Returned object was not a ") + inTypeName);

	return obj;
}

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

static void set_gtk_window_name(py::object window, const char* name) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	auto* x = w->GetHandle();
	gtk_widget_set_name(x, (const gchar*)name);
}

static void gtk_add_window_style(py::object window, const char* style) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	auto* x = w->GetHandle();
	if (!x) {
		dterror("Invalid window");
		return;
	}
	GtkStyleContext* ctx = gtk_widget_get_style_context(x);
	gtk_style_context_add_class(ctx, style);
}

static void gtk_remove_window_style(py::object window, const char* style) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	if (!w) {
		dterror("Invalid window");
		return;
	}
	auto* x = w->GetHandle();
	if (!x) {
		dterror("Invalid window");
		return;
	}
	GtkStyleContext* ctx = gtk_widget_get_style_context(x);
	gtk_style_context_remove_class(ctx, style);
}

static std::shared_ptr<subscriber_t> make_subscriber(receiver_t* receiver, py::object window) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	return subscriber_t::make(receiver, w);
}

static std::shared_ptr<subscriber_t> get_global_subscriber(receiver_t* receiver, py::object window) {
	if(!receiver->global_subscriber) {
		receiver->global_subscriber = make_subscriber(receiver, window);
	}
	return receiver->global_subscriber;
}

static py::object get_object(long x) {
	return subscriber_t::handle_to_py_object(x);
}

void export_retune_mode(py::module& m) {
	py::enum_<retune_mode_t>(m, "retune_mode_t", py::arithmetic())
		.value("AUTO", retune_mode_t::AUTO)
		.value("NEVER", retune_mode_t::NEVER)
		.value("IF_NOT_LOCKED", retune_mode_t::IF_NOT_LOCKED)
		.value("UNCHANGED", retune_mode_t::UNCHANGED)
		;
}

void export_pls_search_range(py::module& m) {
	py::class_<pls_search_range_t>(m, "pls_search_range_t")
		.def(py::init())
		.def_readwrite("start", &pls_search_range_t::start)
		.def_readwrite("end", &pls_search_range_t::end)
		.def_readwrite("timeoutms", &pls_search_range_t::timeoutms)
		.def_readwrite("pls_mode", &pls_search_range_t::pls_mode)
		;
}


static int scan_spectral_peaks(subscriber_t& subscriber, const statdb::spectrum_key_t& spectrum_key,
																			py::array_t<float> peak_freq, py::array_t<float> peak_sr) {
	py::buffer_info infofreq = peak_freq.request();
	if (infofreq.ndim != 1)
		throw std::runtime_error("Bad number of dimensions");
	auto* pfreq = (float*)infofreq.ptr;
	py::buffer_info infosr = peak_sr.request();
	if (infosr.ndim != 1)
		throw std::runtime_error("Bad number of dimensions");
	auto* psr = (float*)infosr.ptr;

	int n = infofreq.shape[0];
	if (n!= infosr.shape[0])
		throw std::runtime_error("Bad Spectrum and freq need to have same size");
	ss::vector_<chdb::spectral_peak_t> peaks;
	peaks.reserve(n);
	for(int i = n-1 ; i>=0; --i) {
		peaks.push_back(chdb::spectral_peak_t{(uint32_t) (pfreq[i]*1000), (uint32_t) psr[i],
				spectrum_key.pol});
	}
	auto subscription_id = subscriber.scan_spectral_peaks(peaks, spectrum_key);
	return subscription_id;
}

static int scan_muxes(subscriber_t& subscriber, py::list mux_list) {
	ss::vector<chdb::dvbs_mux_t,1> dvbs_muxes;
	ss::vector<chdb::dvbc_mux_t,1> dvbc_muxes;
	ss::vector<chdb::dvbt_mux_t,1> dvbt_muxes;
	for(auto m: mux_list) {
		bool ok{false};
		if(!ok)
			try {
				auto* dvbs_mux = m.cast<chdb::dvbs_mux_t*>();
				dvbs_muxes.push_back(*dvbs_mux);
				ok=true;
			} catch (py::cast_error& e) {}
		if(!ok)
			try {
				auto* dvbc_mux = m.cast<chdb::dvbc_mux_t*>();
				dvbc_muxes.push_back(*dvbc_mux);
				ok=true;
			} catch (py::cast_error& e) {}
		if(!ok)
			try {
				auto* dvbt_mux = m.cast<chdb::dvbt_mux_t*>();
				dvbt_muxes.push_back(*dvbt_mux);
				ok=true;
			} catch (py::cast_error& e) {}
	}
	auto ret = subscriber.scan_muxes(dvbs_muxes, dvbc_muxes, dvbt_muxes);
	return ret;
}



void export_subscriber(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	export_retune_mode(m);
	export_pls_search_range(m);
	m.def("get_object", &get_object)
		.def("set_gtk_window_name", &set_gtk_window_name
				, "Set a gtk widget name for a wx window (needed for css styling)"
				, py::arg("window")
				 , py::arg("name"))
		.def("gtk_add_window_style", &gtk_add_window_style
				 , "Set a gtk widget style name for a wx window (needed for css styling)"
				 , py::arg("window")
				 , py::arg("name"))
		.def("gtk_remove_window_style", &gtk_remove_window_style
				, "Remove a gtk widget style name for a wx window (needed for css styling)"
				 , py::arg("window")
				 , py::arg("name"))
		.def("global_subscriber", &get_global_subscriber
				 ,"Connect to the global subscriber to catch non-subscriber specific error messages"
				 , py::arg("receiver")
				 , py::arg("window")
			)
		;
	py::class_<subscriber_t, std::shared_ptr<subscriber_t>>(m, "subscriber_t")
		.def(py::init(&make_subscriber))
		.def("update_current_lnb"
				 , &subscriber_t::update_current_lnb
				 , "Update and save the current lnb"
				 , py::arg("lnb"))
		.def("subscribe_lnb"
				 , &subscriber_t::subscribe_lnb
				 , "Subscribe to a specific lnb without (re)tuning"
				 , py::arg("rf_path")
				 , py::arg("lnb")
				 , py::arg("retune_mode"))
		.def("subscribe_lnb_and_mux"
				 , &subscriber_t::subscribe_lnb_and_mux
				 , "Subscribe to a specific mux usign a specific lnb"
				 , py::arg("rf_path")
				 , py::arg("lnb")
				 , py::arg("mux")
				 , py::arg("blindscan")
				 , py::arg("pls_search_mode")=false
				 , py::arg("retune_mode"))
		.def("scan_spectral_peaks", &scan_spectral_peaks,
				 "scan peaks in the spectrum all at once",
				 py::arg("spectrum_key"), py::arg("peak_freq"), py::arg("peak_sr")
			)
		.def("scan_muxes", &scan_muxes,
				 "scan muxes",
				 py::arg("muxes")
			)
		.def_property_readonly("error_message", [](subscriber_t* self) {
			return get_error().c_str(); })
		.def("unsubscribe"
				 , &subscriber_t::unsubscribe
				 , "End tuning")
		.def("subscribe_spectrum"
				 , &subscriber_t::subscribe_spectrum
				 , "acquire a spectrum for this lnb"
				 , py::arg("rf_path")
				 , py::arg("lnb")
				 , py::arg("pol to scan")
				 , py::arg("start_freq")
				 , py::arg("end_freq")
				 , py::arg("sat_pos") = sat_pos_none
			)
		.def("positioner_cmd"
				 , &subscriber_t::positioner_cmd
				 , "send positioner_cmd"
				 , py::arg("cmd")
				 , py::arg("par")=0
			)
		;
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
				&& mux_common_ptr(i.driver_mux)->tune_src ==  tune_src_t::NIT_TUNED;
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
			, "Information received from driver, with missing info filled in from consolidated_mux"
		)
		.def_property_readonly("consolidated_mux", [](const signal_info_t& i) { //si mux data
			return &i.consolidated_mux;
		}
			, "NIT info after combining all available information, taking into account database, driver and received info"
			)
		.def_property_readonly("bad_received_si_mux", [](const signal_info_t& i) {

			return &i.bad_received_si_mux;
		}
			, "NIT info as received from the current stream, but only iof it conflicts with consolidated_mux"
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
		.def_readwrite("network_id", &sdt_data_t::actual_network_id)
		.def_readwrite("ts_id", &sdt_data_t::actual_ts_id)
		.def_property_readonly("services", [](sdt_data_t& sdt_data) {
			return (ss::vector_<chdb::service_t>&) sdt_data.actual_services;})
		;
}

void export_scan_report(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	using namespace chdb;
	py::class_<scan_stats_t>(m, "scan_stats_t")
		.def(py::init())
		.def_readwrite("pending_peaks", &scan_stats_t::pending_peaks)
		.def_readwrite("pending_muxes", &scan_stats_t::pending_muxes)
		.def_readwrite("active_muxes", &scan_stats_t::active_muxes)
		.def_readwrite("finished_muxes", &scan_stats_t::finished_muxes)
		.def_readwrite("failed_muxes", &scan_stats_t::failed_muxes)
		.def_readwrite("locked_muxes", &scan_stats_t::locked_muxes)
		.def_readwrite("si_muxes", &scan_stats_t::si_muxes)
		;
	py::class_<scan_report_t>(m, "scan_report_t")
		.def(py::init())
		.def_readwrite("spectrum_key", &scan_report_t::spectrum_key)
		.def_readwrite("peak", &scan_report_t::peak)
		.def_readwrite("mux", &scan_report_t::mux)
		.def_readwrite("fe_key", &scan_report_t::fe_key)
		//.def_readwrite("lnb_key", &scan_report_t::lnb_key)
		//.def_readwrite("sat_pos", &scan_report_t::sat_pos)
		.def_readwrite("band", &scan_report_t::band)
		.def_readwrite("scan_stats", &scan_report_t::scan_stats)
		;
}
