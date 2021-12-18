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
#include "adapter.h"
#include "reservation.h"
#include "active_si_stream.h"

using namespace dtdemux;

class active_adapter_reservation_t;
class tuner_thread_t;

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


enum class subscription_type_t {
	NORMAL, /*regular viewing: an lnb is reserved non-exclusively, polband and sat_posh are also reserved
						non-exlcusively. This mans other lnbs on the same dish can be used by other subscriptions,
						and slave demods are allowed as well
					*/
	LNB_EXCLUSIVE,     /*in this case, a second subscriber cannot subscribe to the mux
						at first tune, position data is used from the lnb. Retunes cannot
						change the positioner and diseqc settings afterwards. Instead, the user
						must explicitly force them by a new tune call (diseqc swicthes), or by sending a
						positoner commands (usals, diseqc1.2)

						Also, lnb and dish are reserved exclusively, which means no other lnbs on the dish
						can be used on the same dish
					 */
	DISH_EXCLUSIVE /* To be used for secondary subscriptions which are under the control of a user with a DX
							subscription. E.g., this can be used to spectrum scan using multiple frontends ithout having to
							exclusively lock sat_pos, polband....
							implies all the power of LNB_EXCLUSIVE
						*/
	};



struct tune_options_t {
	scan_target_t scan_target;
	tune_mode_t tune_mode;
	pls_search_range_t pls_search_range;
	retune_mode_t retune_mode{retune_mode_t::AUTO};

	int sat_pos{sat_pos_none}; /*only relevant if tune_mode ==tune_mode_t::LNB_EXCLUSIVE,
															 Its is used to switch the lnb to another sat during spectrum scan

															 For  tune_mode ==tune_mode_t::DISH_EXCLUSIVE (used during blindscan),
															 we rather rely on subscribe_mux, which has the desired sat_pos encoded
															 in the mux
														 */

	//only for spectrum acquisition
	spectrum_scan_options_t spectrum_scan_options;
	constellation_options_t constellation_options;

	//retune_mode_t retune_mode{retune_mode_t::ALLOWED}; //positioner not allowed when in positioner_dialog
	subscription_type_t subscription_type{subscription_type_t::NORMAL};

	explicit tune_options_t(scan_target_t scan_target =  scan_target_t::DEFAULT,
													 tune_mode_t tune_mode= tune_mode_t::NORMAL,
													subscription_type_t subscription_type = subscription_type_t::NORMAL)
		: scan_target(scan_target)
		, tune_mode(tune_mode)
		, subscription_type(subscription_type)
		{
		}

	inline void set_mux_blindtune(bool on) {
		tune_mode = on ? tune_mode_t::MUX_BLIND : tune_mode_t::NORMAL;
	}

	inline bool is_blind() const {
		return tune_mode ==  tune_mode_t::MUX_BLIND;
	}

};

class pol_band_status_t {
	bool tuned{false};
	int voltage = -1; // means unknown
	int tone = -1; // means unknown

public:
	bool is_tuned() const {
		return tuned;
	}

	/*
		returns -1 : error
		0: tone was already correct
		1: tone was set as wanted
	 */
	int set_voltage(int fefd, fe_sec_voltage v);

	fe_sec_tone_mode get_tone() const {
		return (fe_sec_tone_mode) tone;
	}

	/*
		returns -1 : error
		0: tone was already correct
		1: tone was set as wanted
	 */
	int set_tone(int fefd, fe_sec_tone_mode mode);

	/*
		TODO: (bad?) idea is to check the current state of the diseqc switch
		and not resend what is not needed
		returns the first different position
	 */

	void set_tune_status(bool tuned_) {
		tuned = tuned_;
	}
};

class usals_timer_t {
	int16_t usals_pos_start{sat_pos_none};
	int16_t usals_pos_end{sat_pos_none};
	bool started{false};
	steady_time_t start_time;
public:
	void end() {
		if(!started)
			return;
		started = false;
		auto now = steady_clock_t::now();
		auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
		auto speed = std::abs(usals_pos_end - usals_pos_start)*10. /(double) dur;
		dtdebugx("positioner moved from %d to %d in %ldms = %lf degree/s",
						 usals_pos_start, usals_pos_end, dur, speed);
	}
	void start(int16_t old_usals_pos, int16_t new_usals_pos) {
		start_time = steady_clock_t::now();
		usals_pos_start = old_usals_pos;
		usals_pos_end = new_usals_pos;
		started = true;

	}
};


/*@brief: all data for a transponder currently being monitored.
	owned by tuner_thread
 */
class active_adapter_t final : public  std::enable_shared_from_this<active_adapter_t>
{
	enum tune_state_t {
		TUNE_INIT, //we still need to tune
		WAITING_FOR_LOCK, //tuning was started, waiting for lock;
		LOCKED, //tuning was started, si processing is running
	};
	friend class si_t;
	friend class tuner_thread_t;
	friend class active_si_stream_t;
	friend class stream_reader_t;
	friend class dvb_stream_reader_t;
	//friend class rec_manager_t;

public:
	const std::shared_ptr<dvb_frontend_t> current_fe; //accessible by other threads (with some care?)
	thread_private_t<active_adapter_reservation_t> reservation{"receiver", this};
private:
/*
		for uncommitted switch closest to receiver, followed by committed switch.
		1. send the uncommitted switch
    2. then sen committed (will often fail because not powered on)
    3. send U becase we have to await anyway
    4. " " wait an additional 50 ms

		*/

	std::map <uint16_t, std::shared_ptr<stream_filter_t>> stream_filters; //indexed by stream_pid
	std::map <uint16_t, active_si_stream_t> embedded_si_streams; //indexed by stream_pid

	tune_state_t tune_state{TUNE_INIT};
	system_time_t tune_start_time;  //when last tune started
	constexpr static std::chrono::duration tune_timeout{50000ms}; //in ms

	struct pol_band_status_t pol_band_status;
	//tune_mode_t tune_mode{tune_mode_t::NORMAL};
	chdb::fe_delsys_t current_delsys = chdb::fe_delsys_t::SYS_UNDEFINED;
	safe::thread_public_t<false, chdb::any_mux_t> tuned_mux{"tuner", thread_group_t::tuner, {}};
	safe::thread_public_t<false, chdb::lnb_t> tuned_lnb{"tuner", thread_group_t::tuner, {}};

	usals_timer_t usals_timer;

	const chdb::any_mux_t& current_tp() const {
		return tuned_mux.owner_read_ref(); //only to be accessed from tuner_thread
	};

	const chdb::lnb_t& current_lnb() const {
		return tuned_lnb.owner_read_ref();
	}

	bool need_diseqc(const chdb::lnb_t& new_lnb, const chdb::dvbs_mux_t& new_mux);
	void set_current_tp(const chdb::any_mux_t& mux) {
		{
			auto w= tuned_mux.writeAccess();
			{
				auto* old = chdb::mux_key_ptr(*w);
				const auto* new_ = chdb::mux_key_ptr(mux);
				if(old->sat_pos != new_->sat_pos) {
					dtdebugx("set_current_tp: sat_pos changed from %d to %d", old->sat_pos, new_->sat_pos);
				}
				if(old->t2mi_pid != new_->t2mi_pid) {
					dtdebugx("set_current_tp: t2mi_pid changed from %d to %d", old->t2mi_pid, new_->t2mi_pid);
				}
			}
			*w = mux;
		}
		current_fe->update_tuned_mux_nit(mux);
	};

	void set_current_lnb(const chdb::lnb_t& lnb);



	//connected to the correct sat; TODO: replace with something based on lnb
	//tuner_stats_t fe_stats; //python interface: snr and such
	int change_delivery_system(chdb::fe_delsys_t delsys);
	uint32_t get_lo_frequency(uint32_t frequency);


public: //this data is safe to access from other threads
	receiver_t& receiver;
	tuner_thread_t& tuner_thread; //There is only one tuner thread for the whole process

	chdb::any_mux_t current_mux() const {  //currently tuned transponder
		//Do not call current_tp() as this will not lock mutex
		auto r =  tuned_mux.readAccess();
		return *r;
		//@todo make thread safe
	};

	bool uses_lnb(const chdb::lnb_t&lnb) const {
		return current_lnb().k == lnb.k;
		//@todo make thread safe
	}
	chdb::lnb_t
	get_lnb() const { //lnb currently in use
		return current_lnb();
		//@todo make thread safe
	}

	void update_lof(const ss::vector<int32_t,2>& lof_offsets);
private:
	int do_lnb_and_diseqc(chdb::fe_band_t band, fe_sec_voltage_t lnb_voltage);
	int do_lnb(chdb::fe_band_t band, fe_sec_voltage_t lnb_voltage);
	int diseqc(const std::string& diseqc_command);
	int clear();
	int positioner_cmd(chdb::positioner_cmd_t cmd, int par);
	int send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated);
	void handle_fe_event();
	void monitor();
	void init_si(scan_target_t scan_target_);
	void end_si();
private:

	std::map<uint16_t, std::tuple<uint16_t, std::shared_ptr<active_service_t>>> active_services; //indexed by service_id
	/*
		key is the service_id
		first value entry in the map is the current pmt_pid as known by the tuner thread.
		This may differ from active_service.current_pmt_id when a pmt change has not yet been noticed
		by the service thread
	*/


	//tuner_state_t tuner_state;
 	//fe_monitor_state_t fe_monitor_state;
	int retune_count{0}; //will be incremented after each retune
	tune_options_t tune_options;
	active_si_stream_t si;
	int remove_service(active_service_t& channel);
	int remove_all_services(rec_manager_t& recmgr);

public:

	active_adapter_t(receiver_t& receiver_, tuner_thread_t& tuner_thread_,
							 std::shared_ptr<dvb_frontend_t>& current_fe);
	active_adapter_t(active_adapter_t&& other) = delete;
	active_adapter_t(const active_adapter_t& other) = delete;

	active_adapter_t operator=(const active_adapter_t& other) = delete;

	virtual ~active_adapter_t() final;

	template<typename mux_t>
	bool is_tuned_to(const mux_t& mux, const chdb::lnb_t* required_lnb) {
		assert(current_fe.get());
		return current_fe->is_tuned_to(mux, required_lnb);
	}

private:

	template<typename mux_t> inline int retune();
	int restart_tune();


	int lnb_blind_scan(const chdb::lnb_t& lnb, tune_options_t tune_options);

	int lnb_spectrum_scan(const chdb::lnb_t& lnb, tune_options_t tune_options);

	int lnb_scan(const chdb::lnb_t& lnb, tune_options_t tune_options);

	int tune(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
					 tune_options_t tune_options, bool user_requested); //(re)tune to new transponder
	int tune(const chdb::dvbt_mux_t& mux,
					 tune_options_t tune_options, bool user_requested); //(re)tune to new transponder
	int tune(const chdb::dvbc_mux_t&mux,
					 tune_options_t tune_options, bool user_requested); //(re)tune to new transponder
	int retune(const chdb::lnb_t& lnb);
	int tune_it(tune_options_t tune_options, chdb::delsys_type_t delsys_type);

	int add_service(active_service_t& channel);//tune to channel on transponder
	std::tuple<bool, bool> check_status();

public:
	void lnb_update_usals_pos(int16_t usals_pos);
	int open_demux(int mode = O_RDWR | O_NONBLOCK) const;
	static int hi_lo(const chdb::lnb_t& lnb, const chdb::any_mux_t& tp);
	int frontend_no() const {
		return current_fe ? int(current_fe->frontend_no) : -1;
	}

	int get_adapter_no() const {
		return current_fe ? int(current_fe->adapter->adapter_no) : -1;
	}

	bool is_open() const {
		return current_fe.get() && current_fe->is_open();
	}

	std::shared_ptr<stream_reader_t> make_dvb_stream_reader(ssize_t dmx_buffer_size_ = -1);
	std::shared_ptr<stream_reader_t> make_embedded_stream_reader(int stream_pid, ssize_t dmx_buffer_size_ = -1);
	void add_embedded_si_stream(int stream_pid, bool start=false);

	bool read_and_process_data_for_fd(int fd);
private:
	int do_diseqc(bool log_strength, bool retry=false);
	int deactivate();
	void on_stable_pat();
	void on_tuned_mux_key_change(db_txn& wtxn, const chdb::mux_key_t& si_mux_key,
															 bool update_db, bool update_sat_pos);


//	int open_frontend();
//	void close_frontend();
};
