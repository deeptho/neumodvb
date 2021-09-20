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

//#include <Python.h>
#include "receiver/subscriber.h"
#include "active_adapter.h"
#include "receiver/receiver.h"
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/window.h>

mux_subscriber_t::mux_subscriber_t(receiver_t* receiver_, wxWindow* window_) : receiver(receiver_), window(window_) {}

std::shared_ptr<mux_subscriber_t> mux_subscriber_t::make(receiver_t* receiver, wxWindow* window) {
	auto ret = std::make_shared<mux_subscriber_t>(receiver, window);
	receiver->mux_subscribers.writeAccess()->insert({ret.get(), ret});
	return ret;
}

mux_subscriber_t::~mux_subscriber_t() {
	if (subscription_id >= 0)
		unsubscribe();
}

int mux_subscriber_t::subscribe_lnb_and_mux(chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
																						const pls_search_range_t& pls_search_range, retune_mode_t retune_mode) {
	subscription_id =
		receiver->subscribe_lnb_and_mux(lnb, mux, blindscan, pls_search_range, retune_mode, subscription_id);
	active_adapter = receiver->active_adapter_for_subscription(subscription_id);
	return subscription_id < 0 ? -1 : ++tune_attempt;
}

void mux_subscriber_t::update_current_lnb(const chdb::lnb_t& lnb) {
	auto& tuner_thread = receiver->tuner_thread;
	if (!active_adapter)
		return; // can happen when called from gui
	auto& aa = *active_adapter;
	tuner_thread.push_task([&lnb, &tuner_thread, &aa]() { return cb(tuner_thread).update_current_lnb(aa, lnb); }).wait();
}

int mux_subscriber_t::unsubscribe() {
	// auto d = safe_data.writeAccess();
	subscription_id = receiver->unsubscribe(subscription_id);
	active_adapter.reset();
	assert(subscription_id < 0);
#ifndef NDEBUG
	auto mp = receiver->mux_subscribers.writeAccess();
	auto& m = *mp;
	auto it = m.find(this);
	if (it != m.end()) {
		int num_erased = m.erase(this);
#pragma unused(num_erased)
		assert(num_erased == 1);
	}
#endif
	return subscription_id;
}

int mux_subscriber_t::positioner_cmd(chdb::positioner_cmd_t cmd, int par) {
	if (!active_adapter)
		return -1;
	auto& tuner_thread = receiver->tuner_thread;
	int ret = -1;
	tuner_thread
		.push_task([this, &tuner_thread, cmd, par, &ret]() { // epg_record passed by value
			ret = cb(tuner_thread).positioner_cmd(active_adapter, cmd, par);
			return 0;
		})
		.wait();
	return ret;
}

int mux_subscriber_t::subscribe_spectrum(chdb::lnb_t& lnb, chdb::fe_polarisation_t pol, int32_t low_freq,
																				 int32_t high_freq, int sat_pos) {
	active_adapter.reset();

	subscription_id = receiver->subscribe_lnb_spectrum(lnb, pol, low_freq, high_freq, sat_pos, subscription_id);
	active_adapter = receiver->active_adapter_for_subscription(subscription_id);
	return subscription_id;
}

int mux_subscriber_t::get_adapter_no() const { return active_adapter ? active_adapter->get_adapter_no() : -1; }

void mux_subscriber_t::notify_signal_info(const chdb::signal_info_t& info) {
	if (active_adapter && active_adapter->get_adapter_no() == info.stat.lnb_key.adapter_no) {
		auto temp = info;
		temp.tune_attempt = tune_attempt;
		notify(temp);
	}
}

void mux_subscriber_t::notify_spectrum_scan(const statdb::spectrum_t& spectrum) {
	if (active_adapter && active_adapter->get_adapter_no() == spectrum.k.lnb_key.adapter_no) {
		notify(spectrum);
	}
}
