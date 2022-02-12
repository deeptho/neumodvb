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
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/adapter.h"
#include "receiver/neumofrontend.h"
#include "receiver/receiver.h"
#include "receiver/scan.h"
#include "receiver/subscriber.h"
#include "stackstring/stackstring_pybind.h"
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include "viewer/wxpy_api.h"
#include <gtk/gtk.h>
#include <sip.h>
#include <stdio.h>
#include <wx/window.h>

namespace py = pybind11;

void export_scan_stats(py::module& m) {
	py::class_<scan_stats_t>(m, "scan_stats")
		.def_readonly("scheduled_muxes", &scan_stats_t::scheduled_muxes)
		.def_readonly("finished_muxes", &scan_stats_t::finished_muxes)
		.def_readonly("active_muxes", &scan_stats_t::active_muxes)
		.def_readonly("failed_muxes", &scan_stats_t::failed_muxes)
		.def_readonly("last_scanned_mux", &scan_stats_t::last_scanned_mux)
		.def_readonly("last_subscribed_mux", &scan_stats_t::last_subscribed_mux)
		;
}

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

void export_subscriber(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	export_retune_mode(m);
	export_pls_search_range(m);
	m.def("get_object", &get_object);
	m.def("set_gtk_window_name", &set_gtk_window_name
				, "Set a gtk widget name for a wx window (needed for css styling)"
				, py::arg("window")
				, py::arg("name"));
	m.def("gtk_add_window_style", &gtk_add_window_style
				, "Set a gtk widget style name for a wx window (needed for css styling)"
				, py::arg("window")
				, py::arg("name"));
	m.def("gtk_remove_window_style", &gtk_remove_window_style
				, "Remove a gtk widget style name for a wx window (needed for css styling)"
				, py::arg("window")
				, py::arg("name"));
	py::class_<subscriber_t, std::shared_ptr<subscriber_t>>(m, "subscriber_t")
		.def(py::init(&make_subscriber))
		.def("update_current_lnb"
				 , &subscriber_t::update_current_lnb
				 , "Update and save the current lnb"
				 , py::arg("lnb"))
		.def("subscribe_lnb"
				 , &subscriber_t::subscribe_lnb
				 , "Subscribe to a specific lnb without (re)tuning"
				 , py::arg("lnb")
				 , py::arg("retune_mode"))
		.def("subscribe_lnb_and_mux"
				 , &subscriber_t::subscribe_lnb_and_mux
				 , "Subscribe to a specific mux usign a specific lnb"
				 , py::arg("lnb")
				 , py::arg("mux")
				 , py::arg("blindscan")
				 , py::arg("pls_search_mode")=false
				 , py::arg("retune_mode"))
		.def_property_readonly("error_message", [](subscriber_t* self) {
			return get_error().c_str(); })
		.def("unsubscribe"
				 , &subscriber_t::unsubscribe
				 , "End tuning")
		.def("subscribe_spectrum"
				 , &subscriber_t::subscribe_spectrum
				 , "acquire a spectrum for this lnb"
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
		.def_readonly("tune_attempt", &signal_info_t::tune_attempt)
		.def_property_readonly("has_signal", [](const signal_info_t& i) {
			return (i.lock_status& FE_HAS_SIGNAL) ? 1 : 0;
		})
		.def_property_readonly("has_carrier", [](const signal_info_t& i) {
			return (i.lock_status& FE_HAS_CARRIER) ? 1 : 0;
		})
		.def_property_readonly("has_fec", [](const signal_info_t& i) {
			return (i.lock_status& FE_HAS_VITERBI) ? 1 : 0;
		})
		.def_property_readonly("has_sync", [](const signal_info_t& i) {
			return (i.lock_status& FE_HAS_SYNC) ? 1 : 0;
		})
		.def_property_readonly("has_lock", [](const signal_info_t& i) {
			return (i.lock_status& FE_HAS_LOCK) ? 1 : 0;
		})
		.def_property_readonly("has_fail", [](const signal_info_t& i) {
			return (i.lock_status& FE_TIMEDOUT) ? 1 : 0;
		})
		.def_property_readonly("sat_pos_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE;
		})
		.def_property_readonly("network_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.network_id_by != confirmed_by_t::NONE;
		})
		.def_property_readonly("ts_id_confirmed", [](const signal_info_t& i) {
			return i.tune_confirmation.sat_by != confirmed_by_t::NONE &&
				i.tune_confirmation.ts_id_by != confirmed_by_t::NONE;
		})
		.def_property_readonly("has_nit", [](const signal_info_t& i) {
			return i.tune_confirmation.nit_actual_ok;
		})
		.def_property_readonly("has_sdt", [](const signal_info_t& i) {
			return i.tune_confirmation.sdt_actual_ok;
		})
		.def_property_readonly("has_pat", [](const signal_info_t& i) {
			return i.tune_confirmation.pat_ok;
		})
		.def_property_readonly("has_si_done", [](const signal_info_t& i) {
			return i.tune_confirmation.si_done;
		})
		.def_property_readonly("has_no_dvb", [](const signal_info_t& i) {
			return  i.matype >= 0 && (i.matype >>6) != 3;
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
		.def_property_readonly("matype", [](const signal_info_t& i) {
			auto ret = chdb::matype_str(i.matype);
			return  std::string(ret.c_str());
		})
		.def_property_readonly("has_matype", [](const signal_info_t& i) {
			return i.matype >=0;
		})
		.def_property_readonly("mis_mode", [](const signal_info_t& i) {
			return !((i.matype >>5)&1);
		})
		.def_property_readonly("constellation_samples", [](const signal_info_t& i) {
			return constellation_helper(i.constellation_samples);
		})
		.def_property_readonly("dvbs_mux", [](const signal_info_t& i) { //tuned mux
			return &i.mux;
		})
		.def_property_readonly("si_mux", [](const signal_info_t& i) { //si mux data
			return &i.si_mux;
		})
		.def_property_readonly("min_snr", [](const signal_info_t& i) {
			return (int)(chdb::min_snr(i.mux)*1000);
		})
		;
}
