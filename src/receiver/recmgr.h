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
#include "options.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "neumodb/recdb/recdb_extra.h"
#include "txnmgr.h"

class receiver_t;
class active_service_t;
class recmgr_thread_t;
class rec_manager_t;

class mpm_recordings_t {

public:
	ss::string<128> dbname;
	recdb::recdb_t recdb; //database for storing all recordingsx
	recdb::recdb_t idxdb;

	mpm_recordings_t() : recdb(false, false, true /*autoconvert*/),
											 idxdb(recdb) {
	}

	void open(const char* name);

};

class receiver_t;

class recmgr_thread_t : public task_queue_t {
	friend class rec_manager_t;

	receiver_t& receiver;
	std::thread::id thread_id;

	system_time_t last_epg_check_time;
	int epg_check_period = 5*60;

	system_time_t next_epg_clean_time;

	void clean_dbs(system_time_t now, bool at_start);
	periodic_t livebuffer_db_update;

	rec_manager_t& recmgr;
	txnmgr_t<recdb::recdb_t> recdbmgr; //one object per thread, so not a reference
	time_t next_recording_event_time = std::numeric_limits<time_t>::min();

	virtual int run() final;
	virtual int exit();


	void stop_recording(const recdb::rec_t& rec); // important that this is not a reference (async called)
	void update_recording(const recdb::rec_t& rec_in);
	void start_recordings(db_txn& rtxn, system_time_t now);
	void stop_recordings(db_txn& rtxn, system_time_t now);
	int housekeeping(system_time_t now);
	int new_anonymous_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record,
														 int maxgap=5*60) ;
	int new_schedrec(const chdb::service_t& service, epgdb::epg_record_t epg_record);
	//int update_schedrec(const recdb::rec_t& rec);
	int delete_schedrec(const recdb::rec_t& rec);
	void delete_recording(const recdb::rec_t&rec);
	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);

	int toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record);
	void delete_old_livebuffers(db_txn& rtxn, system_time_t now);

	void adjust_anonymous_recording_on_epg_update(db_txn& rec_wtxn, db_txn& epg_wtxn,
																								recdb::rec_t& rec,
																								epgdb::epg_record_t& epg_record);
	int schedule_recordings_for_overlapping_epg(db_txn& rec_wtxn, db_txn& epg_wtxn,
																							const chdb::service_t& service,
																							epgdb::epg_record_t sched_epg_record);
	void remove_old_livebuffers();
	void startup(system_time_t now);
public:

	recmgr_thread_t(receiver_t& receiver_, rec_manager_t& rec_manager);

	~recmgr_thread_t();

	recmgr_thread_t(recmgr_thread_t&& other) = delete;
	recmgr_thread_t(const recmgr_thread_t& other) = delete;
	recmgr_thread_t operator=(const recmgr_thread_t& other) = delete;
	void livebuffer_db_update_(system_time_t now_);
public:

	class cb_t;

};

class recmgr_thread_t::cb_t: public recmgr_thread_t { //callbacks
public:

	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);
	int toggle_recording(const chdb::service_t& service,
											 const epgdb::epg_record_t& epg_record) CALLBACK;

};

class rec_manager_t {
	friend class recmgr_thread_t;
	txnmgr_t<recdb::recdb_t> recdbmgr;
public:
	recmgr_thread_t recmgr_thread;
	receiver_t& receiver;
private:
public:

	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);
	rec_manager_t(receiver_t&receiver_);

};
