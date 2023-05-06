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
struct wxWindow;
struct spectrum_scan_t;

namespace pybind11 {
	class object;
};

struct blindscan_t;
struct sdt_data_t;

struct notification_t {
	int16_t sat_pos{sat_pos_none};
	devdb::rf_path_t rf_path;

	inline bool matches( int16_t sat_pos, const devdb::rf_path_t& rf_path) const {
		return sat_pos == this->sat_pos && rf_path == this->rf_path;
	}
};

class subscriber_t
{
	pid_t owner;
	subscription_id_t subscription_id{-1};
	receiver_t *receiver;
	wxWindow* window{nullptr}; //window which will receive notifications
	std::weak_ptr<active_adapter_t> active_adapter; //set if subscribed to specific mux
public:
	enum class event_type_t : uint32_t {
		ERROR_MSG  = (1<<0),
		SIGNAL_INFO = (1<<1),
		SPECTRUM_SCAN = (1<<2),
		SCAN_START = (1<<3),
		SCAN_MUX_END = (1<<4),
		SDT_ACTUAL = (1<<5)
	};

	safe::Safe<notification_t> notification;
	int event_flag{
		int(event_type_t::ERROR_MSG) |
		int(event_type_t::SCAN_START) |
		int(event_type_t::SCAN_MUX_END) |
		int(event_type_t::SDT_ACTUAL) |
		int(event_type_t::SIGNAL_INFO) |
		int(event_type_t::SPECTRUM_SCAN)}; //which events to report

	inline subscription_id_t get_subscription_id() const {
		return subscription_id;
	}
	template<typename T> void notify(const T& data) const;
	static pybind11::object handle_to_py_object(int64_t handlle);

	void notify_error(const ss::string_& errmsg);
	void notify_scan_start(const scan_stats_t& scan_stats);
	void notify_scan_mux_end(const scan_report_t& report);
	void notify_sdt_actual(const sdt_data_t& sdt_data, dvb_frontend_t* fe, bool from_scanner) const;
	void notify_signal_info(const signal_info_t& info, bool from_scanner) const;
	void notify_spectrum_scan(const statdb::spectrum_t& spectrum);

	subscriber_t(receiver_t* receiver, wxWindow* window);
	static std::shared_ptr<subscriber_t> make(receiver_t * receiver, wxWindow* window);

	~subscriber_t();

	int unsubscribe();
	void update_current_lnb(const devdb::lnb_t & lnb);

	std::unique_ptr<playback_mpm_t> subscribe_service(const chdb::service_t& service);

	template <typename _mux_t>
	int subscribe_mux(const _mux_t& mux, bool blindscan);

	int subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, retune_mode_t retune_mode);
	int subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
														const pls_search_range_t& pls_search_range, retune_mode_t retune_mode);
	int subscribe_spectrum(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,  chdb::fe_polarisation_t pol,
												 int32_t low_freq, int32_t high_freq,
												 int sat_pos=sat_pos_none);

	int scan_spectral_peaks(ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key);
	int scan_muxes(const ss::vector_<chdb::dvbs_mux_t> dvbs_muxes,
								 const ss::vector_<chdb::dvbc_mux_t> dvbc_muxes,
								 const ss::vector_<chdb::dvbt_mux_t> dvbt_muxes);

	int positioner_cmd(devdb::positioner_cmd_t cmd, int par);
	int get_adapter_no() const;

	std::unique_ptr<playback_mpm_t> subscribe_recording(const recdb::rec_t& rec);

};
