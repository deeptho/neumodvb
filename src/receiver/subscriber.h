/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
struct player_cb_t;

class subscriber_t : public std::enable_shared_from_this<subscriber_t>
{

	struct thread_safe_t {
		subscription_id_t subscription_id{subscription_id_t::NONE};
		int command_id{-1};
		std::shared_ptr<active_playback_t> active_playback;
		std::shared_ptr<active_playback_t> streamer;
	};

	std::shared_ptr<player_cb_t> mpv; //cannot yeet be moved into thread safe because called by gui
	pid_t owner;
	/*
		subscription_id can be set/reset only from receiver thread
	 */
	safe::thread_public_t<false /*others_may_write*/,
												thread_safe_t> ts{"receiver", thread_group_t::receiver, {}};
	receiver_t *receiver;
	wxWindow* window{nullptr}; //window which will receive notifications
	std::atomic<bool> scanning_{false}; //subscriber is scanning
	std::atomic<int> stream_id_{-1}; //subscriber is streaming

public:

	enum class event_type_t : uint32_t {
		ERROR_MSG  = (1<<0),
		SIGNAL_INFO = (1<<1),
		SPECTRUM_SCAN = (1<<2),
		POSITIONER_MOTION = (1<<3),
		SCAN_PROGRESS = (1<<4),
		SCAN_MUX_END = (1<<5),
		SDT_ACTUAL = (1<<6)
	};
	int event_flag{
		int(event_type_t::ERROR_MSG) |
		int(event_type_t::POSITIONER_MOTION) |
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

	inline bool is_streaming() const {
		return stream_id_ >=0;
	}

	inline int get_stream_id() const {
		return stream_id_;
	}

	inline void set_stream_id(int stream_id) {
		stream_id_ = stream_id;
	}

	inline subscription_id_t get_subscription_id() const {
		auto r = ts.readAccess();
		return r->subscription_id;
	}

	inline void set_subscription_id(subscription_id_t subscription_id) {
		auto w = this->ts.writeAccess();
		w->subscription_id = subscription_id;
	}

	inline void clear_subscription_id() {
		auto w = this->ts.writeAccess();
		w->subscription_id = subscription_id_t::NONE;
	}

	inline void remove_active_playback() {
		auto w = this->ts.writeAccess();
		if(w->active_playback) {
			w->subscription_id = subscription_id_t::NONE;
			w->active_playback.reset();
		}
	}

	inline void set_active_playback(const std::shared_ptr<active_playback_t>& active_playback) {
		auto w = this->ts.writeAccess();
		assert(!w->active_playback);
		w->active_playback = active_playback;
	}

	inline std::shared_ptr<active_playback_t> get_active_playback() const {
		auto r = this->ts.readAccess();
		return r->active_playback;
	}

	inline void set_mpv(const std::shared_ptr<player_cb_t>& mpv) {
		assert(!this->mpv);
		this->mpv = mpv;
	}

	inline std::shared_ptr<player_cb_t> get_mpv() const {
		return this->mpv;
	}

	inline void remove_mpv() {
		if(this->mpv) {
			this->mpv.reset();
		}
	}

	inline int get_command_id() const {
		auto r = ts.readAccess();
		return r->command_id;
	}

	inline void set_command_id(int command_id) {
		auto w = this->ts.writeAccess();
		w->command_id = command_id;
	}

	//thread safe but is only allowed to be called from receiver_thread
	void remove_ssptr();

	template<typename T> void notify(const T& data) const;
	EXPORT static pybind11::object handle_to_py_object(int64_t handlle);

	void notify_error(const ss::string_& errmsg);
	void notify_scan_progress(const devdb::scan_stats_t& scan_stats);
	void notify_scan_mux_end(const scan_mux_end_report_t& report);
	void notify_sdt_actual(const sdt_data_t& sdt_data) const;
	void notify_signal_info(const signal_info_t& info) const;
	void notify_positioner_motion(const positioner_motion_report_t& motion_report) const;
	void notify_spectrum_scan_band_end(const statdb::spectrum_t& spectrum);

	EXPORT subscriber_t(receiver_t* receiver, wxWindow* window);
	EXPORT static std::shared_ptr<subscriber_t> make(receiver_t * receiver, wxWindow* window);

	EXPORT ~subscriber_t();

	EXPORT int unsubscribe();
	EXPORT void update_current_lnb(const devdb::lnb_t & lnb);

	EXPORT std::unique_ptr<playback_mpm_t> subscribe_service_for_viewing(const chdb::service_t& service);
	EXPORT int subscribe_stream(const devdb::stream_t& stream);

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

	EXPORT int scan_spectral_peaks(const devdb::rf_path_t& rf_path, ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key);

	template<typename mux_t>
	EXPORT int scan_muxes(const ss::vector_<mux_t> muxes, const std::optional<devdb::tune_options_t>& tune_options);

	EXPORT std::tuple<int, std::optional<int>>
	positioner_cmd(devdb::positioner_cmd_t cmd, int par);

	EXPORT std::unique_ptr<playback_mpm_t> subscribe_recording(const recdb::rec_t& rec);

};

using ssptr_t = std::shared_ptr<subscriber_t>;

#ifdef declfmt
#undef declfmt
#endif
#define declfmt(t)																											\
	template <> struct fmt::formatter<t> {																\
		inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) { \
			return ctx.begin();																								\
		}																																		\
																																				\
		format_context::iterator format(const t&, format_context& ctx) const ; \
	}

declfmt(ssptr_t);
