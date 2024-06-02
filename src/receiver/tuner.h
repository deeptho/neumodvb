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
#include "recmgr.h"

class playback_mpm_t;
class active_adapter_t;
struct subscription_options_t;
class streamer_t;

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
	receiver_t& receiver;
	std::thread::id thread_id;
	active_adapter_t& active_adapter;
	/*!
		All functions should be called from the right thread.

	*/
	periodic_t livebuffer_db_update;
	txnmgr_t<recdb::recdb_t> recdbmgr; //one object per thread, so not a reference

	virtual int run() final;
//returns true on error
	void on_epg_update(db_txn& txnepg, system_time_t now,
										 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																											to true or false*/);
	virtual int exit();
	void release_all(subscription_id_t subscription_id);

	subscription_id_t tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
														 const subscription_options_t& tune_options);
	void add_si(active_adapter_t& active_adapter, const chdb::any_mux_t& mux,
							const subscription_options_t& tune_options, subscription_id_t subscription_id);
	int tune(const subscribe_ret_t& sret, const devdb::rf_path_t& rf_path,
					 const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, subscription_options_t tune_options);
	template<typename _mux_t>
	int tune_dvbc_or_dvbt(const _mux_t& mux, subscription_options_t tune_options, subscription_id_t subscription_id);
	subscription_id_t subscribe_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
																	const subscription_options_t& tune_options);
public:
	tuner_thread_t(receiver_t& receiver_, active_adapter_t& aa);

	~tuner_thread_t();

	tuner_thread_t(tuner_thread_t&& other) = delete;
	tuner_thread_t(const tuner_thread_t& other) = delete;
	tuner_thread_t operator=(const tuner_thread_t& other) = delete;
	void on_epg_update_check_recordings(db_txn& recdb_wtxn, db_txn& epg_wtxn, epgdb::epg_record_t& epg_record);
	void on_epg_update_check_autorecs(db_txn& recdb_wtxn, db_txn& epg_wtxn, epgdb::epg_record_t& epg_record);
	void add_live_buffer(const recdb::live_service_t& active_service);
	void remove_live_buffer(subscription_id_t subscription_id);
	void update_dbfe(const devdb::fe_t& updated_dbfe);
public:

	class cb_t;

};

struct mpm_copylist_t;
class tuner_thread_t::cb_t: public tuner_thread_t { //callbacks
public:
	int on_pmt_update(active_adapter_t& active_adapter, const dtdemux::pmt_info_t& pmt);
	int update_service(const chdb::service_t& service);
	int lnb_activate(subscription_id_t subscription_id, const subscribe_ret_t& ret,
									 subscription_options_t tune_options);

	int lnb_spectrum_acquistion(subscription_id_t subscription_id, const subscribe_ret_t& ret,
															subscription_options_t tune_options);

	subscription_id_t subscribe_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
																	const subscription_options_t& tune_options);

	subscription_id_t subscribe_service_for_recording(const subscribe_ret_t& sret,
																										const chdb::any_mux_t& mux, recdb::rec_t& rec,
																										const subscription_options_t& tune_options);

	std::unique_ptr<playback_mpm_t> subscribe_service_for_viewing(const subscribe_ret_t& sret,
																										const chdb::any_mux_t& mux, const chdb::service_t& service,
																										const subscription_options_t& tune_options);

	devdb::stream_t add_stream(const subscribe_ret_t& sret,
														 const chdb::any_mux_t& mux, const devdb::stream_t& stream,
														 const subscription_options_t& tune_options);
	void 	remove_stream(subscription_id_t subscription_id);

	int toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);

	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);

	std::tuple<int, std::optional<int>>
	positioner_cmd(subscription_id_t subscription_id, devdb::positioner_cmd_t cmd, int par);
	int update_current_lnb(subscription_id_t subscription_id,  const devdb::lnb_t& lnb);
	int stop_recording(const recdb::rec_t& rec, mpm_copylist_t& copy_commands);
};
