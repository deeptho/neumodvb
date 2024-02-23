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

#include "recmgr.h"
#include "active_service.h"
#include "receiver.h"
#include "neumodb/chdb/chdb_extra.h"
#include <filesystem>
#include <signal.h>
#include "fmt/chrono.h"

namespace fs = std::filesystem;


/*
	called at startup to clean old livebuffers
*/
void recmgr_thread_t::remove_old_livebuffers() {
	using namespace recdb;
	auto txn = recdbmgr.wtxn();
	auto c = find_first<recdb::live_service_t>(txn);

	auto r = receiver.options.readAccess();
	auto& options = *r;
	for (auto live_service: c.range()) {
		//do not remove livebuffers from other processes
		bool active = (live_service.owner >=0 && !kill((pid_t)live_service.owner, 0));
		if(active)
			continue;
		auto path = fs::path(options.live_path.c_str()) / live_service.dirname.c_str();
		auto dbpath = path / "index.mdb";
		mpm_index_t mpm_index(dbpath.c_str());
		mpm_index.open_index();
		auto rec_txn = mpm_index.mpm_rec.recdb.wtxn();
		auto rec_c = find_first<recdb::rec_t>(rec_txn);
		if (rec_c.is_valid()) {
			auto rec = rec_c.current();
			dterrorf("Found recording in livebuffer - finalizing: {}", rec);
			auto destdir = fs::path(options.recordings_path.c_str()) / rec.filename.c_str();
			mpm_copylist_t copy_command(path, destdir, rec);
			{
				auto idx_txn = rec_txn.child_txn(mpm_index.mpm_rec.idxdb);
				close_last_mpm_part(idx_txn, path.c_str());
				finalize_recording(idx_txn, copy_command, &mpm_index);
				idx_txn.commit();
			}
			rec_txn.commit();
			copy_command.run();
		} else
			rec_txn.abort();
		std::error_code ec;
		bool ret = fs::remove_all(path, ec);
		if (ec) {
			dterrorf("Error deleting {}: {}", path.string(), ec.message());
		} else if (ret)
			dtdebugf("deleted live buffer {}", live_service.dirname.c_str());
		delete_record_at_cursor(c);
	}
	c.destroy();
	txn.commit();
	recdbmgr.flush_wtxn();
}

void recmgr_thread_t::update_recording(const recdb::rec_t& rec_in) {
	auto txn = receiver.recdb.wtxn();
	put_record(txn, rec_in);
	txn.commit();
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	auto epgdb_wtxn = receiver.epgdb.wtxn();
	auto& epgrec = rec_in.epg;
	epgdb::update_epg_recording_status(epgdb_wtxn, epgrec);
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	epgdb_wtxn.commit();
}

void recmgr_thread_t::delete_recording(const recdb::rec_t& rec) {

	using namespace recdb;
	using namespace recdb::rec;

	auto parent_txn = receiver.recdb.wtxn();
	delete_record(parent_txn, rec);
	parent_txn.commit
		();
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	auto epg_wtxn = receiver.epgdb.wtxn();
	// update epgdb.mdb so that gui code can see the record
	auto c = epgdb::epg_record_t::find_by_key(epg_wtxn, rec.epg.k);
	if (c.is_valid()) {
		auto epg = c.current();
		epg.rec_status = epgdb::rec_status_t::NONE;
		assert(epg.k.anonymous == (epg.k.event_id == TEMPLATE_EVENT_ID));
		epgdb::update_record_at_cursor(c, epg);
	}
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	epg_wtxn.commit();
}

/*
	schedule recordings for all epg records overlapping with sched_epg_record
*/
int recmgr_thread_t::schedule_recordings_for_overlapping_epg(db_txn& rec_wtxn, db_txn& epg_wtxn,
																													 const chdb::service_t& service,
																													 epgdb::epg_record_t sched_epg_record) {
	assert(sched_epg_record.k.anonymous == (sched_epg_record.k.event_id == TEMPLATE_EVENT_ID));
	int pre_record_time, post_record_time; {
		auto r = receiver.options.readAccess();
		pre_record_time = r->pre_record_time.count();
		post_record_time = r->post_record_time.count();
	}

	int tolerance = 30 * 60; /* overlapping records cannot start earlier than this*/
	//select all overlapping recordings for recording


	/*find the earliest epg record matching (=sufficiently overlapping) with the anonymous
		recording*/
	using namespace epgdb;
	epg_key_t epg_key = sched_epg_record.k;
	assert (epg_key.anonymous);
	epg_key.start_time -= tolerance;
	epg_key.event_id = 0 ; //minimal value for this field; important for serialization as a lower bound
	epg_key.anonymous = false;
	auto c = epgdb::epg_record_t::find_by_key(
		epg_wtxn,
		epg_key,
		find_geq,
			epgdb::epg_record_t::partial_keys_t::service // iterator will return only records on service
		);
	for (auto epg: c.range()) {
		// update the epg record, with the newest version
		// Mark this matching epg_record for recording
		if(epg.k.anonymous)
			continue;
		if (epg.k.start_time > epg_key.start_time + tolerance) // too large difference in start time
			break;
		assert (epg.k.event_id != TEMPLATE_EVENT_ID);
		if (epg.rec_status ==  epgdb::rec_status_t::SCHEDULED)
			continue;
		epg.rec_status = epgdb::rec_status_t::SCHEDULED;
		put_record(epg_wtxn, epg);
		/*also create a recording for this program;
		 */
		recdb::new_recording(rec_wtxn, service, epg, pre_record_time, post_record_time);
	}
	return 0;
}

int recmgr_thread_t::new_anonymous_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record,
																					int maxgap) {
	assert(sched_epg_record.k.event_id == TEMPLATE_EVENT_ID);
	assert (sched_epg_record.k.anonymous);
	int pre_record_time, post_record_time;
	{
		auto r = receiver.options.readAccess();
		pre_record_time = r->pre_record_time.count();
		post_record_time = r->post_record_time.count();
	}

	// if no non-anonymous recording is created, we will have end_time < start_time

	auto epg_wtxn = receiver.epgdb.wtxn();
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	auto rec_wtxn = recdbmgr.wtxn();

	schedule_recordings_for_overlapping_epg(rec_wtxn, epg_wtxn, service, sched_epg_record);


	/*epg records may not cover anonymous recording fully;
		also insert the anonymous recording itself
	*/

	new_recording(rec_wtxn, service, sched_epg_record, pre_record_time, post_record_time);

	put_record(epg_wtxn, sched_epg_record);

	rec_wtxn.commit();
	lmdb_file=__FILE__; lmdb_line=__LINE__;
	epg_wtxn.commit();
	recdbmgr.release_wtxn();
	housekeeping(now);
	return 0;
}

/*
	insert a new recording into the database.

*/
int recmgr_thread_t::new_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record) {
	/*
		set a flag in the epg database such that epg lists can show the recording status.

		Several cases can occur
		1. sched_epg_record is really present in database and was not changed => ok, recording status is set
		2. sched_epg_record has been updated in the database in the mean time. E.g., start hour might have changed
		=> This is not wanted. To prevent this we lookup for a best matching epg record in the database
		and update that epg record instead
		This also means that the rec_t record will have the newest data

		3. sched_epg_record is a manually created record, which has no or no exact counter part in the epg database
		=> in this case 0 or more epg records may overlap with the desired recording time encoded
		in the "fake" sched_epg_record
		=> we need to flag all overlapping epg records with the record flag.
		However in this case, we need to decide if a) we keep the rec_t record "as is" (= never update rec.epg)
		to respect the wishes of the user or rather b) adjust it to reflect epg as it comes in (which could mean
		that the recording is shortened for example, or started later or earlier).

		In case a), during the actual recoding we could also auto-create "named" recordings as they
		are discovered (so new epg records coming in is not sufficient, recording must actually have started)
		This means that before the scheduled recording, but after having received epg records, the user can
		stop the "full" recording from channel epg

		=> too complex.
		Proposal: handle "fake" recordings as a "template". when a matching epg record is found,
		the template is "broken" into pieces, with the pieced matching the sched_epg_record turned into
		a regular epg record. All other pieces become template recordings, which replace the original template
		record. A template record can be recognized by event_id==0xffffffff


	*/

	/*
		We basically should iteratively call best_matching below
		-if an epg record is found, we call  receiver.recordings.new_recording on it.
		-then we create (max 2) new sched_epg_record1 and sched_epg_record2 records for the
		parts of sched_epg_record not covered by the found epgg record: sched_epg_record1 is before
		the found record, sched_epg_record2 after
		-sched_epg_record1 is inserted as a template recording (no epg data + event_id==TEMPLATE_EVENT_ID)
		because it cannot possible match an epg record (because it is a shortened version of sched_epg_record)
		-sched_epg_record2 is processed again as schedk_epg_record was processed.

		A similar approach will be needed on epg update
	*/

	{
		auto r = receiver.options.readAccess();
		auto rec_wtxn = recdbmgr.wtxn();
		auto epg_wtxn = receiver.epgdb.wtxn();
		lmdb_file=__FILE__; lmdb_line=__LINE__;
		auto rec = new_recording(rec_wtxn, epg_wtxn, service, sched_epg_record, r->pre_record_time.count(), r->post_record_time.count());
		lmdb_file=__FILE__; lmdb_line=__LINE__;
		epg_wtxn.commit();
		rec_wtxn.commit();
		recdbmgr.release_wtxn();
		next_recording_event_time = std::min({next_recording_event_time, rec.epg.k.start_time - r->pre_record_time.count(),
				rec.epg.end_time + r->post_record_time.count()});
	}
	housekeeping(now);
	return 0;
}

int recmgr_thread_t::delete_schedrec(const recdb::rec_t& rec) {
	/*
		not updating next_recording_event_time will only result in a false wakeup

		Note: we could pass in the epg_record used to trigger delete_schedrec as a function
		argument to avoid the epg lookup below:
		just set epg_record.record = false and insert epg_record in epg database.
		This would probably not work
		with manually scheduled recordings (no epg record present in epg database): a fake record
		would now be entered

	*/
	delete_recording(rec);
	housekeeping(now);
	return 0;
}

void recmgr_thread_t::startup(system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	/*
		Check for recordings still in progress due to an earlier crash
		and for recordings

	*/
	using namespace recdb;
	using namespace epgdb;
	auto parent_txn = receiver.recdb.wtxn();
	/*
		clean recording status of epg records, which may not be uptodate after a crash
	 */
	auto clean = [&] (rec_status_t rec_status) {
		auto cr = rec_t::find_by_status_start_time(parent_txn, rec_status, now -7*3600*24, find_type_t::find_geq,
																							 rec_t::partial_keys_t::rec_status);
		for (auto rec : cr.range()) {
			if (rec.epg.end_time <= now) {
				auto epg_wtxn = receiver.epgdb.wtxn();
				lmdb_file=__FILE__; lmdb_line=__LINE__;
				auto c = epgdb::epg_record_t::find_by_key(epg_wtxn, rec.epg.k);
				if (c.is_valid()) {
					auto epg = c.current();
					epg.rec_status = epgdb::rec_status_t::NONE;
					epgdb::update_record_at_cursor(c, epg);
				}
				lmdb_file=__FILE__; lmdb_line=__LINE__;
				epg_wtxn.commit();
			}
		}
	};
	clean(rec_status_t::FINISHED);
	clean(rec_status_t::FAILED);
	clean(rec_status_t::SCHEDULED);

	auto cr = rec_t::find_by_status_start_time(parent_txn, rec_status_t::IN_PROGRESS, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == rec_status_t::IN_PROGRESS);
		if (rec.epg.end_time + rec.post_record_time < now) {
			dtdebugf("Finalising unfinished recording: at start up: {}", rec);
			rec.epg.rec_status = rec_status_t::FINISHED;
			rec.subscription_id = -1;
			rec.owner = -1;
			recdb::update_record_at_cursor(cr.maincursor, rec);
		} else if (rec.epg.k.start_time - rec.pre_record_time <= now) {
			// recording in progress
			rec.epg.rec_status = rec_status_t::SCHEDULED; // will cause recording to be continued
			rec.subscription_id = -1;											// we are called after program has exited
			rec.owner = -1;
			recdb::update_record_at_cursor(cr.maincursor, rec);
			// next_recording_event_time = std::min(next_recording_event_time, rec.epg.end_time + rec.post_record_time);
		} else {
			// recording in future
			dterrorf("Found future recording already in progress");
			rec.epg.rec_status = rec_status_t::SCHEDULED; // will cause recording to be continued
			rec.subscription_id = -1;											// we are called after program has exited
			rec.owner = -1;
			recdb::update_record_at_cursor(cr.maincursor, rec);
			// next_recording_event_time = std::min(next_recording_event_time, rec.epg.k.start_time - rec.pre_record_time);
		}
		/*we could potentially abort the loop here if rec.start_time  is far enough into
			the future such that we know that  rec.start_time - rec.pre_record_time is certainly
			in the future for all further records. This assumes that rec.pre_record_time is upper bounded.

			This would be a an over optimisation
		*/
	}

	cr = rec_t::find_by_status_start_time(parent_txn, rec_status_t::FINISHING, find_type_t::find_geq,
																				rec_t::partial_keys_t::rec_status);
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == rec_status_t::FINISHING);
		if (rec.epg.end_time + rec.post_record_time < now) {
			dtdebugf("Finalising unfinised recording: at start up: {}", rec);
			rec.epg.rec_status = rec_status_t::FINISHED;
			recdb::update_record_at_cursor(cr.maincursor, rec);
			auto epg_wtxn = receiver.epgdb.wtxn();
			auto c = epgdb::epg_record_t::find_by_key(epg_wtxn, rec.epg.k);
			if (c.is_valid()) {
				auto epg = c.current();
				epg.rec_status = epgdb::rec_status_t::NONE;
				assert(epg.k.anonymous == (epg.k.event_id == TEMPLATE_EVENT_ID));
				epgdb::update_record_at_cursor(c, epg);
			}
			epg_wtxn.commit();
		}
	}
	parent_txn.commit();
	remove_old_livebuffers();
}

/*
	New approach: allow overlap of multiple recordings
	Also allow overlap of multiple epg records.

	When a non-anomous recording is scheduled, only enter a recording for that specific epg record

	When an anomous recoding is scheduled and overlaps with some existing epg records, then
	create non-anonymous new recordings for all sufficiently overlapping epg records and insert one
	or more anonymous recordings to cover the remainder (imposing a minimal duration)

	When a new epg record comes in, check all overlapping recordings and
  -for non-anonymus recordings, adjust the event_name and start/end time of the recording
	-for anonymous recordings, if they overlap sufficiently with the epg record, create a new
	 non-anonymous epg record for the recording, cut the anonomous recording and and enter a
	 new anonymous recording for the remainer

 When an existing epg record is update, check all overlapping recordings, and
  -for non-anonymous recordings,  adjust the event_name and start/end time of the recording
  -for anonymous recordings, if they overlap with the epg record, then cut
 */


/*
	For each recording (not live buffer) we store epg data in the
	global recdb database; when new epg arrives we check if this data
	needs an update
*/
void recmgr_thread_t::adjust_anonymous_recording_on_epg_update(db_txn& rec_wtxn, db_txn& epg_wtxn,
																														 recdb::rec_t& rec,
																														 epgdb::epg_record_t& epg_record) {

	/*
		if the epg record is newer than the recording, and overlaps sufficiently with the recording,
		then create a non-anonmous new recording matching the epg record. Also cut out that section
		of the recording. Thus we ensure that recordings are not (much) overlapping

		If on the other hand the epg record has been updated then we only adjust the cutting

	 */
	const int maxgap = 5 * 60;
	using namespace recdb;
	auto txnrec = receiver.recdb.rtxn();
	if (epg_record.rec_status == epgdb::rec_status_t::NONE) {
		epg_record.rec_status = epgdb::rec_status_t::SCHEDULED;
		/*also create a recording for this program;
		 */

		auto r = receiver.options.readAccess();
		auto& options = *r;
		auto rec_wtxn = receiver.recdb.wtxn();
		auto recnew =
			new_recording(rec_wtxn, epg_wtxn, rec.service, epg_record, options.pre_record_time.count(), options.post_record_time.count());
		rec_wtxn.commit();
		next_recording_event_time =
			std::min({next_recording_event_time, epg_record.k.start_time - options.pre_record_time.count(),
					epg_record.end_time + options.post_record_time.count()});
	}

	assert( rec.epg.k.anonymous == (rec.epg.k.event_id == TEMPLATE_EVENT_ID));
	// compute number of gaps to see if anonymous recording should be removed
	if (rec.epg.k.anonymous) {
		int tolerance = 30 * 60; /* overlapping records cannot start earlier than this*/
		time_t anonymous_start_time = rec.epg.k.start_time;
		time_t start_time = rec.epg.end_time;		// start of first newly created recording
		time_t end_time = rec.epg.k.start_time; // latest end of any newly created recording
		// if no non-anonuymous recording is created, we will have end_time < start_time

		// compute number of gaps to see if
		int gaps = 0;
		for (;;) {
			/*find the earliest epg record matching (=sufficiently overlapping) with the anonymous
				recording*/
			if (auto found = epgdb::best_matching(epg_wtxn, rec.epg.k, rec.epg.end_time, tolerance)) {
				if (found->rec_status == epgdb::rec_status_t::NONE) {
					found->rec_status = epgdb::rec_status_t::SCHEDULED;
					dtdebugf("unexpected: epg_record should have been marked as recording already");
				}
				assert(tolerance > 0 || found->k.start_time >= rec.epg.k.start_time);
				time_t gap = found->k.start_time - end_time;
				if (gap >= maxgap)
					gaps++;
				/* note that start_time decreases exactly once in the loop
					 and then remains constant*/
				start_time = std::min(found->k.start_time, start_time);
				end_time = std::max(found->end_time, end_time);
				/*also create a recording for this program;
				 */
				/* continue looking more for later epg records which could also match,
					 by changing sched_epg_record.k.start_time
				*/
				rec.epg.k.start_time = rec.epg.k.start_time + 10 * 60; // handles the case of possibly overlapping records
				tolerance = 0; // ensure that only records with later start_time will be found
				if (rec.epg.k.start_time >= rec.epg.end_time)
					break; // nothing more todo

			} else
				break; // no records found
		}

		time_t gap = rec.epg.end_time - end_time;
		if (gap >= maxgap)
			gaps++;
		rec.epg.k.start_time = anonymous_start_time; // restore overwritten value
		if (gaps == 0) {
			/*epg records cover anonymous recording fully;
				anonymous recording can be deleted
			*/
			delete_recording(rec);
		}
	}
}

/*
	For each recording in progress, check if it should be stopped
	and stop if needed
*/
void recmgr_thread_t::stop_recordings(db_txn& recdb_rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	// auto cr = recdb::rec::find_first(parent_txn);
	auto cr = rec_t::find_by_status_start_time(recdb_rtxn, epgdb::rec_status_t::IN_PROGRESS, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == epgdb::rec_status_t::IN_PROGRESS);
		if (rec.epg.end_time + rec.post_record_time < now) {
			next_recording_event_time = std::min(next_recording_event_time, rec.epg.end_time + rec.post_record_time);
			dtdebugf("END recording {}", rec);
			stop_recording(rec);
#if 0 //TO CHECK: not needed and counter productive?
			rec.epg.rec_status = epgdb::rec_status_t::FINISHING;
			update_recording(rec);
#endif
		}
	}
}

/*
	For each record in the global database, check if recording should be started,
	and act accordingly
*/

void recmgr_thread_t::start_recordings(db_txn& rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	using namespace epgdb;

	auto cr = rec_t::find_by_status_start_time(rtxn, rec_status_t::SCHEDULED, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);
	time_t last_start_time = 0;
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == rec_status_t::SCHEDULED);
		dterrorf("CANDIDATE REC: {}", rec.epg);
		if (rec.epg.k.start_time - rec.pre_record_time <= now) {
			if (rec.epg.end_time + rec.post_record_time <= now) {
				dtdebugf("TOO LATE; skip recording: {}", rec);
				auto wtxn = receiver.recdb.wtxn();
				rec.epg.rec_status = rec_status_t::FAILED;
				put_record(wtxn, rec);
				wtxn.commit();
			} else {
				using namespace epgdb;
				using namespace chdb;

				/*the following should not be needed as recmgr_t::startup already ensures that rec_t records
					are uptodate at startup and on_epg_update already updates themm.

					The following code is intended to also detect external epg updates (e.g., xmltv).

					However, even those could be handled by an on_epg_update or by requiring xmltv import
					to trigger the receiver_thread process to re-update the rec_t records

				*/
				{
					auto txn = receiver.epgdb.rtxn();
					epg_key_t epg_key = rec.epg.k;
					auto ec =
						epgdb::epg_record_t::find_by_key(txn, epg_key, find_type_t::find_eq, epg_record_t::partial_keys_t::all);
					if (ec.is_valid())
						rec.epg = ec.current();
				}
				{
					auto txn = receiver.chdb.rtxn();
					// rec.service = epgdb::service_for_epg_record(txn, epg_record);
					auto ec = chdb::service_t::find_by_key(txn, rec.service.k.mux, rec.service.k.service_id);
					if (ec.is_valid())
						rec.service = ec.current();
				}

				rec.epg.rec_status = rec_status_t::IN_PROGRESS;
				rec.owner = getpid();
				subscribe_ret_t sret{subscription_id_t::NONE, false/*failed*/};
				rec.subscription_id = (int)sret.subscription_id;
				update_recording(rec); // must be done now to avoid starting the same recording twice
				receiver.start_recording(rec);
			}
		} else {
			auto r = receiver.options.readAccess();
			next_recording_event_time =
				std::min(next_recording_event_time, rec.epg.k.start_time - r->max_pre_record_time.count());
			// recording is in the future; nothing todo
			if (rec.epg.k.start_time - r->max_pre_record_time.count() >= now)
				break; //@todo we can break here, because records should be ordered according to start time
			else {
				assert(rec.epg.k.start_time >= last_start_time);
				last_start_time = rec.epg.k.start_time;
			}
		}
	}
}

int recmgr_thread_t::housekeeping(system_time_t now) {
	auto prefix =fmt::format("-RECMGR-HOUSEKEEPING");
	log4cxx::NDC ndc(prefix.c_str());
	if (system_clock_t::to_time_t(now) > next_recording_event_time) {
			using namespace recdb;
			auto parent_txn = receiver.recdb.rtxn();
			// check for any recordings to stop
			stop_recordings(parent_txn, now);
			// check for any recordings to start
			start_recordings(parent_txn, now);
			parent_txn.commit();
	}
	return 0;
}

rec_manager_t::rec_manager_t(receiver_t& receiver_)
	: recdbmgr(receiver_.recdb)
	, recmgr_thread(receiver_, *this)
	, receiver(receiver_) {
}


/*
	returns true if a recording is active, but not in our process
 */
static inline bool not_ours_and_active(recdb::rec_t & rec) {
	return rec.owner != -1 && rec.owner != getpid() && !kill((pid_t)rec.owner, 0);
}

int recmgr_thread_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	dtdebugf("toggle_recording: {}", epg_record);
	int ret = -1;
	bool error{false};
	auto txn = receiver.recdb.rtxn();

	//returns true if record was found
	auto fn = [&](bool anonymous) {
		// look up the recording
		bool found{false};
		if (auto rec = recdb::rec::best_matching(txn, epg_record, anonymous)) {
			if(not_ours_and_active(*rec)) {
				user_errorf("Cannot change recording of another active process: {}", *rec);
				error |= true;
				return false;
			}
			switch (rec->epg.rec_status) {
			case epgdb::rec_status_t::SCHEDULED: {
				dtdebugf("Toggle: Remove existing (not yet started) recording: {}", *rec);
				// recording has not yet started,
				ret = delete_schedrec(*rec);
				break;
			}

			case epgdb::rec_status_t::FINISHING:
				break;
			case epgdb::rec_status_t::IN_PROGRESS: {
				dtdebugf("Toggle: Stop running recording: {}", *rec);
				stop_recording(*rec);
				break;
			}
			case epgdb::rec_status_t::FINISHED: {
				dtdebugf("Recording finished in the past! {}", *rec);
				break;
			}
			case epgdb::rec_status_t::FAILED: {
				dtdebugf("Recording failed in the past! {}", *rec);
				break;
			} break;
			default:
				assert(0);
			}
			found = true;
		}

		if (found) {
			return found;
		}
		dtdebugf("Toggle: Insert new recording");
		assert ((epg_record.k.event_id == TEMPLATE_EVENT_ID) == epg_record.k.anonymous);
		ret = epg_record.k.anonymous ? new_anonymous_schedrec(service, epg_record)
			: new_schedrec(service, epg_record);
		return true;
	};
	if(error) {
		txn.abort();
		return -1;
	}
	bool found{false};
	if (!epg_record.k.anonymous)
		found = fn(false);
	if (!found) {
		//also try to match anonymous recordings to non-anonymous epg records
		fn(true);
	}
	txn.commit();
	return 0;
}

void recmgr_thread_t::update_autorec(recdb::autorec_t& autorec) {
	db_txn recdb_wtxn = receiver.recdb.wtxn();
	if(autorec.id <0) { //template
		recdb::make_unique_id(recdb_wtxn, autorec);
	}
	put_record(recdb_wtxn, autorec);
	recdb_wtxn.commit();
	/*force processing the autorec in next housekeeping loop, which can take up to 1 second.
		houskeeping is not called directly as adding/updating an autorec needs to check all
		epg data, which can take a long time
	 */
	next_recording_event_time = std::numeric_limits<time_t>::min();
}

void recmgr_thread_t::delete_autorec(const recdb::autorec_t& autorec) {
	db_txn recdb_wtxn = receiver.recdb.wtxn();
	delete_record(recdb_wtxn, autorec);
	recdb_wtxn.commit();
}

void mpm_recordings_t::open(const char* name) {
	dbname = name;
	recdb.open(dbname.c_str(), true /*allow_degraded_mode*/);
	{
		auto txn = recdb.wtxn();
		recdb::recdb_t::clean_log(txn);
		txn.commit();
	}
	idxdb.open_secondary("idx", true /*allow degraded mode*/);
}

void recmgr_thread_t::clean_dbs(system_time_t now, bool at_start) {
	{
		auto wtxn = receiver.chdb.wtxn();
		chdb::chdb_t::clean_log(wtxn);
		if(at_start)
			chdb::clean_scan_status(wtxn);
		if(at_start) {
			//ancient ubuntu does not know std::chrono::months{1}; hence use hours
			chdb::clean_expired_services(wtxn, std::chrono::hours{24*30});
		}
		wtxn.commit();
	}
	{
		auto wtxn = receiver.recdb.wtxn();
		recdb::recdb_t::clean_log(wtxn);
		wtxn.commit();
	}
	{
		auto wtxn = receiver.statdb.wtxn();
		statdb::statdb_t::clean_log(wtxn);
		wtxn.commit();
	}

	if (now > next_epg_clean_time) {
		auto wtxn = receiver.epgdb.wtxn();
		lmdb_file=__FILE__; lmdb_line=__LINE__;
		epgdb::clean(wtxn, now - 4h); // preserve last 4 hours
		wtxn.commit();
		next_epg_clean_time = now + 12h;
	}
}

void recmgr_thread_t::stop_recording(const recdb::rec_t& rec) // important that this is not a reference (async called)
{
	if(rec.owner != getpid()) {
		dterrorf("Error stopping recording we don't own: {}", rec);
		return;
	}

	assert(rec.subscription_id >= 0);
	if (rec.subscription_id < 0)
		return;

	auto subscription_id = subscription_id_t{rec.subscription_id};
	auto active_adapter = receiver.find_active_adapter(subscription_id);
	mpm_copylist_t copy_list;
	int ret;
	auto& tuner_thread = active_adapter->tuner_thread; //call by reference safe because of subsequent .wait
	tuner_thread.push_task([&tuner_thread, &rec, &copy_list, &ret]() {
		ret=cb(tuner_thread).stop_recording(rec, copy_list);
		return 0;
	}).wait();
	if (ret >= 0) {
		assert(copy_list.rec.epg.rec_status == epgdb::rec_status_t::FINISHED);
		copy_list.run();
	}
	copy_list.rec.subscription_id = -1;
	copy_list.rec.owner = -1;
	update_recording(copy_list.rec);

	auto& receiver_thread = receiver.receiver_thread;
	receiver_thread.push_task([&receiver_thread, subscription_id]() {
		auto ssptr = receiver_thread.receiver.get_ssptr(subscription_id);
		cb(receiver_thread).unsubscribe(ssptr);
		return 0;
	});

}

int recmgr_thread_t::run() {
	thread_id = std::this_thread::get_id();
	/*TODO: The timer below is used to gather signal strength, cnr and ber.
		When used from a gui, it may be better to let the gui handle this asynchronously
	*/
	set_name("recmgr");
	logger = Logger::getLogger("recmgr"); // override default logger for this thread
	double period_sec = 1.0;
	timer_start(period_sec);
	now = system_clock_t::now();
	clean_dbs(now, true);
	startup(now);
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorf("error in poll: {}", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		// printf("n={:d}\n", n);
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				ss::string<128> prefix;
				prefix.format("RECMGR-CMD");
				log4cxx::NDC ndc(prefix.c_str());
				// an external request was received
				// run_tasks returns -1 if we must exit
				if (run_tasks(now) < 0) {
					// detach();
					return 0;
				}
			} else if (is_timer_fd(evt)) {
				dttime_init();
				clean_dbs(now, false);
				dttime(100);
				housekeeping(now);
				dttime(100);
				livebuffer_db_update.run([this](system_time_t now) { livebuffer_db_update_(now); }, now);
				dttime(100);
				auto delay = dttime(-1);
				if (delay >= 500)
					dterrorf("clean cycle took too long delay={:d}", delay);
			} else {
			}
		}
	}
	return 0;
}

int recmgr_thread_t::cb_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record) {
	return this->recmgr_thread_t::toggle_recording(service, epg_record);
}

void recmgr_thread_t::cb_t::update_autorec(recdb::autorec_t& autorec)
{
	this->recmgr_thread_t::update_autorec(autorec);
}

void recmgr_thread_t::cb_t::delete_autorec(const recdb::autorec_t& autorec)
{
	this->recmgr_thread_t::delete_autorec(autorec);
}

recmgr_thread_t::recmgr_thread_t(receiver_t& receiver_, rec_manager_t& recmgr)
	: task_queue_t(thread_group_t::tuner)
	, receiver(receiver_)
	, livebuffer_db_update(60)
	, recmgr(recmgr)
	, recdbmgr(receiver.recdb)
{
}

recmgr_thread_t::~recmgr_thread_t() {
	//exit();
}

int recmgr_thread_t::exit() {
	dtdebugf("recmgr exit");
	return 0;
}

/*
	Called periodically to store update time for still active live buffers
	and clean old ones.

	Todo: do we need the update time? If not, this could be simplified by only updating
	at service switch time
 */
void recmgr_thread_t::livebuffer_db_update_(system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	// constexpr int tolerance = 15*60; //we look 15 minutes in to the future (and past
	auto recdb_wtxn = recdbmgr.wtxn();
	auto recdb_rtxn = recdbmgr.wtxn();
	auto retention_time = receiver.options.readAccess()->livebuffer_retention_time.count();
	auto c = find_first<recdb::live_service_t>(recdb_rtxn);
	auto pid =getpid();
	for (auto live_service : c.range()) {
		if((int) live_service.owner != pid)
			continue; //leave other processes alone
		if (live_service.last_use_time < 0 || //live_service still in use
				live_service.last_use_time >= now - retention_time) //live buffer may be reused for a brief while
			continue;

		dtdebugf("Removing old live buffer: adapter {} created={} end={} update={}",
						 (int)live_service.adapter_no,
						 fmt::localtime(live_service.creation_time),
						 fmt::localtime(live_service.last_use_time), fmt::localtime(now));
		ss::string<128> dirname;
		{
			auto r = receiver.options.readAccess();
			dirname.format("{:s}{:s}", r->live_path, live_service.dirname);
		}
		dtdebugf("DELETING TIMESHIFT DIR {}\n", dirname.c_str());
		rmpath(dirname.c_str());
		delete_record(recdb_wtxn, live_service);
	}
	c.destroy();
	recdb_wtxn.commit();
	recdb_rtxn.commit();
}
