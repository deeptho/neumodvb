/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
//#include "devmanager.h"
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

struct tune_options_t;
struct spectrum_scan_t;

inline void todo(const char*s)
{
	dterrorx("TODO: %s", s);
	assert(0);
}


struct scan_stats_t;
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
	void on_epg_update(system_time_t now, const epgdb::epg_record_t& epg_record);
	void update_recording(recdb::rec_t&rec, const chdb::service_t& service, const epgdb::epg_record_t& epgrec);

	std::optional<recdb::rec_t> start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec);
	int stop_recording(const recdb::rec_t& key, mpm_copylist_t& copy_commands);
	void forget_recording(const recdb::rec_t& key);

};

/*
	Groups all functions which run in the tuner thread.
	All public functions can only be used within tasks which are pushed using "push_task".
	They will be executed asynchronously by the tuner thread

	Threre is a single tuner_thread for all tuners in the system
	The tuner thread controls the dvb front ends, and performs all si processing
	TODO: perhaps some of the si processing should be moved on one or more seperate threads
	For instance, service related processing on one thread (which accesses the channel lmdb database)
	and all epg related information on a seperate thread (which accesses the epg lmdb database).

	We could also go for one thread per tuner, but these threads will block each other while accessing
	lmdb,
*/
class tuner_thread_t : public task_queue_t {
	//	int next_subscription_id= 0;
	friend class active_adapter_t;
	friend class active_si_stream_t;
	friend class si_t;
	receiver_t& receiver;
	std::thread::id thread_id;
	std::map<frontend_fd_t, std::shared_ptr<active_adapter_t>> active_adapters; //indexed by open file descriptor

	/*!
		All functions should be called from the right thread.

	*/

	system_time_t last_epg_check_time;
	int epg_check_period = 5*60;

	system_time_t next_epg_clean_time;

	void clean_dbs(system_time_t now, bool at_start);
	periodic_t livebuffer_db_update;
	rec_manager_t recmgr;
	void livebuffer_db_update_(system_time_t now);
	virtual int run() final;
//returns true on error
	active_adapter_t* find_active_adapter_for_event(const epoll_event* event) const;
	void on_epg_update(db_txn& txnepg, system_time_t now,
										 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																											to true or false*/);
	void on_notify_signal_info(chdb::signal_info_t& info);
	virtual int exit();
public:
	tuner_thread_t(receiver_t& receiver_);

	~tuner_thread_t();

	tuner_thread_t(tuner_thread_t&& other) = delete;
	tuner_thread_t(const tuner_thread_t& other) = delete;
	tuner_thread_t operator=(const tuner_thread_t& other) = delete;
	inline int set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options);
	inline int prepare_si(active_adapter_t& active_adapter, const chdb::any_mux_t& mux, bool start);
	inline int request_retune(active_adapter_t& active_adapter);
public:

	class cb_t;

};

class tuner_thread_t::cb_t: public tuner_thread_t { //callbacks
public:
	int on_lnb_lof_offset_update(int fefd, const ss::vector<int32_t,2>& lof_offsets);
	int remove_active_adapter(active_adapter_t& tuner);
	int remove_service(active_adapter_t& tuner, active_service_t& channel);
	int add_service(active_adapter_t& tuner, active_service_t& channel);//tune to channel on transponder
	int on_pmt_update(active_adapter_t& active_adapter, const dtdemux::pmt_info_t& pmt);
	void on_notify_signal_info(chdb::signal_info_t& signal_info);

	int update_service(const chdb::service_t& service);

	int lnb_activate(std::shared_ptr<active_adapter_t> active_adapter, const devdb::lnb_t& lnb,
					 tune_options_t tune_options);

	int tune(std::shared_ptr<active_adapter_t> active_adapter, const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
					 tune_options_t tune_options);
	template<typename _mux_t>
	int tune(std::shared_ptr<active_adapter_t> tuner, const _mux_t& mux,
					 tune_options_t tune_options);
	int set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options);
	int prepare_si(active_adapter_t& active_adapter, const chdb::any_mux_t& mux, bool start);
	int request_retune(active_adapter_t& active_adapter);


	void toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);

	void update_recording(const recdb::rec_t& rec);
	int positioner_cmd(std::shared_ptr<active_adapter_t> active_adapter, devdb::positioner_cmd_t cmd, int par);
	int update_current_lnb(active_adapter_t& adapter,  const devdb::lnb_t& lnb);
};


/*
	Groups all functions to connect to a softcam
*/
class scam_thread_t : public task_queue_t {
	friend class scam_t;

	//receiver_thread_t& receiver_thread;
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


static int last = -2;
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

	/// All data in this class can only be accessed serially
//	mutable std::mutex mutex; //TODO: check when/if this is needed

	/*!
		A subscription is defined by a subscription_id. Each subscription
		can either tune\ to a tp, or to a tp + a channel

		A subscription can also reserve a single non-tuned tp for exclusive use

		Tuners and channels can be shared by multiple subscriptions


	*/
	int next_subscription_id = 0; //

	/*!
		All adapters/frontends on the system. The frontends can either be in a closed or open state.
		Open state means that they have an file descriptor opened by this program

		all_frontends contains all frontends on the system, whether we are using them or not.
		If we are not using them,  shared_ptr.use_count() will be 1
	*/
	std::shared_ptr<adaptermgr_t> adaptermgr;

	//for channel scan
	std::shared_ptr<scanner_t> scanner;

		/*
		channels which are currently subscribed to. Note that each subscribed channel implies
		a subscribed tuner. The channels are either active or becoming active.
		This data structure can only be accessed from receiver_thread

	*/
  //@todo: there is no reason anymore to make services_map a safe_map as it is only used in receiver_thread
	using services_map = std::map<subscription_id_t, std::shared_ptr<active_service_t>>;
	services_map reserved_services; //open transponders, indexed by subscription id

	using playback_map = std::map<subscription_id_t, std::shared_ptr<active_playback_t>>;
	playback_map reserved_playbacks; //recordings being played back, indexed by subscription id


private:
	virtual int exit() final;

	std::shared_ptr<active_adapter_t> active_adapter_for_subscription(subscription_id_t subscription_id);

	void remove_active_adapter(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id);
	void unsubscribe_(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id, bool service_only);
	void unsubscribe_active_service(std::vector<task_queue_t::future_t>& futures,
																	active_service_t& active_service, subscription_id_t subscription_id);
	void unsubscribe_lnb(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id);
	void unsubscribe_scan(std::vector<task_queue_t::future_t>& futures, subscription_id_t subscription_id);


	subscription_id_t subscribe_lnb(std::vector<task_queue_t::future_t>& futures, db_txn& wtxn, devdb::lnb_t& lnb,
																	tune_options_t tune_options, subscription_id_t subscription_id);



/*!
		find a suitable lnb  for tuning to a mux
		if subscription_id >=0, then first unregister any mux for that subscription
		ans then make a new reservation, taking into account that the old and the new mux might be the same
		if subscription_id <0, then create a new subscription

		Returns -1 if subscription failed (no free tuners)

	*/
	subscription_id_t subscribe_mux_(std::vector<task_queue_t::future_t>& futures,
																	 std::shared_ptr<active_adapter_t>& old_active_adapter,
																	 db_txn &txn, const chdb::dvbs_mux_t& mux,
																	 subscription_id_t subscription_id,
																	 tune_options_t tune_options, const devdb::lnb_t* required_lnb);


	/*!
		find a suitable lnb  for tuning to a mux
		if subscription_id >=0, then first unregister any mux for that subscription
		ans then make a new reservation, taking into account that the old and the new mux might be the same
		if subscription_id <0, then create a new subscription

		Returns -1 if subscription failed (no free tuners)

	*/
	template<typename _mux_t>
	subscription_id_t subscribe_mux_(std::vector<task_queue_t::future_t>& futures,
																	 std::shared_ptr<active_adapter_t>& old_active_adapter,
																	 db_txn &txn, const _mux_t& mux, subscription_id_t subscription_id,
																	 tune_options_t tune_options, const devdb::lnb_t* required_lnb /*unused*/);

	template<typename _mux_t>
	std::unique_ptr<playback_mpm_t>
	subscribe_service_(std::vector<task_queue_t::future_t>& futures,
										 db_txn &txn,
										 const _mux_t& mux, const chdb::service_t& service,
										 active_service_t* old_active_service,
										 subscription_id_t subscription_id);

	std::unique_ptr<playback_mpm_t>
	subscribe_recording_(const recdb::rec_t& rec, subscription_id_t subscription_id);

protected:


	template<typename _mux_t>
	subscription_id_t subscribe_mux(std::vector<task_queue_t::future_t>& futures, db_txn& txn,
										const _mux_t& mux, subscription_id_t subscription_id,
										tune_options_t tune_options, const devdb::lnb_t* required_lnb);
	template<class mux_t>
	subscription_id_t subscribe_mux_in_use(std::vector<task_queue_t::future_t>& futures, const mux_t& mux,
													 subscription_id_t subscription_id, tune_options_t tune_options,
													 const devdb::lnb_t* required_lnb);
	int request_retune(std::vector<task_queue_t::future_t>& futures,
										 active_adapter_t& active_adapter, subscription_id_t subscription_id);

	std::unique_ptr<playback_mpm_t> subscribe_service(
		std::vector<task_queue_t::future_t>& futures, db_txn&txn,
		const chdb::any_mux_t& mux, const chdb::service_t& service,
		subscription_id_t subscription_id);
#ifdef OLDTUNE
	std::tuple<std::shared_ptr<dvb_frontend_t>,  devdb::lnb_t> find_lnb
	(std::vector<task_queue_t::future_t>& futures,
	 std::shared_ptr<active_adapter_t>& old_active_adapter,
	 db_txn &txn, const chdb::dvbs_mux_t& mux, subscription_id_t subscription_id);
#endif
public:
	receiver_t& receiver;
	//@todo remove unsafe accesses of browse_history
private:
	//safe to access from other threads (only tasks can be called)

public:

	//event_handle_t event_handle_test;
	//buffer_t streambuffer;

	receiver_thread_t(receiver_t& receiver);
	~receiver_thread_t();


private:
	inline std::shared_ptr<active_adapter_t> make_active_adapter(const devdb::fe_key_t& fe_key);

	std::unique_ptr<playback_mpm_t>
	subscribe_service_in_use(std::vector<task_queue_t::future_t>& futures,
													 const chdb::service_t& service,
													 subscription_id_t subscription_id);

	//////////////////////



	void log_message(const char*file, unsigned int line, const char*prefix, const char*fmt, va_list ap);
	active_service_t* find_open_channel_by_stream_pid(uint16_t pid); //used by scam

#if 0
	std::shared_ptr<active_adapter_t> active_adapter_with_fe(dvb_frontend_t*fe);
#endif


	//implemented in two versions: once in pyneumodaivb.cc and once in main.cc
	void inform_python();

	int update_recording(recdb::rec_t&rec, const epgdb::epg_record_t& epgrec);

	template<typename mux_t>
	subscription_id_t subscribe_scan(std::vector<task_queue_t::future_t>& futures, ss::vector_<mux_t>& muxes,
																	 bool scan_found_muxes, int max_num_subscriptions,
																	 subscription_id_t subscription_id);


	subscription_id_t subscribe_scan(std::vector<task_queue_t::future_t>& futures, ss::vector_<chdb::dvbs_mux_t>& muxes,
																	 ss::vector_<devdb::lnb_t>* lnbs, bool scan_found_muxes, int max_num_subscriptions,
																	 subscription_id_t subscription_id);

	subscription_id_t subscribe_spectrum(std::vector<task_queue_t::future_t>& futures, const devdb::lnb_t& lnb,
																			 const ss::vector_<devdb::fe_band_pol_t> bands, tune_options_t tune_options,
																			 subscription_id_t subscription_id);

	virtual int run() final;
public:
	//functions safe to call from other threads
	scan_stats_t get_scan_stats(subscription_id_t subscription_id);

	class cb_t;

	time_t scan_start_time() const;

};



class receiver_thread_t::cb_t : public receiver_thread_t { //callbacks
public:

	template<typename _mux_t>
	subscription_id_t subscribe_mux(const _mux_t& mux, subscription_id_t subscription_id, tune_options_t tune_options, const devdb::lnb_t* required_lnb);

	subscription_id_t subscribe_lnb(devdb::lnb_t& lnb, tune_options_t tune_options, subscription_id_t subscription_id);

	std::unique_ptr<playback_mpm_t>
	subscribe_service(const chdb::service_t& service,
										subscription_id_t subscription_id = subscription_id_t{-1});

	std::unique_ptr<playback_mpm_t>
	subscribe_recording(const recdb::rec_t& rec, subscription_id_t subscription_id);

	subscription_id_t subscribe_scan(ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<devdb::lnb_t>* lnbs,
																	 bool scan_found_muxes=true, int max_num_subscriptions=-1,
																	 subscription_id_t subscription_id=  subscription_id_t{-1});

	template<typename _mux_t>
	subscription_id_t scan_muxes(ss::vector_<_mux_t>& muxes, subscription_id_t subscription_id);

	subscription_id_t scan_muxes(ss::vector_<chdb::dvbs_mux_t>& muxes, subscription_id_t subscription_id);

	void unsubscribe(subscription_id_t subscription_id);
	void abort_scan();
	void start_recording(recdb::rec_t rec);
	void stop_recording(recdb::rec_t rec);
	int start_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);
	int start_recording(const chdb::service_t& service, system_time_t start_time, int duration);
	int stop_recording(const chdb::service_t& service, system_time_t t);
	int stop_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);

	void on_scan_mux_end(const active_adapter_t* active_adapter, const chdb::any_mux_t& finished_mux);
};

struct player_cb_t {
	virtual void notify(const chdb::signal_info_t& info) {};
	virtual void update_playback_info() {};
	virtual void update_epg_info(const epgdb::epg_record_t& epg) {};
	player_cb_t() {};
	virtual ~player_cb_t() {};
};


class receiver_t {

	int toggle_recording_(const chdb::service_t& service,
										 const epgdb::epg_record_t& epg_record);
	int toggle_recording_(const chdb::service_t& service, system_time_t start_time,
											 int duration, const char* event_name);

public:
	//safe to access from other threads (only tasks can be called)

	using  options_t = safe::Safe<neumo_options_t>;
	options_t options;

	receiver_thread_t receiver_thread;
	scam_thread_t scam_thread;
	tuner_thread_t tuner_thread; //there is only one tuner thread for the whole program

	using mpv_map = safe::Safe<std::map<void*, std::shared_ptr<player_cb_t>>>;
	mpv_map active_mpvs;


	statdb::statdb_t statdb;
	devdb::devdb_t devdb;
	chdb::chdb_t chdb;
	//safe to access from other threads
	epgdb::epgdb_t epgdb;
	recdb::recdb_t recdb;

		/*!
		tuners which are currently subscribed to. These tuners are either active,
		or they are in the process of becoming active. This data structure can only be accessed in receiver_thread
		*/
	using subscribed_aa_map = std::map<subscription_id_t, std::shared_ptr<active_adapter_t>>;
	using safe_subscribed_aa_map = safe::thread_public_t<false, subscribed_aa_map>;
	safe_subscribed_aa_map subscribed_aas{"receiver", thread_group_t::receiver, {}}; //open muxes, indexed by subscription id

	using subscriber_map = safe::Safe<std::map<void*, std::shared_ptr<subscriber_t>>>;
	subscriber_map subscribers;//indexed by subscription id


	chdb::history_mgr_t browse_history;
	recdb::rec_history_mgr_t rec_browse_history;

	receiver_t(const neumo_options_t* options= nullptr);
	~receiver_t();

	devdb::lnb_t reread_lnb(const devdb::lnb_t& lnb);
	template<typename _mux_t>
	int subscribe_mux(const _mux_t& mux, bool blindscan, int subscription_id);

	int subscribe_lnb_blindscan(devdb::lnb_t& lnb,  const devdb::fe_band_pol_t& band_pol,
																						int subscription_id);

	int subscribe_lnb_spectrum(devdb::lnb_t& lnb, const chdb::fe_polarisation_t& pol,
														 int32_t low_freq, int32_t high_freq,
														 int sat_pos, int subscription_id);

	int subscribe_lnb(devdb::lnb_t& lnb,  retune_mode_t retune_mode, int subscription_id);

	int subscribe_lnb_and_mux(devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
														const pls_search_range_t& pls_search_range,
														retune_mode_t retune_mode, int subscription_id);

	inline int subscribe_lnb_and_mux(devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool blindscan,
																	 retune_mode_t retune_mode, int subscription_id) {
		return subscribe_lnb_and_mux(lnb, mux, blindscan, pls_search_range_t{},  retune_mode, subscription_id);
	}

	template<typename _mux_t>
	int scan_muxes(ss::vector_<_mux_t>& muxes, int subscription_id);

	std::unique_ptr<playback_mpm_t> subscribe_service(
		const chdb::service_t& service, int subscription_id = int{-1});

	std::unique_ptr<playback_mpm_t>
	subscribe_recording(const recdb::rec_t& rec, int subscription_id);

	int unsubscribe(int subscription_id);


	int toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);
	int toggle_recording(const chdb::service_t& service);

	inline int toggle_recording(const chdb::service_t& service, time_t start_time,
															 int duration, const char* event_name) {
		return toggle_recording_(service,  system_clock_t::from_time_t(start_time),
														 duration, event_name);
	}


	void start_recording(const recdb::rec_t& rec_in);
	void stop_recording(const recdb::rec_t& rec_in);

	void start();
	void stop();

	chdb::language_code_t get_current_audio_language(int subscription_id);

	ss::vector<chdb::language_code_t, 8> subtitle_languages(int subscription_id);
	chdb::language_code_t get_current_subtitle_language(int subscription_id);


	void notify_signal_info(const chdb::signal_info_t& info);
	void notify_spectrum_scan(const statdb::spectrum_t& scan);

	void update_playback_info();
	scan_stats_t get_scan_stats(int subscription_id);

	std::shared_ptr<active_adapter_t> active_adapter_for_subscription(subscription_id_t subscription_id);

	void set_options(const neumo_options_t& options);
	neumo_options_t get_options();

	inline time_t scan_start_time() const {
		return receiver_thread.scan_start_time();
	}

};
