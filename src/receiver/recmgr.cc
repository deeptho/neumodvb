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

#include "recmgr.h"
#include "active_service.h"
#include "receiver.h"
#include <filesystem>
namespace fs = std::filesystem;

/*
	make and insert a new recording into the global recording database
*/
recdb::rec_t rec_manager_t::new_recording(db_txn& rec_wtxn, const chdb::service_t& service,
																					epgdb::epg_record_t& epgrec, int pre_record_time, int post_record_time) {
	auto stream_time_start = milliseconds_t(0);
	auto stream_time_end = stream_time_start;

	// TODO: times in start_play_time may have a different sign than stream_times (which can be both negative and
	// positive)
	using namespace recdb;
	time_t real_time_start = 0;
	// real_time end will determine when epg recording will be stopped
	time_t real_time_end = 0;
	int subscription_id = -1;
	using namespace recdb;
	using namespace recdb::rec;
	ss::string<256> filename;

	const auto rec_type = rec_type_t::RECORDING;
	assert(epgrec.rec_status != epgdb::rec_status_t::IN_PROGRESS);

	epgrec.rec_status = epgdb::rec_status_t::SCHEDULED;
	auto rec = rec_t(rec_type, subscription_id, stream_time_start, stream_time_end, real_time_start, real_time_end,
									 pre_record_time, post_record_time, filename, service, epgrec, {});
	put_record(rec_wtxn, rec);

	return rec;
}
/*
	make and insert a new recording into the global recording database
*/
recdb::rec_t rec_manager_t::new_recording(const chdb::service_t& service, epgdb::epg_record_t& epgrec,
																					int pre_record_time, int post_record_time) {
	auto parent_txn = receiver.recdb.wtxn();
	auto ret = new_recording(parent_txn, service, epgrec, pre_record_time, post_record_time);
	parent_txn.commit();
	auto epg_txn = receiver.epgdb.wtxn();
	// update epgdb.mdb so that gui code can see the record
	auto c = epgdb::epg_record_t::find_by_key(epg_txn, epgrec.k);
	if (c.is_valid()) {
		assert(epgrec.k.event_id != TEMPLATE_EVENT_ID);
		epgdb::put_record_at_key(c, c.current_serialized_primary_key(), epgrec);
	}
	epg_txn.commit();

	return ret;
}

void rec_manager_t::add_live_buffer(active_service_t& active_service) {
	using namespace recdb;
	auto l = active_service.get_live_service();
	auto txn = receiver.recdb.wtxn();
	put_record(txn, l);
	txn.commit();
}

void rec_manager_t::remove_live_buffer(active_service_t& active_service) {
	using namespace recdb;
	auto l = active_service.get_live_service_key();
	auto txn = receiver.recdb.wtxn();
	delete_record(txn, l);
	txn.commit();
}

/*
	called at startup to clean old livebuffers
*/
void rec_manager_t::remove_livebuffers() {
	using namespace recdb;
	auto txn = receiver.recdb.wtxn();
	auto c = find_first<recdb::live_service_t>(txn);

	auto r = receiver.options.readAccess();
	auto& options = *r;

	for (; c.is_valid(); c.next()) {
		auto mpm = c.current();
		auto path = fs::path(options.live_path.c_str()) / mpm.dirname.c_str();
		auto dbpath = path / "index.mdb";
		mpm_index_t mpm_index(dbpath.c_str());
		mpm_index.open_index();
		auto rec_txn = mpm_index.mpm_rec.recdb.wtxn();
		auto rec_idx = find_first<recdb::rec_t>(rec_txn);
		if (rec_idx.is_valid()) {
			auto rec = rec_idx.current();
			dterror("Found recording in livebuffer - finalizing: " << rec);
			auto destdir = fs::path(options.recordings_path.c_str()) / rec.filename.c_str();
			mpm_copylist_t copy_command(path, destdir, rec);
			{
				auto idx_txn = rec_txn.child_txn(mpm_index.mpm_rec.idxdb);
				close_last_mpm_part(idx_txn, path.c_str());
				idx_txn.commit();
			}
			finalize_recording(copy_command, &mpm_index);
			copy_command.run(rec_txn);
		}
		rec_txn.commit();
		std::error_code ec;
		bool ret = fs::remove_all(path, ec);
		if (ec) {
			dterror("Error deleting " << path << ":" << ec.message());
		} else if (ret)
			dtdebugx("deleted live buffer %s", mpm.dirname.c_str());
		delete_record_at_cursor(c);
	}
	txn.commit();
}

/*
	called when service or epgrec changes (e.g., on_epg_update, so that also rec must change) or
	when status of recording changes

	Note that this is called from the tuner thread, which is the same thread calling epg code.
	We count on that to avoid races.


*/

recdb::rec_t update_recording_epg(db_txn& parent_txn, db_txn& epg_txn, const recdb::rec_t& rec,
																	const epgdb::epg_record_t& epgrec) {

	using namespace recdb;
	using namespace recdb::rec;
	bool key_changed = (epgrec.k != rec.epg.k); // can happen when start_time changed

	auto newrec = rec;
	newrec.epg = epgrec;
	if (key_changed)
		delete_record(parent_txn, rec);
	put_record(parent_txn, newrec);

	auto c = epgdb::epg_record_t::find_by_key(epg_txn, epgrec.k);
	if (c.is_valid()) {
		const auto& existing = c.current();
		if (existing.rec_status != epgrec.rec_status) { // epgrec can come from stream!
			auto existing = c.current();
			existing.rec_status = epgrec.rec_status;
			assert(existing.k.event_id != TEMPLATE_EVENT_ID);
			epgdb::put_record_at_key(c, c.current_serialized_primary_key(), existing);
		}
	}

	return newrec;
}

void rec_manager_t::update_recording(const recdb::rec_t& rec_in) {
	auto txn = receiver.recdb.wtxn();
	put_record(txn, rec_in);
	txn.commit();
	auto epg_txn = receiver.epgdb.wtxn();
	auto& epgrec = rec_in.epg;
	auto c = epgdb::epg_record_t::find_by_key(epg_txn, epgrec.k);
	if (c.is_valid()) {
		const auto& existing = c.current();
		if (existing.rec_status != epgrec.rec_status) { // epgrec can come from stream!
			auto existing = c.current();
			if (existing.k.event_id == TEMPLATE_EVENT_ID &&
					(epgrec.rec_status == epgdb::rec_status_t::FINISHED ||
					 epgrec.rec_status == epgdb::rec_status_t::FINISHING))
				delete_record(epg_txn, existing); //anonymous recordings are shown on epg screen until they are finised.
			else {
				existing.rec_status = epgrec.rec_status;
			//assert(existing.k.event_id != TEMPLATE_EVENT_ID);
				epgdb::put_record_at_key(c, c.current_serialized_primary_key(), existing);
			}
		}
	}
	epg_txn.commit();
}

void rec_manager_t::delete_recording(const recdb::rec_t& rec) {

	using namespace recdb;
	using namespace recdb::rec;

	auto parent_txn = receiver.recdb.wtxn();
	delete_record(parent_txn, rec);
	parent_txn.commit();
	auto epg_txn = receiver.epgdb.wtxn();
	// update epgdb.mdb so that gui code can see the record
	auto c = epgdb::epg_record_t::find_by_key(epg_txn, rec.epg.k);
	if (c.is_valid()) {
		auto epg = c.current();
		epg.rec_status = epgdb::rec_status_t::NONE;
		assert(epg.k.event_id != TEMPLATE_EVENT_ID);
		epgdb::put_record_at_key(c, c.current_serialized_primary_key(), epg);
	}
	epg_txn.commit();
}

/*
	insert a new anonymous recording into the database.
	An anonymous recording is one created with a specific start and end time but
	without a real epg record (the epg record has event_id == TEMPLATE_EVENT_ID and an auto generated
	or user specifief title.

	We attempt to replace this anonymous recording with non-anonymous recordings, based on epg_records,
	but only if these epg_records are mostly fully contained in the desired recording interval.

	If the found epg records sufficiently cover the desired recording period, the anonymous recording
	itself is not created. Otherwise it is. This heuristic offers a reasonable compromise between
	-allowing the user to specify what to record in case of absent epg
	-prefering recordings split and named after actual epg recordings.

*/
int rec_manager_t::new_anonymous_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record,
																					int maxgap) {
	assert(sched_epg_record.k.event_id == TEMPLATE_EVENT_ID);
	int pre_record_time, post_record_time;
	{
		auto r = receiver.options.readAccess();
		pre_record_time = r->pre_record_time.count();
		post_record_time = r->post_record_time.count();
	}

	/*
		attempt to create non-anonymous recordings based on available epg records
	*/
	time_t anonymous_start_time = sched_epg_record.k.start_time;
	time_t start_time = sched_epg_record.end_time;	 // start of first newly created recording (if any)
	time_t end_time = sched_epg_record.k.start_time; // latest end of any newly created recording (if any)
	// if no non-anonymous recording is created, we will have end_time < start_time

	int gaps = 0;
	auto txnepg = receiver.epgdb.wtxn();
	auto txnrec = receiver.recdb.wtxn();
	int tolerance = 30 * 60; /* overlapping records cannot start earlier than this*/
	for (;;) {
		/*find the earliest epg record matching (=sufficiently overlapping) with the anonymous
			recording*/
		if (auto found = epgdb::best_matching(txnepg, sched_epg_record.k, sched_epg_record.end_time, tolerance)) {
			// update the epg record, with the newest version
			// Mark this matching epg_record for recording
			{
				found->rec_status = epgdb::rec_status_t::SCHEDULED;
				assert(found->k.event_id != TEMPLATE_EVENT_ID);
				put_record(txnepg, *found);
				/*also create a recording for this program;
				 */
				auto rec = new_recording(txnrec, service, *found, pre_record_time, post_record_time);
				next_recording_event_time = std::min(
					{next_recording_event_time, found->k.start_time - pre_record_time, found->end_time + post_record_time});
			}
			assert(tolerance > 0 || found->k.start_time >= sched_epg_record.k.start_time);
			time_t gap = found->k.start_time - end_time;
			if (gap >= maxgap)
				gaps++;
			/* note that start_time decreases exactly once in the loop
				 and then remains constant*/
			start_time = std::min(found->k.start_time, start_time);
			end_time = std::max(found->end_time, end_time);
			/* continue looking more for later epg records which could also match,
				 by changing sched_epg_record.k.start_time
			*/
			sched_epg_record.k.start_time = found->k.start_time + 10 * 60; // handles the case of possibly overlapping records
			tolerance = 0; // ensure that only records with later start_time will be found
			if (sched_epg_record.k.start_time >= sched_epg_record.end_time)
				break; // nothing more todo

		} else
			break; // no records found
	}

	time_t gap = sched_epg_record.end_time - end_time;
	if (gap >= maxgap)
		gaps++;
	sched_epg_record.k.start_time = anonymous_start_time; // restore overwritten value
	if (gaps > 0) {
		/*epg records do not cover anonymous recording fully;
			also insert the anonymous recording itself
		*/

		auto rec = new_recording(txnrec, service, sched_epg_record, pre_record_time, post_record_time);

		put_record(txnepg, sched_epg_record);

		next_recording_event_time = std::min({next_recording_event_time, sched_epg_record.k.start_time - pre_record_time,
				sched_epg_record.end_time + post_record_time});
	}

	txnrec.commit();
	txnepg.commit();

	housekeeping(now);
	return 0;
}

/*
	insert a new recording into the database.

*/
int rec_manager_t::new_schedrec(const chdb::service_t& service, epgdb::epg_record_t sched_epg_record) {
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

		auto rec = new_recording(service, sched_epg_record, r->pre_record_time.count(), r->post_record_time.count());
		next_recording_event_time = std::min({next_recording_event_time, rec.epg.k.start_time - r->pre_record_time.count(),
				rec.epg.end_time + r->post_record_time.count()});
	}
	housekeeping(now);
	return 0;
}

int rec_manager_t::delete_schedrec(const recdb::rec_t& rec) {
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

int rec_manager_t::new_autorec(const recdb::autorec_t& rec) { return 0; }

int rec_manager_t::update_autorec(const recdb::autorec_t& rec) {
	//@todo
	return 0;
}

int rec_manager_t::delete_autorec(const recdb::autorec_t& rec) { return 0; }

void rec_manager_t::startup(system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	/*
		Check for recordings still in progress due to an earlier crash
		and for recordings

	*/
	using namespace recdb;
	using namespace epgdb;
	auto parent_txn = receiver.recdb.wtxn();
	// auto cr = recdb::rec::find_first(parent_txn);
	auto cr = rec_t::find_by_status_start_time(parent_txn, rec_status_t::IN_PROGRESS, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == rec_status_t::IN_PROGRESS);
		if (rec.epg.end_time + rec.post_record_time < now) {
			dtdebug("Finalising unfinished recording: at start up: " << rec);
			rec.epg.rec_status = rec_status_t::FINISHED;
			recdb::put_record_at_key(cr.maincursor, cr.current_serialized_primary_key(), rec);
		} else if (rec.epg.k.start_time - rec.pre_record_time <= now) {
			// recording in progress
			rec.epg.rec_status = rec_status_t::SCHEDULED; // will cause recording to be continued
			rec.subscription_id = -1;											// we are called after program has exited
			recdb::put_record_at_key(cr.maincursor, cr.current_serialized_primary_key(), rec);
			// next_recording_event_time = std::min(next_recording_event_time, rec.epg.end_time + rec.post_record_time);
		} else {
			// recording in future
			dterror("Found future recording already in progress");
			rec.epg.rec_status = rec_status_t::SCHEDULED; // will cause recording to be continued
			rec.subscription_id = -1;											// we are called after program has exited
			recdb::put_record_at_key(cr.maincursor, cr.current_serialized_primary_key(), rec);
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
			dtdebug("Finalising unfinised recording: at start up: " << rec);
			rec.epg.rec_status = rec_status_t::FINISHED;
			recdb::put_record_at_key(cr.maincursor, cr.current_serialized_primary_key(), rec);
			auto epg_txn = receiver.epgdb.wtxn();
			auto c = epgdb::epg_record_t::find_by_key(epg_txn, rec.epg.k);
			if (c.is_valid()) {
				auto epg = c.current();
				epg.rec_status = epgdb::rec_status_t::NONE;
				assert(epg.k.event_id != TEMPLATE_EVENT_ID);
				epgdb::put_record_at_key(c, c.current_serialized_primary_key(), epg);
			}
			epg_txn.commit();
		}
	}
	parent_txn.commit();
	remove_livebuffers();
}

/*
	For each recording (not live buffer) we store epg data in the
	global recdb database; when new epg arrives we check if this data
	needs an update
*/
void rec_manager_t::on_epg_update(db_txn& txnepg, epgdb::epg_record_t& epg_record) {
	const int maxgap = 5 * 60;
	using namespace recdb;
	auto txnrec = receiver.recdb.rtxn();

	/*
		In the epg records stored in recdb, find the one which matches best, and update it
	*/
	if (auto rec_ = recdb::rec::best_matching(txnrec, epg_record)) {

		auto& rec = *rec_;
		txnrec.commit();

		// we found a matching record
		if (epg_record.rec_status == epgdb::rec_status_t::NONE) {
			epg_record.rec_status = epgdb::rec_status_t::SCHEDULED;
			if (rec.epg.k.event_id == TEMPLATE_EVENT_ID) {
				/*also create a recording for this program;
				 */

				auto r = receiver.options.readAccess();
				auto& options = *r;

				auto recnew =
					new_recording(rec.service, epg_record, options.pre_record_time.count(), options.post_record_time.count());
				next_recording_event_time =
					std::min({next_recording_event_time, epg_record.k.start_time - options.pre_record_time.count(),
							epg_record.end_time + options.post_record_time.count()});
			}
		}
		// perhaps adjust start time of the recording, or event name
		if (rec.epg.k.event_id != TEMPLATE_EVENT_ID) {
			auto txnrec = receiver.recdb.wtxn();
			auto newrec = update_recording_epg(txnrec, txnepg, rec, epg_record);
			next_recording_event_time = std::min({next_recording_event_time, newrec.epg.k.start_time - newrec.pre_record_time,
					newrec.epg.end_time + newrec.post_record_time});
			txnrec.commit();
		}

		// compute number of gaps to see if anonymous recording should be removed
		if (rec.epg.k.event_id == TEMPLATE_EVENT_ID) {
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
				if (auto found = epgdb::best_matching(txnepg, rec.epg.k, rec.epg.end_time, tolerance)) {
					if (found->rec_status == epgdb::rec_status_t::NONE) {
						found->rec_status = epgdb::rec_status_t::SCHEDULED;
						dtdebug("unexpected: epg_record should have been marked as recording already");
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
	} else {
		txnrec.commit();
	}
}

/*
	For each recording in progress, check if it should be stopped
	and stop if needed
*/

void rec_manager_t::stop_recordings(db_txn& rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	// auto cr = recdb::rec::find_first(parent_txn);
	auto cr = rec_t::find_by_status_start_time(rtxn, epgdb::rec_status_t::IN_PROGRESS, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);

	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == epgdb::rec_status_t::IN_PROGRESS);
		if (rec.epg.end_time + rec.post_record_time < now) {
			next_recording_event_time = std::min(next_recording_event_time, rec.epg.end_time + rec.post_record_time);
			dtdebug("END recording " << rec);
			receiver.stop_recording(rec);
			rec.epg.rec_status = epgdb::rec_status_t::FINISHING;
			update_recording(rec);
		}
	}
}

/*
	For each record in the global database, check if recording should be started,
	and act accordingly
*/

void rec_manager_t::start_recordings(db_txn& rtxn, system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	using namespace epgdb;

	auto cr = rec_t::find_by_status_start_time(rtxn, rec_status_t::SCHEDULED, find_type_t::find_geq,
																						 rec_t::partial_keys_t::rec_status);
	time_t last_start_time = 0;
	for (auto rec : cr.range()) {
		assert(rec.epg.rec_status == rec_status_t::SCHEDULED);
		dterror("CANDIDATE REC: " << rec.epg);
		if (rec.epg.k.start_time - rec.pre_record_time <= now) {
			if (rec.epg.end_time + rec.post_record_time <= now) {
				dtdebug("TOO LATE; skip recording: " << rec);
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
					auto ec = chdb::service_t::find_by_key(txn, rec.service.k);
					if (ec.is_valid())
						rec.service = ec.current();
				}

				rec.epg.rec_status = rec_status_t::IN_PROGRESS;
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

int rec_manager_t::housekeeping(system_time_t now) {
	ss::string<128> prefix;
	prefix << "-RECMGR-HOUSEKEEPING";
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
	: receiver(receiver_) {
}

int rec_manager_t::toggle_recording(const chdb::service_t& service, const epgdb::epg_record_t& epg_record, bool insert,
																		bool remove) {
	// bool update_global_db = true;
	assert(!insert || !remove);
	auto x = to_str(epg_record);
	dtdebugx("toggle_recording: %s", x.c_str());
	int ret = -1;

	auto txn = receiver.recdb.rtxn();
	// look up the recording
	if (auto rec = recdb::rec::best_matching(txn, epg_record)) {
		if (insert) {
			dtdebug("Toggle: Recording already exists! " << to_str(*rec));
			txn.abort();
			return -1;
		} else {
			switch (rec->epg.rec_status) {
			case epgdb::rec_status_t::SCHEDULED: {
				txn.commit();
				dtdebug("Toggle: Remove existing (not yet started) recording " << to_str(*rec));
				// recording has not yet started,
				ret = delete_schedrec(*rec);
				return ret;
			}

			case epgdb::rec_status_t::FINISHING:
				return 0;
			case epgdb::rec_status_t::IN_PROGRESS: {
				txn.commit();
				dtdebug("Toggle: Stop running recording " << to_str(*rec));
				receiver.stop_recording(*rec);
				rec->epg.rec_status = epgdb::rec_status_t::FINISHED;
				update_recording(*rec);
				return 0;
			}
			case epgdb::rec_status_t::FINISHED: {
				dtdebug("Recording finished in the past! " << to_str(*rec));
				break;
			}
			case epgdb::rec_status_t::FAILED: {
				dtdebug("Recording failed in the past! " << to_str(*rec));
				break;
			} break;
			default:
				assert(0);
			}
		}
	}

	if (remove) {
		dtdebug("Toggle: no recording to remove! ");
		return -1;
	} else {
		dtdebug("Toggle: Insert new recording");

		ret = epg_record.k.event_id == TEMPLATE_EVENT_ID ? new_anonymous_schedrec(service, epg_record)
			: new_schedrec(service, epg_record);
		return ret;
	}
	txn.commit();
	return 0;
}

void mpm_recordings_t::open(const char* name) {
	dbname = name;
	recdb.open(dbname.c_str());
	{
		auto txn = recdb.wtxn();
		recdb::recdb_t::clean_log(txn);
		txn.commit();
	}

	recepgdb.open_secondary("epg");
	idxdb.open_secondary("idx");
}
