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
//#include <atomic>
#include <thread>
#include "neumofrontend.h"
#include "util/util.h"
#include "neumodb/chdb/chdb_extra.h"
//#include "neumodb/statdb/statdb_extra.h"
#include "task.h"
#include "util/safe/safe.h"

struct tune_options_t;

/* DVB-S */
/** lnb_slof: switch frequency of LNB */
#define DEFAULT_SLOF (11700*1000UL)
/** lnb_lof1: local frequency of lower LNB band */
#define DEFAULT_LOF1_UNIVERSAL (9750*1000UL)
/** lnb_lof2: local frequency of upper LNB band */
#define DEFAULT_LOF2_UNIVERSAL (10600*1000UL)
/** Lnb standard Local oscillator frequency*/
#define DEFAULT_LOF_STANDARD (10750*1000UL)


/*official satellite position*/
enum class sat_pos_t : int {};

/*real usals position as sent to positioner*/
enum class usals_pos_t : int {};

/*virtual usals position, i.e, the usals position of an offset lnb if it would be in the center*/
enum class virt_usals_pos_t : int {};

/*official dish position, corresponding to sat_pos taking into account offset location
 Dish is pointed to this position */
enum class dish_pos_t : int {};



unconvertable_int(int, adapter_no_t);
unconvertable_int(int, frontend_no_t);

class dvb_adapter_t;
class dvb_frontend_t;
class dvbdev_monitor_t;
class adaptermgr_t;
class receiver_t;
class fe_monitor_thread_t;

enum class tune_mode_t {
	IDLE,
	NORMAL,
	MUX_BLIND,
	SPECTRUM,
	SCAN_BLIND,
	POSITIONER_CONTROL,
	UNCHANGED
	};

enum class retune_mode_t {
	AUTO, //for normal tuning: retune if lock failed or if wrong sat detected
	NEVER, //never retune
	IF_NOT_LOCKED,
	UNCHANGED
	};


namespace chdb {
	struct signal_info_t;
}

namespace statdb {
	struct spectrum_t;
}

struct pls_search_range_t {
	int start{-1};
	int end{-1};
	chdb::fe_pls_mode_t pls_mode;
	int timeoutms{25};
};

	enum class confirmed_by_t {
		NONE,
		PAT,
		SDT,
		NIT,
		AUTO,
		FAKE
	};
	struct tune_confirmation_t {
		constexpr static  std::chrono::duration sat_pos_change_timeout = 15000ms; //in ms

		bool on_wrong_sat{false};
		bool unstable_sat{false}; //caused by dish motion; return needed
		confirmed_by_t sat_by{confirmed_by_t::NONE};
		confirmed_by_t ts_id_by{confirmed_by_t::NONE};
		confirmed_by_t network_id_by{confirmed_by_t::NONE};
		bool nit_actual_seen{false};
		bool nit_actual_ok{false};
		bool sdt_actual_seen{false};
		bool sdt_actual_ok{false};
		bool pat_seen{false};
		bool pat_ok{false};
		bool si_done{false};
		void clear(bool preserve_wrong_sat) {
			*this = tune_confirmation_t();
		}

		tune_confirmation_t()
			{}
		bool operator== (const tune_confirmation_t& other) {
			return on_wrong_sat == other.on_wrong_sat &&
				sat_by == other.sat_by &&
				ts_id_by == other.ts_id_by &&
				network_id_by == other.network_id_by &&
				si_done == other.si_done &&
				nit_actual_ok == other.nit_actual_ok &&
				sdt_actual_ok == other.sdt_actual_ok &&
				pat_ok == other.pat_ok;
		}
		bool operator!= (const tune_confirmation_t& other) {
			return ! operator==(other);
		}

	};

struct constellation_options_t {
	//bool get_constellation{false};
	int num_samples{0};
};


struct spectrum_scan_options_t {
	time_t start_time{};
	bool append{false}; //append to existing file
	int16_t sat_pos{sat_pos_none};
	chdb::fe_band_pol_t band_pol; //currently scanning band
	bool scan_both_polarisations{false}; //
	dtv_fe_spectrum_method spectrum_method{SPECTRUM_METHOD_FFT};
	int start_freq{0}; //in kHz
	int end_freq{std::numeric_limits<int>::max()}; //in kHz
	int resolution{0}; //in kHz for spectrum and for blindscan, 0 means: use driver default
	int fft_size{256}; //power of 2; 	int end_freq = -1; //in kHz
	spectrum_scan_options_t() {
		start_time = system_clock::to_time_t(now);
	}
};

struct cmdseq_t {
	std::vector<uint32_t> pls_codes;
	struct dtv_properties cmdseq{};
	std::array<struct dtv_property, 16> props;

	cmdseq_t() {
		cmdseq.props = & props[0];
	}

	void init_pls_codes();

	template<typename T>
	void add (int cmd, T data) {
		assert(cmdseq.num < props.size()-1);
		memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
		cmdseq.props[cmdseq.num].cmd = cmd;
		cmdseq.props[cmdseq.num].u.data = (int)data;
		cmdseq.num++;
	};

	void add (int cmd, const dtv_fe_constellation& constellation);
	void add_clear();
	void add_pls_codes(int cmd);
	void add_pls_range(int cmd, const pls_search_range_t& pls_search_range);
	int tune(int fefd, int heartbeat_interval);
	int scan(int fefd, bool init);
	int spectrum(int fefd, dtv_fe_spectrum_method method);
};


enum class  api_type_t {UNDEFINED, DVBAPI, NEUMO};

struct spectrum_scan_t {
	/*
		with the current driver code, memory must be reserved in user space, before calling get_spectrum.
		Driver code could be modified so that it returns the number of frequencies based on inputs, and
		the number of candidates, but that would required a second ioctl call.

		for those drivers that can compute candidates, we may as well compute them in user space (and
		adapt the driver code, wh
	 */
	time_t start_time{};
	chdb::lnb_key_t lnb_key;
	int16_t usals_pos{sat_pos_none};
	int16_t sat_pos{sat_pos_none};
	ss::vector<int32_t,2> lof_offsets;
	chdb::fe_band_pol_t band_pol;
	dtv_fe_spectrum_method spectrum_method{SPECTRUM_METHOD_FFT};
	int32_t start_freq{-1}; //in kHz
	int32_t end_freq{-1}; //in kHz
	uint32_t resolution{0}; //in kHz for spectrum and for blindscan

	static constexpr int max_num_freq{65536};
	static constexpr int max_num_peaks{512};
	ss::vector<uint32_t, max_num_freq> freq;
	ss::vector<int32_t, max_num_freq> rf_level;
	ss::vector<spectral_peak_t, max_num_peaks> peaks;


	~spectrum_scan_t() {
	}

	inline void resize( int num_freq, int num_candidates);

	inline void adjust_frequencies(const chdb::lnb_t& lnb, int high_band);

};


//owned by monitor thread
class dvb_frontend_t
{
	friend class fe_monitor_thread_t;
	api_type_t api_type { api_type_t::UNDEFINED};
	int tuned_frequency{0}; // as reported by driver, compensated for lnb offset
public:
	static  api_type_t get_api_type() ;

	struct lock_status_t {
		bool lock_lost{false}; //
		fe_status_t fe_status{};
	};


	struct thread_safe_t {
		chdb::fe_t dbfe;
		bool can_be_used{false}; // true if device can be opened in write mode
		bool info_valid{false}; // true if we could retrieve device info; "false" indicates an error
		int fefd{-1}; //file handle if open
		int last_saved_freq{0}; //for spectrum scan: last frequency written to spectrum file
		tune_mode_t tune_mode{tune_mode_t::IDLE};
		spectrum_scan_options_t spectrum_scan_options;
		lock_status_t lock_status;
	};

	const frontend_no_t frontend_no;
	safe::Safe<thread_safe_t>  ts;
	dvb_adapter_t* const adapter{nullptr};
private:
	std::weak_ptr<fe_monitor_thread_t> monitor_thread_;
	int num_constellation_samples{0};

	int check_frontend_parameters();
	uint32_t get_lo_frequency(uint32_t frequency);
public:
	static constexpr uint32_t lnb_lof_standard = DEFAULT_LOF_STANDARD;
	static constexpr uint32_t  lnb_slof = DEFAULT_SLOF;
	static constexpr uint32_t lnb_lof_low = DEFAULT_LOF1_UNIVERSAL;
	static constexpr uint32_t lnb_lof_high = DEFAULT_LOF2_UNIVERSAL;
private:
	static int get_frontend_info(const adapter_no_t adapter_no,
															 const frontend_no_t  frontend_no, thread_safe_t& t);
public:
	std::shared_ptr<fe_monitor_thread_t> get_monitor_thread() {
		return monitor_thread_.lock(); //convert to shared_ptr
	}
	void set_lock_status(fe_status_t fe_status);

	lock_status_t get_lock_status();
	int tune(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, const tune_options_t& options);
	int tune(const chdb::dvbt_mux_t& mux, bool blindscan);
	int tune(const chdb::dvbc_mux_t& mux, bool blindscan);
	int start_lnb_spectrum_scan(const chdb::lnb_t& lnb, spectrum_scan_options_t spectrum_scan_options);
	void set_monitor_thread(const std::weak_ptr<fe_monitor_thread_t>& thr) {
		monitor_thread_ = thr;
	}

	int send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated);
	int send_positioner_message(chdb::positioner_cmd_t cmd, int32_t par, bool repeated=false);

	//int send_usals_message(double angle);

	int clear();
private:
	int open_device(thread_safe_t& t, bool rw=true, bool allow_failure=false);
	void close_device(thread_safe_t& t); //callable from main thread
public:
	void update_tuned_mux_nit(const chdb::any_mux_t& mux);
	void  update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation);
private:

	void set_lnb_lof_offset(const chdb::dvbs_mux_t& dvbs_mux, chdb::lnb_t& lnb);
	void get_signal_info(chdb::signal_info_t& signal_info, bool get_constellation);
	void get_mux_info(chdb::signal_info_t& ret, struct dtv_properties& cmdseq, api_type_t api, int& i);
	std::optional<statdb::spectrum_t> get_spectrum(const ss::string_& spectrum_path);

public:
	dvb_frontend_t(dvb_adapter_t* adapter_, frontend_no_t
								 frontend_no_)
		: frontend_no(frontend_no_)
		, adapter(adapter_)
		{}

	static std::shared_ptr<dvb_frontend_t> make (dvb_adapter_t* adapter, frontend_no_t frontend_no);

	template<typename mux_t>
	bool is_tuned_to(const mux_t& mux, const chdb::lnb_t* required_lnb) const;

	bool is_open() const {
		auto t = ts.readAccess();
		return t->fefd >= 0;
	}
	bool is_fefd(int fd) const {
		auto t = ts.readAccess();
		return t->fefd ==fd;
	}

	~dvb_frontend_t() {
		//printf("destructor\n");
	}
};



class use_count_t {
	friend class receiver_thread_t;
	int use_count =0; //indicates the number of subscriptions having reserved this tuner
	//a tuner reserved by more than two subscriptions cannot change frequency
	//a reserved tuner can be inactive (current_tp_active)

 protected:
	virtual void on_zero_use_count() {}
 public:
	use_count_t()
	{}

	/*! returns the old use count
	 */
	int register_subscription() {
		return use_count++;
	}

	/*!
		returns the new use count
	*/
	int unregister_subscription() {
		use_count--;
		assert(use_count>=0);
		if(use_count<0)
			use_count=0;
		if(use_count == 0)
			on_zero_use_count();
		return use_count;
	}

	int operator()() const {
		return use_count;
	}
};




/*@brief: all reservation data for a tuner. reservations are modified atomically while holding
	a lock. Afterwards the tune is requested to change state to the new reserved state, but this
	may take some time

	adapter_reservation_t has pubic membes. It is assumed that this class is only accessed
	after taking a lock
 */
class adapter_reservation_t {
	friend class dvb_adapter_t;

	dvbdev_monitor_t* adaptermgr;
	dvb_adapter_t * adapter;

public:
	use_count_t use_count_mux; /* if >0 it is not allowed to change the mux; this implies
															a sat and polarisation_band reservation (those use_counts are NOT incremented)
													 */
	use_count_t use_count_polband; //if >0 it is not allowed to change polarisation and band

	tune_confirmation_t tune_confirmation; //have ts_id,network_id, sat_id been confirmed by SI data?
	chdb::any_mux_t reserved_mux;   //mux as it is currently reserved. Will be updated with si data
	chdb::lnb_t reserved_lnb; //lnb currently in use

	dvb_frontend_t* reserved_fe{nullptr};
	bool is_reserved_fe(dvb_frontend_t* fe) const {
			return fe == reserved_fe;
		}


	bool exclusive{false}; //single user can change lnb at will; no slaves allowed

	/*TODO: we also need an exclusive reservation for channel searching
		exclusive could mean two things (still to decide):
		1. only owner can reserve anything
		2. only owner can reserve anything, but has to respect reserved_sat_pos reserved_band and reserved_polarisation.
		Option 2 would be very useful when searching with coupled tuners

	 */
public:

	adapter_reservation_t(dvbdev_monitor_t* adaptermgr, dvb_adapter_t* adapter)
		: adaptermgr(adaptermgr)
		, adapter(adapter) {
	}

	/*
		check if mux is the same transponder as the currently tuned on.
		This compares  atm frequency and polarisation, so as to account for possible
		differences in ts_id and network_id
	*/

	bool is_tuned_to(const chdb::any_mux_t& mux, const chdb::lnb_t* required_lnb) const;
	bool is_tuned_to(const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb) const;
	//required_lnb is not actually used below
	bool is_tuned_to(const chdb::dvbc_mux_t& mux, const chdb::lnb_t* required_lnb) const;
	bool is_tuned_to(const chdb::dvbt_mux_t& mux, const chdb::lnb_t* required_lnb) const;

	std::shared_ptr<dvb_frontend_t>
	can_tune_to(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
							bool adapter_will_be_released, bool blindscan) const;

	template<typename mux_t>
	std::shared_ptr<dvb_frontend_t>
	can_tune_to(const mux_t& mux, bool adapter_will_be_released, bool blindscan) const;

};


class dvb_adapter_t
{
	friend class dvbdev_monitor_t;
	friend class adaptermgr_t;
	dvbdev_monitor_t* adaptermgr;

	void update_adapter_can_be_used();
	std::map<frontend_no_t, std::shared_ptr<dvb_frontend_t>>  frontends;
public:

	const adapter_no_t adapter_no;

	safe::thread_public_t<true, adapter_reservation_t> reservation;

	bool can_be_used{false}; // true if all frontends can opened for write

	std::shared_ptr<dvb_frontend_t> fe_for_delsys(chdb::fe_delsys_t delsys) const;
	dvb_adapter_t(dvbdev_monitor_t* adaptermgr_, int adapter_no_)
		: adaptermgr(adaptermgr_)
		, adapter_no(adapter_no_)
		, reservation("receiver", thread_group_t::receiver, {}, adaptermgr_, this)
		{}

	/*!
		functions return old use count
	 */

	//exclusive reservation; if wint_move_dish: dish will not move after reservation
	int reserve_fe(dvb_frontend_t*fe, const chdb::lnb_t & lnb, bool wont_move_dish);

	int reserve_fe(dvb_frontend_t*fe, const chdb::lnb_t & lnb, const chdb::dvbs_mux_t& mux
								 /*,
									 reservation_type_t reservation_type*/);
	int reserve_fe(dvb_frontend_t*fe, const chdb::dvbc_mux_t& mux);
	int reserve_fe(dvb_frontend_t*fe, const chdb::dvbt_mux_t& mux);

	chdb::usals_location_t get_usals_location() const;

	int change_fe(dvb_frontend_t*fe, const chdb::lnb_t & lnb, int sat_pos);
	/*!
		change to different mux on same frontend
	 */
	int change_fe(dvb_frontend_t*fe, const chdb::lnb_t & lnb, const chdb::dvbs_mux_t& mux);

	int change_fe(dvb_frontend_t*fe, const chdb::dvbt_mux_t& mux);

	int change_fe(dvb_frontend_t*fe, const chdb::dvbc_mux_t& mux);


	/*!
		returns the new mux use count
	*/
	int release_fe();
};


/*
	shared_from_this
	needed because thread will destroy its own data safely
	as opposed to some other thread waiting for this fe_monitor thread
	to exit and then destroying the fe_monitor_thread_t data structure.
	We do not want to wait, as FE_MON syscalls can be quite slow
	in some cases*/

class fe_monitor_thread_t : public task_queue_t, public std::enable_shared_from_this<fe_monitor_thread_t> {

public:
		receiver_t& receiver;

private:

	dvb_frontend_t* fe{nullptr};

	void monitor_signal();
	virtual int exit() final {
		return -1;
	}

	//void update_mpv(const chdb::signal_info_t& info);

	inline void handle_frontend_event();
public:
	class cb_t;

	fe_monitor_thread_t(receiver_t& receiver_, dvb_frontend_t* fe_)
		: task_queue_t(thread_group_t::fe_monitor)
		, receiver(receiver_)
		, fe(fe_)
		{
		}

	static std::shared_ptr<fe_monitor_thread_t> make
	(receiver_t& receiver_, dvb_frontend_t* fe_);

	int run();
};



class fe_monitor_thread_t::cb_t : public fe_monitor_thread_t { //callbacks
public:
	chdb::signal_info_t get_signal_info();
};



/*
	represents all adapters in the system, even those which are not used.
	Dead adapters have can_be_used==false
 */
class adaptermgr_t {
	friend class receiver_thread_t;
	friend class dvbdev_monitor_t;
	friend class dvb_adapter_t;
	friend class adapter_reservation_t;
private:
	std::map<adapter_no_t, dvb_adapter_t> adapters;
	int inotfd{-1};
	std::map<dvb_frontend_t*, std::shared_ptr<fe_monitor_thread_t>> monitors;

	virtual ~adaptermgr_t() = default; //to make dynamic_cast work
	adaptermgr_t(receiver_t& receiver_)
		: receiver(receiver_)
		{}

	dvb_adapter_t*	find_adapter(int adapter_no) {
		auto it_adapter = adapters.find(adapter_no_t(adapter_no));
		return (it_adapter == adapters.end()) ?  nullptr: &it_adapter->second;
	}

	int num_adapters() const {
		return adapters.size();
	}

public:
	receiver_t& receiver;

	template<typename mux_t>
	std::shared_ptr<dvb_frontend_t>
	find_adapter_for_tuning_to_mux
	(db_txn& txn, const mux_t& mux, const dvb_adapter_t* adapter_to_release, bool blindscan) const;


	std::tuple<std::shared_ptr<dvb_frontend_t>,  chdb::lnb_t>
	find_lnb_for_tuning_to_mux
	(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
	 const dvb_adapter_t* adapter_to_release, bool blindscan) const;

	std::shared_ptr<dvb_frontend_t>
	find_fe_for_lnb
	(const chdb::lnb_t& lnb,
	 const dvb_adapter_t* adapter_to_release, bool need_blindscan, bool need_spectrum) const;


	int get_fd() const {
		return inotfd;
	}
	int run();
	int start();
	int stop();
	static std::shared_ptr<adaptermgr_t> make(receiver_t& receiver);
	void dump(FILE* fp) const;

	void start_frontend_monitor(dvb_frontend_t* fe);

	void stop_frontend_monitor(dvb_frontend_t* fe);

};
