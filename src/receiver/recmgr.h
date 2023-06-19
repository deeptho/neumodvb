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
#include "options.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "neumodb/recdb/recdb_extra.h"


class receiver_t;
class active_service_t;

bool update_recording_epg(db_txn& epg_txn, const epgdb::epg_record_t& epgrec);
class mpm_recordings_t {

public:
	ss::string<128> dbname;
	recdb::recdb_t recdb; //database for storing all recordings
	//chdb::chdb_t servicedb; //secondary database for storing a single service record  (part of recdb)

	epgdb::epgdb_t recepgdb; //secondary database for storing epg data related to recordings  (part of recdb)
	recdb::recdb_t idxdb;


	mpm_recordings_t() : recdb(false, false, true /*autoconvert*/), recepgdb(recdb), idxdb(recdb) {
	}

	void open(const char* name);

};


class receiver_t;


struct recdb_txnmgr_t {
	std::optional<db_txn> recdb_wtxn_;
	std::optional<db_txn> recdb_rtxn_;
	recdb::recdb_t& recdb;

	/*
		transactions cannot be copied, so we return a reference
	 */
	inline db_txn& rtxn() {
		if(recdb_wtxn_)
			return  *recdb_wtxn_;
		else if(!recdb_rtxn_)
			recdb_rtxn_.emplace(recdb.rtxn());
		return *recdb_rtxn_;
	}

	inline db_txn wtxn() {
		if(recdb_wtxn_)
			return  recdb_wtxn_->child_txn();
		else if(recdb_rtxn_) {
			recdb_rtxn_->abort();
			recdb_rtxn_.reset();
			recdb_rtxn_.emplace(recdb.wtxn());
		}
		return recdb_rtxn_->child_txn();
	}

	inline void commit() {
		if(recdb_rtxn_) {
			assert(!recdb_wtxn_);
			recdb_rtxn_->abort();
			recdb_rtxn_.reset();
		}
		if(recdb_wtxn_) {
			recdb_wtxn_->commit();
			recdb_wtxn_.reset();
		}
	}

	recdb_txnmgr_t(recdb::recdb_t& recdb)
		: recdb(recdb) {
	}

	~recdb_txnmgr_t() {
		commit();
	}

};

class rec_manager_t {
	friend class tuner_thread_t;

public:
	receiver_t& receiver;
private:
	recdb::rec_t new_recording(db_txn& rec_wtxn, const chdb::service_t& service, epgdb::epg_record_t& epg_record,
														 int pre_record_time, int post_record_time);
	recdb::rec_t new_recording(db_txn& rec_wtxn, db_txn& epg_wtxn, const chdb::service_t& service, epgdb::epg_record_t& epg_record,
														 int pre_record_time, int post_record_time);
	void delete_recording(const recdb::rec_t&rec);

	void start_recordings(db_txn& rtxn, system_time_t now);
	void stop_recordings(db_txn& rtxn, system_time_t now);
	void delete_old_livebuffers(db_txn& rtxn, system_time_t now);
	time_t next_recording_event_time = std::numeric_limits<time_t>::min();
	void adjust_anonymous_recording_on_epg_update(db_txn& rec_wtxn, db_txn& epg_wtxn,
																								recdb::rec_t& rec,
																								epgdb::epg_record_t& epg_record);
	int schedule_recordings_for_overlapping_epg(db_txn& rec_wtxn, db_txn& epg_wtxn,
																							const chdb::service_t& service,
																							epgdb::epg_record_t sched_epg_record);

	void on_epg_update_check_recordings(recdb_txnmgr_t& recdb_txnmgr,
																		db_txn& epg_wtxn, epgdb::epg_record_t& epg_record);
	void on_epg_update_check_autorecs(recdb_txnmgr_t& recdb_txnmgr,
																		db_txn& epg_wtxn, epgdb::epg_record_t& epg_record);

public:
	void startup(system_time_t now);
	int housekeeping(system_time_t now);
	int new_anonymous_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record,
														 int maxgap=5*60) ;
	int new_schedrec(const chdb::service_t& service, epgdb::epg_record_t epg_record);
	//int update_schedrec(const recdb::rec_t& rec);
	int delete_schedrec(const recdb::rec_t& rec);
	void on_epg_update(db_txn& txnepg, epgdb::epg_record_t& epg_record /*may be updated by setting epg_record.record
																																			 to true or false*/);


	void update_autorec(recdb::autorec_t& autorec);
	void delete_autorec(const recdb::autorec_t& autorec);

	void add_live_buffer(active_service_t& active_service);
	void remove_live_buffer(active_service_t& active_service);
	rec_manager_t(receiver_t&receiver_);

	int toggle_recording(const chdb::service_t& service,
											 const epgdb::epg_record_t& epg_record) CALLBACK;

	void remove_livebuffers();
	void update_recording(const recdb::rec_t& rec_in);
};
