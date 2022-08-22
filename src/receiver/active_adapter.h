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
#include "devmanager.h"
#include "reservation.h"
#include "active_si_stream.h"
#include "devmanager.h"
#include "tune_options.h"
#include <bitset>

using namespace dtdemux;

class active_fe_thread_safe_t;
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





class usals_timer_t {
	int16_t usals_pos_start{sat_pos_none};
	int16_t usals_pos_end{sat_pos_none};
	bool started{false};
	bool stamped{false};
	steady_time_t start_time;
	steady_time_t first_pat_time;
public:
	inline void start(int16_t old_usals_pos, int16_t new_usals_pos) {
		start_time = steady_clock_t::now();
		usals_pos_start = old_usals_pos;
		usals_pos_end = new_usals_pos;
		started = true;

	}
	inline void stamp() {
		if(!started)
			return;
		stamped = true;
		first_pat_time = steady_clock_t::now();
		dtdebugx("positioner stamp");
	}

	void end() {
		if(!started)
			return;
		started = false;
		auto end = stamped ? first_pat_time : steady_clock_t::now();
		auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_time).count();
		auto speed = std::abs(usals_pos_end - usals_pos_start)*10. /(double) dur;
		dtdebugx("positioner moved from %d to %d in %ldms = %lf degree/s",
						 usals_pos_start, usals_pos_end, dur, speed);
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
	friend struct dvb_stream_reader_t;
	//friend class rec_manager_t;

public:
	receiver_t& receiver;
	const std::shared_ptr<dvb_frontend_t> fe; //accessible by other threads (with some care?)
	thread_private_t<active_fe_thread_safe_t> reservation{"receiver", this};
private:
/*
		for uncommitted switch closest to receiver, followed by committed switch.
		1. send the uncommitted switch
    2. then sen committed (will often fail because not powered on)
    3. send U becase we have to await anyway
    4. " " wait an additional 50 ms

		*/
	std::bitset<256> processed_isis;
	safe::Safe<std::map <uint16_t, std::shared_ptr<stream_filter_t>>> stream_filters; //indexed by stream_pid
	std::map <uint16_t, active_si_stream_t> embedded_si_streams; //indexed by stream_pid

	tune_state_t tune_state{TUNE_INIT};
	system_time_t tune_start_time;  //when last tune started
	constexpr static std::chrono::duration tune_timeout{15000ms}; //in ms

	//safe::thread_public_t<false, chdb::any_mux_t> tuned_muxxxx{"tuner", thread_group_t::tuner, {}};
	usals_timer_t usals_timer;

	chdb::any_mux_t current_tp() const {
		return fe->tuned_mux();
	};

#if 1
	inline const devdb::lnb_t& current_lnb() const {
		return fe->ts.readAccess()->reserved_lnb;
	}
#endif
	void set_current_tp(const chdb::any_mux_t& mux) {
		fe->update_tuned_mux_nit(mux);
	};

	void update_current_lnb(const devdb::lnb_t& lnb);

	//connected to the correct sat; TODO: replace with something based on lnb
	//tuner_stats_t fe_stats; //python interface: snr and such

	uint32_t get_lo_frequency(uint32_t frequency);


public: //this data is safe to access from other threads
	tuner_thread_t& tuner_thread; //There is only one tuner thread for the whole process

	chdb::any_mux_t current_mux() const {  //currently tuned transponder
		return current_tp();
		//@todo make thread safe
	};

	inline devdb::lnb_key_t get_lnb_key() const {
		return fe->ts.readAccess()->reserved_lnb.k;
	}

	bool uses_lnb(const devdb::lnb_t&lnb) const {
		return fe->ts.readAccess()->reserved_lnb.k == lnb.k;
		//@todo make thread safe
	}
	devdb::lnb_t
	get_lnb() const { //lnb currently in use
		return fe->ts.readAccess()->reserved_lnb;
		//@todo make thread safe
	}

	void update_lof(fe_thread_safe_t& ts, const ss::vector<int32_t,2>& lof_offsets);
private:
	int clear();
	int send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated);
	void handle_fe_event();
	void monitor();
	void prepare_si(const chdb::any_mux_t mux, bool start);
	void init_si(scan_target_t scan_target_);
	void end_si();
private:

	std::map<uint16_t, std::tuple<uint16_t, std::shared_ptr<active_service_t>>> active_services; //indexed by service_id
	/*
		key is the service_id
		first value entry in the map is the current pmt_pid as known by the tuner thread.
		This may differ from active_service.current_pmt_pid when a pmt change has not yet been noticed
		by the service thread
	*/
	active_si_stream_t si;
	int remove_service(active_service_t& channel);
	int remove_all_services(rec_manager_t& recmgr);

public:

	active_adapter_t(receiver_t& receiver_,
									 std::shared_ptr<dvb_frontend_t>& fe_);
	active_adapter_t(active_adapter_t&& other) = delete;
	active_adapter_t(const active_adapter_t& other) = delete;

	active_adapter_t operator=(const active_adapter_t& other) = delete;

	virtual ~active_adapter_t() final;

	template<typename mux_t>
	bool is_tuned_to(const mux_t& mux, const devdb::lnb_t* required_lnb) {
		assert(fe.get());
		return fe->is_tuned_to(mux, required_lnb);
	}

private:

	template<typename mux_t> inline int retune();
	int restart_tune();


	int lnb_blind_scan(const devdb::lnb_t& lnb, tune_options_t tune_options);

	int lnb_spectrum_scan(const devdb::lnb_t& lnb, tune_options_t tune_options);

	int lnb_activate(const devdb::lnb_t& lnb, tune_options_t tune_options);

	int tune(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
					 tune_options_t tune_options, bool user_requested); //(re)tune to new transponder

	template<typename mux_t>
	int tune(const mux_t& mux, tune_options_t tune_options, bool user_requested);

	int retune(const devdb::lnb_t& lnb);
	int add_service(active_service_t& channel);//tune to channel on transponder
	std::tuple<bool, bool> check_status();

	void  update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation);
	int do_diseqc(bool log_strength, bool retry=false);
	int deactivate();
	void on_stable_pat();
	void on_first_pat();
	void on_tuned_mux_change(const chdb::any_mux_t& si_mux);
	void update_bad_received_si_mux(const std::optional<chdb::any_mux_t>& mux);

public:
	void lnb_update_usals_pos(int16_t usals_pos);
	int open_demux(int mode = O_RDWR | O_NONBLOCK) const;

	int frontend_no() const {
		return fe ? int(fe->frontend_no) : -1;
	}

	int get_adapter_no() const {
		return fe ? int(fe->adapter_no) : -1;
	}

	int64_t get_adapter_mac_address() const {
		return fe ? int64_t(fe->adapter_mac_address) : -1;
	}

	bool is_open() const {
		return fe.get() && fe->is_open();
	}

	std::shared_ptr<stream_reader_t> make_dvb_stream_reader(ssize_t dmx_buffer_size_ = -1);
	std::shared_ptr<stream_reader_t> make_embedded_stream_reader(const chdb::any_mux_t& mux,
																															 ssize_t dmx_buffer_size_ = -1);
	void add_embedded_si_stream(const chdb::any_mux_t& emdedded_mux, bool start=false);

	bool read_and_process_data_for_fd(int fd);

};
