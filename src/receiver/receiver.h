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

#include "util/access.h"

#include "tuner.h"

#include "options.h"
#include "recmgr.h"
#include "mpm.h"
#include "devmanager.h"
#include "streamparser/packetstream.h"
#include "streamparser/psi.h"
#include "util/safe/safe.h"

#define DVB_DEV_PATH "/dev/dvb/adapter"
#define FRONTEND_DEV_NAME "frontend"
#define DEMUX_DEV_NAME    "demux"
#define DVR_DEV_NAME      "dvr"



#include "util/util.h"

unconvertable_int(int, frontend_fd_t);

class active_mpm_t;
class MpvPlayer;
class adaptermgr_t;
class dvb_frontend_t;
class dvb_adapter_t;
class subscriber_t;
using ssptr_t = std::shared_ptr<subscriber_t>;

struct subscription_options_t;
struct tune_pars_t;
struct spectrum_scan_t;
struct scan_mux_end_report_t;
struct sdt_data_t;

class active_adapter_t;
class tuner_thread_t;
class scam_t;
class scam_thread_t;
class scanner_t;






class receiver_thread_t;

class service_thread_t;
class active_service_t;
class active_playback_t;
struct pmt_service_t;
namespace epgdb {
	struct epg_record_t;
};



/*
	Groups all functions which run in the stream thread
*/
class service_thread_t : public task_queue_t {
	friend class active_service_t;
	active_service_t& active_service;

public:

	service_thread_t(active_service_t& active_service_)
		: task_queue_t(thread_group_t::service)
		,active_service(active_service_) {
	}


	~service_thread_t() {
	}


	service_thread_t(service_thread_t&& other) = delete;
	service_thread_t(const service_thread_t& other) = delete;
	service_thread_t operator=(const service_thread_t& other) = delete;

private:
	virtual int run() final;
	virtual int exit() final;
	void scam_process_received_control_words(struct epoll_event& evt);

	void check_for_and_handle_scam_error();

public:
	class cb_t;
};

class service_thread_t::cb_t : public service_thread_t { //callbacks
public:
	//int deactivate(active_service_t& channel);
	std::optional<recdb::rec_t> start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec);
	int stop_recording(const recdb::rec_t& key, mpm_copylist_t& copy_commands);
	void forget_recording_in_livebuffer(const recdb::rec_t& key);
	int start_streaming(subscription_id_t subscription_id);
};



/*
	Groups all functions to connect to a softcam
*/
class scam_thread_t : public task_queue_t {
	friend class scam_t;

	std::thread::id thread_id;

	/*!
		All functions should be called from the right thread.

	*/
	void check_thread() const {
		assert(std::this_thread::get_id() == thread_id && "Called from the wrong thread");
	}

	std::shared_ptr<scam_t> scam; //should be unique_ptr, but then it does not compile without complete definition of sscam
	virtual int run() final;
	int wait_for_and_handle_events(bool do_not_handle_demux_events); //single run of the event loop;
	virtual int exit() final;


public:
	class cb_t;
	scam_thread_t(receiver_thread_t& _receiver);

	virtual ~scam_thread_t() {
	}

	scam_thread_t(scam_thread_t&& other) = delete;
	scam_thread_t(const scam_thread_t& other) = delete;
	scam_thread_t operator=(const scam_thread_t& other) = delete;


};

class scam_thread_t::cb_t : public scam_thread_t
{

public:
	/*!
		set the pmt for a service and add the service in the process
	*/
	int update_pmt(active_service_t* active_service,
								 int adapter_no, const dtdemux::pmt_info_t& pmt, bool isnext);
	//int register_active_service(active_service_t* active_service);
	int unregister_active_service(active_service_t* active_service, int adapter_no);

};



//struct tp_si_data_t;

//typedef int dvb_fd_t;


#define MAX_CH_PER_TP (8)
#define dvb_receiver_error(text, args...)						\
	do {																							\
		this->error(__func__, __LINE__, text, ##args);	\
	} while(0)

#define dvb_receiver_debug(text, args...)						\
	do {																							\
		this->debug(__func__, __LINE__, text, ##args);	\
	} while(0)


//static int last = -2;
/* @brief: describes a transport stream for one streamed channel
	 including all useful pids
*/
using namespace dtdemux;



struct preferred_languages_t {
	int count;
	int codes[32];

	preferred_languages_t(): count(0) {}
};

extern preferred_languages_t preferred_languages;

struct frontend_idx_t {
	const int adapter_no = -1;
	const int frontend_no = -1; //LNB is attached to frontend_no
	//int frontend_type = SYS_UNDEFINED;

	frontend_idx_t(int adapter_no, int frontend_no) :
		adapter_no(adapter_no), frontend_no(frontend_no)
		{
		}


	bool operator<(const frontend_idx_t& other) const {
		return (adapter_no < other.adapter_no) ? true :
			(adapter_no == other.adapter_no) ?  (frontend_no < other.frontend_no) :
			false;
	}

};


class receiver_thread_t : public task_queue_t  {
	friend class scanner_t;
	friend class scan_t;

	/// All data in this class can only be accessed serially
//	mutable std::mutex mutex; //TODO: check when/if this is needed
	/*!
		All adapters/frontends on the system. The frontends can either be in a closed or open state.
		Open state means that they have an file descriptor opened by this program

		all_frontends contains all frontends on the system, whether we are using them or not.
		If we are not using them,  shared_ptr.use_count() will be 1
	*/
	std::shared_ptr<adaptermgr_t> adaptermgr;

	using aa_map_t = safe::Safe<std::map<subscription_id_t, std::shared_ptr<active_adapter_t>>>;
	aa_map_t active_adapters;

	//for channel scan
	using  scanner_ptr_t = safe::Safe<std::shared_ptr<scanner_t>>;
	scanner_ptr_t scanner;

	time_t next_command_event_time = std::numeric_limits<time_t>::min();

	active_adapter_t* find_or_create_active_adapter
	(std::vector<task_queue_t::future_t>& futures, const subscribe_ret_t& sret);

	virtual int exit() final;
	void unsubscribe_mux_and_service_only(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
																				ssptr_t ssptr);
	void unsubscribe_playback_only(std::vector<task_queue_t::future_t>& futures, ssptr_t ssptr);
	void unsubscribe_all(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
											 ssptr_t ssptr);
	void release_active_adapter(std::vector<task_queue_t::future_t>& futures,
															subscription_id_t subscription_id,
															const devdb::fe_t& updated_dbfe, bool is_streaming);
	void remove_stream(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id);
	void unsubscribe_lnb(std::vector<task_queue_t::future_t>& futures, ssptr_t ssptr);
	bool unsubscribe_scan(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
												ssptr_t ssptr);
	void unsubscribe(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, ssptr_t ssptr);

	subscription_id_t subscribe_lnb(std::vector<task_queue_t::future_t>& futures, db_txn& wtxn,
																	devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																	subscription_options_t tune_options, ssptr_t ssptr);
	std::unique_ptr<playback_mpm_t>
	subscribe_playback_(const recdb::rec_t& rec, ssptr_t ssptr);
protected:

	subscription_id_t subscribe_spectrum(
		std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::sat_t& sat,
		const chdb::band_scan_t& band_scan,
		ssptr_t ssptr, subscription_options_t tune_options,
		const chdb::scan_id_t& scan_id,
		bool do_not_unsubscribe_on_failure);

	template<typename _mux_t>
	subscription_id_t
	subscribe_mux(std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn,
								const _mux_t& mux, ssptr_t ssptr,
								subscription_options_t tune_options,
								const chdb::scan_id_t& scan_id, bool do_not_unsubscribe_on_failure);

	template<class mux_t>
	std::tuple<subscription_id_t, devdb::fe_key_t> subscribe_mux_in_use(
		std::vector<task_queue_t::future_t>& futures,
		std::shared_ptr<active_adapter_t>& old_active_adapter, db_txn& devdb_wtxn,
		const mux_t& mux, ssptr_t ssptr, const subscription_options_t& tune_options,
		const devdb::rf_path_t* required_rf_path, const chdb::scan_id_t& scan_id);

	subscription_id_t subscribe_service_for_recording(
		std::vector<task_queue_t::future_t>& futures, db_txn& devdb_wtxn, const chdb::any_mux_t& mux,
		recdb::rec_t& rec, ssptr_t ssptr);

	std::tuple<std::optional<devdb::stream_t>, std::unique_ptr<playback_mpm_t>>
	subscribe_stream(const chdb::any_mux_t& mux, const chdb::service_t* pservice,
										ssptr_t ssptr, const devdb::stream_t* stream);

	devdb::stream_t update_and_toggle_stream(const devdb::stream_t& stream_);

public:
	receiver_t& receiver;
	//@todo remove unsafe accesses of browse_history
private:
	//safe to access from other threads (only tasks can be called)

		inline std::shared_ptr<scanner_t> get_scanner() const {
		auto r= scanner.readAccess();
		return *r;
	}

	inline void set_scanner(std::shared_ptr<scanner_t>& scanner_) {
		auto w= scanner.writeAccess();
		*w = scanner_;
	}
	inline void reset_scanner() {
		auto w= scanner.writeAccess();
		w->reset();
	}

public:

	inline std::shared_ptr<active_adapter_t> find_active_adapter(subscription_id_t subscription_id) {
		if (must_exit()) {
			dterrorf("Cannot retrieve active adapter because shutting down: {:d}", (int) subscription_id);
			return nullptr;
		}
		auto [it, found] = find_in_safe_map(this->active_adapters, subscription_id);
		return found ? it->second : nullptr;
	}

	receiver_thread_t(receiver_t& receiver);
	~receiver_thread_t();


private:
	void startup_commands(system_time_t now_);
	void startup_streams(system_time_t now_);

	std::unique_ptr<playback_mpm_t>
	subscribe_service_in_use(std::vector<task_queue_t::future_t>& futures,
													 const chdb::service_t& service,
													 ssptr_t ssptr);

	//////////////////////



	void log_message(const char*file, unsigned int line, const char*prefix, const char*fmt, va_list ap);
	active_service_t* find_open_channel_by_stream_pid(uint16_t pid); //used by scam

	//implemented in two versions: once in pyneumodaivb.cc and once in main.cc
	void inform_python();

	template<typename mux_t>
	subscription_id_t scan_muxes(std::vector<task_queue_t::future_t>& futures, ss::vector_<mux_t>& muxes,
															 const subscription_options_t& tune_options,
															 int max_num_subscriptions,
															 ssptr_t ssptr);

	subscription_id_t scan_spectral_peaks(std::vector<task_queue_t::future_t>& futures,
																				const devdb::rf_path_t& rf_path,
																				ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key,
																				bool scan_found_muxes, int max_num_subscriptions,
																				ssptr_t scan_ssptr);

	subscription_id_t scan_bands(std::vector<task_queue_t::future_t>& futures,
															 const ss::vector_<chdb::sat_t>& sats,
															 const ss::vector_<chdb::fe_polarisation_t>& pols,
															 const subscription_options_t& tune_options,
															 int max_num_subscriptions,
															 ssptr_t ssptr);

	subscription_id_t subscribe_spectrum(std::vector<task_queue_t::future_t>& futures, const devdb::lnb_t& lnb,
																			 const ss::vector_<chdb::sat_sub_band_pol_t> bands, subscription_options_t tune_options,
																			 ssptr_t ssptr);

	virtual int run() final;

	int housekeeping(system_time_t now);
	void start_stop_commands(auto& cursor, db_txn& devdb_rtxn, system_time_t now_, bool clean);
	void start_commands(db_txn& rtxn, system_time_t now);
	void stop_commands(db_txn& rtxn, system_time_t now);
	void save_db_command(devdb::scan_command_t& cmd, time_t now);
	bool stop_command(devdb::scan_command_t& cmd, devdb::run_result_t run_result, time_t now);
	void on_scan_command_end(db_txn& devdb_wtxn, ssptr_t scan_ssptr, const devdb::scan_stats_t& scan_stats);

	bool start_command(devdb::scan_command_t& cmd, time_t now);

public:
	//functions safe to call from other threads
	class cb_t;

	time_t scan_start_time() const;
	std::tuple<std::string, int> get_api_type() const;

	inline std::shared_ptr<dvb_frontend_t> fe_for_dbfe(const devdb::fe_key_t& fe_key) const {
		return adaptermgr->fe_for_dbfe(fe_key);
	}

};



class receiver_thread_t::cb_t : public receiver_thread_t { //callbacks
public:

	template<typename _mux_t>
	std::tuple<subscription_id_t, devdb::fe_key_t>
	subscribe_mux(const _mux_t& mux, ssptr_t ssptr, subscription_options_t tune_options,
								const chdb::scan_id_t& scan_id={});

	subscription_id_t subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, subscription_options_t tune_options,
																	ssptr_t ssptr);

	std::unique_ptr<playback_mpm_t>
	subscribe_service(const chdb::service_t& service,
										ssptr_t ssptr = {});

	inline devdb::stream_t update_and_toggle_stream(const devdb::stream_t& stream) {
		return receiver_thread_t::update_and_toggle_stream(stream);
	}

	std::unique_ptr<playback_mpm_t>
	subscribe_playback(const recdb::rec_t& rec, ssptr_t ssptr);

	template<typename _mux_t>
	subscription_id_t scan_muxes(ss::vector_<_mux_t>& muxes, const subscription_options_t& tune_options,
															 ssptr_t ssptr);

	subscription_id_t scan_spectral_peaks(const devdb::rf_path_t& rf_path, ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key,
																				ssptr_t scan_ssptr);
	subscription_id_t scan_bands(const ss::vector_<chdb::sat_t>& sats,
															 const ss::vector_<chdb::fe_polarisation_t>& pols,
															 subscription_options_t tune_options,
															 ssptr_t ssptr);

	void unsubscribe(ssptr_t ssptr);
	void abort_scan();
	void start_recording(recdb::rec_t rec);
	int start_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);
	int start_recording(const chdb::service_t& service, system_time_t start_time, int duration);

	void send_signal_info_to_scanner(const signal_info_t& info, const ss::vector_<subscription_id_t>& subscription_ids);
	void send_sdt_actual_to_scanner(const sdt_data_t& sdt_data, const ss::vector_<subscription_id_t>& subscription_ids);
	void send_spectrum_to_scanner(const devdb::fe_t& finished_fe, const spectrum_scan_t& scan,
																const chdb::scan_id_t& scan_id,
																const ss::vector_<subscription_id_t>& subscription_ids);
	void send_scan_mux_end_to_scanner(const devdb::fe_t& finished_fe, const chdb::any_mux_t& mux,
																		const chdb::scan_id_t& scan_id, ssptr_t ssptr);

	int scan_now();
	void renumber_card(int old_number, int new_number);

	std::tuple<int, std::optional<int>>
	positioner_cmd(ssptr_t ssptr, devdb::positioner_cmd_t cmd, int par);
};

struct player_cb_t {
	virtual void notify(const signal_info_t& info) {};
	virtual void update_playback_info() {};
	player_cb_t() {};
	virtual ~player_cb_t() {};
};


class receiver_t {
	bool inited{false};

	EXPORT int toggle_recording_(const chdb::service_t& service,
										 const epgdb::epg_record_t& epg_record);
	EXPORT int toggle_recording_(const chdb::service_t& service, system_time_t start_time,
															 int duration, const char* event_name);
public:

	ssptr_t get_ssptr(subscription_id_t subscription_id);

	//safe to access from other threads (only tasks can be called)

	using  options_t = safe::Safe<neumo_options_t>;
	options_t options;

	std::optional<db_upgrade_info_t> db_upgrade_info;

	receiver_thread_t receiver_thread;
	scam_thread_t scam_thread;
	rec_manager_t rec_manager;
	statdb::statdb_t statdb;
	devdb::devdb_t devdb;
	chdb::chdb_t chdb;
	//safe to access from other threads
	epgdb::epgdb_t epgdb;
	recdb::recdb_t recdb;

	using subscriber_map = safe::Safe<std::map<void*, ssptr_t>, std::recursive_mutex>;
	subscriber_map subscribers;//indexed by address

	std::shared_ptr<subscriber_t> global_subscriber; //for sending error messages

	chdb::history_mgr_t browse_history;
	recdb::rec_history_mgr_t rec_browse_history;


/*
	obtains a shared ptr to an active adapter. The active adapter remains valid
	as long as a subscription is held
*/
	inline std::shared_ptr<active_adapter_t> find_active_adapter(subscription_id_t subscription_id) {
		return receiver_thread.find_active_adapter(subscription_id);
	}

	EXPORT receiver_t(const neumo_options_t* options= nullptr);
	EXPORT ~receiver_t();
	EXPORT bool init();
	devdb::lnb_t reread_lnb_lof_offsets(const devdb::lnb_t& lnb);

	template<typename _mux_t>
	subscription_id_t
	subscribe_mux(const _mux_t& mux, bool blindscan, ssptr_t ssptr);

	subscription_id_t subscribe_lnb_spectrum(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																					 const chdb::fe_polarisation_t& pol,
																					 int32_t low_freq, int32_t high_freq,
																					 const chdb::sat_t& sat, ssptr_t ssptr);

	subscription_id_t subscribe_lnb(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, devdb::retune_mode_t retune_mode,
																	ssptr_t ssptr);

	subscription_id_t subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
																					bool blindscan, const pls_search_range_t& pls_search_range,
																					devdb::retune_mode_t retune_mode, ssptr_t ssptr);

	inline subscription_id_t subscribe_lnb_and_mux(devdb::rf_path_t& rf_path, devdb::lnb_t& lnb,
																								 const chdb::dvbs_mux_t& mux, bool blindscan,
																								 devdb::retune_mode_t retune_mode, ssptr_t ssptr) {
		return subscribe_lnb_and_mux(rf_path, lnb, mux, blindscan, pls_search_range_t{},  retune_mode, ssptr);
	}

	subscription_id_t scan_spectral_peaks(const devdb::rf_path_t& rf_path,
																				ss::vector_<chdb::spectral_peak_t>& peaks,
																				const statdb::spectrum_key_t& spectrum_key, ssptr_t scan_ssptr);

	template<typename _mux_t>
	subscription_id_t scan_muxes(ss::vector_<_mux_t>& muxes, const subscription_options_t& tune_options,
															 ssptr_t ssptr);

	subscription_id_t scan_bands(const ss::vector_<chdb::sat_t>& sats,
															 const ss::vector_<chdb::fe_polarisation_t>& pols,
															 subscription_options_t tune_options,
															 ssptr_t ssptr);

	std::unique_ptr<playback_mpm_t> subscribe_service_for_viewing(
		const chdb::service_t& service, ssptr_t ssptr = {});

	EXPORT devdb::stream_t update_and_toggle_stream(const devdb::stream_t& stream);

	std::unique_ptr<playback_mpm_t>
	subscribe_playback(const recdb::rec_t& rec, ssptr_t ssptr);

	EXPORT void unsubscribe(ssptr_t ssptr);

	EXPORT int update_autorec(recdb::autorec_t& autorec);
	EXPORT int delete_autorec(const recdb::autorec_t& autorec);

	EXPORT int toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);
	EXPORT int toggle_recording(const chdb::service_t& service);

	inline int toggle_recording(const chdb::service_t& service, time_t start_time,
															 int duration, const char* event_name) {
		return toggle_recording_(service,  system_clock_t::from_time_t(start_time),
														 duration, event_name);
	}


	void start_recording(const recdb::rec_t& rec_in);
	void start();
	EXPORT void stop();

	chdb::language_code_t get_current_audio_language(ssptr_t ssptr);

	ss::vector<chdb::language_code_t, 8> subtitle_languages(ssptr_t ssptr);
	chdb::language_code_t get_current_subtitle_language(ssptr_t ssptr);


	void update_playback_info();

	EXPORT void set_options(const neumo_options_t& options);

	EXPORT neumo_options_t get_options();

	inline time_t scan_start_time() const {
		return receiver_thread.scan_start_time();
	}

	EXPORT std::tuple<std::string, int> get_api_type() const;

	EXPORT void renumber_card(int old_number, int new_number);
	EXPORT devdb::tune_options_t get_default_tune_options(devdb::subscription_type_t subscription_type) const;
	spectrum_scan_options_t get_default_spectrum_scan_options(devdb::subscription_type_t
																														subscription_type) const;

	subscription_options_t get_default_subscription_options(devdb::subscription_type_t subscription_type) const;

	inline std::shared_ptr<dvb_frontend_t> fe_for_dbfe(const devdb::fe_key_t& fe_key) const {
		return receiver_thread.fe_for_dbfe(fe_key);
	}

	//thread safe; called from fe_monitor; notify python subscribers synschronously and scanner asynchronously
	void on_positioner_motion(const devdb::fe_t& fe, const devdb::dish_t& dish, double speed, int delay,
														const ss::vector_<subscription_id_t>& subscription_ids);

	//thread safe; called from fe_monitor; notify python subscribers synschronously and scanner asynchronously
	void on_spectrum_scan_end(const devdb::fe_t& finished_fe, const spectrum_scan_t& scan,
														const ss::vector_<subscription_id_t>& subscription_ids);

	//thread safe; called from fe_monitor; notify python subscribers synschronously and scanner asynchronously
	void on_signal_info(const signal_info_t& info, const ss::vector_<subscription_id_t>& subscription_ids);

	//thread-safe; called from tuner; notify non-scanning python subscribers synchronously and scanner asynchronously
	void on_sdt_actual(const sdt_data_t& sdt_data, const ss::vector_<subscription_id_t>& subscription_ids);

	//thread-safe; called from tuner; notify scanner asynchronously
	void on_scan_mux_end(const devdb::fe_t& finished_fe, const chdb::any_mux_t& mux,
											 const chdb::scan_id_t& scan_id, subscription_id_t subscription_id);

	void notify_spectrum_scan_band_end(subscription_id_t scan_subscription_id, const statdb::spectrum_t& spectrum);

	//thread-safe; called from scanner; notify single python scanning subscriber synchronously
	void notify_scan_mux_end(subscription_id_t scan_subscription_id, const scan_mux_end_report_t& report);

	//thread-safe; called from scanner; notify single python scanning subscriber synchronously
	void notify_scan_progress(subscription_id_t scan_subscription_id, const devdb::scan_stats_t& scan_stats);
	void activate_spectrum_scan(spectrum_scan_options_t& spectrum_scan_options,
															devdb::lnb_pol_type_t lnb_pol_type);
	chdb::scan_id_t deactivate_spectrum_scan(const spectrum_scan_t& spectrum_scan);

	inline devdb::usals_location_t get_usals_location() const {
		auto r = options.readAccess();
		return r->usals_location;
	}
};

EXPORT extern thread_local thread_group_t thread_group;
