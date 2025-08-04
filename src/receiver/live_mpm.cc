/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "active_service.h"
#include "mpm.h"
#include "receiver.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/util.h"
#include <atomic>
#include <errno.h>
#include <filesystem>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fmt/chrono.h"
using namespace std::chrono;
namespace fs = std::filesystem;

static ss::string<128> relfilename(const recdb::file_t & file) {
	ss::string<128> ret;
	auto real_time_start = std::chrono::floor<seconds>(std::chrono::system_clock::from_time_t(file.real_time_start));
	ret.format("{:02d}_{:%Y%m%d_%T}.ts", file.fileno, real_time_start);
	return ret;
}

void meta_marker_t::init(system_time_t now) {
	last_seen_txn_id = -1;
	num_bytes_safe_to_read = 0;
	current_file_record = {};
	current_marker = {};
	current_marker.packetno_start = std::numeric_limits<uint32_t>::max();
	livebuffer_start_time = now;
	livebuffer_end_time = now;
}

void meta_marker_t::register_playback_client(playback_mpm_t* client) {
	if (std::find(playback_clients.begin(), playback_clients.end(), client) != playback_clients.end()) {
		dterrorf("Attempting to register client which is already registered");
		return;
	}
	playback_clients.push_back(client);
}

void meta_marker_t::unregister_playback_client(playback_mpm_t* client) {
	auto it = std::find(playback_clients.begin(), playback_clients.end(), client);
	if (it == playback_clients.end()) {
		dterrorf("Attempting to unregister client which is not registered");
		return;
	}
	playback_clients.erase(it);
}

/*
	determine the most recent file in use by any playback client
*/
int meta_marker_t::playback_clients_newest_fileno() const {
	int fileno = -1;
	for (auto c : playback_clients) {
		fileno = std::max(c->current_fileno(), fileno);
	}
	return fileno;
}

/*
	waits for a change in this meta_marker compared to "other" and then
	updates other; mutex and other_mutex should be locked before calling
*/
void meta_marker_t::wait_for_update(meta_marker_t& other, std::mutex& mutex, int64_t byte_pos_to_read) {
	dttime_init();

	// lk is now locked
	std::unique_lock<std::mutex> lk(mutex, std::adopt_lock);
	assert(other.num_bytes_safe_to_read <= num_bytes_safe_to_read || num_bytes_safe_to_read == -1);

	cv.wait(lk, [this, byte_pos_to_read, &other] {
		// relock lk
		auto ret = was_interrupted ||
			/*live mpm has moved the position where we want to read
			 */
			(num_bytes_safe_to_read > byte_pos_to_read && //data is available
			 current_pmt_marker.packetno_start>=0); //pmt was received
		if (!other.started) {
			if (ret) {
#if 0
				dtdebugf("metamarker WAIT safe_to_read={:d}/{:d} fileno={:d}/{:d} ret={:d}",
								 num_bytes_safe_to_read, other.num_bytes_safe_to_read,
								 current_file_record.fileno, other.current_file_record.fileno,
								 ret);
#endif
				other.started = true;
			}
		}
#if 0
		if(!ret) {
			dtdebugf("WAIT: num_bytes_safe_to_read={} other.num_bytes_safe_to_read={} "
							 "current_pmt_marker.packetno_start={}",
							 num_bytes_safe_to_read, other.num_bytes_safe_to_read,
							 current_pmt_marker.packetno_start);
		}
#endif
		return ret;
	});
	dttime(2000);

	was_interrupted = false;
	other.current_marker = current_marker;
	other.livebuffer_start_time = livebuffer_start_time;
	other.livebuffer_end_time = livebuffer_end_time;
	other.num_bytes_safe_to_read = num_bytes_safe_to_read;
	other.current_file_record = current_file_record;
	other.current_pmt_marker = current_pmt_marker;
	lk.release(); // needed because caller expects both mutexes to remain locked
}

/*
	waits for a change in this meta_marker compared to "other" and then
	updates other
	"this" is the live stream (active_mpm), other is the playback stream (playback_mpm)
*/
	void active_mpm_t::wait_for_update(meta_marker_t& other, int64_t byte_pos_to_read) {
		meta_marker.writeAccess()->wait_for_update(other, meta_marker.mutex(), byte_pos_to_read);
}

/*
	first and last record from database (for non live).
	This data is assumed to remain constant
*/
int meta_marker_t::update_from_db(db_txn& txn, recdb::marker_t& end_marker) {
	last_seen_txn_id = txn.txn_id();
	using namespace recdb;
	auto c = find_last<recdb::marker_t>(txn);
	if (!c.is_valid()) {
		dterrorf("Could not obtain last marker");
		return -1;
	}
	end_marker = c.current();

	return 0;
}

int meta_marker_t::update_from_db(db_txn& txn, recdb::marker_t& end_marker, milliseconds_t start_play_time,
																	bool need_file_record) {
	last_seen_txn_id = txn.txn_id();
	auto c = recdb::marker_t::find_by_key(txn, recdb::marker_key_t(start_play_time), find_geq);
	if (!c.is_valid()) {
		dtdebugf("Could not obtain marker for time {}", start_play_time);
		return -1;
	}
	current_marker = c.current();
	if (need_file_record) {
		auto cf = recdb::file_t::find_by_fileno(txn, current_file_record.fileno, find_eq);
		if (!cf.is_valid()) {
			dterrorf("Could not read current_file_record");
			return -1;
		}
		current_file_record = cf.current();
	}
	using namespace recdb;
	c = find_last<recdb::marker_t>(txn);
	if (!c.is_valid()) {
		dterrorf("Could not obtain last marker");
		return -1;
	}
	end_marker = c.current();

	return 0;
}

mpm_index_t::mpm_index_t(const char* idx_dirname_)
	: idx_dirname(idx_dirname_) {
}

/*!
	Opens the recording and  the index database
*/
void mpm_index_t::open_index() {
	mpm_rec.open(idx_dirname.c_str());
}

mpm_t::mpm_t(bool readonly)
	: db(std::make_shared<mpm_index_t>())
	, filemap(mmap_size, readonly) {
}

mpm_t::mpm_t(mpm_t&other)
//: live_parent(nullptr)
	:	db(other.db)
	, filemap(other.filemap.map_len, other.filemap.readonly)
	, dirname(other.dirname) {
}


mpm_t::mpm_t(active_mpm_t&other, bool readonly)
//: live_parent(&other)
	: db(other.db)
	, filemap(other.filemap.map_len, readonly)
	, dirname(other.dirname) {
}


active_mpm_t::active_mpm_t(active_service_t* active_service_)
	: mpm_t(false)
	, stream_buffer_t(active_service_, &db->mpm_rec.idxdb)
	, periodic(60*30)
	, creation_time ( system_clock_t::now())
{
	using namespace dtdemux;
	dirname = make_dirname(active_service, now);
	file_time_limit = active_service->receiver.options.readAccess()->livebuffer_mpm_part_duration;
	create();
}

ss::string<128> active_mpm_t::make_dirname(active_service_t* active_service, system_time_t start_time) {
	// auto start_time=time(NULL);
	/*TODO: 1. sometimes mux and ts_id are incorrect at start; This is a problem when two
		channels are streamed from the same mux
		2. After changing channel on the same mux, directory already exists
	*/
	ss::string<128> dirname;
	dirname.format("{:s}/A{:02d}_t{:05d}_sid{:05d}_{:%Y%m%d_%T}",
								 active_service->receiver.options.readAccess()->live_path.c_str(),
									active_service->get_adapter_no(), active_service->current_service.k.ts_id,
								 active_service->current_service.k.service_id,
								 std::chrono::floor<std::chrono::seconds>(start_time));
	return dirname;
}

void active_mpm_t::mkdir(const char* dirname) {
	if (!mkpath(dirname)) {
		dterrorf("Could not create dir {}", dirname);
		throw std::runtime_error("Failed to create live buffer");
	}
}

/*!
	create the directory structure, including the database
	opens the index database
*/
void active_mpm_t::create() {
	mkdir(dirname.c_str());
	db->idx_dirname.format("{}/index.mdb", dirname);
	if (!mkpath(db->idx_dirname)) {
		dterrorf("Could not create dir {}", db->idx_dirname.c_str());
		throw std::runtime_error("Failed to create live buffer");
	}
	db->open_index();
	if (next_data_file(creation_time) < 0)
		throw std::runtime_error("Failed to create live buffer");
}

void active_mpm_t::destroy() {
	std::error_code ec;
	stream_parser.exit();
	if (!fs::remove_all(dirname.c_str(), ec)) {
		dterrorf("Error deleting {}:{}", dirname, ec.message());
		throw std::runtime_error("Failed to remove live buffer");
	}
}

/*!
	move small part of data at the end of the file (past last i-frame)
	to a new file, while handling  undecrypted or already parsed data properly
*/

void active_mpm_t::transfer_filemap(int fd, int64_t num_bytes_in_final_mmap) {
	mmap_t newfilemap(filemap.map_len, false);
	// fd will be owned by filemap
	newfilemap.init(fd, 0);
	/* num_bytes_processed number of bytes proccessed in the current mapped region of the
		 file; num_bytes_processed+filemap.offset will be the final size of the old file
	*/
	auto num_bytes_processed =
		num_bytes_in_final_mmap - current_file_stream_packetno_start * (int64_t)ts_packet_t::size - filemap.offset;
	assert(num_bytes_processed <= filemap.decrypt_pointer);
	assert(filemap.decrypt_pointer <= filemap.write_pointer);
	if (num_bytes_processed < filemap.write_pointer) {
		auto* start = filemap.buffer + num_bytes_processed; //first byte of new file
		auto num_bytes_to_move = filemap.write_pointer - num_bytes_processed;
		assert(num_bytes_processed == filemap.write_pointer - num_bytes_to_move);
		dtdebugf("Moving {:d} bytes to new file", num_bytes_to_move);
		memcpy(newfilemap.buffer, start, num_bytes_to_move);
		newfilemap.decrypt_pointer = filemap.decrypt_pointer - num_bytes_processed;
		newfilemap.write_pointer = filemap.write_pointer - num_bytes_processed;
		assert(newfilemap.write_pointer == num_bytes_to_move);
	} else {
		assert(num_bytes_processed == filemap.write_pointer);
	}
	if (filemap.fd >= 0) {
		dtdebugf("TRUNCATE from ={:d} to {:d} num_bytes_in_final_mmap={:d}", filesize_fd(filemap.fd),
						 num_bytes_processed + filemap.offset, num_bytes_in_final_mmap);
		/*filemap.offset is the number of bytes before the current mmap in the file
			num_bytes_processed is the numbe of bytes in the current, mmap which maps the last part pf the fole
			num_bytes_processed + filemap.offset is the new size of the file
		 */
		if (ftruncate(filemap.fd, num_bytes_processed + filemap.offset) < 0) {
			dterrorf("Error while truncating {}", strerror(errno));
		}
		filemap.unmap();
		filemap.close();
	}
	filemap = std::move(newfilemap); // transfer resources from newfilemap
}

/*!
	Start a recording based on an epg record; this must be done when the recording should really
	start, which could be slightly earlier than the official start, or later than the official
	start (when the recording was already in progress, when user decided to record)

	now is the current time

	epgrec is needed to compute start_time

*/
recdb::rec_t active_mpm_t::start_recording(
	subscription_id_t subscription_id, recdb::rec_t rec /*on purpose not a reference!*/) {

	{
		auto mm = meta_marker.readAccess();
		rec.stream_time_start = mm->livebuffer_stream_time_start;
		rec.real_time_start = system_clock_t::to_time_t(mm->livebuffer_start_time);
	}
	rec.stream_time_end = stream_parser.event_handler.last_saved_marker.k.time;
	rec.epg.rec_status = epgdb::rec_status_t::IN_PROGRESS;
	// TODO: times in start_play_time may have a different sign than stream_time (which can be both negative and
	// positive)
	using namespace recdb;
	num_recordings_in_progress++;
	dtdebugf("num_recordings_in_progress changed to {:d}", num_recordings_in_progress);
	using namespace recdb;
	using namespace recdb::rec;
	if (rec.filename.size() == 0)
		recdb::rec::make_filename(rec.filename, rec.service, rec.epg);
	assert(rec.filename[0] != '/'); // we want relative paths

	auto rectxn = db->mpm_rec.recdb.wtxn();

	put_record(rectxn, rec);
	rectxn.commit();
	return rec;
}

static inline int64_t overlap_duration(int64_t a1, int64_t a2, int64_t b1, int64_t b2) {
	auto left = std::max(a1, b1);
	auto right = std::min(a2, b2);
	return right - left;
}

void mpm_copylist_t::run() {
	auto mpm_db = dst_dir / "index.mdb";
	mpm_index_t mpmidx(mpm_db.c_str());
	mpmidx.open_index();
	auto txn = mpmidx.mpm_rec.idxdb.rtxn();
	run(txn);
	txn.abort();
}

void mpm_copylist_t::run(db_txn& txn) {
	std::error_code ec;
	auto dbdir = dst_dir / "index.mdb"; // location of the recording's database
	// open the recording database in the mpm
	using namespace recdb;
	auto c = find_first<recdb::file_t>(txn);
	for (auto file : c.range()) {
		if (file.fileno < fileno_offset)
			continue;
		auto& dstfname = file.filename;
		auto f = file;
		f.fileno -= fileno_offset;
		auto srcfname = ::relfilename(f);
		auto src = src_dir / srcfname.c_str();
		auto dst = dst_dir / dstfname.c_str();
		fs::create_hard_link(src, dst, ec); /*
																					TODO: this part could be too slow in some cases (especially if link is replaced
																					by copy) In that case, the linking can be done in another thread provided the
																					rec_t record is not deleted in the livebuffer yet. This prevents the
																					recording's data from being deleted before it is linked/copied*/
		if (ec) {
			dterrorf("Error hardlinking {} to {}: {}", src.c_str(), dst.c_str(), ec.message());
		}
	}
}

/*
	copy data and database records from a live recording into a recording
	TODO: this code needs to be also called from the livebuffer cleanup code,
	to recover after a crash

	called from recmgr at startup to clean old livebuffers
	Also called from active_mpmt_t at end of recording
*/

int finalize_recording(db_txn& livebuffer_idxdb_rtxn, mpm_copylist_t& copy_command, mpm_index_t* db) {
	// filesystem location where recording will be stored
	auto& destdir = copy_command.dst_dir;
	auto& rec = copy_command.rec;
	auto dbdir = destdir / "index.mdb"; // location of the recording's database
	// create directory in which to store the recording and the recording's database directory
	if (!mkpath(dbdir.c_str())) {
		dterrorf("Could not create dir {}", dbdir.c_str());
		throw std::runtime_error("Failed to create database dir");
	}

	// open the database in which to store the recording
	mpm_index_t recidx(dbdir.c_str());

	recidx.open_index();
	auto rec_txn = recidx.mpm_rec.recdb.wtxn(); // txn for the new mpm recording database

	auto start = rec.real_time_start;
	auto end = rec.real_time_end;

	// insert the description of the recording in the database
	put_record(rec_txn, rec);

	// A parent transaction and its cursors may not issue any other operations than
	// mdb_txn_commit and mdb_txn_abort while it has active child transactions.
	auto idx_txn = rec_txn.child_txn(recidx.mpm_rec.idxdb);

	milliseconds_t stream_time_start{-1}; // for data in the livebuffer (usually 0)
	milliseconds_t stream_time_end{-1};		// for data in the livebuffer
	// in case the recording already existed, append the livebuffer's data instead of overwriting it
	milliseconds_t stream_time_offset{};
	int64_t packetno_offset{0};
	int64_t stream_packetno_start{-1};

	using namespace recdb;
	{
		/* if files already exist from an earlier recording (e.g., recording has restarted after a crash) ,
			 then update numbering so as to append files rather than overwrite*/
		auto c = find_first<recdb::file_t>(idx_txn);
		if (c.is_valid()) {
			const auto& last = c.current();
			stream_time_offset = last.stream_time_end;
			copy_command.fileno_offset = last.fileno + 1;
			packetno_offset = last.stream_packetno_end;
		}
	}
	{ // copy the file records into the database
		auto c = find_first<recdb::file_t>(livebuffer_idxdb_rtxn);

		for (auto f : c.range()) {
			bool not_finalised = (f.stream_time_end == std::numeric_limits<milliseconds_t>::max());
			if (not_finalised)
				break; // at this point either the last file has been updated with proper end_times, or mpm is still live
			stream_time_end = f.stream_time_end;
			auto real_time_end = f.real_time_end;
			if (overlap_duration(start, end, f.real_time_start, real_time_end) > 0) {
				if (int64_t(stream_time_start) < 0) {
					stream_time_start = f.k.stream_time_start;
				}
				if (not_finalised) {
					dterrorf("File in recording was not finalised");
					auto c = find_last<recdb::marker_t>(livebuffer_idxdb_rtxn);
					if (!c.is_valid()) {
						dterrorf("no index records");
					} else {
						auto marker = c.current();
						stream_time_end = marker.k.time;
						f.stream_time_end = stream_time_end;
						f.real_time_end = real_time_end;
						assert(f.stream_packetno_end != std::numeric_limits<int64_t>::max());
					}
				}
				f.fileno += copy_command.fileno_offset;
				if(copy_command.fileno_offset !=0) {
					f.filename= ::relfilename(f);
				}
				f.k.stream_time_start += stream_time_offset;
				f.stream_packetno_start += packetno_offset;
				f.stream_packetno_end += packetno_offset;
				// insert the file record
				put_record(idx_txn, f);
				// put the file in the recording's filesystem directory
				// copy_command.filenames.push_back(f.filename.c_str());
			}
		}
	}
	{																								// copy the marker records into the database
		recdb::marker_key_t k(rec.stream_time_start); // refers to the livebuffer's value (no offset!)
		auto c = recdb::marker_t::find_by_key(livebuffer_idxdb_rtxn, k, find_geq);
		for (auto marker : c.range()) {
			if (marker.k.time <= stream_time_end) {
				if(stream_packetno_start<0)
					stream_packetno_start = marker.packetno_start;
				// insert the marker record
				marker.packetno_start += packetno_offset;
				marker.packetno_end += packetno_offset;
				marker.k.time += stream_time_offset;
				put_record(idx_txn, marker);

			} else
				break;
		}
	}
	{
// copy the pmt markers into the recording database
		auto c = recdb::pmt_marker_t::find_by_key
			(livebuffer_idxdb_rtxn,
			 stream_packetno_start, // refers to the livebuffer's value (no offset!)
			 find_leq);
		for (auto sd : c.range()) {
			if (sd.stream_time_start <= stream_time_end) {
				// insert the marker record
				sd.packetno_start +=  packetno_offset;
				sd.stream_time_start += stream_time_offset;
				put_record(idx_txn, sd);
			} else
				break;
		}
	}

	idx_txn.commit();
	rec_txn.commit();
	return 0;
}

/*!
	Stop a recording, which may have been stopped already.
	Returns the finalized recording record
	Called from the recmgr code.
*/

int active_mpm_t::stop_recording(const recdb::rec_t& rec_in, mpm_copylist_t& copy_command) {

	auto now = system_clock_t::now();
	// lookup record in the livebuffer database
	auto rec1_txn = db->mpm_rec.recdb.rtxn();
	auto c = recdb::rec_t::find_by_key(rec1_txn, rec_in.epg.k, find_eq);
	if (!c.is_valid()) {
		dterrorf("Stopping a non existing recording");
		return -1;
	}
	auto rec = c.current(); // most uptodate version of record
	if (rec.epg.rec_status == epgdb::rec_status_t::FINISHED) {
		dtdebugf("recording was already stopped");
		return -1;
	}
	rec1_txn.abort();
	{
		auto ret = next_data_file(now);
		dtdebugf("Closed last mpm part as part of ending recording ret={:d}", ret);
	}
	assert(num_recordings_in_progress > 0);
	rec.stream_time_end = stream_parser.event_handler.last_saved_marker.k.time;
	// rec.stream_packetno_end = stream_parser.event_handler.last_saved_marker.packetno_end;
	rec.real_time_end = system_clock_t::to_time_t(now);
	rec.epg.rec_status = epgdb::rec_status_t::FINISHING;
	auto rec_txn = db->mpm_rec.recdb.wtxn(); // we need to reopen transaction. next_data_file opended its own transaction
	num_recordings_in_progress--;
	dtdebugf("num_recordings_in_progress changed to {:d}", num_recordings_in_progress);
	// filesystem location where recording will be stored

	rec.epg.rec_status =
		epgdb::rec_status_t::FINISHED; // will be stored in the recording  and in the global recordings database
	put_record(rec_txn, rec);					 // store in the live buffer
	rec_txn.commit();

	auto destdir =
		fs::path(active_service->receiver.options.readAccess()->recordings_path.c_str()) / fs::path(rec.filename.c_str());
	copy_command = mpm_copylist_t(fs::path(dirname.c_str()), destdir, rec);

	auto livebuffer_idxdb_rtxn = db->mpm_rec.idxdb.rtxn(); // for accessing the livebuffer's database
	::finalize_recording(	livebuffer_idxdb_rtxn, copy_command, db.get());
	livebuffer_idxdb_rtxn.abort();
	return 0;
}

void active_mpm_t::forget_recording_in_livebuffer(const recdb::rec_t& rec) {
	/*delete the recording in the livebuffer's database, marking that recording has been
		successfully moved to the recording
	*/
	auto rec_txn = db->mpm_rec.recdb.wtxn(); // for accessing the livebuffer's database
	/*now delete the recording record from the livebuffer database to indicate that the
		recording is no longer needed*/
	delete_record(rec_txn, rec);
	rec_txn.commit();
}

void active_mpm_t::update_recording(recdb::rec_t& rec, const chdb::service_t& service,
																		const epgdb::epg_record_t& epgrec) {
	auto rec_wtxn = db->mpm_rec.recdb.wtxn();
	rec_wtxn.commit();
}

/*!
	Update the current end times and end packet no's of all active recordings
*/
void active_mpm_t::update_recordings(db_txn& parent_txn, system_time_t now) {
	if (num_recordings_in_progress == 0)
		return;
	{
		int num = 0;
		using namespace recdb;
		auto cr = find_first<recdb::rec_t>(parent_txn);
		for (auto rec : cr.range()) {
			if (rec.epg.rec_status != epgdb::rec_status_t::IN_PROGRESS)
				continue;
			rec.stream_time_end = stream_parser.event_handler.last_saved_marker.k.time;
			// rec.stream_packetno_end = stream_parser.event_handler.last_saved_marker.packetno_end;
			rec.real_time_end = system_clock_t::to_time_t(now);
			num++;
#if 0
			put_record_at_key(cr, cr.current_serialized_primary_key(), rec);
#else
			update_record_at_cursor(cr, rec);
#endif
		}
		if (num_recordings_in_progress != num) {
			dtdebugf("num_recordings_in_progress changed from {:d} to {:d}", num_recordings_in_progress, num);
		}
		num_recordings_in_progress = num;
	}
}

/*
	Needed when we exit while a  recording is in progress, or when recovering a recording
	after the gui is restarted (e.g., after a crash)

	return -1 on error
*/
int close_last_mpm_part(db_txn& idx_txn, const ss::string_& dirname) {
	using namespace recdb;
	recdb::marker_t last_marker;
	recdb::file_t last_file;
	auto cmarker = find_last<recdb::marker_t>(idx_txn);
	int ret = 0;
	if (cmarker.is_valid()) {
		last_marker = cmarker.current();
	} else  {
		dterrorf("Could not find last marker in mpm");
		return -1;
	}

	auto cfile = find_last<recdb::file_t>(idx_txn);
	if (cfile.is_valid()) {
		last_file = cfile.current();
	} else  {
		dterrorf("Could not find last file in mpm");
		return -1;
	}

	auto end_packet = last_marker.packetno_end;
	auto first_packet = last_file.stream_packetno_start;

	bool not_finalised = (last_file.stream_packetno_end == std::numeric_limits<int64_t>::max());
	if (not_finalised) {
		assert(last_file.fileno >= 0);
		last_file.stream_time_end = last_marker.k.time;
		auto file_duration_seconds = (500+int64_t(last_file.stream_time_end - last_file.k.stream_time_start)) / 1000;
		last_file.real_time_end = last_file.real_time_start + file_duration_seconds;
		last_file.stream_packetno_end = last_marker.packetno_end;
		put_record(idx_txn, last_file);
		dtdebugf("Finalized last_file");
	}

	ss::string<128> filename;
	auto fname = ::relfilename(last_file);
	filename.format("{:s}/{:s}", dirname.c_str(), fname.c_str());

	auto* fp_out = fopen64(filename.c_str(), "a");
	if (!fp_out) {
		dterror_nicef("Could not create output file {}", filename);
		idx_txn.abort();
		ret = -1;
	} else {
		int fd = fileno(fp_out);
		assert(end_packet >= first_packet);
		auto num_bytes_in_last_file = (end_packet - first_packet) * (int64_t)ts_packet_t::size;

		if (ftruncate(fd, num_bytes_in_last_file) < 0) {
			dterrorf("Error while truncating {}", strerror(errno));
			fclose(fp_out);
			ret = -1;
		}
	}

	return ret;
}

/*!
	create a new empty data file, open it and map it to memory
	if old file and map exist, then it is closed and unmapped
*/
int active_mpm_t::next_data_file(system_time_t now) {
	auto idx_txn = db->mpm_rec.idxdb.wtxn();
	using namespace recdb;
	auto cfile = db->mpm_rec.idxdb.tcursor<file_t>(idx_txn);
	current_file_time_start = now;
	auto mm = meta_marker.writeAccess();
	int64_t end_packet = stream_parser.event_handler.last_saved_marker.packetno_end;
	auto num_bytes_in_final_mmap = end_packet * ts_packet_t::size;
	assert(mm->num_bytes_safe_to_read <= num_bytes_in_final_mmap);
	mm->current_marker = stream_parser.event_handler.last_saved_marker;
	auto stream_time_end = mm->current_marker.k.time; // could be 0
	if (current_fileno != -1) {
		// first finalise last file record if there is one
		// stream_time_end may be slightly off because bytes may have been received after last pcr
		auto tmp = mm->current_file_record; // make a copy because of possible concurrent access
		tmp.stream_time_end = stream_time_end;
		tmp.real_time_end = system_clock_t::to_time_t(now);

		tmp.stream_packetno_end = end_packet;
		put_record(cfile, tmp);
	}
	current_fileno++;

	auto new_file_stream_time_start = stream_time_end;
	auto new_file_stream_packetno_start = end_packet;
	mm->current_file_record.real_time_start = system_clock_t::to_time_t(now);
	mm->current_file_record.fileno = current_fileno;

	auto relfilename  = ::relfilename(mm->current_file_record);

	current_filename.clear();
	current_filename.format("{:s}/{:s}", dirname.c_str(), relfilename.c_str());

	auto* fp_out = fopen64(current_filename.c_str(), "w+");
	if (!fp_out) {
		dterror_nicef("Could not create output file {}", current_filename);
		idx_txn.abort();
		return -1;
	}
	int fd = fileno(fp_out);
	if (ftruncate(fd, initial_file_size) < 0) {
		dterrorf("Error while truncating {}", strerror(errno));
		idx_txn.abort();
		fclose(fp_out);
		return -1;
	}
	dtdebugf("Start streaming to {}", current_filename);

	if (setvbuf(fp_out, NULL, _IONBF, 0)) // TODO: is this needed?
		dterrorf("setvbuf failed: {}", strerror(errno));
	transfer_filemap(fd, num_bytes_in_final_mmap);
	assert(num_bytes_in_final_mmap >= mm->num_bytes_safe_to_read);
	assert(mm->num_bytes_safe_to_read <= num_bytes_in_final_mmap);
	// mm->current_marker = 	stream_parser.event_handler.last_saved_marker;
	mm->current_file_record.k.stream_time_start = new_file_stream_time_start;
	mm->current_file_record.stream_time_end = std::numeric_limits<milliseconds_t>::max(); // signifies infinite
	mm->current_file_record.real_time_end = std::numeric_limits<time_t>::max(); // signifies infinite
	mm->current_file_record.stream_packetno_start = new_file_stream_packetno_start;
	mm->current_file_record.stream_packetno_end = std::numeric_limits<int64_t>::max(); // signifies infinite
	mm->current_file_record.filename = relfilename;

	put_record(cfile, mm->current_file_record);
	idx_txn.commit();
#if 0
	dtdebugf("NOTIFY: num_bytes_safe_to_read={} "
					 "current_pmt_marker.packetno_start={} "
					 "fileno={}",
					 mm->num_bytes_safe_to_read,
					 mm->current_pmt_marker.packetno_start,
					 mm->current_file_record.fileno
		);
#endif
	mm->cv.notify_all();

	current_file_stream_packetno_start = new_file_stream_packetno_start;
	return 1;
}

void active_mpm_t::close() {
	current_fileno = -1;
	filemap.unmap();
	filemap.close();
	stream_parser.exit();
	// TODO: check that parser is complete destroyed
	this->active_service = nullptr;
	dtdebugf("mpm closed");
}


bool active_mpm_t::file_used_by_recording(const recdb::file_t& file) const {
	auto txn = db->mpm_rec.idxdb.rtxn();
	using namespace recdb;
	auto c = find_first<recdb::rec_t>(txn);
	for (const auto& rec : c.range()) {
		auto start = rec.real_time_start;
		auto end = rec.real_time_end;
		if (overlap_duration(start, end, file.real_time_start, file.real_time_end) > 0) {
			return true;
		}
	}
	return false;
}

void active_mpm_t::delete_old_data(db_txn& parent_txn, system_time_t now) {
	/*
		remove old data by removing old mpm parts. Removal is done file by file (typically 5 min of mpeg data)
		The deleted data is the one older than  now - live_buffer_duration, with the following exceptions:
		1.  only files not currently in use by live viewing, or not newer than those currently in use
		by live viewing should be removed. Otherwise we may be viewing an old part of the live buffer and
		when a newer part is then reached, it may no longer exist and we have a gap in playback
		2. we do not delete old data when recordings are in progress. These

	*/
	using namespace recdb;
	auto cfile = find_first<recdb::file_t>(parent_txn);
	auto timeshift_duration = active_service->receiver.options.readAccess()->timeshift_duration;
	auto new_data_start_time = now - timeshift_duration;
	milliseconds_t new_data_stream_time_start{0};
	for (const auto& file : cfile.range()) {
		auto e = file.real_time_end;
		// double test is needed because e can be equal to std::numeric_limits<time_t>::max()
		auto delta = now - system_clock_t::from_time_t(e);
		if (system_clock_t::to_time_t(now) > e && delta > timeshift_duration) {
			ss::string<128> filename;
			filename.format("{:s}/{:s}", dirname.c_str(), file.filename.c_str());
			auto playing_fileno = meta_marker.readAccess()->playback_clients_newest_fileno();
			if ((int)file.fileno < playing_fileno) {
				if (!file_used_by_recording(file)) {
					dtdebugf("REMOVE TIMESHIFT FILE {:d}: {:s} age={:d}", file.fileno, filename.c_str(),
									 std::chrono::duration_cast<std::chrono::seconds>(delta).count());
					std::filesystem::remove(std::filesystem::path(filename.c_str()));
					new_data_stream_time_start = std::max(new_data_stream_time_start, file.stream_time_end);
					delete_record_at_cursor(cfile); //@todo: does this cfile cursor point to the current "file"?
				}
				break; // done

			} else {
				dtdebugf("POSTPONE REMOVE TIMESHIFT FILE {:d} ({:d} still playing back): {:s} age={:d}s", file.fileno,
								 playing_fileno, filename.c_str(), std::chrono::duration_cast<std::chrono::seconds>(delta).count());
				new_data_start_time =
					system_clock_t::from_time_t(std::min(file.real_time_start, system_clock_t::to_time_t(new_data_start_time)));
			}
		} else {
			dtdebugf("KEEP TIMESHIFT FILE {:d}: {:s}\n", file.fileno, file.filename.c_str());
			break;
		}
	}
	auto mm = meta_marker.writeAccess();
	mm->livebuffer_start_time = std::max(new_data_start_time, mm->livebuffer_start_time);
	mm->livebuffer_stream_time_start = std::max(new_data_stream_time_start, mm->livebuffer_stream_time_start);
}

active_mpm_t::~active_mpm_t()
{
	//streamparser.unregister_parser(pid)
}

void active_mpm_t::save_pmt(system_time_t now_, const dtdemux::pmt_info_t& pmt_info,
														const ss::bytebuffer<256>& pmt_sec_data) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	const auto& marker = this->stream_parser.event_handler.last_saved_marker;

	auto current_pmt_marker = pmt_marker_t(pmt_info.stream_packetno_end, now, marker.k.time, pmt_info.pmt_pid,
																				pmt_info.audio_languages(), pmt_info.subtitle_languages(), pmt_sec_data);
	auto txnidx = this->db->mpm_rec.idxdb.wtxn();
	put_record(txnidx, current_pmt_marker);
	txnidx.commit();
	{
		auto mm = this->meta_marker.writeAccess();
		mm->stream_status = stream_status_t::ACTIVE;
		mm->current_pmt_marker = current_pmt_marker;
	}
}

playback_info_t active_mpm_t::get_current_program_info() const {
	playback_info_t ret;
	if(active_service)
		ret.service = active_service->current_service;

	auto mm = this->meta_marker.readAccess();
	ret.stream_status = mm->stream_status;
	ret.start_time = mm->livebuffer_start_time;
	ret.end_time = mm->livebuffer_end_time;
	ret.play_time = mm->livebuffer_end_time;
	ret.is_recording= false;
	return ret;

}

recdb::live_service_t active_mpm_t::get_live_service(subscription_id_t subscription_id) const {
	assert(active_service);
	auto& receiver = active_service->receiver;
	const char* p = this->dirname.c_str() + receiver.options.readAccess()->live_path.size();
	if (p[0] == '/')
		p++;
	assert(p - this->dirname.c_str() < this->dirname.size());
	//note that last_use_time is set to -1, meaning: still being used
	return recdb::live_service_t(getpid() /*owner*/ , (int)subscription_id,
															 system_clock_t::to_time_t(this->creation_time),
															 (int8_t) active_service->get_adapter_no(),
															 -1, active_service->get_current_service(), p, {}/*last_epg_update_time*/ /*, epg*/);
}

void active_mpm_t::housekeeping(system_time_t now) {
	auto parent_txn = this->db->mpm_rec.idxdb.wtxn();
	auto rec_txn = parent_txn.child_txn(this->db->mpm_rec.recdb);
	// Update stream_time_end and real_time end periodically
	this->update_recordings(rec_txn, now);
	rec_txn.commit();
	/*check if newer epg data hase arrived and
		transfer it into the local mpm database

		@todo: is it wise to run directory deletion from this thread?
	*/

	periodic.run([this, &parent_txn](system_time_t now) { this->delete_old_data(parent_txn, now); }, now);

	parent_txn.commit();
	/*@todo:
		1) global recording database must also be kept up todate.
		2) when recordings stop, receiver thread should know about this
		It may be more efficient to do part of the housekeeping in the receiver thread

		update_recordings can be run a second time on the global db

		stop_completed_recordings has side effects: it calls finalize recording.
		@todo: separate these side effects

		@todo: if we update the global db from the receiver thread (more efficient:
		only a single transaction) there could be border cases which cause races.

		The main dangerous cases are those where receiver and active_mpm have different
		views on which recordings are running (e.g., receiver first stops recording, but
		active_mpm has not taken action. Then receiver is asked to restart the same recording,
		but active_mpm sees it is already running).

		=> conclusion may be that start/stop recording should only be done from receiver thread?

		*/
}


bool active_mpm_t::process_service_data(int num_bytes_decrypted_now) {
	bool may_start_new_file = false;
	/*
		For an encrypted channel, note that the code below will not parse unencrypted data such
		as PMT and PAT while problems with video/audio scrambling exist and as a result num_bytes_decrypted_now==0.
		However, video and audio streams are not present until after the first pmt is successfully read. So we should be safe
		@todo: we could make discarding data more clever by only skipping encrypted packets
	*/
	bool has_new_payload{false};
	assert(num_bytes_decrypted_now + this->filemap.decrypt_pointer <= this->filemap.write_pointer);

	//set location where stream_parser will start parsing data
	this->set_buffer(num_bytes_decrypted_now);

	if(num_bytes_decrypted_now) {
		assert(num_bytes_decrypted_now % ts_packet_t::size == 0);

		auto old_packetno_start = this->stream_parser.event_handler.last_saved_marker.packetno_start;
		dttime_init();
		this->stream_parser.parse();
		dttime(500);
		this->advance_decrypt_pointer(num_bytes_decrypted_now);

		if (this->stream_parser.event_handler.last_saved_marker.packetno_start != old_packetno_start) {
			has_new_payload = true;
			may_start_new_file = true;
			/*A marker was discovered in the current data (end of i-frame);
				Only then it is ok to switch to a new data file; reason is that num_bytes_safe_to_read
				must be increased as soon as possible in order to minimize delay for reading threads.
				However we can only increase it when we know the current file is no longer
				growing, i.e., just after a marker. So we proceed when this marker has been read very
				recently
			*/
		}
		/*
			todo:
			1. find range of packets encrypted with same pid and same parity
			2. lookup the key with the right parity which should not be marked "outdated"
			and should not be "too new". The latter could occur when a key was lost
			or when for some reason processing is heavily delayed (should not happen)
			2.a. If no key can be found, then continue reading data, but not decrypting it; continue reading data
			untl key becomes available
			2.b. decrypt what can be
			decryped
			3. if we encounter a new
			parity, mark key for current parity invalid (being careful not to mark a newer key invalid) and continue with
			current key

			todo 1: filemap.advance should keep both decrypt_pointer and file_pointer in memory
			(or in two mapped regions)
			todo 2: implement method for waiting for keys
			todo 3: is it useful to decrypt more than 1 packet at a time?

		*/
		assert(num_bytes_decrypted_now >=0);
		this->num_bytes_decrypted += num_bytes_decrypted_now;
	}

#if 0
	dtdebug_nicex("MPM STATUS: read={:d} parsed={:d} decrypted={:d}", num_bytes_read,
								stream_parser.event_handler.last_saved_marker.packetno_end*ts_packet_t::size,
									num_bytes_decrypted);
#endif

	if (may_start_new_file && this->file_time_limit >= 0s &&
			(now - this->current_file_time_start > this->file_time_limit) &&
			this->num_recordings_in_progress == 0) {
		/* we start a new file; ideally, we would like the new file to start with a combination
			 pat/pmt/i-frame. @todo The current implementation does not work as it splits at the end
			 of an i-frame, but at least this ensures that all data for an iframe is in a single file;
			 fixing the problem also means moving more data
			*/
		this->next_data_file(now);
	} else if (num_bytes_decrypted_now) {
		auto mm = this->meta_marker.writeAccess();
		mm->livebuffer_end_time = now;
		mm->current_marker = this->stream_parser.event_handler.last_saved_marker;
		assert(mm->num_bytes_safe_to_read <= this->num_bytes_decrypted); // KNOWN PROBLEM: we may not go back!!
		/*
			playpback_mpms should never read past the next pmt, as a a pmt change can occur
		 */
		auto v = this->num_bytes_decrypted;
		if(likely(active_service->pmt_parser)) {
			assert(v>= active_service->pmt_parser->last_section_end_bytepos);
			v=std::min(v, active_service->pmt_parser->last_section_end_bytepos);
		}
		else if(active_service->pat_parser) {
			assert(v>= active_service->pat_parser->last_section_end_bytepos);
			v=std::min(v, active_service->pat_parser->last_section_end_bytepos);
		}
		v= std::min(v, mm->current_marker.packetno_end  * (int64_t) dtdemux::ts_packet_t::size);
		mm->num_bytes_safe_to_read = v;
		if (!mm->started && mm->num_bytes_safe_to_read > 0) {
			mm->started = true;
			dtdebugf("notifying metamarker: safe_to_read={:d}", mm->num_bytes_safe_to_read);
		}
			//		TODO: add num_bytes_decrypted??? How to save time at start? e.g., first minute alway safe to read?
		mm->cv.notify_all();
	}
	return has_new_payload;
}

std::unique_ptr<playback_mpm_t> active_mpm_t::make_playback_mpm(subscription_id_t subscription_id) {
	auto ret = std::make_unique<playback_mpm_t>(*this, active_service->current_service, subscription_id);
	this->meta_marker.writeAccess()->register_playback_client(ret.get());
	return ret;
}

void active_mpm_t::register_parser_pid(int service_id, const pid_info_t& pidinfo)
{
	if (is_video(pidinfo.stream_type))
		this->stream_parser.register_video_pids(service_id, pidinfo.stream_pid, pidinfo.stream_type);
	else if (is_audio(pidinfo))
		this->stream_parser.register_audio_pids(service_id, pidinfo.stream_pid, pidinfo.stream_type);
}
