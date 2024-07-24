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
#include <thread>
#include "neumofrontend.h"
#include "util/util.h"
#include "neumodb/devdb/devdb_extra.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/tune_options.h"
#include "task.h"
#include "util/safe/safe.h"
#include "signal_info.h"
#include "util/access.h"
#include <boost/context/continuation_fcontext.hpp>

typedef boost::context::continuation continuation_t;

struct subscription_options_t;
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


/*official satellite position*/
enum class sat_pos_t : int {};

/*real usals position as sent to positioner*/
enum class usals_pos_t : int {};

/*virtual usals position, i.e, the usals position of an offset lnb if it would be in the center*/
enum class virt_usals_pos_t : int {};

/*official dish position, corresponding to sat_pos taking into account offset location
 Dish is pointed to this position */
enum class dish_pos_t : int {};

class dvb_frontend_t;

class dvbdev_monitor_t;
class adaptermgr_t;
class receiver_t;
class fe_monitor_thread_t;
struct signal_info_t;

namespace statdb {
	struct spectrum_t;
}



class cmdseq_t {
	std::vector<uint32_t> pls_codes;
	struct dtv_properties cmdseq{};
	std::array<struct dtv_property, 32> props;
	mutable int cursor = 0;
public:
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

	inline void add (int cmd) {
		assert(cmdseq.num < props.size()-1);
		memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
		cmdseq.props[cmdseq.num].cmd = cmd;
		cmdseq.num++;
	};

	/*
		used to iterate over properties by command instead of index, in an
		efficient manner if properties are accessed in teh same order they were requested
		from driver
		Requesting the same property multiple times is also fast
	 */
	inline const struct dtv_property* get(int cmd) const {
		for(int i=0; i < (int)cmdseq.num; ++i) {
			auto* ret = &cmdseq.props[cursor];
			if (ret->cmd == (uint32_t)cmd)
				return ret;
			cursor = (++cursor) % cmdseq.num;
		}
		return nullptr;
	}

	void add (int cmd, const dtv_fe_constellation& constellation);
	void add (int cmd, const dtv_matype_list& matype_list);
	void add_clear();
	void add_pls_codes(int cmd);
	void add_pls_code(int code) {
		pls_codes.push_back(code);
	}

	void add_pls_range(int cmd, const pls_search_range_t& pls_search_range);
	int tune(int fefd, int heartbeat_interval);
	int get_properties(int fefd);
	int scan(int fefd, bool init);
	int spectrum(int fefd, dtv_fe_spectrum_method method);
};


enum class api_type_t {UNDEFINED, DVBAPI, NEUMO};

struct spectrum_scan_t {
	/*
		with the current driver code, memory must be reserved in user space, before calling get_spectrum.
		Driver code could be modified so that it returns the number of frequencies based on inputs, and
		the number of candidates, but that would required a second ioctl call.

		for those drivers that can compute candidates, we may as well compute them in user space (and
		adapt the driver code, wh
	 */
	system_time_t start_time{};
	devdb::rf_path_t rf_path{};
	int16_t adapter_no{-1};
	int16_t usals_pos{sat_pos_none};
	chdb::sat_t sat;
	ss::vector<int32_t,2> lof_offsets;
	chdb::sat_sub_band_pol_t band_pol;
	dtv_fe_spectrum_method spectrum_method{SPECTRUM_METHOD_FFT};
	int32_t start_freq{-1}; //in kHz
	int32_t end_freq{-1}; //in kHz
	uint32_t resolution{0}; //in kHz for spectrum and for blindscan

	std::optional<statdb::spectrum_t> spectrum; //could be empty in case writing to filesystem fails
	static constexpr int max_num_freq{65536};
	static constexpr int max_num_peaks{512};
	ss::vector<uint32_t, max_num_freq> freq;
	ss::vector<int32_t, max_num_freq> rf_level;
	ss::vector<spectral_peak_t, max_num_peaks> peaks;


	~spectrum_scan_t() {
	}

	inline void resize( int num_freq, int num_candidates);

	inline void adjust_frequencies(const devdb::lnb_t& lnb, int high_band);

};


//owned by monitor thread
class signal_monitor_t {
	friend class fe_monitor_thread_t;
	statdb::signal_stat_t stat;

	float snr_sum{0.};
	float signal_strength_sum{0.};
	float ber_sum{0.};
	int stats_count{0};
	int tune_count{-1};

	void update_stat(receiver_t& receiver, const signal_info_t& info);
	void end_stat(receiver_t& receiver);

	public:
	signal_monitor_t()  {
	}
};

/*@brief: all reservation data for a tuner. reservations are modified atomically while holding
	a lock. Afterwards the tune is requested to change state to the new reserved state, but this
	may take some time

	fe_state_t has pubic members. It is assumed that this class is only accessed
	after taking a lock
 */
class fe_state_t {

public:
	int tune_count{0};
	tune_confirmation_t tune_confirmation; //have ts_id,network_id, sat_id been confirmed by SI data?
	chdb::any_mux_t reserved_mux;   /*mux as it is currently reserved. Will be updated with si data
																		and will always contain the best confirmed-to-be-correct information
																	*/
	std::optional<chdb::any_mux_t> received_si_mux; /* mux as received from the SI stream*/
	bool received_si_mux_is_bad{false}; //true if content is deemed incorrect
	devdb::lnb_t reserved_lnb; //lnb currently in use
	devdb::rf_path_t reserved_rf_path; //rf_path currently reserved

	inline devdb::lnb_connection_t reserved_lnb_connection() const {
		auto *conn = connection_for_rf_path(reserved_lnb, reserved_rf_path);
		assert(conn);
		return *conn;
	}

	mutable devdb::fe_t dbfe;
	bool info_valid{false}; // true if we could retrieve device info; "false" indicates an error
	int fefd{-1}; //file handle if open
	int last_saved_freq{0}; //for spectrum scan: last frequency written to spectrum file
	devdb::tune_mode_t tune_mode{devdb::tune_mode_t::IDLE};
	bool use_blind_tune{false};
	fe_lock_status_t lock_status;
	subscription_options_t tune_options;
	system_time_t start_time;
	std::optional<steady_time_t> positioner_start_move_time;

	void set_lock_status(api_type_t api_type, fe_status_t fe_status);

	std::optional<signal_info_t> last_signal_info; //last retrieved signal_info
	/*
		check if mux is the same transponder as the currently tuned on.
		This compares  atm frequency and polarisation, so as to account for possible
		differences in ts_id and network_id
	*/

	bool is_tuned_to(const chdb::any_mux_t& mux, const devdb::rf_path_t* required_rf_path,
									 bool ignore_t2mi_pid) const;
	bool is_tuned_to(const chdb::dvbs_mux_t& mux, const devdb::rf_path_t* required_rf_path,
									 bool ignore_t2mi_pid) const;
	//required_lnb is not actually used below
	bool is_tuned_to(const chdb::dvbc_mux_t& mux, const devdb::rf_path_t* required_rf_path,
									 bool ignore_t2mi_pid) const;
	bool is_tuned_to(const chdb::dvbt_mux_t& mux, const devdb::rf_path_t* required_rf_path,
									 bool ignore_t2mi_pid) const;
};

/*
	status of secondary equipment
 */
class sec_status_t {
	//int config_id{0}; //updates by at least 1 each time a  tuner is switched to a new configuration
	bool tuned{false};
	int rf_input{-1}; //-1 means unknown or never set
	std::optional<devdb::lnb_key_t> lnb_key; //set after diseqc has been sent
	int16_t sat_pos{sat_pos_none}; //set after diseqc has been sent
	bool rf_input_changed{false}; // true means that rf_input was changed and we have not set voltage yet
	fe_rf_input_control ic;

	int voltage{-1};  // -1 means unknown or never set
	int tone{-1};     // -1 means unknown or never set
	steady_time_t powerup_time; //time when lnb was last powered up (any voltage > 0)

public:
	int retune_count{0};
	bool is_tuned() const {
		return tuned;
	}

	sec_status_t() {
		ic.owner=-1;
		ic.config_id = -1;
		ic.rf_in = -1;
	}

	/*
		returns -1 : error
		0: tone was already correct
		1: tone was set as wanted
	 */
	int set_voltage(int fefd, fe_sec_voltage v);

	int set_rf_input(int fefd, int new_rf_input, const tune_pars_t& tune_pars);

	std::tuple<bool,bool> need_diseqc_or_lnb(const devdb::rf_path_t& new_rf_path, const devdb::lnb_t& new_lnb,
																					 int16_t new_sat_pos,  const subscription_options_t& tune_options);

	inline void diseqc_done(const devdb::lnb_key_t& lnb_key, int16_t sat_pos) {
		this->lnb_key = lnb_key;
		this->sat_pos = sat_pos;
	}
	inline fe_sec_tone_mode get_tone() const {
		return (fe_sec_tone_mode) tone;
	}

	inline fe_sec_voltage get_voltage() const {
		return (fe_sec_voltage) voltage;
	}

	inline int positioner_wait_after_powerup(int ms) const {
		assert(voltage != SEC_VOLTAGE_OFF);
		auto deadline  = powerup_time + std::chrono::milliseconds(ms);
		std::this_thread::sleep_until (deadline);
		dtdebugf("slept after powerup");
		return 0;
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

//owned by active_adapter_t and tuner_thread_t
class dvb_frontend_t : public std::enable_shared_from_this<dvb_frontend_t>
{
	friend class active_adapter_t;
	friend class tuner_thread_t;
	continuation_t main_fiber;
	continuation_t task_fiber;
	bool must_abort_task{false};
	adaptermgr_t* adaptermgr;

	chdb::delsys_type_t current_delsys_type { chdb::delsys_type_t::NONE };

	int num_constellation_samples{0};
	sec_status_t sec_status;
	std::shared_ptr<fe_monitor_thread_t> monitor_thread;

public:
	const api_type_t api_type { api_type_t::UNDEFINED };
	const int api_version{-1}; //1000 times the floating point value of version
	static constexpr uint32_t lnb_lof_standard = DEFAULT_LOF_STANDARD;
	static constexpr uint32_t  lnb_slof = DEFAULT_SLOF;
	static constexpr uint32_t lnb_lof_low = DEFAULT_LOF1_UNIVERSAL;
	static constexpr uint32_t lnb_lof_high = DEFAULT_LOF2_UNIVERSAL;

	const adapter_no_t adapter_no;
	const frontend_no_t frontend_no;

	adapter_mac_address_t adapter_mac_address{-1};
	card_mac_address_t card_mac_address{-1};

	safe::Safe<fe_state_t> ts;
	std::condition_variable ts_cv;
	safe::Safe<signal_monitor_t> signal_monitor;

private:
	__attribute__((optnone)) //Without this  the code will crash!
	void resume_task();
	bool wait_for(tuner_thread_t& tuner_thread, double seconds);

	void run_task(auto&& task);

	int check_frontend_parameters();
	uint32_t get_lo_frequency(uint32_t frequency);
	int send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated);
	int send_positioner_message(devdb::positioner_cmd_t cmd, int32_t par, bool repeated=false);

	std::tuple<bool, bool, bool>
	set_rf_path(tuner_thread_t& tuner_thread, int fefd, const devdb::rf_path_t& new_rf_path,
							const devdb::lnb_t& lnb, int16_t sat_pos,
							const subscription_options_t& tune_options, bool set_rf_input);


	int get_mux_info(signal_info_t& ret, const cmdseq_t& cmdseq, api_type_t api);

	void start_frontend_monitor();

	int start_lnb_spectrum_scan(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb);

	bool wait_for_positioner(tuner_thread_t& tuner_thread);

	int tune_(const chdb::dvbt_mux_t& mux, const subscription_options_t& options);
	int tune_(const chdb::dvbc_mux_t& mux, const subscription_options_t& options);
	int tune_(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
						const subscription_options_t& options);

	std::tuple<int, int>
	tune(tuner_thread_t& tuner_thread,
			 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
			 const subscription_options_t& tune_options);


	std::tuple<int, int>
	lnb_spectrum_scan(tuner_thread_t& tuner_thread, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
										const subscription_options_t& tune_options);


	template<typename mux_t>
	int tune(const mux_t& mux, const subscription_options_t& tune_options);

	template<typename mux_t>
	void request_retune(tuner_thread_t& tuner_thread, bool user_requested);

	void request_tune(tuner_thread_t& tuner_thread,
										const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
										const subscription_options_t& tune_options);


	template<typename mux_t>
	void request_tune(const mux_t& mux, const subscription_options_t& tune_options);

	void request_lnb_spectrum_scan(tuner_thread_t& tuner_thread,
																 const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																 const subscription_options_t& tune_options);

int request_positioner_control(tuner_thread_t& tuner_thread, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
															 const subscription_options_t& tune_options);

	std::tuple<int, int, int, double>
	get_positioner_move_stats(int16_t old_usals_pos, int16_t new_usals_pos, steady_time_t now) const;

	int request_signal_info(cmdseq_t& cmdseq, signal_info_t& ret, bool get_constellation);


	int stop();
	int start();
	void update_tuned_mux_nit(const chdb::any_mux_t& mux);
	void update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad);
	inline void reset_tuned_mux_tune_confirmation() {
		auto w = this->ts.writeAccess();
		w->tune_confirmation = {};
	}
	int reset_ts();

	int start_fe_and_lnb(const devdb::rf_path_t& rf_path, const devdb::lnb_t & lnb);

	int start_fe_lnb_and_mux(const devdb::rf_path_t& rf_path, const devdb::lnb_t & lnb, const chdb::dvbs_mux_t& mux);

	template<typename mux_t>
	int start_fe_and_dvbc_or_dvbt_mux(const mux_t& mux);

	/*!
		returns the new mux use count
	*/
	int release_fe();

	std::tuple<int, int> diseqc(int16_t sat_pos, chdb::sat_sub_band_t sat_sub_band,
															fe_sec_voltage_t lnb_voltage, bool skip_positioner);
	std::tuple<int, int> do_lnb_and_diseqc(int16_t sat_pos, chdb::sat_sub_band_t band, fe_sec_voltage_t lnb_voltage,
																				 bool skip_positioner);
	int do_lnb(chdb::sat_sub_band_t band, fe_sec_voltage_t lnb_voltage);
	int positioner_cmd(devdb::positioner_cmd_t cmd, int par);

	inline int set_tune_options(const subscription_options_t& tune_options) {
		auto w = this->ts.writeAccess();
		w->tune_options = tune_options;
		return 0;
	}

	inline void update_dbfe(const std::optional<devdb::fe_t>& dbfe) {
		this->ts.writeAccess()->dbfe = dbfe ? *dbfe : devdb::fe_t{};
	}
	inline std::optional<signal_info_t> get_last_signal_info(bool wait) {
		auto fn = [this] () {
			return ts.readAccess()->last_signal_info;
		};
		auto ret = fn();
		if (wait && !ret) {
			std::unique_lock<std::mutex> lk(ts.mutex());
			ts_cv.wait(lk, [&]() {
				ret = ts.readAccess(std::adopt_lock)->last_signal_info;//adopt_lock because ts.mutex() has been locked now
				return ret;
			});
		}
		return ret;
	}

public:
	dvb_frontend_t(adaptermgr_t* adaptermgr_,
								 adapter_no_t adapter_no_, frontend_no_t frontend_no_,
								 api_type_t api_type_,  int api_version_)
		: adaptermgr(adaptermgr_)
		, api_type(api_type_)
		, api_version(api_version_)
		, adapter_no(adapter_no_)
		, frontend_no(frontend_no_)
		{}

	static std::shared_ptr<dvb_frontend_t> make(adaptermgr_t* adaptermgr,
																							 adapter_no_t adapter_no,
																							 frontend_no_t frontend_no,
																							 api_type_t api_type,  int api_version);

	int open_device(bool rw=true);
	void close_device();

	void stop_frontend_monitor_and_wait();

	std::optional<signal_info_t> update_lock_status_and_signal_info(fe_status_t fe_status, bool get_constellation);
	std::optional<spectrum_scan_t> get_spectrum(const ss::string_& spectrum_path);

	fe_lock_status_t get_lock_status();
	void set_lock_status(fe_status_t fe_status);
	void clear_lock_status();

	static std::tuple<api_type_t, int> get_api_type(); //returns api_type and version
	devdb::usals_location_t get_usals_location() const;

	inline chdb::any_mux_t tuned_mux() const {
		return this->ts.readAccess()->reserved_mux;
	}

	inline devdb::fe_key_t fe_key() const {
		return this->ts.readAccess()->dbfe.k;
	}

	inline devdb::fe_t dbfe() const {
		return this->ts.readAccess()->dbfe;
	}

	inline auto get_subscription_ids() const {
		ss::vector<subscription_id_t, 4> subscription_ids;
		auto r = this->ts.readAccess();
		for(auto& sub: r->dbfe.sub.subs) {
			subscription_ids.push_back((subscription_id_t)sub.subscription_id);
		}
		return subscription_ids;
	}

	template<typename mux_t>
	inline bool is_tuned_to(const mux_t& mux, const devdb::rf_path_t* required_rf_path,
													bool ignore_t2mi_pid=false) const;

	inline bool is_open() const {
		auto t = ts.readAccess();
		return t->fefd >= 0;
	}
	inline bool is_fefd(int fd) const {
		auto t = ts.readAccess();
		return t->fefd ==fd;
	}

	~dvb_frontend_t();

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
	bool is_paused{false};
	std::shared_ptr<dvb_frontend_t> fe{nullptr};

	void update_lock_status_and_signal_info(fe_status_t fe_status);
	virtual int exit() final;

	inline void handle_frontend_event();
public:
	class cb_t;

	fe_monitor_thread_t(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_)
		: task_queue_t(thread_group_t::fe_monitor)
		, receiver(receiver_)
		, fe(fe_)
		{
		}

	bool wait_for(double seconds);
	static std::shared_ptr<fe_monitor_thread_t> make
	(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_);

	int run();
};



class fe_monitor_thread_t::cb_t : public fe_monitor_thread_t { //callbacks
public:
	int pause();
	int unpause();
};



/*
	represents all adapters in the system, even those which are not used.
	Dead adapters have can_be_used==false
 */
class adaptermgr_t {
	friend class receiver_thread_t;
	friend class dvbdev_monitor_t;
	friend class dvb_adapter_t;
	friend class fe_state_t;
private:
	api_type_t api_type { api_type_t::UNDEFINED };
	int api_version{-1}; //1000 times the floating point value of version

	using frontend_map = std::map<std::tuple<adapter_no_t, frontend_no_t>, std::shared_ptr<dvb_frontend_t>>;
	using safe_frontend_map = safe::thread_public_t<false, frontend_map>;
	safe_frontend_map frontends{"receiver", thread_group_t::receiver, {}}; //frontends, indexed by (adapter_no, frontend_no)
	int inotfd{-1};

	virtual ~adaptermgr_t() = default; //to make dynamic_cast work
	adaptermgr_t(receiver_t& receiver_)
		: receiver(receiver_)
		{}

	inline std::shared_ptr<dvb_frontend_t>	find_fe(const devdb::fe_key_t& fe_key) const {
		auto [it, found] = find_in_safe_map_if_with_owner_read_ref(frontends, [&fe_key](const auto& x) {
			auto& [key_, fe] = x;
			return (int64_t) fe->adapter_mac_address == fe_key.adapter_mac_address &&
				(int) fe->frontend_no == fe_key.frontend_no;
		});
		return found ? it->second : nullptr; //TODO
	}

public:
	receiver_t& receiver;

	int get_fd() const {
		return inotfd;
	}
	int run();
	int start();
	int stop();
	std::tuple<std::string, int> get_api_type() const;

	void renumber_card(int old_number, int new_number);

	inline std::shared_ptr<dvb_frontend_t>	fe_for_dbfe(const devdb::fe_key_t& fe_key) const {
		auto [it, found] = find_in_safe_map_if(frontends, [&fe_key](const auto& x) {
			auto& [key_, fe] = x;
			return (int64_t) fe->adapter_mac_address == fe_key.adapter_mac_address &&
				(int) fe->frontend_no == fe_key.frontend_no;
		});
		return found ? it->second : nullptr; //TODO
	}



	static std::shared_ptr<adaptermgr_t> make(receiver_t& receiver);
};
