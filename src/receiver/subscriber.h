/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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

#pragma once

#include "neumo.h"
#include "task.h"
//#include "simgr/simgr.h"
#include "util/access.h"
#include "devmanager.h"
#include "options.h"
#include "recmgr.h"
#include "mpm.h"
#include "signal_info.h"
#include "streamparser/packetstream.h"
#include "streamparser/psi.h"
#include "util/safe/safe.h"
#include "scan.h"

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

struct wxWindow;
struct spectrum_scan_t;

namespace pybind11 {
	class object;
};

struct blindscan_t;
struct sdt_data_t;

class subscriber_t
{
	pid_t owner;
	subscription_id_t subscription_id{-1};
	receiver_t *receiver;
	wxWindow* window{nullptr}; //window which will receive notifications
	std::atomic<bool> scanning_{false}; //subscriber is scanning
public:
	const int command_id{-1}; //non-zero if we are running a command

	enum class event_type_t : uint32_t {
		ERROR_MSG  = (1<<0),
		SIGNAL_INFO = (1<<1),
		SPECTRUM_SCAN = (1<<2),
		//SCAN_START = (1<<3),
		SCAN_PROGRESS = (1<<4),
		SCAN_MUX_END = (1<<5),
		SDT_ACTUAL = (1<<6)
	};
	int event_flag{
		int(event_type_t::ERROR_MSG) |
		//int(event_type_t::SCAN_START) |
		int(event_type_t::SCAN_PROGRESS) |
		int(event_type_t::SCAN_MUX_END) |
		int(event_type_t::SDT_ACTUAL) |
		int(event_type_t::SIGNAL_INFO) |
		int(event_type_t::SPECTRUM_SCAN)}; //which events to report

	inline bool is_scanning() const {
		return scanning_;
	}

	inline void set_scanning(bool val) {
		scanning_ = val;
	}

	inline subscription_id_t get_subscription_id() const {
		return subscription_id;
	}
	void set_initial_subscription_id(subscription_id_t subscription_id) {
		assert(this->subscription_id == subscription_id_t::NONE || this->subscription_id == subscription_id);
		this->subscription_id = subscription_id;
	}
	template<typename T> void notify(const T& data) const;
	EXPORT static pybind11::object handle_to_py_object(int64_t handlle);

	void notify_error(const ss::string_& errmsg);
	void notify_scan_progress(const devdb::scan_stats_t& scan_stats);
	void notify_scan_mux_end(const scan_mux_end_report_t& report);
	void notify_sdt_actual(const sdt_data_t& sdt_data) const;
	void notify_signal_info(const signal_info_t& info) const;
	void notify_spectrum_scan_band_end(const statdb::spectrum_t& spectrum);

	EXPORT subscriber_t(receiver_t* receiver, wxWindow* window,
											subscription_id_t  subscription_id=subscription_id_t::NONE,
											int command_id=-1);
	EXPORT static std::shared_ptr<subscriber_t> make(receiver_t * receiver, wxWindow* window,
																									 subscription_id_t subscription_id=subscription_id_t::NONE,
																									 int command_id =-1);

	EXPORT ~subscriber_t();

	EXPORT int unsubscribe();
	EXPORT void update_current_lnb(const devdb::lnb_t & lnb);

	EXPORT std::unique_ptr<playback_mpm_t> subscribe_service(const chdb::service_t& service);

	template <typename _mux_t>
	EXPORT int subscribe_mux(const _mux_t& mux, bool blindscan);

	EXPORT int subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, devdb::retune_mode_t retune_mode);

	EXPORT int subscribe_lnb_and_mux(
		devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
		const pls_search_range_t& pls_search_range, devdb::retune_mode_t retune_mode);
	EXPORT int subscribe_spectrum_acquisition(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,  chdb::fe_polarisation_t pol,
																						int32_t low_freq, int32_t high_freq,
																						const chdb::sat_t& sat);

	EXPORT int scan_bands(const ss::vector_<chdb::sat_t>& sats,
												const std::optional<devdb::tune_options_t>& tune_options,
												const devdb::band_scan_options_t& band_scan_options);

	EXPORT int scan_spectral_peaks(ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key);

	template<typename mux_t>
	EXPORT int scan_muxes(const ss::vector_<mux_t> muxes, const std::optional<devdb::tune_options_t>& tune_options);

	EXPORT std::tuple<int, std::optional<int>>
	positioner_cmd(devdb::positioner_cmd_t cmd, int par);

	EXPORT std::unique_ptr<playback_mpm_t> subscribe_recording(const recdb::rec_t& rec);

};
