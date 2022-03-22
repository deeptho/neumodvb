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

#include "active_playback.h"
#include "active_service.h"
#include "date/date.h"
#include "date/iso_week.h"
#include "date/tz.h"
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
using namespace date;
using namespace date::clock_cast_detail;

/*
	returns a language code_t containing the index of the pmt language entry corresponding to pref,
	or an invalid language_code_t if the pmt does not contain pref
*/
static std::tuple<chdb::language_code_t, int>
language_preference_in_vector(const ss::vector<chdb::language_code_t, 4> lang_codes,
															const chdb::language_code_t& pref) {
	using namespace chdb;
	int idx = 0;	 // index to one of the audio languages in pmt
	int order = 0; /* for duplicate entries in pmt, order will be 0,1,2,...*/
	for (const auto& lang_code : lang_codes) {
		if (chdb::is_same_language(lang_code, pref)) {
			// order is the order of preference
			if (order == pref.position) // in pref, position 1 means the second language of this type
				return {lang_code, idx};
			order++;
		}
		idx++;
	}
	return {language_code_t(-1, 0, 0, 0), 0}; // invalid
}

/*
	returns the best subtitle language based on user preferences.
	Prefs is an array of languages 3 chars indicating the subtitle
	language; the fourth byte is used to distinghuish between duplicates;
*/
static std::tuple<chdb::language_code_t, int> best_language(const ss::vector<chdb::language_code_t> pmt_langs,
																														const ss::vector<chdb::language_code_t>& prefs) {
	using namespace chdb;
	for (const auto& p : prefs) {
		auto [ret, pos] = language_preference_in_vector(pmt_langs, p);
		if (ret.position >= 0)
			return {ret, pos};
	}

	// return the first language as the default:
	if (pmt_langs.size() > 0)
		return {pmt_langs[0], 0};
	return {language_code_t(0, 'e', 'n', 'g'), 0}; // dummy
}

playback_mpm_t::playback_mpm_t(receiver_t& receiver_, int subscription_id_)
	: mpm_t(true)
	, receiver(receiver_)
	, subscription_id(subscription_id_) {
	error = true;
};

playback_mpm_t::playback_mpm_t(active_mpm_t& other,
															 const chdb::service_t& live_service,
															 const recdb::stream_descriptor_t& streamdesc,
															 int subscription_id_)
	: mpm_t(other, true)
	, receiver(other.active_service->receiver)
	, live_mpm(&other)
	, is_live(true)
	, subscription_id(subscription_id_) {
	assert(filemap.readonly);
	auto ls = language_state.writeAccess();
	ls->current_streams = streamdesc;
	ls->audio_pref = live_service.audio_pref;
	ls->subtitle_pref = live_service.subtitle_pref;
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
	db->idx_dirname << dirname << "/"
									<< "index.mdb";
	db->open_index();
	current_byte_pos = 0;
	recdb::file_t empty{};
	currently_playing_file.assign(empty);
	auto txn = db->mpm_rec.recdb.rtxn();
	using namespace recdb;
	auto c = find_first<recdb::rec_t>(txn);
	if (c.is_valid()) {
		currently_playing_recording = c.current();
		auto ls = language_state.writeAccess();
		ls->audio_pref = currently_playing_recording.service.audio_pref;
		ls->subtitle_pref = currently_playing_recording.service.subtitle_pref;
		auto txni = db->mpm_rec.idxdb.rtxn();
		auto c1 = recdb::stream_descriptor_t::find_by_key(txni, ls->next_streams.packetno_start + 1, find_geq);
		if (c1.is_valid()) {
			dtdebug("found initial pmt");
			ls->current_streams = c1.current();
			c1.next();
			if (c1.is_valid()) {
				dtdebug("found pending pmt change");
				ls->next_streams = c1.current();
			} else {
				dterror("no pending pmt");
			}
		}
		txni.abort();
	} else {
		dterrorx("Cannot find rec in %s", db->idx_dirname.c_str());
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
	This function is thread safe
*/
void playback_mpm_t::call_language_callbacks(language_state_t& ls) {
	auto& desc = ls.current_streams;
	auto [lang_code, pos] = best_language(desc.audio_langs, ls.audio_pref);
	if (lang_code.position != ls.current_audio_language.position) {
		for (auto& [subscription_id, cb] : ls.audio_language_change_callbacks) {
			dtdebugx("Calling language_code changed callback");
			cb(lang_code, pos);
		}
		ls.current_audio_language = lang_code;
	}

	std::tie(lang_code, pos) = best_language(desc.subtitle_langs, ls.subtitle_pref);
	if (lang_code.position != ls.current_subtitle_language.position) {
		for (auto& [subscription_id, cb] : ls.subtitle_language_change_callbacks) {
			dtdebugx("Calling subtitle language_code changed callback");
			cb(lang_code, pos);
		}
		ls.current_subtitle_language = lang_code;
	}
}

void playback_mpm_t::check_pmt_change() {
	auto ls = language_state.writeAccess();
	bool apply_now = (ls->next_streams.packetno_start >=0 &&
										(current_byte_pos >= ts_packet_t::size * ls->next_streams.packetno_start));
	if (apply_now) {
		ls->current_streams = ls->next_streams;
		call_language_callbacks(*ls);
		// now look for the next pmt change (if any)
		auto txn = db->mpm_rec.idxdb.rtxn();
		auto c = recdb::stream_descriptor_t::find_by_key(txn, ls->next_streams.packetno_start + 1, find_geq);
		if (c.is_valid()) {
			dtdebug("found pending pmt change");
			ls->next_streams = c.current();
		} else {
			dtdebug("no pending pmt change");
			ls->next_streams =
				recdb::stream_descriptor_t(); // clear and mark as not present (stream_packetno_end set to large value)
		}
	}
}

/*
	The code below should only be run
	when we are running live or almost live, which can be detected by having reached the bytepos
	stored in current_stream_desc.stream_packetno_end


	1) when we start streaming, in live mode, we load next_stream_desc
	which is the last one received (retrieved from active_service) when in live mode
	or the first one loaded from a recording database
	We call the callbacks and then
	in live mode: set next_stream_desc=upper limit to indicate that we have called the callbacks
	for a recording: set next_stream_desc to the second pmt available (or upper limit if there is none)

	2) when a new pmt comes in via "on_pmt_change" in live mode, we update next_stream_desc,
	but only if  next_stream_desc == upper limit

	3) whenever we output a byte past next_stream_desc
	we call the callbacks from current_stream_desc, and set it to upper limit

	4) when we jump in timeshift mode, we retrieve the stream_desc before the jump point,
	call the cbs (if needed) and then load the next stream_desc.

*/

/*called when a live mpm detects a pmt change; returns new audio/subtitle descriptors
	This function is thread safe
*/
void playback_mpm_t::on_pmt_change(const recdb::stream_descriptor_t& desc) {
	auto ls = language_state.writeAccess();
	bool pending_stream_change = (ls->next_streams.packetno_start >=0);
	// call_language_callbacks(ls, desc);
	if (pending_stream_change) {
		dtdebug("older stream change not yet processed - skipping (viewing may fail)");
		return;
	} else {
		dtdebug("setting next_streams");
	}

	ls->next_streams = desc; // this must be the next stream change
}

int playback_mpm_t::set_language_pref(int idx, bool for_subtitles) {
	auto ls = language_state.writeAccess();
	auto langs = for_subtitles ? ls->current_streams.subtitle_langs : ls->current_streams.audio_langs;
	if (idx < 0 || idx >= langs.size()) {
		dterrorx("set_language: index %d out of range", idx);
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
		chdb::service_t service = live_mpm->active_service->get_current_service();
		update(service);
		/*This needs to be run from tuner thread to avoid excessive blocking
		 */
		receiver.tuner_thread.push_task([this, service, for_subtitles]() // service passed by value!
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

	return ret;
}

playback_info_t playback_mpm_t::get_current_program_info() const {
	auto ret = live_mpm ? live_mpm->active_service->get_current_program_info() : get_recording_program_info();
	ret.is_live = is_live;
	{
		auto ls = language_state.readAccess();
		ret.audio_language = ls->current_audio_language;
		ret.subtitle_language = ls->current_subtitle_language;
	}
	{
		auto delta = int64_t(get_current_play_time()) / 1000;
		auto p = ret.start_time + std::chrono::duration<int64_t>(delta);
		ret.play_time = p;
	}
	{
		auto txnrecepg = this->db->mpm_rec.recepgdb.rtxn();
		ret.epg = epgdb::running_now(txnrecepg, ret.service.k, ret.play_time);
		txnrecepg.abort();
	}
	return ret;
}

void playback_mpm_t::force_abort() {
	if (live_mpm) {
		dtdebugx("FORCE abort playback_mpm=%p live_mpm= %s", this,
						 live_mpm->active_service->get_current_service().name.c_str());
		must_exit = true;
		live_mpm->meta_marker.writeAccess()->interrupt();
	}
}

/*
	Restart playback at a random time instance.
	Open new files and change the mapped part of the file as needed
*/
int playback_mpm_t::move_to_time(milliseconds_t start_play_time) {

	error = false;
	if (start_play_time < milliseconds_t(0))
		start_play_time = milliseconds_t(0);
	dtdebug("Starting move_to_time");
	auto txn = db->mpm_rec.idxdb.rtxn();
	auto ret = open_(txn, start_play_time);
	txn.abort();
	return ret;
}

/*
	opens part fileno of a recording and close the existing one.
	returns -1 n error; 0 if the file map was unchanged or 1 if it was changed
	fileno=-1 will be ignored; otherwise fileno is the fileno which should be opened
*/

int playback_mpm_t::open_(db_txn& txn, milliseconds_t start_time) {
	error = false;
	bool current_file_ok = false;
	{
		auto f = currently_playing_file.readAccess();
		current_file_ok = (filemap.fd >= 0 && start_time >= f->k.stream_time_start && start_time < f->stream_time_end &&
											 f->stream_time_end != std::numeric_limits<milliseconds_t>::max());
		if (current_file_ok) {
			dtdebugx("Opening file which is already open file=%d", f->fileno);
		}
	}
	int fd = filemap.fd;
	if (!current_file_ok) {
		// The desired data is not in the current file. Open the correct one and close our filemap
		fd = open_file_containing_time(txn, start_time);
		if (fd < 0) { /* error*/
			filemap.unmap();
			filemap.close();
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

		Itn case the opened file is still live, end_marker
	*/
	recdb::marker_t current_marker;
	get_end_marker_from_db(txn, end_marker);
	bool return_to_live = false;
	if (get_marker_for_time_from_db(txn, current_marker, start_time) < 0) {
		dtdebugx("Requested start_play_time is beyond last logged packet");
		if (live_mpm) {
			auto mm = live_mpm->meta_marker.readAccess();
			if (start_time >= mm->current_marker.k.time) {
				return_to_live = true;
				current_marker = mm->current_marker;
				start_time = mm->current_marker.k.time;
			}
		} else {
			start_time = end_marker.k.time;
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
				dtdebugx("file %s was not properly closed", f->filename.c_str());
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
		The file is alreadt open (fd>=0). We now map data from the current file
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
		dtdebugx("current_byte_pos=%ld start_byte_pos=%ld\n", current_byte_pos, start_byte_pos);
	}
	current_byte_pos = start_byte_pos;

	return 1;
}

/*
	find the file part containing start_time, using database lookup only

*/
int playback_mpm_t::open_file_containing_time(db_txn& txn, milliseconds_t start_time) {
	error = false;
	// find a file which starts at start_time, or if none exists, which contains start_time
	using namespace recdb;
	auto c = file_t::find_by_key(txn, file_key_t(start_time), find_leq); // largest start_time smaller than the desired
																																			 // one
	if (!c.is_valid()) {
		// this can only happen if file is corrupt
		dterror("Could not find file corresponding to time " << milliseconds_t(start_time));
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
			dtdebugx("This file fileno=%d is already open", filemap.fd);
			fd = filemap.fd;
		} else {
			// current_filename  contains a relative path
			current_filename.clear();
			current_filename.sprintf("%s/%s", dirname.c_str(), r.filename.c_str());
		}
		// open the file, setting fd>=0 on success, otherwise -1
		for (; fd < 0;) {
			dtdebugx("Opening %s", current_filename.c_str());
			fd = ::open(current_filename.c_str(), O_RDONLY);
			if (fd < 0) {
				if (errno == EINTR)
					continue; // retry
				if (errno == ENOENT) {
					dtdebugx("File %s does not exist (may have been deleted; will try next one).", current_filename.c_str());
				} else {
					dtdebugx("Could not open data file %s: %s", current_filename.c_str(), strerror(errno));
				}
			}
			break;
		}

		if (fd >= 0) {
			// store info about the currently playing file
			currently_playing_file.assign(r);
			dtdebugx("currently_playing_file.fileno=%d fd=%d", r.fileno, fd);
			break;
		}
	}
	if (fd < 0) {
		if (filemap.buffer) {
			dtdebug("Could not open any data file; keeping current one");
			// user wanted to move backward foward/ beyond what is available; it is best to preserve what we have
			return filemap.fd;
		} else {
			dtdebug("Could not open any data file; returning error");
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
	auto txn = db->mpm_rec.idxdb.rtxn();
	// WRONG: assert(currently_playing_file.stream_time_end!= std::numeric_limits<milliseconds_t>::max());
	// WRONG, e.g., if paused for a long time:
	// assert(meta_marker.currently_playing_file.fileno == 1 + currently_playing_file.fileno);
	int fileno = 0;
	{
		auto f = currently_playing_file.readAccess();
		fileno = f->fileno;
	}

	auto ret = open_(txn, milliseconds_t(currently_playing_file.readAccess()->k.stream_time_start));
	return ret;
}

int64_t playback_mpm_t::read_data(char* outbuffer, uint64_t numbytes) {
	if (error || numbytes == 0)
		return 0;
	if (live_mpm) {
		dttime_init();
		auto ret = read_data_live_(outbuffer, numbytes);
		if (ret >= 0)
			check_pmt_change();
		dttime(300);
		return ret;
	} else
		return read_data_nonlive_(outbuffer, numbytes);
}

/*
	Wait until live data becomes available.
	Returns remaining_space>0 if new data was found
	Returns 0 if no data is found and no more data exists in the current mpm part, or if must_exit
 */
int64_t playback_mpm_t::wait_for_live_data(uint8_t*& buffer) {

	if (error)
		return 0;

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
			return 0;

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
		}
	}
	if(error)
		return 0;
	return remaining_space;
}

int64_t playback_mpm_t::read_data_live_(char* outbuffer, uint64_t numbytes) {

	if (error || numbytes == 0)
		return 0;

	int remaining_space = -1;
	uint8_t* buffer = nullptr;
	dttime_init();

	for (; !error;) {

		remaining_space = wait_for_live_data(buffer);
		if (must_exit)
			return -1;
		if(remaining_space >= ts_packet_t::size)
			break;
		assert(filemap.get_read_buffer(buffer) == 0);

		/*the current file has been fully played because map growing was tried, but remaining_space still 0.
			Also, the current file is not the last one because then we would have restarted or exited
			the loop.

			However our own view of the currently playing file may be out of date: last time we checked
			if may have been still growing
		*/

		auto txn = db->mpm_rec.idxdb.rtxn();
		auto end_time = currently_playing_file.readAccess()->stream_time_end;
		if (end_time == std::numeric_limits<milliseconds_t>::max()) {
			// this must be incorrect: file can no longer be growing, we must reread to find the correct end_time
			using namespace recdb;
			auto start_time = currently_playing_file.readAccess()->k.stream_time_start;
			auto fileno = currently_playing_file.readAccess()->fileno;
			auto c = file_t::find_by_key(txn, file_key_t(start_time), find_eq);
			assert(c.is_valid());
			auto r = c.current();
			dtdebug("Reread end_time for current file: " << end_time);
			end_time = r.stream_time_end;
			assert(fileno == r.fileno);
			assert(end_time != std::numeric_limits<milliseconds_t>::max());
			dtdebugx("currently_playing_file.fileno=%d", r.fileno);
			currently_playing_file.assign(c.current());
		}
		if (open_(txn, end_time) < 0) { // end time of current file is start time of new one
			dtdebug("Cannot open next part");
			filemap.unmap();
			filemap.close();
			return 0;
		}
		txn.abort();
		continue; // more data is available
	}
	dttime(200);

	if (error) {
		assert(0);
		return 0;
	}

	auto n = std::min(numbytes, (uint64_t)remaining_space);
	assert(n > 0);
	memcpy(outbuffer, buffer, n);
	// save_debug(buffer, n);
	dttime(100);
	filemap.advance_read_pointer(n);
	dttime(100);
	current_byte_pos += n;
	return n;
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

int64_t playback_mpm_t::read_data_nonlive_(char* outbuffer, uint64_t numbytes) {
	// @todo: this function is now almost the same as read_data_live_ => merge
	if (error || numbytes == 0)
		return 0;

	int remaining_space = -1;
	uint8_t* buffer = nullptr;
	dttime_init();

	for (; !error;) {

		remaining_space = filemap.get_read_buffer(buffer);
		if (remaining_space >= ts_packet_t::size) {
			assert(buffer);
			break; // keep streaming data which is known to be available
		}
		//load next file if available
		{
			recdb::marker_t end_marker;
			auto stream_time_end = currently_playing_file.readAccess()->stream_time_end;
			//check if file was properly closed and close it if not
			if ((int64_t)stream_time_end == std::numeric_limits<int64_t>::max() && !live_mpm) {
				auto wtxn = db->mpm_rec.idxdb.wtxn();
				recdb::marker_t current_marker;
				get_end_marker_from_db(wtxn, end_marker);
				auto f = currently_playing_file.writeAccess();
				dtdebugx("file %s was not properly closed", f->filename.c_str());
				stream_time_end = end_marker.k.time;
				f->stream_time_end = stream_time_end;
				f->stream_packetno_end = end_marker.packetno_end;
				wtxn.commit();
			}
			auto txn = db->mpm_rec.idxdb.rtxn();
			auto ret = ((int64_t)stream_time_end == std::numeric_limits<int64_t>::max()) ? -1 : open_(txn, stream_time_end);
			if (ret <= 0) {
				dterror("End of nonlive stream detected");
				must_exit = true;
				return 0;
			} else {
				continue;
			}
		}
	}

	dttime(200);
	if (error) {
		assert(0);
		return -1;
	}

	auto n = std::min(numbytes, (uint64_t)remaining_space);
	assert(n > 0);
	memcpy(outbuffer, buffer, n);
	// save_debug(buffer, n);
	filemap.advance_read_pointer(n);
	current_byte_pos += n;
	return n;
}

void playback_mpm_t::register_audio_changed_callback(int subscription_id, language_state_t::callback_t cb) {
	assert(subscription_id >= 0);
	assert(cb != nullptr);
	auto ls = language_state.writeAccess();
	dtdebugx("Register audio_changed_cb subscription_id=%d s=%d", subscription_id,
					 (int)ls->audio_language_change_callbacks.size());
	ls->audio_language_change_callbacks[subscription_id] = cb;
}

void playback_mpm_t::unregister_audio_changed_callback(int subscription_id) {
	assert(subscription_id >= 0);
	auto ls = language_state.writeAccess();
	dtdebugx("Unregister audio_changed_cb subscription_id=%d s=%d", subscription_id,
					 (int)ls->audio_language_change_callbacks.size());
	ls->audio_language_change_callbacks.erase(subscription_id);
}

void playback_mpm_t::register_subtitle_changed_callback(int subscription_id, language_state_t::callback_t cb) {
	assert(subscription_id >= 0);
	assert(cb != nullptr);
	auto ls = language_state.writeAccess();
	dtdebugx("Register subtitle_changed_cb subscription_id=%d s=%d", subscription_id,
					 (int)ls->subtitle_language_change_callbacks.size());

	ls->subtitle_language_change_callbacks[subscription_id] = cb;
}

void playback_mpm_t::unregister_subtitle_changed_callback(int subscription_id) {
	assert(subscription_id >= 0);
	auto ls = language_state.writeAccess();
	dtdebugx("Unregister subtitle_changed_cb subscription_id=%d s=%d", subscription_id,
					 (int)ls->subtitle_language_change_callbacks.size());
	ls->subtitle_language_change_callbacks.erase(subscription_id);
}

chdb::language_code_t playback_mpm_t::get_current_audio_language() {
	auto ls = language_state.readAccess();
	return ls->current_audio_language;
}

chdb::language_code_t playback_mpm_t::get_current_subtitle_language() {
	auto ls = language_state.readAccess();
	return ls->current_subtitle_language;
}

ss::vector_<chdb::language_code_t> playback_mpm_t::audio_languages() {
	auto ls = language_state.readAccess();
	return ls->current_streams.audio_langs;
}

ss::vector_<chdb::language_code_t> playback_mpm_t::subtitle_languages() {
	auto ls = language_state.readAccess();
	return ls->current_streams.subtitle_langs;
}

active_service_t* playback_mpm_t::active_service() const {
	return live_mpm->active_service;
}

/*
	first and last record from database (for non live).
	This data is assumed to remain constant
*/
int playback_mpm_t::get_end_marker_from_db(db_txn& txn, recdb::marker_t& end_marker) {
	using namespace recdb;
	auto c = find_last<recdb::marker_t>(txn);
	if (!c.is_valid()) {
		dterror("Could not obtain last marker");
		return -1;
	}
	end_marker = c.current();
	return 0;
}

int playback_mpm_t::get_marker_for_time_from_db(db_txn& txn, recdb::marker_t& current_marker,
																								milliseconds_t start_play_time) {

	auto c = recdb::marker_t::find_by_key(txn, recdb::marker_key_t(start_play_time), find_geq);
	if (!c.is_valid()) {
		dtdebug("Could not obtain marker for time " << start_play_time);
		return -1;
	}
	current_marker = c.current();

	return 0;
}
