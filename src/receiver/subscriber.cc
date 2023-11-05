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

//#include <Python.h>
#include "receiver/subscriber.h"
#include "active_adapter.h"
#include "receiver/receiver.h"
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/window.h>

subscriber_t::subscriber_t(receiver_t* receiver_, wxWindow* window_) : receiver(receiver_), window(window_)
{
	owner = getpid();
}

std::shared_ptr<subscriber_t> subscriber_t::make(receiver_t* receiver, wxWindow* window) {
	auto ret = std::make_shared<subscriber_t>(receiver, window);
	receiver->subscribers.writeAccess()->insert({ret.get(), ret});
	return ret;
}

subscriber_t::~subscriber_t() {
	if ((int) subscription_id >= 0)
		unsubscribe();
}

std::unique_ptr<playback_mpm_t> subscriber_t::subscribe_service(const chdb::service_t& service) {
	auto mpm = receiver->subscribe_service(service, subscription_id);
	if (!mpm.get()) {
		subscription_id = subscription_id_t{-1};
		notify_error(get_error());
	} else {
		subscription_id = mpm->subscription_id;
		assert((int)subscription_id  >=0);
	}
	return mpm;
}

template <typename _mux_t>
int subscriber_t::subscribe_mux(const _mux_t& mux, bool blindscan)
{
	auto ret = receiver->subscribe_mux(mux, blindscan, subscription_id);
	if((int) ret<0) {
		assert((int) subscription_id<0);
		return (int) ret;
	}
	subscription_id = ret;
	return (int) subscription_id;
}

int subscriber_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, retune_mode_t retune_mode) {
	subscription_id = (subscription_id_t)
		receiver->subscribe_lnb(rf_path, lnb, retune_mode, subscription_id);
	return (int) subscription_id;
}

int subscriber_t::subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																				const chdb::dvbs_mux_t& mux, bool blindscan,
																				const pls_search_range_t& pls_search_range, retune_mode_t retune_mode) {
	subscription_id = receiver->subscribe_lnb_and_mux(rf_path, lnb, mux, blindscan, pls_search_range, retune_mode,
																										subscription_id);
	return (int) subscription_id;
}

int subscriber_t::scan_bands(const ss::vector_<chdb::sat_t>& sats,
														 const ss::vector_<chdb::fe_polarisation_t>& pols,
														 int32_t low_freq, int32_t high_freq) {
	{
		auto w = notification.writeAccess();
		auto & n = *w;
		n.sat_pos = sat_pos_none;
		n.rf_path = {};
		//todo: allow multiple scans on different sats/lnbs
	}

	auto tune_options = receiver->get_default_tune_options(true /*for scan*/);
	tune_options.spectrum_scan_options.start_freq = low_freq;
	tune_options.spectrum_scan_options.end_freq = high_freq;
	subscription_id_t ret{subscription_id};
	ret = receiver->scan_bands(sats, pols, tune_options, subscription_id);
	assert(ret==subscription_id); //subscription_id is passed by reference
	return (int)subscription_id;

}

int subscriber_t::scan_spectral_peaks(ss::vector_<chdb::spectral_peak_t>& peaks,
																			const statdb::spectrum_key_t& spectrum_key) {
	{
		auto w = notification.writeAccess();
		auto & n = *w;
		n.sat_pos = spectrum_key.sat_pos;
		n.rf_path = spectrum_key.rf_path;
		//todo: allow multiple scans on different sats/lnbs
	}
	subscription_id = receiver->scan_spectral_peaks(peaks, spectrum_key, subscription_id);
	return (int)subscription_id;
}

int subscriber_t::scan_muxes(ss::vector_<chdb::dvbs_mux_t> dvbs_muxes,
														 ss::vector_<chdb::dvbc_mux_t> dvbc_muxes,
														 ss::vector_<chdb::dvbt_mux_t> dvbt_muxes,
														 const std::optional<tune_options_t>& tune_options_) {
	{
		auto w = notification.writeAccess();
		auto & n = *w;
		n.sat_pos = sat_pos_none;
		n.rf_path = {};
		//todo: allow multiple scans on different sats/lnbs
	}

	auto& tune_options = tune_options_ ? *tune_options_ :
		receiver->get_default_tune_options(true /*for scan*/);

	subscription_id_t ret{subscription_id};
	if(dvbs_muxes.size() > 0) {
		ret = receiver->scan_muxes(dvbs_muxes, tune_options, subscription_id);
		assert(ret==subscription_id); //subscription_id is passed by reference
		if((int) ret<0)
			return (int) ret;
	}
	assert(ret == subscription_id || (int) subscription_id == -1);
	if(dvbc_muxes.size() > 0) {
		ret = receiver->scan_muxes(dvbc_muxes, tune_options, ret);
		assert(ret==subscription_id); //subscription_id is passed by reference
		if((int) ret<0)
			return (int) ret;
	}
	assert(ret == subscription_id || (int) subscription_id == -1);
	if(dvbt_muxes.size() > 0) {
		subscription_id = receiver->scan_muxes(dvbt_muxes, tune_options, subscription_id);
		assert(ret==subscription_id); //subscription_id is passed by reference
		if((int) ret<0)
			return (int) ret;
	}
	assert(ret == subscription_id || (int) subscription_id == -1);
	return (int)subscription_id;
}

std::unique_ptr<playback_mpm_t> subscriber_t::subscribe_recording(const recdb::rec_t& rec) {
	auto mpm = 		receiver->subscribe_playback(rec, subscription_id);
	if (!mpm.get()) {
		subscription_id = subscription_id_t::NONE;
	} else {
		subscription_id = mpm->subscription_id;
	}
	return mpm;
}

void subscriber_t::update_current_lnb(const devdb::lnb_t& lnb) {
	//call by reference ok because of subsequent wait_for_all
	auto subscription_id = this->subscription_id;
	auto aa = receiver->find_active_adapter(subscription_id);
	if(aa) {
		bool usals_pos_changed{false};
		auto& tuner_thread = aa->tuner_thread;
		tuner_thread.push_task([&tuner_thread, subscription_id, lnb, &usals_pos_changed]() {
			usals_pos_changed = cb(tuner_thread).update_current_lnb(subscription_id, lnb);
			return 0;
		}).wait();
	}
}

int subscriber_t::unsubscribe() {
	// auto d = safe_data.writeAccess();
	if((int) subscription_id<0) {
		dtdebugf("ignoring unubscribe (subscription_id<0)");
		return -1;
	}
	dtdebugf("calling receiver->unsubscribe");
	subscription_id = receiver->unsubscribe(subscription_id);
	assert((int) subscription_id < 0);
	dtdebugf("calling receiver->unsubscribe -1");

	auto mp = receiver->subscribers.writeAccess();
	auto& m = *mp;
	auto it = m.find(this);
	if (it != m.end()) {
#ifndef NDEBUG
		dtdebugf("Erasing subscription window={:p} subscription_id={:d}", fmt::ptr(window), (int)subscription_id);
		int num_erased = m.erase(this);
#pragma unused(num_erased)
		assert(num_erased == 1);
#endif
	}
	dtdebugf("calling receiver->unsubscribe -2");
	return (int) subscription_id;
}

int subscriber_t::positioner_cmd(devdb::positioner_cmd_t cmd, int par) {
	auto& receiver_thread = receiver->receiver_thread;
	int ret = -1;
	receiver_thread //call by reference ok because of subsequent wait_for_all
		.push_task([subscription_id = this->subscription_id,
								&receiver_thread, cmd, par, &ret]() { // epg_record passed by value
			ret = cb(receiver_thread).positioner_cmd(subscription_id, cmd, par);
			return 0;
		})
		.wait();
	return ret;
}

int subscriber_t::subscribe_spectrum(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																		 chdb::fe_polarisation_t pol, int32_t low_freq,
																		 int32_t high_freq, int sat_pos) {

	subscription_id = receiver->subscribe_lnb_spectrum(rf_path, lnb, pol, low_freq, high_freq, sat_pos,
																										 subscription_id);
	if ((int)subscription_id < 0)
		notify_error(get_error());
	return (int) subscription_id;
}

void subscriber_t::notify_signal_info(const signal_info_t& signal_info) const {
	if (!(event_flag & int(subscriber_t::event_type_t::SIGNAL_INFO)))
		return;
	notify(signal_info);
}

void subscriber_t::notify_signal_info(const signal_info_t& signal_info,
																			const ss::vector_<subscription_id_t>& subscription_ids) const {
	if (!(event_flag & int(subscriber_t::event_type_t::SIGNAL_INFO)))
		return;
	if(subscription_ids.contains(subscription_id)) {
		/* GUI tuned to a specific mux, e.g., positioner_dialog*/
		notify(signal_info);
	}
}

void subscriber_t::notify_scan_progress(const scan_stats_t& scan_stats, bool is_start) {
	auto match = is_start ? subscriber_t::event_type_t::SCAN_START
		: subscriber_t::event_type_t::SCAN_PROGRESS;
	if (!(event_flag & int(match)))
		return;
	notify(scan_stats);
}

void subscriber_t::notify_scan_mux_end(const scan_mux_end_report_t& report) {
	if (!(event_flag & int(subscriber_t::event_type_t::SCAN_MUX_END)))
		return;
	notify(report);
}

void subscriber_t::notify_sdt_actual(const sdt_data_t& sdt_data,
																		 const ss::vector_<subscription_id_t>& subscription_ids) const
{
	if (!(event_flag & int(subscriber_t::event_type_t::SDT_ACTUAL)))
		return;
	if(subscription_ids.contains(subscription_id)) {
		/* GUI tuned to a specific mux, e.g., positioner_dialog*/
		notify(sdt_data);
	}
}

void subscriber_t::notify_sdt_actual(const sdt_data_t& sdt_data) const
{
	if (!(event_flag & int(subscriber_t::event_type_t::SDT_ACTUAL)))
		return;
	notify(sdt_data);
}

void subscriber_t::notify_error(const ss::string_& errmsg) {
	if (!(event_flag & int(subscriber_t::event_type_t::ERROR_MSG)))
		return;
	auto temp = std::string(errmsg);
	notify(temp);
}

void subscriber_t::notify_spectrum_scan(const statdb::spectrum_t& spectrum,
																				const ss::vector_<subscription_id_t>& subscription_ids) {
	if (!(event_flag & int(subscriber_t::event_type_t::SPECTRUM_SCAN)))
		return;
	if(subscription_ids.contains(subscription_id)) {
		notify(spectrum);
	}
}



template
int subscriber_t::subscribe_mux<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux, bool blindscan);


template
int subscriber_t::subscribe_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, bool blindscan);


template
int subscriber_t::subscribe_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, bool blindscan);
