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

#ifndef NDEBUG
static void  testf(db_txn& txn, recdb::file_t& file) {
	auto c = recdb::find_first<recdb::file_t>(txn);
	for(auto f: c.range()) {
		if(f.k.stream_time_start == file.k.stream_time_start && f.fileno != file.fileno) {
			assert(0);
		}
	}
}
#endif

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
void meta_marker_t::wait_for_update(meta_marker_t& other, std::mutex& mutex) {
	dttime_init();

	// lk is now locked
	std::unique_lock<std::mutex> lk(mutex, std::adopt_lock);
	assert(other.num_bytes_safe_to_read <= num_bytes_safe_to_read);

	cv.wait(lk, [this, &other] {
		// relock lk
		auto ret = was_interrupted ||
			(num_bytes_safe_to_read > other.num_bytes_safe_to_read && //data is available
			 last_streams.packetno_start>=0); //pmt was received
		if (!other.started) {
			if (ret) {
				dtdebugf("metamarker WAIT safe_to_read={:d} ret={:d}", num_bytes_safe_to_read, ret);
				other.started = true;
			}
		}
		return ret;
	});
	dttime(2000);

	was_interrupted = false;
	other.current_marker = current_marker;
	other.livebuffer_start_time = livebuffer_start_time;
	other.livebuffer_end_time = livebuffer_end_time;
	other.num_bytes_safe_to_read = num_bytes_safe_to_read;
	other.current_file_record = current_file_record;
	other.last_streams = last_streams;
	lk.release(); // needed because caller expects both mutexes to remain locked
}

/*
	waits for a change in this meta_marker compared to "other" and then
	updates other
	"this" is the live stream (active_mpm), other is the playback stream (playback_mpm)
*/
void active_mpm_t::wait_for_update(meta_marker_t& other) {
	meta_marker.writeAccess()->wait_for_update(other, meta_marker.mutex());
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


active_mpm_t::active_mpm_t(active_service_t* parent_, system_time_t now)
	: mpm_t(false)
	, active_service(parent_)
	, creation_time (now)
	, stream_parser(&db->mpm_rec.idxdb)
{
	using namespace dtdemux;
	dirname = make_dirname(parent_, now);
	file_time_limit = active_service->receiver.options.readAccess()->livebuffer_mpm_part_duration;
	active_service->pat_parser = stream_parser.register_pat_pid();
	active_service->pat_parser->section_cb = [this](const pat_services_t& pat_services, const subtable_info_t& i) {
		assert(!i.timedout);
		for (const auto& e : pat_services.entries) {
			if (e.service_id == active_service->current_service.k.service_id) {
				active_service->have_pat = true;
				dtdebugf("PAT START PMT=0x{:x}", e.pmt_pid);
				active_service->update_pmt_pid(e.pmt_pid);
				active_service->pmt_parser = stream_parser.register_pmt_pid(e.pmt_pid, e.service_id);
				active_service->pmt_parser->section_cb =
					[this](pmt_parser_t* parser, const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data) {
						if(pmt.service_id == active_service->current_service.k.service_id) {
							//on 30.0W 12398, multiple services share the same pmt_pid. We need the correct one
							active_service->update_pmt(pmt, isnext, sec_data);
						}
					return dtdemux::reset_type_t::NO_RESET;
				};
				break;
			}
		}
		return dtdemux::reset_type_t::NO_RESET;
	};
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
	if (next_data_file(creation_time, 0) < 0)
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

void active_mpm_t::transfer_filemap(int fd, int64_t new_num_bytes_safe_to_read) {
	mmap_t newfilemap(filemap.map_len, false);
	// fd will be owned by filemap
	newfilemap.init(fd, 0);
	// num_bytes_processed number of bytes proccessed in the current filemap
	auto num_bytes_processed =
		new_num_bytes_safe_to_read - current_file_stream_packetno_start * (int64_t)ts_packet_t::size - filemap.offset;
	assert(num_bytes_processed <= filemap.decrypt_pointer);
	assert(filemap.decrypt_pointer <= filemap.write_pointer);
	if (num_bytes_processed < filemap.write_pointer) {
		auto* start = filemap.buffer + num_bytes_processed;
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
		dtdebugf("TRUNCATE from ={:d} to {:d} new_num_bytes_safe_to_read={:d}", filesize_fd(filemap.fd),
						 num_bytes_processed + filemap.offset, new_num_bytes_safe_to_read);
		// assert(new_num_bytes_safe_to_read<= num_bytes_processed +filemap.offset);
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

static inline int overlap_duration(int a1, int a2, int b1, int b2) {
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
#ifndef NDEBUG
				testf(idx_txn, f);
#endif
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
// copy the stream_descriptor markers into the recording database
		auto c = recdb::stream_descriptor_t::find_by_key
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
		auto ret = next_data_file(now, -1);
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

void active_mpm_t::self_check(meta_marker_t& m) {
	assert(m.num_bytes_safe_to_read >= stream_parser.event_handler.last_saved_marker.packetno_end * ts_packet_t::size);
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
#ifndef NDEBUG
				testf(idx_txn, last_file);
#endif
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
int active_mpm_t::next_data_file(system_time_t now, int64_t new_num_bytes_safe_to_read) {
	auto idx_txn = db->mpm_rec.idxdb.wtxn();
	using namespace recdb;
	auto cfile = db->mpm_rec.idxdb.tcursor<file_t>(idx_txn);
	current_file_time_start = now;
	auto mm = meta_marker.writeAccess();
	if (new_num_bytes_safe_to_read < 0)
		new_num_bytes_safe_to_read = mm->num_bytes_safe_to_read;
	mm->current_marker = stream_parser.event_handler.last_saved_marker;
	auto stream_time_end = mm->current_marker.k.time; // could be 0
	assert(mm->current_marker.packetno_end * ts_packet_t::size <= new_num_bytes_safe_to_read);
	assert(new_num_bytes_safe_to_read >= mm->num_bytes_safe_to_read);
	assert(new_num_bytes_safe_to_read % ts_packet_t::size == 0);
	auto end_packet = new_num_bytes_safe_to_read / ts_packet_t::size;
	if (current_fileno != -1) {
		// first finalise last file record if there is one
		// stream_time_end may be slightly off because bytes may have been received after last pcr
		auto tmp = mm->current_file_record; // make a copy because of possible concurrent access
		tmp.stream_time_end = stream_time_end;
		tmp.real_time_end = system_clock_t::to_time_t(now);

		tmp.stream_packetno_end = end_packet;
#ifndef NDEBUG
		testf(idx_txn, tmp);
#endif
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
	transfer_filemap(fd, new_num_bytes_safe_to_read);
	mm->num_bytes_safe_to_read = new_num_bytes_safe_to_read;
	// mm->current_marker = 	stream_parser.event_handler.last_saved_marker;
	mm->current_file_record.k.stream_time_start = new_file_stream_time_start;
	mm->current_file_record.stream_time_end = std::numeric_limits<milliseconds_t>::max(); // signifies infinite
	mm->current_file_record.real_time_end = std::numeric_limits<time_t>::max(); // signifies infinite
	mm->current_file_record.stream_packetno_start = new_file_stream_packetno_start;
	mm->current_file_record.stream_packetno_end = std::numeric_limits<int64_t>::max(); // signifies infinite
	mm->current_file_record.filename = relfilename;

	self_check(*mm);
#if 0
	//the following is incorrect; the old value should be preserved
	mm->num_bytes_safe_to_read =  mm->current_file_record.stream_packetno_start*
		ts_packet_t::size;
#endif
	self_check(*mm);
#ifndef NDEBUG
				testf(idx_txn, mm->current_file_record);
#endif

	put_record(cfile, mm->current_file_record);
	idx_txn.commit();

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

playback_info_t active_mpm_t::get_current_program_info() const {
	playback_info_t ret;
	if(active_service)
		ret.service = active_service->current_service;

	auto mm = this->meta_marker.readAccess();
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
															 system_clock_t::to_time_t(this->creation_time), active_service->get_adapter_no(),
															 -1, active_service->get_current_service(), p/*, epg*/);
}
