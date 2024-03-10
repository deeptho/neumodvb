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
#include "active_playback.h"
#include "active_service.h"
#include "mpm.h"
#include "receiver.h"
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
#include "util/dtassert.h"

playback_mpm_t::playback_mpm_t(receiver_t& receiver_, subscription_id_t subscription_id_)
	: mpm_t(true)
	, receiver(receiver_)
	, subscription_id(subscription_id_) {
	error = true;
};

playback_mpm_t::playback_mpm_t(active_mpm_t& other,
															 const chdb::service_t& live_service,
															 const recdb::stream_descriptor_t& streamdesc,
															 subscription_id_t subscription_id_)
	: mpm_t(other, true)
	, receiver(other.active_service->receiver)
	, live_mpm(&other)
	, subscription_id(subscription_id_) {
	assert(filemap.readonly);
	auto ls = stream_state.writeAccess();
	ls->current_streams = streamdesc;
	ls->next_streams = {};
	ls->audio_pref = live_service.audio_pref;
	ls->subtitle_pref = live_service.subtitle_pref;
}

/*
	Find the pmt which is active when processing the byte at address bytepos
	and the the next one. The next one will have packno_start==-1 if the stream is live
	and a very large value if there is no pending pmt change
 */
void playback_mpm_t::find_current_pmts(int64_t bytepos)
{
	constexpr auto never = std::numeric_limits<int>::max();
	auto curp = bytepos / ts_packet_t::size;
	auto rtxn = db->mpm_rec.idxdb.rtxn();
	auto c = recdb::stream_descriptor_t::find_by_key(rtxn, curp, find_leq);
	auto ls = stream_state.writeAccess();
	if (!c.is_valid()) {
		c =  recdb::find_first<recdb::stream_descriptor_t>(rtxn);
	}
	if(c.is_valid()) {
		ls->current_streams = c.current();
		c.next();
		ls->next_streams = c.is_valid() ? c.current() : recdb::stream_descriptor_t();
		if (ls->next_streams.packetno_start >= 0)
			next_stream_change_ = ls->next_streams.packetno_start * ts_packet_t::size;
		else if (live_mpm)
			next_stream_change_ = -1; //live_mpm
		else
			next_stream_change_ = never;
	} else {
		//assert(false);
		return;
	}
	update_pmt(*ls); //trigger sending of new pmt, which is now present in current_streams
}

void playback_mpm_t::open_recording(const char* dirname_) {
	db.reset();
	filemap.init();
	/*@todo: replace this by removing the init calls altogether
		init is only called when playing a recording
		but this can be replaced by creating a new playback_mpm_t record
	*/
	const_cast<ss::string<128>&>(dirname) = dirname_;
	error = false;
	current_filename.clear();

	assert(filemap.readonly);
	live_mpm = nullptr;
	db = std::make_shared<mpm_index_t>();
	db->idx_dirname.format("{}/index.mdb", dirname);
	try {
		db->open_index();
	} catch(const db_upgrade_info_t & upgrade_info) {
		auto r = receiver.options.readAccess();
		dtdebugf("Need upgrade from {}", r->upgrade_dir.c_str());
		assert(0);
	}

	current_byte_pos = 0;
	recdb::file_t empty{};
	currently_playing_file.assign(empty);
	auto txn = db->mpm_rec.recdb.rtxn();
	using namespace recdb;
	auto c = find_first<recdb::rec_t>(txn);
	if (c.is_valid()) {
		currently_playing_recording = c.current();
		{
			auto ls = stream_state.writeAccess();
			ls->audio_pref = currently_playing_recording.service.audio_pref;
			ls->subtitle_pref = currently_playing_recording.service.subtitle_pref;
		}
		assert(current_byte_pos == 0);
		find_current_pmts(current_byte_pos);
	} else {
		dterrorf("Cannot find rec in {}", db->idx_dirname);
	}
	txn.abort();
}

/*! open the first available file with fileno>= the provided argument.
	In case of concurrent deletes, the returned fileno may be larger than requested
	Returns 1 on succes, 0 on failure but with current mapping intact, -1 on failure but with
	current mapping unmaped
*/

void playback_mpm_t::close() {
	if (live_mpm)
		live_mpm->meta_marker.writeAccess()->unregister_playback_client(this);
	if (filemap.buffer) {
		filemap.unmap();
		filemap.close();
	}
	db.reset();
}


/*
	returns the byte pos at which next pmt change will occur

 */
int playback_mpm_t::next_stream_change() { //byte at which new pmt becomes active (coincides with end of old pmt)
	constexpr auto never = std::numeric_limits<int>::max();
	if (next_stream_change_ >= 0 &&  current_byte_pos < next_stream_change_)
		return next_stream_change_ - current_byte_pos; /* fast path: return cached version.
																 */
	//we should never have read past next_stream_change_
	assert(next_stream_change_ < 0 || current_byte_pos == next_stream_change_);

	/* The cache next_stream_change_ can contain:
		  -some future byte
			- -1, meaning that the cache is invalid
			- never, meaning that this is not a live playback and we are sure no more pmt changes will happen
	*/

  /*consult live_mpm to see if update is pending. this info is set when calling live_mpm->wait_for_update
		and only refers to the range last_seen_live_meta_marker.num_bytes_safe_to_read

		The goal is to avoid consulting the database if no update is pending for sure
   */
	if(live_mpm) {
		if (last_seen_live_meta_marker.last_streams.packetno_start <0) {
			//wait for initial pmt
			live_mpm->wait_for_update(last_seen_live_meta_marker);
			assert(must_exit || last_seen_live_meta_marker.last_streams.packetno_start >=0);
			return never;
		}

		auto ls = stream_state.readAccess();
		assert(last_seen_live_meta_marker.last_streams.packetno_start >= ls->current_streams.packetno_start);
		if (last_seen_live_meta_marker.last_streams.packetno_start == ls->current_streams.packetno_start) {
			//no update pending for sure
			//dtdebugf("returning next pmt change never (live)");
			return never; //no update pending for sure
		/* otherwise: we cannot rely on last_seen_live_meta_marker.last_stream.packetno_start because
			 there may have been multiple pmt updates (unlikely); so we need to consult the database
		*/

		}
	}
	find_current_pmts(current_byte_pos);
	if (next_stream_change_ == -1)
		return never;

	return next_stream_change_ - current_byte_pos;
}

/*
	After a new pmt has been activated, activate it so that it will be sent in the output
	stream before sending any other stream data.
 */
void playback_mpm_t::update_pmt(stream_state_t& ss) {
	if(num_pmt_bytes_to_send >0)
		return; /* we cannot handle a pmt change when one is still in progress.
								 Return 0 indicates that we first need to clear the pmt buffer
							*/
	//pmt with all audio streams
	current_pmt = parse_pmt_section(ss.current_streams.pmt_section, ss.current_streams.pmt_pid);
	preferred_streams_pmt_ts.clear();

	std::tie(ss.current_audio_language, ss.current_subtitle_language ) =
		current_pmt.make_preferred_pmt_ts(preferred_streams_pmt_ts, ss.audio_pref, ss.subtitle_pref);
	assert(current_pmt.pmt_pid ==  ss.current_streams.pmt_pid);

	//activate the pmt for ouput
	num_pmt_bytes_to_send = preferred_streams_pmt_ts.size();
	/*
		todo:  make pat
	 */
}


int playback_mpm_t::set_language_pref(int idx, bool for_subtitles) {
	auto ls = stream_state.writeAccess();
	auto langs = for_subtitles ? ls->current_streams.subtitle_langs : ls->current_streams.audio_langs;
	if (idx < 0 || idx >= langs.size()) {
		dterrorf("set_language: index {:d} out of range", idx);
		return -1;
	}

	chdb::language_code_t selected_lan = langs[idx];

	auto update = [&selected_lan, for_subtitles](chdb::service_t& service) {
		auto& prefs = for_subtitles ? service.subtitle_pref : service.audio_pref;
		if (prefs.size() < 4)
			prefs.resize_no_init(prefs.size() + 1);
		if (prefs[0] == selected_lan)
			return;
		rotate(prefs, -1);

		prefs[0] = selected_lan;
	};

	if (live_mpm) {
		auto &active_service = *live_mpm->active_service;
		chdb::service_t service = active_service.get_current_service();
		update(service);
		/*This needs to be run from tuner thread to avoid excessive blocking
		 */
		auto& tuner_thread = active_service.active_adapter().tuner_thread;
		tuner_thread.push_task([this, service, for_subtitles]() // service passed by value!
			{
				auto txn = receiver.chdb.wtxn();
				/*
					handle the case where some other thread has updated other things
					like the service name
				*/
				if (for_subtitles)
					chdb::service::update_subtitle_pref(txn, service);
				else
					chdb::service::update_audio_pref(txn, service);
				txn.commit();
				return 0;
			});
	} else {
		update(currently_playing_recording.service);
		auto txn = db->mpm_rec.recdb.wtxn();
		put_record(txn, currently_playing_recording);
	}

	if (for_subtitles)
		ls->current_subtitle_language = selected_lan;
	else
		ls->current_audio_language = selected_lan;
	return 1;
}

playback_info_t playback_mpm_t::get_recording_program_info() const {
	playback_info_t ret;
	ret.service = currently_playing_recording.service;
	ret.start_time = system_clock_t::from_time_t(currently_playing_recording.real_time_start);
	ret.end_time = system_clock_t::from_time_t(currently_playing_recording.real_time_end);
	ret.play_time = ret.start_time;
	ret.is_recording = !live_mpm;
	ret.is_timeshifted = false;
	return ret;
}

playback_info_t playback_mpm_t::get_current_program_info() const {
	auto ret = live_mpm ? live_mpm->active_service->get_current_program_info() : get_recording_program_info();
	if(live_mpm)
		ret.is_timeshifted = is_timeshifted;

	{
		auto ls = stream_state.readAccess();
		ret.audio_language = ls->current_audio_language;
		ret.subtitle_language = ls->current_subtitle_language;
	}
	{
		auto delta = int64_t(get_current_play_time()) / 1000;
		auto p = ret.start_time + std::chrono::duration<int64_t>(delta);
		ret.play_time = p;
	}
	{
		auto txnrec = this->db->mpm_rec.recdb.rtxn();
		auto c = recdb::find_first<recdb::rec_t>(txnrec);
		if(c.is_valid()) {
			auto rec = c.current();
			ret.epg = rec.epg;
		}
		txnrec.abort();
	}
	return ret;
}

void playback_mpm_t::force_abort() {
	if (live_mpm) {
		dtdebugf("FORCE abort playback_mpm={:p} live_mpm={:s}", fmt::ptr(this),
						 live_mpm->active_service->get_current_service().name);
		must_exit = true;
		live_mpm->meta_marker.writeAccess()->interrupt();
	}
}

/*
	Restart playback at a random time instance.
	Open new files and change the mapped part of the file as needed
*/
int playback_mpm_t::move_to_time(milliseconds_t start_play_time) {
	if(live_mpm)
		is_timeshifted = true;
	error = false;
	clear_stream_state();
	if (start_play_time < milliseconds_t(0))
		start_play_time = milliseconds_t(0);
	dtdebugf("Starting move_to_time");
	auto idxdb_txn = db->mpm_rec.idxdb.rtxn();
	auto ret = open_(idxdb_txn, start_play_time);
	idxdb_txn.abort();
	return ret;
}

int playback_mpm_t::move_to_live() {
	assert(live_mpm);
	milliseconds_t start_play_time{0};
	auto ret = move_to_time(start_play_time);
	is_timeshifted = false;
	return ret;
}

/*
	opens part fileno of a recording and close the existing one.
	returns -1 on error; 0 if the file map was unchanged or 1 if it was changed
	fileno=-1 will be ignored; otherwise fileno is the fileno which should be opened
*/

int playback_mpm_t::open_(db_txn& idxdb_txn, milliseconds_t start_time) {
	error = false;
	bool current_file_ok = false;
	{
		auto f = currently_playing_file.readAccess();
		current_file_ok = (filemap.fd >= 0 && start_time >= f->k.stream_time_start && start_time < f->stream_time_end &&
											 f->stream_time_end != std::numeric_limits<milliseconds_t>::max());
		if (current_file_ok) {
			dtdebugf("Opening file which is already open file={:d}", f->fileno);
		}
	}
	int fd = filemap.fd;
	if (!current_file_ok) {
		// The desired data is not in the current file. Open the correct one and close our filemap
		fd = open_file_containing_time(idxdb_txn, start_time);
		if (fd < 0) { /* error*/
			filemap.unmap();
			filemap.close();
			receiver.global_subscriber->notify_error(get_error()); //TODO: should be replaced by a "subscriber_notify+error"
			return -1;
		}
	}

	/*the following is needed because we may have moved forward several file parts (e.g., when old file parts have
		been deleted)*/
	recdb::marker_t end_marker;

	/*
		Now that we have the correct file opened, we need to
		find the correct byte position in the file at which to start playback
		This will be stored in current_marker

		We also need to know where the stream (= all files) ends, according to the database.
		This will be stored in end_marker

		In case the opened file is still live, end_marker
	*/
	recdb::marker_t current_marker;
	get_end_marker_from_db(idxdb_txn, end_marker);
	if (start_time >= end_marker.k.time || get_marker_for_time_from_db(idxdb_txn, current_marker, start_time) < 0) {
		dtdebugf("Requested start_play_time is beyond last logged packet");
		if (live_mpm) {
			auto mm = live_mpm->meta_marker.readAccess();
			if (start_time >= mm->current_marker.k.time) {
				is_timeshifted = false; //handles the case where a user jumps forward past current time
				current_marker = mm->current_marker;
				start_time = mm->current_marker.k.time;
			}
		} else {
			start_time = end_marker.k.time;
			current_byte_pos = end_marker.packetno_end;
			must_exit = true;
			return 0;
		}
	}

	int64_t stream_packetno_start{0};
	int64_t stream_packetno_end{0}; // either the last packet in the file or infinity if still is still growing
	{
		auto f = currently_playing_file.writeAccess();
		stream_packetno_start = f->stream_packetno_start;
		stream_packetno_end = f->stream_packetno_end;
		if (stream_packetno_end == std::numeric_limits<int64_t>::max()) {
			if (!live_mpm) {
				dtdebugf("file {} was not properly closed", f->filename);
				stream_packetno_end = end_marker.packetno_end;
				f->stream_packetno_end = stream_packetno_end;
			}
		}
	}

	/*-In case of deleted files, current_marker may be outside the found file, then seek to the start of that file
		-when we seek to time 0, we wish to start at byte 0, rather than at the first byte of the first marker

		In case we are dealing with a live buffer, older file parts may have been deleted.
		In this case use_file_start is set to true;
	*/
	bool use_file_start = (current_marker.packetno_start < stream_packetno_start);
	use_file_start |= (start_time == milliseconds_t(0));

	/*end_packet is the the last readable packet in the current file.
		If the current file is no longer growing,
		stream_packetno_end (less than infinity) points to this last packet
		Otherwise  end_marker.packetno_end (the end of the last file) is what
		we want.

		last_packet_in_current_file is that last packet of the current file if the current
		file is no longer live/growing. Otherwie it is the last packet in the currently growing
		file which has already been logged in the database
	*/
	auto last_packet_in_current_file = std::max(
		stream_packetno_start,
		(stream_packetno_end == std::numeric_limits<int64_t>::max() ? end_marker.packetno_end : stream_packetno_end));

	// packet at which playback will start
	auto start_packet = use_file_start ? stream_packetno_start : current_marker.packetno_start;
	auto start_offset = start_packet - stream_packetno_start;
	assert(start_offset >= 0);
	assert(start_offset <= last_packet_in_current_file - stream_packetno_start ||
				 start_offset <= current_marker.packetno_end);
	assert(start_time != milliseconds_t(0) || start_offset == 0);
	/*
		The file is already open (fd>=0). We now map data from the current file
		starting at start_packet and ending at the end of the file,
		filemap.init can actually map a larger range, which can contain invalid data at the end
		of the map
	*/
	filemap.init(fd, start_offset * dtdemux::ts_packet_t::size,
							 (last_packet_in_current_file - stream_packetno_start) * dtdemux::ts_packet_t::size);

	// stream must continue where it ended, except if this is a deliberate jump indicated
	// by setting bytepos to zero

	auto start_byte_pos = start_packet * dtdemux::ts_packet_t::size;
	if (!(current_byte_pos == start_byte_pos || current_byte_pos == 0)) {
		dtdebugf("current_byte_pos={:d} start_byte_pos={:d}", current_byte_pos, start_byte_pos);
	}
	current_byte_pos = start_byte_pos;

	return 1;
}

/*
	find the file part containing start_time, using database lookup only

*/
int playback_mpm_t::open_file_containing_time(db_txn& idxdb_txn, milliseconds_t start_time) {
	error = false;
	// find a file which starts at start_time, or if none exists, which contains start_time
	using namespace recdb;
	auto c = file_t::find_by_key(idxdb_txn, file_key_t(start_time), find_leq); // largest start_time smaller than the desired
																																			 // one
	if (!c.is_valid()) {
		// this can only happen if file is corrupt
		user_errorf("Could not find file corresponding to time {}", milliseconds_t(start_time));
		return -1;
	}

	int fd = -1;

	/*
		loop over records such that r.start_time <= start_time < nextr.start_time
	*/
	for (const auto& r : c.range()) {
		if (start_time >= r.stream_time_end)
			break; // desired time is beyond end of r;

		if (start_time < r.k.stream_time_start) {
			/* start_time is older than what is available in any of the file parts of this
				 recording. This can happen with live buffers which have been running such a ling
				 time that older file parts have already been removed to get rid of old data
				 In this case we jump forward to the next available file
			*/
			start_time = milliseconds_t(r.k.stream_time_start);
		}

		if (currently_playing_file.readAccess()->k == r.k && filemap.fd >= 0) {
			dtdebugf("This file fileno={:d} is already open", filemap.fd);
			fd = filemap.fd;
		} else {
			// current_filename  contains a relative path
			current_filename.clear();
			current_filename.format("{:s}/{:s}", dirname, r.filename);
		}
		// open the file, setting fd>=0 on success, otherwise -1
		for (; fd < 0;) {
			dtdebugf("Opening {}", current_filename);
			fd = ::open(current_filename.c_str(), O_RDONLY);
			if (fd < 0) {
				if (errno == EINTR)
					continue; // retry
				if (errno == ENOENT) {
					dtdebugf("File {} does not exist (may have been deleted; will try next one).", current_filename);
				} else {
					dtdebugf("Could not open data file {}: {}", current_filename, strerror(errno));
				}
			}
			break;
		}

		if (fd >= 0) {
			// store info about the currently playing file
			currently_playing_file.assign(r);
			dtdebugf("currently_playing_file.fileno={:d} fd={:d}", r.fileno, fd);
			break;
		}
	}
	if (fd < 0) {
		if (filemap.buffer) {
			dtdebugf("Could not open any data file; keeping current one");
			// user wanted to move backward foward/ beyond what is available; it is best to preserve what we have
			return filemap.fd;
		} else {
			dtdebugf("Could not open any data file; returning error");
			return fd;
		}
	}
	return fd;
}

/*
	opens part fileno of a recording;
	returns -1 n error; 0 if the file map was unchanged or 1 if it was changed
*/

int playback_mpm_t::open_next_file() {
	auto idxdb_txn = db->mpm_rec.idxdb.rtxn();
	// WRONG: assert(currently_playing_file.stream_time_end!= std::numeric_limits<milliseconds_t>::max());
	// WRONG, e.g., if paused for a long time:
	// assert(meta_marker.currently_playing_file.fileno == 1 + currently_playing_file.fileno);

	auto ret = open_(idxdb_txn, milliseconds_t(currently_playing_file.readAccess()->k.stream_time_start));
	idxdb_txn.abort();
	return ret;
}


/*
	read up to outbytes bytes in outbuffer, while not reading more than inbytes bytes from the input stream
	The call may return earlier if not enough data is available
	Returns number of inputs bytes consumed and number of output bytes written
	Returns -1 on error or if must_exot
 */
std::tuple<int, int> playback_mpm_t::read_data_(char* outbuffer, int outbytes, int inbytes) {
	if (error || inbytes == 0 || outbytes == 0)
		return {0, 0};

	int remaining_space = -1;
	uint8_t* buffer = nullptr;
	dttime_init();

	for (; !error;) {

		remaining_space = read_data_from_current_file(buffer);
		/* remaining_space<0 means errors
			 remaining_space == 0 means: no more data in current file part; for live_mpm this will only happend on exit
		 */
		if (must_exit)
			return {-1, -1};
		if (remaining_space >= ts_packet_t::size)
			break; // enough data which is known to be available to continue processing
		assert(!live_mpm || filemap.get_read_buffer(buffer) == 0);

		/*the current file has been fully played because map growing was tried, but remaining_space still 0.
			Also, the current file is not the last one because then we would have restarted or exited
			the loop.

			However our own view of the currently playing file may be out of date: last time we checked
			it, it may have been still growing
		*/

		/*check if file was properly closed and close it if not
			The only file which is perhaps not properly closed should be the very last one.

			This can only happen if we are playing back a recording that was aborted
			due to a crash; if live_mpm != nullptr, then the writer code must have solve the problem
			in parallel.

			It can also happen when live_mpm!=nullptr, because then the database will contain the correct
			information, but the current thread may not have the newest information
		*/

		auto end_time = currently_playing_file.readAccess()->stream_time_end;
		if ((int64_t)end_time == std::numeric_limits<int64_t>::max() && !live_mpm) {
			/*According to our information, this file is still growing, but this is incorrect.
				File was properly closed when recording ws created.
				The only file which is perhaps not properly closed should be the very last one
			*/
			recdb::marker_t end_marker;
			recdb::marker_t current_marker;
			auto wtxn = db->mpm_rec.idxdb.wtxn();
			get_end_marker_from_db(wtxn, end_marker);
			auto f = currently_playing_file.writeAccess();
			dtdebugf("file {} was not properly closed", f->filename.c_str());
			end_time = end_marker.k.time; //time of the very last info written into the file
			f->stream_time_end = end_time;
			f->stream_packetno_end = end_marker.packetno_end;
			wtxn.commit();
		}
		auto idxdb_txn = db->mpm_rec.idxdb.rtxn();
		if ((int64_t)end_time == std::numeric_limits<int64_t>::max() && live_mpm) {
				/* We must reread the database to find the correct end_time,  which will have been written
					 by the service thread performing the writing
				*/
				using namespace recdb;
				auto start_time = currently_playing_file.readAccess()->k.stream_time_start;
#ifndef NDEBUG
				auto fileno = currently_playing_file.readAccess()->fileno;
#endif
				auto c = file_t::find_by_key(idxdb_txn, file_key_t(start_time), find_eq);
				assert(c.is_valid());
				auto r = c.current();
				end_time = r.stream_time_end;
				dtdebugf("Reread end_time for current file: {}", end_time);
				assert(fileno == r.fileno); //fails with r.fileno=2, fileno=1, stary_time=33914
				assert(end_time != std::numeric_limits<milliseconds_t>::max());
				dtdebugf("currently_playing_file.fileno={:d}", r.fileno);
				currently_playing_file.assign(c.current());
			}

		bool still_not_open = (int64_t)end_time == std::numeric_limits<int64_t>::max();
		auto ret = still_not_open ? -1 : open_(idxdb_txn, end_time);
		idxdb_txn.abort();
		if(ret == 0 && live_mpm)
			continue;
		if (ret <= 0) { // end time of current file is start time of new one
			dtdebugf("Cannot open next part");
			filemap.unmap();
			filemap.close();
			return {-1, -1};
		}
		continue; // more data should be available; so retry accessing it
	}
	dttime(200);

	if (error) {
		assert(0);
		return {-1, -1};
	}
	inbytes = std::min(inbytes, remaining_space);
	auto [num_bytes_out, num_bytes_in] = copy_filtered_packets(outbuffer, buffer, outbytes, inbytes);
	dttime(100);
	filemap.advance_read_pointer(num_bytes_in);
	dttime(100);
	current_byte_pos += num_bytes_in;
	return {num_bytes_out, num_bytes_in};
}


/*
	read up to num_bytes data in output buffer.
	Returns -1 on error
	Returns 0 only at end of stream

 */
int64_t playback_mpm_t::read_data(char* outbuffer, uint64_t num_bytes) {
	if (error || num_bytes == 0)
		return 0;
	int num_bytes_read{0};
	while(num_bytes_read == 0 && !must_exit && !error) {
    /*below, read_data_live_ and read_data_nonlive_ can read 0 bytes
			for two reasons: 1) due to pmt filtering, no real data may be available yet
			2) or real data may still be available, but the reading code has reached the point
			where pmt data should be sent.

			The loop is therefore needed, to retry the read.
		 */

		auto max_bytes = next_stream_change(); /* max_bytes byte at which is the number of bytes to read before
																							new pmt becomes active (coincides with end of old pmt);
																							we may never read past that point without calling next_stream_change
																							again
																					 */
		if(must_exit)
			return 0;
		if(num_pmt_bytes_to_send > 0) {
			auto num_pmt_bytes_sent  = read_pmt_data(outbuffer + num_bytes_read, num_bytes);
			num_bytes -= num_pmt_bytes_sent;
			num_bytes_read += num_pmt_bytes_sent;

			if(num_bytes == 0) {
				/*the pmt might be fully sent, but usually this indicates
					that not enough space was available for the pmt in the output buffer
					We return what we have written and will be called later again, at which point
					read_pmt_data will be called again (and possibly write 0 bytes into output buffer)
				*/
				return num_bytes_read;
			}
		}

		assert(max_bytes >=0);
		auto [num_bytes_out, num_bytes_in] = read_data_(outbuffer + num_bytes_read, num_bytes, max_bytes);
		if (num_bytes_out >= 0) { //if there is no error
			num_bytes_read += num_bytes_out;
			num_bytes -= num_bytes_out;
		} else {
			break;
		}
		/*in rare cases, our caller may ask for less than 188 bytes. We will never be able to provide this,
			because we always return multiples of 188 bytes. We cannot return 0 bytes, because that will be
			interpreted as end of stream by mpv code.
			As a workaround we send pmt bytes, which can be done in terms of a partial packets
		*/
		if (num_bytes_read ==0 && num_bytes < ts_packet_t::size) {
			dtdebugf("Returning pmt data to fill partial packet");
			auto ls = stream_state.readAccess();
			num_pmt_bytes_to_send =  preferred_streams_pmt_ts.size();
			assert(num_pmt_bytes_to_send >= 0);
			auto ret  = read_pmt_data(outbuffer + num_bytes_read, num_bytes);
			num_bytes_read += ret;
			num_bytes -= ret;
		}
		assert(live_mpm || num_bytes_read != 0); /*live_mpm will lead to blocking (ok), but otherwise we have a problem;
																							 num_bytes_read < 0 is ok; indicates and error
																							 num_bytes_read > 0 is also ok; indicates progress
																						 */
	}
	return (must_exit || error) ? -1 : num_bytes_read;
}

/*
	Wait until live data becomes available.
	Returns remaining_space>0 if new data was found
	Returns as soon as at least 1 packet is available for reading from the input stream
	Returns -1 on error or on must_exit
	Returns 0 if no data is found and no more data exists in the current mpm part; there could be more mpm parts

 */
int64_t playback_mpm_t::read_data_from_current_file(uint8_t*& buffer) {

	if (error)
		return -1;

	int remaining_space{0};
	dttime_init();
	for(; !error; ) {
		remaining_space = filemap.get_read_buffer(buffer);
		if(!live_mpm)
			break;
		if (remaining_space >= ts_packet_t::size) {
			break; // keep streaming data which is known to be available
		}
		/*consult the live streamer, which will either tell us how much we can move the
			cursor in the current file part, or that a new file part was started;
			meta_marker will be overwritten.
			This will return immediately, except if waiting for data is needed

			last_seen_live_meta_marker will be updated with:
			current_marker, curret_file_record (last file in the live buffer) and num_bytes_safe_to_read
		*/
		live_mpm->wait_for_update(last_seen_live_meta_marker);
		if (must_exit)
			return -1;

		auto last_fileno = last_seen_live_meta_marker.current_file_record.fileno;
		if (current_fileno() == (int)last_fileno) {
			/*
				This way of computing new_end_pos ensures that we also get data which has not yet been
				analyzed. This ensures mimimal latency in mpv
			*/
			auto new_end_pos =
				last_seen_live_meta_marker.num_bytes_safe_to_read -
				last_seen_live_meta_marker.current_file_record.stream_packetno_start * dtdemux::ts_packet_t::size;
			assert(new_end_pos != std::numeric_limits<int64_t>::max());
			assert(last_seen_live_meta_marker.current_marker.packetno_end <=
						 last_seen_live_meta_marker.num_bytes_safe_to_read / ts_packet_t::size);

			if (filemap.grow_map(new_end_pos) >= 0) {
				remaining_space = filemap.get_read_buffer(buffer);
				assert(remaining_space % ts_packet_t::size ==0);
				dttime(300);
				if (remaining_space >= ts_packet_t::size) {
					break; // success; we have data
				}
			}
			auto current_file_is_still_live =
				last_seen_live_meta_marker.current_file_record.stream_packetno_end == std::numeric_limits<int64_t>::max();
			if (current_file_is_still_live)
				continue; // We need to wait for data
			else
				break;
		} else
			return 0;
	}
	if(error)
		return -1;
	return remaining_space;
}



milliseconds_t playback_mpm_t::get_current_play_time() const {
	auto txn = db->mpm_rec.idxdb.rtxn();
	{
		auto c = recdb::marker_t::find_by_packetno(txn, current_byte_pos / ts_packet_t::size, find_leq);
		if (!c.is_valid())
			return milliseconds_t(0);
		auto m = c.current();
		return m.k.time;
	}
}


void playback_mpm_t::register_audio_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb) {
	assert((int) subscription_id >= 0);
	assert(cb != nullptr);
	auto ls = stream_state.writeAccess();
	dtdebugf("Register audio_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->audio_language_change_callbacks.size());
	ls->audio_language_change_callbacks[subscription_id] = cb;
}

void playback_mpm_t::unregister_audio_changed_callback(subscription_id_t subscription_id) {
	assert((int) subscription_id >= 0);
	auto ls = stream_state.writeAccess();
	dtdebugf("Unregister audio_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->audio_language_change_callbacks.size());
	ls->audio_language_change_callbacks.erase(subscription_id);
}

void playback_mpm_t::register_subtitle_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb) {
	assert((int) subscription_id >= 0);
	assert(cb != nullptr);
	auto ls = stream_state.writeAccess();
	dtdebugf("Register subtitle_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->subtitle_language_change_callbacks.size());

	ls->subtitle_language_change_callbacks[subscription_id] = cb;
}

void playback_mpm_t::unregister_subtitle_changed_callback(subscription_id_t subscription_id) {
	assert((int) subscription_id >= 0);
	auto ls = stream_state.writeAccess();
	dtdebugf("Unregister subtitle_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->subtitle_language_change_callbacks.size());
	ls->subtitle_language_change_callbacks.erase(subscription_id);
}

chdb::language_code_t playback_mpm_t::get_current_audio_language() {
	auto ls = stream_state.readAccess();
	return ls->current_audio_language;
}

chdb::language_code_t playback_mpm_t::get_current_subtitle_language() {
	auto ls = stream_state.readAccess();
	return ls->current_subtitle_language;
}

ss::vector_<chdb::language_code_t> playback_mpm_t::audio_languages() {
	auto ls = stream_state.readAccess();
	return ls->current_streams.audio_langs;
}

ss::vector_<chdb::language_code_t> playback_mpm_t::subtitle_languages() {
	auto ls = stream_state.readAccess();
	return ls->current_streams.subtitle_langs;
}

active_service_t* playback_mpm_t::active_service() const {
	return live_mpm ? live_mpm->active_service : nullptr;
}

/*
	first and last record from database (for non live).
	This data is assumed to remain constant
*/
int playback_mpm_t::get_end_marker_from_db(db_txn& txn, recdb::marker_t& end_marker) {
	using namespace recdb;
	auto c = find_last<recdb::marker_t>(txn);
	if (!c.is_valid()) {
		dterrorf("Could not obtain last marker");
		return -1;
	}
	end_marker = c.current();
	return 0;
}

int playback_mpm_t::get_marker_for_time_from_db(db_txn& idxdb_txn, recdb::marker_t& current_marker,
																								milliseconds_t start_play_time) {

	auto c = recdb::marker_t::find_by_key(idxdb_txn, recdb::marker_key_t(start_play_time), find_geq);
	if (!c.is_valid()) {
		dtdebugf("Could not obtain marker for time {}", start_play_time);
		return -1;
	}
	current_marker = c.current();

	return 0;
}

int64_t  playback_mpm_t::read_pmt_data(char* outbuffer, uint64_t num_bytes) {
	auto ls = stream_state.readAccess();
	if(num_pmt_bytes_to_send < 0 ) {
		//initialisation
		num_pmt_bytes_to_send = preferred_streams_pmt_ts.size();
	}

	if(num_pmt_bytes_to_send > 0) {
		auto n = std::min(num_bytes, (uint64_t)num_pmt_bytes_to_send);
		assert(n > 0);
		memcpy(outbuffer,
					 preferred_streams_pmt_ts.buffer()
					 + (preferred_streams_pmt_ts.size() - num_pmt_bytes_to_send), n);
		num_pmt_bytes_to_send -= n;
		return n;
	}
	return 0;
}

/*
	copy full packets to the mpv buffer, discarding pmt packets
	inbytes = number of bytes that are available and allowed to read in inbuffer
	outbytes = number of bytes available in outbuffer
	Returns number of bytes placed in outbuffer,  and number of bytes read from inbuffer
	Both can be smaller than min(inbytes, outbytes) if last input packet
	is not yet complete and/or because some input packets are discarded
	The number of packets read/written can also equal zero
 */
std::tuple<int,int> playback_mpm_t::copy_filtered_packets(char* outbuffer, uint8_t* inbuffer, int outbytes, int inbytes)
{
	inbytes -= inbytes % ts_packet_t::size;
	outbytes -= outbytes % ts_packet_t::size;
	auto ls = stream_state.readAccess();

	if(inbytes<=0)
		return {0, 0};
	auto *inptr = inbuffer;
	auto *inptr_end = inptr + inbytes;
	auto *outptr = outbuffer;
	auto *outptr_end = outptr + outbytes;
	int num_read{0};

	for (; inptr < inptr_end && outptr < outptr_end; inptr +=  dtdemux::ts_packet_t::size) {
		int pid = (((uint16_t)(inptr[1] & 0x1f)) << 8) | inptr[2];
		if (pid == current_pmt.pmt_pid || pid == 0 /*pat*/)
			continue;
		memcpy(outptr, inptr, dtdemux::ts_packet_t::size);
		outptr += dtdemux::ts_packet_t::size;
		num_read += dtdemux::ts_packet_t::size;
	}
	return {num_read, inptr - inbuffer};
}



/*
	Possible states:
	1) num_pmt_bytes_to_send>0: then return pmt bytes to reader until  num_pmt_bytes_to_send=0
	2) num_pmt_bytes_to_send==0 and  packetno < ls->next_streams.packetno_start:
     then we are sending filtered stream data: pmt packets are removed, other packets
		 are passed to reader
	3)) num_pmt_bytes_to_send==0 and  packetno == ls->next_streams.packetno_start:
     then we must call pmt call backs and we also must also go to state 1, with  num_pmt_bytes_to_send set
		 to the size of the newly active pmt


   Overall play state:
    current_streams: current pmt_data + byte at which this became active
    next_streams: next pmt_data + byte at which this becomes active. packetno_start = -1 means: there is no new pmt
		currently_playing_file: descriptor of mpm file wich is currently mapped
		current_byte_pos: position of next byte to read from mpm recording (live or non live)
		num_pmt_bytes_to_send: if larger than 0, number of pmt bytes still to send before reading from actual recorded stream
		current_pmt_pid: -> part of current_streams: pmt_pid used to send pmt data
		currently_playing_recording: overall information about recording (like epg data)



 */
