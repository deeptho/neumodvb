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
#include "recmgr.h"

class playback_mpm_t;
class active_adapter_t;
struct tune_options_t;

namespace dtdemux {
	struct pmt_info_t;
}


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
	friend class active_adapter_t;
	friend class active_si_stream_t;
	friend class si_t;
	friend class rec_manager_t;

	receiver_t& receiver;
	std::thread::id thread_id;
	std::map<subscription_id_t, std::shared_ptr<active_adapter_t>> active_adapters;

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
	void on_epg_update(db_txn& txnepg, system_time_t now,
										 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																											to true or false*/);
	virtual int exit();
	inline std::shared_ptr<active_adapter_t> make_active_adapter(const devdb::fe_t& dbfe);
	std::shared_ptr<active_adapter_t> active_adapter_for_subscription(subscription_id_t subscription_id);

	void release_all(subscription_id_t subscription_id);

	std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
	tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
					 const tune_options_t& tune_options);
	void add_si(active_adapter_t& active_adapter, const chdb::any_mux_t& mux,
							const tune_options_t& tune_options, subscription_id_t subscription_id);
	int tune(std::shared_ptr<active_adapter_t> active_adapter, const devdb::rf_path_t& rf_path,
					 const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, tune_options_t tune_options,
					 const devdb::resource_subscription_counts_t& use_counts,
					 subscription_id_t subscription_id);
	template<typename _mux_t>
	int tune(std::shared_ptr<active_adapter_t> tuner, const _mux_t& mux, tune_options_t tune_options,
					 subscription_id_t subscription_id);

	std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
	subscribe_mux(const subscribe_ret_t& sret,
								const chdb::any_mux_t& mux,
								const tune_options_t& tune_options);
	void stop_recording(recdb::rec_t rec); // important that this is not a reference (async called)
public:
	tuner_thread_t(receiver_t& receiver_);

	~tuner_thread_t();

	tuner_thread_t(tuner_thread_t&& other) = delete;
	tuner_thread_t(const tuner_thread_t& other) = delete;
	tuner_thread_t operator=(const tuner_thread_t& other) = delete;
	inline int set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options);

public:

	class cb_t;

};

class tuner_thread_t::cb_t: public tuner_thread_t { //callbacks
public:
	int release_active_adapter(subscription_id_t subscription_id);
#if 0
	int remove_service(active_adapter_t& active_adapter, subscription_id_t subscription_id, active_service_t& channel);
#endif
	int on_pmt_update(active_adapter_t& active_adapter, const dtdemux::pmt_info_t& pmt);
	int update_service(const chdb::service_t& service);

	int lnb_activate(subscription_id_t subscription_id, const subscribe_ret_t& ret,
									 tune_options_t tune_options);

	std::tuple<subscription_id_t, std::shared_ptr<active_adapter_t>>
	subscribe_mux(const subscribe_ret_t& sret,
								const chdb::any_mux_t& mux,
								const tune_options_t& tune_options);

	subscription_id_t
	subscribe_service_for_recording(const subscribe_ret_t& sret,
											const chdb::any_mux_t& mux, recdb::rec_t& rec,
											const tune_options_t& tune_options);

	std::unique_ptr<playback_mpm_t> subscribe_service(const subscribe_ret_t& sret,
																										const chdb::any_mux_t& mux, const chdb::service_t& service,
																										const tune_options_t& tune_options);

	int set_tune_options(active_adapter_t& active_adapter, tune_options_t tune_options);

	int toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);

	void update_recording(const recdb::rec_t& rec);

	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);

	int positioner_cmd(subscription_id_t subscription_id, devdb::positioner_cmd_t cmd, int par);
	int update_current_lnb(subscription_id_t subscription_id,  const devdb::lnb_t& lnb);

	std::tuple<subscription_id_t, devdb::fe_key_t>
	tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
					 const tune_options_t& tune_options);

};
