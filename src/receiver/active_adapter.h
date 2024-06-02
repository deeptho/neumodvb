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
#include "devmanager.h"
#include "active_si_stream.h"
#include "devmanager.h"
#include "neumodb/devdb/tune_options.h"
#include <bitset>

using namespace dtdemux;

//class active_fe_state_t;
class tuner_thread_t;
class streamer_t;
/* DVB-S */
/** lnb_slof: switch frequency of LNB */
#define DEFAULT_SLOF (11700*1000UL)
/** lnb_lof1: local frequency of lower LNB band */
#define DEFAULT_LOF1_UNIVERSAL (9750*1000UL)
/** lnb_lof2: local frequency of upper LNB band */
#define DEFAULT_LOF2_UNIVERSAL (10600*1000UL)
/** Lnb standard Local oscillator frequency*/
#define DEFAULT_LOF_STANDARD (10750*1000UL)


/* DVB-T DVB-C */
/* default option : full auto except bandwith = 8MHz*/
/* AUTO settings */
#define BANDWIDTH_DEFAULT           BANDWIDTH_8_MHZ
#define HP_CODERATE_DEFAULT         FEC_AUTO
#define MODULATION_DEFAULT          QAM_AUTO
#define SAT_MODULATION_DEFAULT      QPSK
#define TRANSMISSION_MODE_DEFAULT   TRANSMISSION_MODE_AUTO
#define GUARD_INTERVAL_DEFAULT      GUARD_INTERVAL_AUTO
#define HIERARCHY_DEFAULT           HIERARCHY_NONE

#if HIERARCHY_DEFAULT == HIERARCHY_NONE && !defined (LP_CODERATE_DEFAULT)
#define LP_CODERATE_DEFAULT (FEC_NONE) /* unused if HIERARCHY_NONE */
#endif

/* ATSC */
#define ATSC_MODULATION_DEFAULT     VSB_8

/* The lnb type*/


#if DVB_API_VERSION >= 5
#define MAX_CMDSEQ_PROPS_NUM 12
#endif





class usals_timer_t {
	int16_t usals_pos_start{sat_pos_none};
	int16_t usals_pos_end{sat_pos_none};
	bool started{false};
	bool stamped{false};
	steady_time_t start_time;
	steady_time_t first_pat_time;
public:
	inline void start(int16_t old_usals_pos, int16_t new_usals_pos) {
		usals_pos_start = old_usals_pos;
		usals_pos_end = new_usals_pos;
		started = true;
		printf("usals timer start\n");
	}

	inline void stamp( bool restart) {
		if(!started)
			return;
		if(stamped && !restart)
			return;
		stamped = true;
		first_pat_time = steady_clock_t::now();
		dtdebugf("positioner stamp");
		printf("usals timer stamp\n");
	}

std::optional<std::tuple<steady_time_t, int16_t, int16_t>> end();
};

struct lock_state_t {
	bool locked_normal{false};
	bool locked_minimal{false}; //good enough to log in database as NOLOCK when blindscanning
	chdb::lock_result_t tune_lock_result;
	bool temp_tune_failure{false};
	bool is_not_ts{false}; //true if we detected something else than an mpeg ts on this mux
	bool is_dvb{false}; //true if we detected an mpeg ts on this mux
};


/*@brief: all data for a transponder currently being monitored.
	owned by tuner_thread
 */
class active_adapter_t final : public  std::enable_shared_from_this<active_adapter_t>
{
	enum tune_state_t {
		TUNE_INIT, //we still need to tune
		TUNE_REQUESTED, //tuning was requested, waiting for info from fe_monitor;
		WAITING_FOR_LOCK, //tuning was started, waiting for lock;
		LOCKED, //tuning was started, si processing is running
		LOCK_TIMEDOUT, //tuning was started, waiting for lock;
		TUNE_FAILED, //tuning failed due to incorrect tuning parameters
		TUNE_FAILED_TEMP, //tuning failed temporarily because of lack of resources
	};
	friend class si_t;
	friend class tuner_thread_t;
	friend class active_si_stream_t;
	friend class stream_reader_t;
	friend struct dvb_stream_reader_t;

public:
	receiver_t& receiver;
	const std::shared_ptr<dvb_frontend_t> fe; //accessible by other threads (with some care?)
private:
/*
		for uncommitted switch closest to receiver, followed by committed switch.
		1. send the uncommitted switch
    2. then sen committed (will often fail because not powered on)
    3. send U becase we have to await anyway
    4. " " wait an additional 50 ms

		*/
	std::bitset<256> processed_isis;
	steady_time_t last_new_matype_time;

	safe::Safe<std::map <uint16_t, std::shared_ptr<stream_filter_t>>> stream_filters; //indexed by stream_pid
	std::map <uint16_t, active_si_stream_t> embedded_si_streams; //indexed by stream_pid

	tune_state_t tune_state{TUNE_INIT};
	subscription_options_t tune_options;
	lock_state_t lock_state;
	bool isi_processing_done{false};
	system_time_t tune_start_time;  //when last tune started
	constexpr static std::chrono::duration tune_timeout{15000ms}; //in ms

	//safe::thread_public_t<false, chdb::any_mux_t> tuned_muxxxx{"tuner", thread_group_t::tuner, {}};
	usals_timer_t usals_timer;

	chdb::any_mux_t current_tp() const {
		return fe->tuned_mux();
	};
	inline void update_tuned_mux_nit(const chdb::any_mux_t& mux) {
		this->fe->update_tuned_mux_nit(mux);
	}
	inline std::optional<signal_info_t> get_last_signal_info(bool wait) {
		return this->fe->get_last_signal_info(wait);
	}

	inline const devdb::lnb_t& current_lnb() const {
		return fe->ts.readAccess()->reserved_lnb;
	}
	inline const devdb::rf_path_t& current_rf_path() const {
		return fe->ts.readAccess()->reserved_rf_path;
	}
	inline int current_rf_coupler_id() const {
		auto r = fe->ts.readAccess();
		auto* conn = connection_for_rf_path(r->reserved_lnb, r->reserved_rf_path);
		return conn ?  conn->rf_coupler_id : -1;
	}

	void set_current_tp(const chdb::any_mux_t& mux) {
		assert((mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
						mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING) ||
					 chdb::scan_in_progress(mux_common_ptr(mux)->scan_id));
		fe->update_tuned_mux_nit(mux);
	};

	bool update_current_lnb(const devdb::lnb_t& lnb);

	//connected to the correct sat; TODO: replace with something based on lnb
	//tuner_stats_t fe_stats; //python interface: snr and such

	uint32_t get_lo_frequency(uint32_t frequency);
#if 0
	subscription_id_t start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec);
#endif
public: //this data is safe to access from other threads

	tuner_thread_t tuner_thread;

	chdb::any_mux_t current_mux() const {  //currently tuned transponder
		return current_tp();
		//@todo make thread safe
	};

	inline devdb::lnb_key_t get_lnb_key() const {
		return fe->ts.readAccess()->reserved_lnb.k;
	}

	bool uses_lnb(const devdb::lnb_key_t& lnb_key) const {
		return fe->ts.readAccess()->reserved_lnb.k == lnb_key;
		//@todo make thread safe
	}
	devdb::lnb_t
	get_lnb() const { //lnb currently in use
		return fe->ts.readAccess()->reserved_lnb;
		//@todo make thread safe
	}

	void update_lof(devdb::lnb_t& lnb, int16_t sat_pos, chdb::fe_polarisation_t pol,
									int nit_frequency, int driver_frequency);

private:
	int clear();
	int send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated);
	void handle_fe_event();
	void monitor();

	chdb::any_mux_t prepare_si(chdb::any_mux_t mux, bool start,
														 subscription_id_t subscription_id =subscription_id_t::NONE,
														 bool add_to_running_mux=false);

	template <typename mux_t> inline  mux_t prepare_si(
		mux_t mux, bool start, subscription_id_t subscription_id =subscription_id_t::NONE,
		bool add_to_running_mux=false) {
		chdb::any_mux_t mux_{mux};
		mux_ = prepare_si(mux_, start, subscription_id, add_to_running_mux);
		return *std::get_if<mux_t>(&mux_);
	}

	void init_si(devdb::scan_target_t scan_target_);
	void end_si();
	void reset_si();
private:
	std::map<subscription_id_t, std::shared_ptr<streamer_t>> streamers; //indexed by subscription_id
	std::map<subscription_id_t,
					 std::shared_ptr<active_service_t>> subscribed_active_services; //indexed by subscription_id

	/*
		key is the subscription_id
		first value entry in the map is the current pmt_pid as known by the tuner thread.
		This may differ from active_service.current_pmt_pid when a pmt change has not yet been noticed
		by the service thread
	*/

	active_si_stream_t si;
	int remove_service(subscription_id_t subscription_id);
	int remove_all_services();
	void check_for_new_streams();
	void check_for_unlockable_streams();
	void check_for_non_existing_streams();

	active_adapter_t(active_adapter_t&& other) = delete;
	active_adapter_t(const active_adapter_t& other) = delete;
public:
	active_adapter_t(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_);
	inline void start_tuner_thread() {
		tuner_thread.start_running();
	}
	static inline std::shared_ptr<active_adapter_t>
	make(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_) {
		auto ret= std::make_shared<active_adapter_t>(receiver_, fe_);
		ret->start_tuner_thread();
		return ret;
	}

	active_adapter_t operator=(const active_adapter_t& other) = delete;

	virtual ~active_adapter_t() final;

	template<typename mux_t>
	bool is_tuned_to(const mux_t& mux, const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid=false) {
		assert(fe.get());
		if(chdb::is_template(mux))
			return fe->is_tuned_to(mux, required_rf_path, ignore_t2mi_pid);
		auto tuned_mux = fe->tuned_mux();
		auto tuned_mux_key = *chdb::mux_key_ptr(tuned_mux);
		auto mux_key = *chdb::mux_key_ptr(mux);
		//allow mismatch in t2mi_pid
		tuned_mux_key.t2mi_pid = -1;
		mux_key.t2mi_pid = -1;
		return mux_key == tuned_mux_key;
	}

private:
	void reset();
	void destroy(); //called from tuner_thread on exit
	template<typename mux_t> inline int retune();
	int restart_tune(const chdb::any_mux_t& mux, subscription_id_t subscription_id);

	int lnb_spectrum_scan(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
												subscription_options_t tune_options);

	int lnb_activate(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb, subscription_options_t tune_options);

	int tune(const subscribe_ret_t& sret, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
					 const chdb::dvbs_mux_t& mux, subscription_options_t tune_options);

	int retune(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
							subscription_options_t tune_options, bool user_requested, subscription_id_t subscription_id);
	template<typename mux_t>
	int retune(const mux_t& mux_, const subscription_options_t tune_options, bool user_requested,
						 subscription_id_t subscription_id);

	template<typename mux_t>
	int tune_dvbc_or_dvbt(const mux_t& mux, subscription_options_t tune_options, bool user_requested,
					 subscription_id_t subscription_id);

	int add_service(subscription_id_t subscription_id, active_service_t& channel);//tune to channel on transponder
	std::tuple<bool, bool, bool, bool> check_status();

	std::shared_ptr<active_service_t>
	tune_service_in_use(const subscribe_ret_t& sret, const chdb::service_t& service);

	std::shared_ptr<active_service_t>
	tune_service(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
							 const chdb::service_t& service);

	void  update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation);
	int do_diseqc(bool log_strength, bool retry=false);
	int deactivate();
	void on_stable_pat();
	void on_first_pat(bool restart);
	void on_tuned_mux_change(const chdb::any_mux_t& si_mux);
	void update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad);
	void check_isi_processing();
	int request_retune(const chdb::any_mux_t& mux_,
										 const subscription_options_t& tune_options,
										 subscription_id_t subscription_id);
public:
	devdb::usals_location_t get_usals_location();
	int open_demux(int mode = O_RDWR | O_NONBLOCK) const;

	inline devdb::fe_t dbfe() const {
		return fe ? fe->dbfe() : devdb::fe_t{};
	}

	inline devdb::fe_key_t fe_key() const {
		return fe ? fe->fe_key() : devdb::fe_key_t{};
	}

	inline int frontend_no() const {
		return fe ? int(fe->frontend_no) : -1;
	}

	inline int get_adapter_no() const {
		return fe ? int(fe->adapter_no) : -1;
	}

	inline int64_t get_adapter_mac_address() const {
		return fe ? int64_t(fe->adapter_mac_address) : -1;
	}

	inline bool is_open() const {
		return fe.get() && fe->is_open();
	}

private:
	std::shared_ptr<stream_reader_t> make_dvb_stream_reader(ssize_t dmx_buffer_size_ = -1);
	std::shared_ptr<stream_reader_t> make_embedded_stream_reader(const chdb::any_mux_t& mux,
																															 ssize_t dmx_buffer_size_ = -1);
	bool add_embedded_si_stream(const chdb::any_mux_t& emdedded_mux, bool start=false);

	bool read_and_process_data_for_fd(const epoll_event* evt);

	std::unique_ptr<playback_mpm_t>
	tune_service_for_viewing(const subscribe_ret_t& sret,
							 const chdb::any_mux_t& mux, const chdb::service_t& service);

	std::optional<recdb::rec_t>
	tune_service_for_recording(const subscribe_ret_t& sret,
														 const chdb::any_mux_t& mux,
														 const recdb::rec_t& rec);

	devdb::stream_t add_stream(const subscribe_ret_t& sret, const devdb::stream_t& stream,
														 const chdb::any_mux_t& mux);

	void remove_stream(subscription_id_t subscription_id);

	std::shared_ptr<active_service_t>
	active_service_for_subscription(subscription_id_t subscription_id);
};
