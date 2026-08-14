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

//#include <Python.h>
#include "receiver/subscriber.h"
#include "active_adapter.h"
#include "receiver/receiver.h"
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/window.h>

subscriber_t::subscriber_t(receiver_t* receiver_, wxWindow* window_)
	: receiver(receiver_)
	, window(window_)
{
	owner = getpid();
}

std::shared_ptr<subscriber_t> subscriber_t::make(receiver_t* receiver, wxWindow* window) {
	auto ret = std::make_shared<subscriber_t>(receiver, window);
	receiver->subscribers.writeAccess()->insert({ret.get(), ret});
	return ret;
}

subscriber_t::~subscriber_t() {
	if ((int) get_subscription_id() >= 0)
		unsubscribe(true /*wait*/);
}

std::unique_ptr<playback_mpm_t> subscriber_t::subscribe_service_for_viewing(const chdb::service_t& service) {
	clear_error();
	auto ssptr = this->shared_from_this();
	auto mpm = receiver->subscribe_service_for_viewing(service, ssptr);
	if (!mpm.get()) {
		notify_message(get_error());
	} else {
		assert(get_subscription_id() == mpm->subscription_id);
		assert((int)mpm->subscription_id  >=0);
	}
	return mpm;
}


int subscriber_t::subscribe_mux(const chdb::any_mux_t& mux, bool blindscan)
{
	clear_error();
	auto ssptr = this->shared_from_this();
	auto ret = receiver->subscribe_mux(mux, blindscan, ssptr);
	if((int) ret<0) {
		assert(ret == get_subscription_id() || (int)ret<0);
		return (int) ret;
	}
	return (int) ret;
}

int subscriber_t::subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, devdb::retune_mode_t retune_mode) {
	clear_error();
	auto ssptr = this->shared_from_this();
	auto ret =  receiver->subscribe_lnb(rf_path, lnb, retune_mode, ssptr);
	return (int) ret;
}

int subscriber_t::subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																				chdb::dvbs_mux_t& mux, bool blindscan,
																				const pls_search_range_t& pls_search_range,
																				devdb::retune_mode_t retune_mode) {
	clear_error();
	if(!is_template(mux) &&  (chdb::mux_key_ptr(mux)->mux_id ==0))
		mux.c.tune_src = chdb::tune_src_t::TEMPLATE;

#ifndef NDEBUG
	assert(is_template(mux) == (chdb::mux_key_ptr(mux)->mux_id ==0));
#endif
	auto ssptr = this->shared_from_this();
	auto ret = receiver->subscribe_lnb_and_mux(rf_path, lnb, mux, blindscan, pls_search_range, retune_mode, ssptr);
	return (int) ret;
}

int subscriber_t::scan_bands(const ss::vector_<chdb::sat_t>& sats,
														 const std::optional<devdb::tune_options_t>& tune_options_,
														 const devdb::band_scan_options_t& band_scan_options) {
	clear_error();
	set_scanning(true);
	auto so = receiver->get_default_subscription_options(devdb::subscription_type_t::BAND_SCAN);
	if(tune_options_)
		(devdb::tune_options_t&)so  = *tune_options_;
	so.spectrum_scan_options = receiver->get_default_spectrum_scan_options
		(devdb::subscription_type_t::BAND_SCAN);
	so.need_spectrum = true;
	so.spectrum_scan_options.recompute_peaks = true;
	so.spectrum_scan_options.start_freq = band_scan_options.start_freq;
	so.spectrum_scan_options.end_freq = band_scan_options.end_freq;
	auto ssptr = this->shared_from_this();
	auto ret = receiver->scan_bands(sats, band_scan_options.pols, so, ssptr);
	assert(ret == get_subscription_id() || (int) ret == -1);
	return (int) ret;
}

int subscriber_t::scan_spectral_peaks(const devdb::rf_path_t& rf_path, ss::vector_<chdb::spectral_peak_t>& peaks,
																			const statdb::spectrum_key_t& spectrum_key) {
	clear_error();
	set_scanning(true);
	auto ssptr = this->shared_from_this();
	auto ret = receiver->scan_spectral_peaks(rf_path, peaks, spectrum_key, ssptr);
	assert (ret==get_subscription_id() || ret == subscription_id_t::TUNE_FAILED);
	return (int)ret;
}

template<typename mux_t>
int subscriber_t::scan_muxes(ss::vector_<mux_t> muxes,
														 const std::optional<devdb::tune_options_t>& tune_options_) {
	clear_error();
	set_scanning(true);
	auto so = receiver->get_default_subscription_options(devdb::subscription_type_t::MUX_SCAN);
	if(tune_options_)
		(devdb::tune_options_t&)so = *tune_options_;
	auto& tune_options = tune_options_ ? *tune_options_ :
		receiver->get_default_subscription_options(devdb::subscription_type_t::MUX_SCAN);
	auto ssptr = this->shared_from_this();
	if(muxes.size() > 0) {
		auto ret = receiver->scan_muxes(muxes, so, ssptr);
		assert(ret == get_subscription_id() || (int)ret<0);
		return (int) ret;
	} else {
		user_errorf("No muxes to scan");
		return -1;
	}
}

std::unique_ptr<playback_mpm_t> subscriber_t::subscribe_recording(const recdb::rec_t& rec) {
	clear_error();
	auto ssptr = this->shared_from_this();
	auto mpm = 		receiver->subscribe_playback(rec, ssptr);
	if (!mpm.get()) {
		assert((int)get_subscription_id() < 0);
	} else {
		assert(get_subscription_id() == mpm->subscription_id);
	}
	return mpm;
}

void subscriber_t::update_current_lnb(const devdb::lnb_t& lnb) {
	//call by reference ok because of subsequent wait_for_all
	auto subscription_id = this->get_subscription_id();
	auto aa = receiver->find_active_adapter(subscription_id);
	if(aa) {
		bool usals_pos_changed{false};
		auto& tuner_thread = aa->tuner_thread;
		tuner_thread.push_task([&tuner_thread, subscription_id, lnb, &usals_pos_changed]() {
			usals_pos_changed = cb(tuner_thread).update_current_lnb_from_gui(subscription_id, lnb);
			return 0;
		}).wait();
	}
}

void subscriber_t::remove_ssptr() {
	auto w = receiver->subscribers.writeAccess();
	auto& m = *w;
	auto it = m.find(this);
	assert(it!=m.end());
	auto subscription_id = this->get_subscription_id();
	auto* window = this->window;
	assert((int)subscription_id <0);
	if (it != m.end()) {
		m.erase(it);
		dtdebugf("Erasing subscription window={:p} subscription_id={:d} len={}", fmt::ptr(window),
						 (int)subscription_id, m.size());
	}
}

int subscriber_t::unsubscribe(bool wait) {
	clear_error();
	set_scanning(false);
	auto subscription_id = this->get_subscription_id();
	if((int) subscription_id<0) {
		dtdebugf("ignoring unubscribe (subscription_id<0)");
		return -1;
	}
	dtdebugf("calling receiver->unsubscribe");
	auto ssptr = this->shared_from_this();
	receiver->unsubscribe(ssptr, wait);
	dtdebugf("called receiver->unsubscribe");
	return (int) subscription_id;
}

std::tuple<int, std::optional<int>>
subscriber_t::positioner_cmd(devdb::positioner_cmd_t cmd, int par) {
	clear_error();
	auto& receiver_thread = receiver->receiver_thread;
	int ret = -1;
	std::optional<int> new_usals_pos;
	receiver_thread //call by reference ok because of subsequent wait_for_all
		.push_task([this, &receiver_thread, cmd, par, &ret, &new_usals_pos]() { // epg_record passed by value
			auto ssptr = this->shared_from_this();
			std::tie(ret, new_usals_pos) = cb(receiver_thread).positioner_cmd(ssptr, cmd, par);
			return 0;
		})
		.wait();
	return {ret, new_usals_pos};
}

int subscriber_t::subscribe_spectrum_acquisition(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																		 chdb::fe_polarisation_t pol, int32_t low_freq,
																								 int32_t high_freq, const chdb::sat_t& sat) {
	clear_error();
	set_scanning(false);
	auto ssptr = this->shared_from_this();
	auto ret = receiver->subscribe_lnb_spectrum(rf_path, lnb, pol, low_freq, high_freq, sat,
																										 ssptr);
	if ((int)ret < 0)
		notify_message(get_error());
	return (int) ret;
}

template int subscriber_t::scan_muxes(ss::vector_<chdb::dvbs_mux_t> muxes,
																			const std::optional<devdb::tune_options_t>& tune_options_);

template int subscriber_t::scan_muxes(ss::vector_<chdb::dvbc_mux_t> muxes,
																			const std::optional<devdb::tune_options_t>& tune_options_);

template int subscriber_t::scan_muxes(ss::vector_<chdb::dvbt_mux_t> muxes,
																			const std::optional<devdb::tune_options_t>& tune_options_);

fmt::format_context::iterator
fmt::formatter<ssptr_t>::format(const ssptr_t& ssptr, format_context& ctx) const {
	return ssptr.get() ?
		fmt::format_to(ctx.out(), "SUB{}", (int) ssptr->get_subscription_id() ) :
		fmt::format_to(ctx.out(), "NOSUB");
}
