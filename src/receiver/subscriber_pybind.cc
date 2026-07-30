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
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/shared_ptr.h>
#include "stackstring/stackstring_pybind.h"
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/devmanager.h"
#include "receiver/neumo-frontend.h"
#include "receiver/receiver.h"
#include "receiver/active_si_stream.h"
#include "receiver/scan.h"
#include "receiver/subscriber.h"
#include "viewer/wxpy_api.h"
#include "scan.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <wx/window.h>

namespace nb = nanobind;
using numpy_float_array = nb::ndarray<float, nb::numpy, nb::ndim<1>, nb::c_contig>;
using numpy_uint8_array = nb::ndarray<uint64_t, nb::numpy, nb::ndim<1>, nb::c_contig>;
using numpy_int_array = nb::ndarray<int, nb::numpy, nb::ndim<1>, nb::c_contig>;

template <typename T> T* wxLoad(nb::object src, const wxString& inTypeName) {
	/* Extract PyObject from handle */
	PyObject* source = src.ptr();

	T* obj = nullptr;

	bool success = wxPyConvertWrappedPtr(source, (void**)&obj, inTypeName);
	wxASSERT_MSG(success, _T("Returned object was not a ") + inTypeName);

	return obj;
}

static void set_gtk_window_name(nb::object window, const char* name) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	if(!w) {
		dterrorf("Could not set window name {}", name);
	} else {
		auto* x = w->GetHandle();
		if(!x) {
			dterrorf("Could not get handle for window {}", name);
		} else {
			gtk_widget_set_name(x, (const gchar*)name);
		}
	}
}

static void gtk_add_window_style(nb::object window, const char* style) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	auto* x = w->GetHandle();
	if (!x) {
		dterrorf("Invalid window");
		return;
	}
	GtkStyleContext* ctx = gtk_widget_get_style_context(x);
	gtk_style_context_add_class(ctx, style);
}

static void gtk_remove_window_style(nb::object window, const char* style) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	if (!w) {
		dterrorf("Invalid window");
		return;
	}
	auto* x = w->GetHandle();
	if (!x) {
		dterrorf("Invalid window");
		return;
	}
	GtkStyleContext* ctx = gtk_widget_get_style_context(x);
	gtk_style_context_remove_class(ctx, style);
}

static std::shared_ptr<subscriber_t> make_subscriber(receiver_t* receiver, nb::object window) {
	auto* w = wxLoad<wxWindow>(window, "wxWindow");
	return subscriber_t::make(receiver, w);
}

static std::shared_ptr<subscriber_t> get_global_subscriber(receiver_t* receiver, nb::object window) {
	if(!receiver->global_subscriber) {
		receiver->global_subscriber = make_subscriber(receiver, window);
	}
	return receiver->global_subscriber;
}

static nb::object get_object(long x) {
	return subscriber_t::handle_to_py_object(x);
}

static int scan_spectral_peaks(subscriber_t& subscriber,
															 const devdb::rf_path_t& rf_path,
															 const statdb::spectrum_key_t& spectrum_key,
															 numpy_float_array peak_freq, numpy_float_array peak_sr) {
	if (peak_freq.ndim() != 1)
		throw std::runtime_error("Bad number of dimensions");
	auto* pfreq = peak_freq.data();

	if (peak_sr.ndim() != 1)
		throw std::runtime_error("Bad number of dimensions");
	auto* psr = peak_sr.data();

	auto n = peak_freq.shape(0);
	if (n!= peak_sr.shape(0))
		throw std::runtime_error("Bad Spectrum and freq need to have same size");
	ss::vector_<chdb::spectral_peak_t> peaks;
	peaks.reserve(n);
	for(int i = n-1 ; i>=0; --i) {
		peaks.push_back(chdb::spectral_peak_t{(uint32_t) (pfreq[i]*1000), (uint32_t) psr[i],
				spectrum_key.pol});
	}
	auto subscription_id = subscriber.scan_spectral_peaks(rf_path, peaks, spectrum_key);
	return subscription_id;
}

static int scan_bands_on_sats(subscriber_t& subscriber, ss::vector_<chdb::sat_t>& sats,
															const devdb::tune_options_t& tune_options,
															const devdb::band_scan_options_t& band_scan_options) {
	using namespace chdb;

	for(int i=0; i < sats.size();++i) {
		auto& sat =  sats[i];
		auto [l, h] = sat_band_freq_bounds(sat.sat_band, sat_sub_band_t::NONE);
		l = band_scan_options.start_freq == -1 ? l : std::max(l, band_scan_options.start_freq);
		h = band_scan_options.end_freq == -1 ? h : std::max(h, band_scan_options.end_freq);
		if(h<=l) {
			sats.erase(i); //no overlap
			--i;
			continue;
		}
	}
	auto subscription_id = subscriber.scan_bands(sats, tune_options, band_scan_options);
	return subscription_id;
}


template<typename mux_t>
static int scan_muxes(subscriber_t& subscriber, const ss::vector_<mux_t>& muxes,
											const std::optional<devdb::tune_options_t>& tune_options) {
	return subscriber.scan_muxes(muxes, tune_options);
}

static int scan_muxes_on_sats(subscriber_t& subscriber, db_txn& chdb_rtxn,
															ss::vector_<chdb::sat_t>& sats,
															const devdb::tune_options_t& tune_options,
															const devdb::band_scan_options_t& band_scan_options) {

	using namespace chdb;
	using namespace devdb;
	assert(tune_options.subscription_type == subscription_type_t::MUX_SCAN);

	ss::vector<chdb::dvbs_mux_t,1> dvbs_muxes;
	ss::vector<chdb::dvbc_mux_t,1> dvbc_muxes;
	ss::vector<chdb::dvbt_mux_t,1> dvbt_muxes;

	for(auto sat: sats) {
		auto [l, h] =sat_band_freq_bounds(sat.sat_band, sat_sub_band_t::NONE);

		auto addmux = [&]<typename mux_t>(db_txn& chdb_rtxn, int sat_pos, ss::vector<mux_t,1>& mux_list, int l,
																			int h) {
			auto c = mux_t::find_by_key(chdb_rtxn, sat_pos, find_type_t::find_geq, mux_t::partial_keys_t::sat_pos);
			for(const auto& m : c.range()) {
				if((int)m.frequency >= l && (int)m.frequency <=h)
					mux_list.push_back(m);
			}
		};

		if(sat.sat_pos == sat_pos_dvbt)
			addmux(chdb_rtxn, sat.sat_pos, dvbt_muxes, l, h);
		else if (sat.sat_pos == sat_pos_dvbc)
			addmux(chdb_rtxn, sat.sat_pos, dvbc_muxes, l, h);
		else
			addmux(chdb_rtxn, sat.sat_pos, dvbs_muxes, l, h);
	}

	auto ret = 0;
	if(dvbs_muxes.size() > 0) {
		auto ret_ = subscriber.scan_muxes(dvbs_muxes, tune_options);
		if(ret_ < 0)
			ret = ret_;
		assert(ret_<0 || ret==ret_);
	}
	if(dvbc_muxes.size() > 0) {
		auto ret_ = subscriber.scan_muxes(dvbc_muxes, tune_options);
		if(ret_ < 0)
			ret = ret_;
		assert(ret_<0 || ret==ret_);
	}
	if(dvbt_muxes.size() > 0) {
		auto ret_ = subscriber.scan_muxes(dvbt_muxes, tune_options);
		if(ret_ < 0)
			ret = ret_;
		assert(ret_<0 || ret==ret_);
	}
	return ret;
}

extern void export_pls_search_range(nb::module_& m);
extern void export_playback_info(nb::module_& m);
extern void export_signal_info(nb::module_& m);
extern void export_sdt_data(nb::module_& m);
extern void export_scan_report(nb::module_& m);
extern void export_pls_search_range(nb::module_& m);
extern void export_position_motion_report(nb::module_& m);

void export_subscriber(nb::module_& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	export_signal_info(m);
	export_playback_info(m);
	export_pls_search_range(m);
	export_position_motion_report(m);
	export_sdt_data(m);
	export_scan_report(m);

	m.def("get_object", &get_object)
		.def("set_gtk_window_name", &set_gtk_window_name
				, "Set a gtk widget name for a wx window (needed for css styling)"
				, nb::arg("window")
				 , nb::arg("name"))
		.def("gtk_add_window_style", &gtk_add_window_style
				 , "Set a gtk widget style name for a wx window (needed for css styling)"
				 , nb::arg("window")
				 , nb::arg("name"))
		.def("gtk_remove_window_style", &gtk_remove_window_style
				, "Remove a gtk widget style name for a wx window (needed for css styling)"
				 , nb::arg("window")
				 , nb::arg("name"))
		.def("global_subscriber", &get_global_subscriber
				 ,"Connect to the global subscriber to catch non-subscriber specific error messages"
				 , nb::arg("receiver")
				 , nb::arg("window")
			)
		;
	nb::class_<subscriber_t>(m, "subscriber_t")
		.def(nb::new_(&make_subscriber))
		.def("update_current_lnb"
				 , &subscriber_t::update_current_lnb
				 , "Update and save the current lnb"
				 , nb::arg("lnb"))
		.def("subscribe_lnb"
				 , &subscriber_t::subscribe_lnb
				 , "Subscribe to a specific lnb without (re)tuning"
				 , nb::arg("rf_path")
				 , nb::arg("lnb")
				 , nb::arg("retune_mode"))
		.def("subscribe_mux"
				 , &subscriber_t::subscribe_mux
				 , "Subscribe to a specific mux using a specific lnb"
				 , nb::arg("mux")
				 , nb::arg("blindscan"))
		.def("subscribe_lnb_and_mux"
				 , &subscriber_t::subscribe_lnb_and_mux
				 , "Subscribe to a specific mux using a specific lnb"
				 , nb::arg("rf_path")
				 , nb::arg("lnb")
				 , nb::arg("mux")
				 , nb::arg("blindscan")
				 , nb::arg("pls_search_mode")=false
				 , nb::arg("retune_mode"))
		.def("scan_spectral_peaks", &scan_spectral_peaks,
				 "scan peaks in the spectrum all at once",
				 nb::arg("rf_path"),
				 nb::arg("spectrum_key"), nb::arg("peak_freq"), nb::arg("peak_sr")
			)
		.def("scan_muxes", &scan_muxes<chdb::dvbs_mux_t>, "scan muxes"
				 , nb::arg("muxes")
				 , nb::arg("tune_options")=nullptr
			)
		.def("scan_muxes", &scan_muxes<chdb::dvbc_mux_t>, "scan muxes"
				 , nb::arg("muxes")
				 , nb::arg("tune_options")=nullptr
			)
		.def("scan_muxes", &scan_muxes<chdb::dvbt_mux_t>, "scan muxes"
				 , nb::arg("muxes")
				 , nb::arg("tune_options")=nullptr
			)
		.def("scan_muxes_on_sats", &scan_muxes_on_sats
				 , "scan all muxes on selected sats"
				 , nb::arg("chdb_rtxn")
				 , nb::arg("sats")
				 , nb::arg("tune_options")
				 , nb::arg("band_scan_options")
			)
		.def("scan_bands_on_sats", &scan_bands_on_sats
				 , "acquire spectra and then scan peaks for selected sats"
				 , nb::arg("sats")
				 , nb::arg("tune_options")
				 , nb::arg("band_scan_options")
			)
		.def_prop_ro("error_message", [](subscriber_t* self) {
			return get_error().c_str(); })
		.def("unsubscribe"
				 , &subscriber_t::unsubscribe
				 , "End tuning",
				 nb::arg("wait") = false)
		.def("subscribe_spectrum_acquisition"
				 , &subscriber_t::subscribe_spectrum_acquisition
				 , "acquire a spectrum for this lnb"
				 , nb::arg("rf_path")
				 , nb::arg("lnb")
				 , nb::arg("pol to scan")
				 , nb::arg("start_freq")
				 , nb::arg("end_freq")
				 , nb::arg("sat")
			)
		.def("positioner_cmd"
				 , &subscriber_t::positioner_cmd
				 , "send positioner_cmd"
				 , nb::arg("cmd")
				 , nb::arg("par")=0
			)
		;
}
