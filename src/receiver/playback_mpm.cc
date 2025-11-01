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
#include "mpm_cursor.h"

playback_mpm_t::playback_mpm_t(receiver_t& receiver_, subscription_id_t subscription_id_, const char* dirname, const char* idx_dirname)
	: mpm_t(true)
	, receiver(receiver_)
	, part_cursor(dirname, idx_dirname)
	, subscription_id(subscription_id_) {
	part_cursor.init();
};

playback_mpm_t::playback_mpm_t(active_mpm_t& other,
															 const chdb::service_t& live_service,
															 subscription_id_t subscription_id_)
	: mpm_t(other, true)
	, receiver(other.active_service->receiver)
	, live_mpm(&other)
	, part_cursor(other.dirname.c_str(), other.db->idx_dirname.c_str())
	, subscription_id(subscription_id_) {
	assert(filemap.readonly);
	auto ls = stream_state.writeAccess();
	ls->current_pmt_marker = other.meta_marker.readAccess()->current_pmt_marker;
	ls->audio_pref = live_service.audio_pref;
	ls->subtitle_pref = live_service.subtitle_pref;
	part_cursor.init();
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
	dtdebugf("Setting live_mpm=nullptr");
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
	After a new pmt has been activated, activate it so that it will be sent in the output
	stream before sending any other stream data.
 */
void playback_mpm_t::update_pmt(stream_state_t& ss) {
	//pmt with all audio streams
	current_pmt = parse_pmt_section(ss.current_pmt_marker.pmt_section, ss.current_pmt_marker.pmt_pid);
#ifdef PMTREWRITE
	auto saved = generated_ts.size();
	if(!this->pmt_writer)
		pmt_writer = std::make_unique<pmt_writer_t>(ss.current_pmt_marker.pmt_pid);
	this->pat_writer.add_single_service_pat(generated_ts, current_pmt.service_id, current_pmt.pmt_pid);
	assert(generated_ts.size() > saved);
	assert(num_generated_bytes_to_send>=0);
	num_generated_bytes_to_send += generated_ts.size() - saved;
	assert(num_generated_bytes_to_send>=0);
	saved = generated_ts.size();
	std::tie(ss.current_audio_language, ss.current_audio_pid,
					 ss.current_subtitle_language, ss.current_subtitle_pid ) =
		pmt_writer->add_preferred_pmt_ts(generated_ts, current_pmt,
																		 ss.current_audio_language,
																		 ss.current_subtitle_language,
																		 ss.audio_pref, ss.subtitle_pref);
	assert(generated_ts.size() > saved);
	num_generated_bytes_to_send += generated_ts.size() - saved;
	assert(num_generated_bytes_to_send>=0);
	//num_generated_bytes_to_send = generated_ts.size();
	dtdebugf("setting num_generated_bytes_to_send={}", num_generated_bytes_to_send );
#else
	//generated_ts.append_raw(ss.current_pmt_marker.pmt_section.buffer(), ss.current_pmt_marker.pmt_section.size());

	auto [audio_idx, audio_desc, audio_lang ] =
		current_pmt.best_audio_language(ss.current_audio_language, ss.audio_pref);
	auto [subtitle_idx, subtitle_desc, desc2, subtitle_lang] = current_pmt.best_subtitle_language
		(ss.current_subtitle_language,  ss.subtitle_pref);
	if(audio_desc) {
		ss.current_audio_language = audio_lang;
		dtdebugf("Setting audio_lang={}\n", audio_idx);
		ss.set_language_pref(audio_idx, false/*for_subtitles*/);
		ss.current_audio_pid = audio_desc->stream_pid;
	}

	if(subtitle_idx>=0) {
		ss.current_subtitle_language = subtitle_lang;
		dtdebugf("update_pmt: Setting subtitle_lang={}", subtitle_idx);
		ss.set_language_pref(subtitle_idx, true/*for_subtitles*/);
		ss.current_subtitle_pid = subtitle_desc ? subtitle_desc->stream_pid : 0x1fff;
	} else {
		dtdebugf("update_pmt: Not setting subtitle_lang");
		ss.current_subtitle_pid = 0x1fff;
	}

#endif
	this->current_audio_pid = ss.current_audio_pid;
	this->current_subtitle_pid = ss.current_subtitle_pid;
	assert(current_pmt.pmt_pid ==  ss.current_pmt_marker.pmt_pid);
	have_pmt = true;
}

int stream_state_t::set_language_pref(int idx, bool for_subtitles) {
	auto langs = for_subtitles ? this->current_pmt_marker.subtitle_langs : this->current_pmt_marker.audio_langs;
	if (idx < 0 || idx >= langs.size()) {
		dterrorf("set_language: index {:d} out of range", idx);
		return -1;
	}

	chdb::language_code_t selected_lan = langs[idx];

	if(selected_lan.position == 0 && selected_lan.lang1 == 0 && selected_lan.lang2 == 0 && selected_lan.lang3 == 0) {
		//no subtitles wanted
		idx = -1;
	}
	if (for_subtitles)
		this->current_subtitle_language = selected_lan;
	else
		this->current_audio_language = selected_lan;
	for (const auto& [i, cb]:  this->language_change_callbacks) {
		cb(selected_lan, idx, for_subtitles);
	}
	return 1;
}

int playback_mpm_t::set_language_pref(int idx, bool for_subtitles) {
	auto ls = stream_state.writeAccess();
	auto langs = for_subtitles ? ls->current_pmt_marker.subtitle_langs : ls->current_pmt_marker.audio_langs;
	if (idx < 0 || idx >= langs.size()) {
		dterrorf("set_language: index {:d} out of range", idx);
		return -1;
	}
	chdb::language_code_t selected_lan = langs[idx];

	auto update = [&selected_lan, for_subtitles](chdb::service_t& service) {
		auto& prefs = for_subtitles ? service.subtitle_pref : service.audio_pref;
		if (prefs[0] == selected_lan)
			return;
		if (prefs.size() < 4)
			prefs.resize_no_init(prefs.size() + 1);
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
		auto wtxn = db->mpm_rec.recdb.wtxn();
		put_record(wtxn, currently_playing_recording);
		wtxn.commit();
	}

	if(selected_lan.position == 0 && selected_lan.lang1 == 0 && selected_lan.lang2 == 0 && selected_lan.lang3 == 0) {
		//no subtitles wanted
		idx = -1;
	}

	if (for_subtitles)
		ls->current_subtitle_language = selected_lan;
	else
		ls->current_audio_language = selected_lan;
	update_pmt(*ls); //trigger sending of new pmt, which is now present in current_pmt_marker
	return idx;
}

playback_info_t playback_mpm_t::get_recording_program_info() const {
	playback_info_t ret;
	ret.service = currently_playing_recording.service;
	ret.start_time = system_clock_t::from_time_t(currently_playing_recording.real_time_start);
	ret.end_time = system_clock_t::from_time_t(currently_playing_recording.real_time_end);
	ret.play_time = ret.start_time;
	ret.is_recording = !live_mpm;
	ret.is_timeshifted = false;
	ret.epg = currently_playing_recording.epg;
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


inline void playback_epg_state_t::update(receiver_t& receiver, int recdb_txn_id,
																				 playback_info_t& ret)  {
	auto play_time = system_clock_t::to_time_t(ret.play_time);
	if(this->last_seen_recdb_txn_id != recdb_txn_id || !this->current_epg ||
			 play_time < this->current_epg->k.start_time || play_time >= this->current_epg->end_time
		) {
		auto epgdb_rxtn = receiver.epgdb.rtxn();
		this->current_epg = epgdb::running_now(epgdb_rxtn, ret.service.k, ret.play_time);
		epgdb_rxtn.abort();
	}
	this->last_seen_recdb_txn_id = recdb_txn_id;
}

playback_info_t playback_mpm_t::get_current_program_info() const {
	auto* as = live_mpm ? live_mpm->active_service : nullptr;
	auto ret = live_mpm ? live_mpm->get_current_program_info() : get_recording_program_info();
	if(as)
		ret.is_timeshifted = is_timeshifted;

	{
		auto ls = stream_state.readAccess();
		ret.audio_language = ls->current_audio_language;
		ret.subtitle_language = ls->current_subtitle_language;
	}
	{
		auto delta = int64_t(part_cursor.get_current_play_time()) / 1000;
		auto p = ret.start_time + std::chrono::duration<int64_t>(delta);
		ret.play_time = p;
	}

	if(live_mpm) {
		auto recdb_rtxn = this->receiver.recdb.rtxn();
		auto txn_id = recdb_rtxn.txn_id();
		recdb_rtxn.abort();
		epg_state.update(this->receiver, txn_id, ret);
		ret.epg = epg_state.current_epg;
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
	if (start_play_time < milliseconds_t(0))
		start_play_time = milliseconds_t(0);
	dtdebugf("calling open_ start_play_time={}", start_play_time);
	auto ret = part_cursor.seek_to_time(start_play_time);
	return ret;
}

//called by playback_mpm
int playback_mpm_t::move_to_packetno(int32_t packetno) {
	if(live_mpm)
		is_timeshifted = true;
	error = false;
	dtdebugf("calling seek_to_bytepos packetno={}", packetno);
	auto ret = part_cursor.seek_to_bytepos(packetno * (int64_t) ts_packet_t::size);
	return ret;
}

//called by neumompv.cc seek_fn
int64_t playback_mpm_t::move_to_bytepos(int64_t bytepos) {
	if(live_mpm)
		is_timeshifted = true;
	error = false;
	dtdebugf("calling seek_to_bytepos bytepos={}", bytepos);
	auto ret = part_cursor.seek_to_bytepos(bytepos);
	return ret>=0 ? bytepos : -1;
}

//called by neumompv.cc size_fn
int64_t playback_mpm_t::get_size() {
	auto ret = part_cursor.get_size();
	return ret>=0 ? ret : -1;
}

/*
	returns the number of seconds at which playback should start
 */

int64_t playback_mpm_t::move_to_live() {
	assert(live_mpm);
	live_mpm->wait_for_update(last_seen_live_meta_marker, ts_packet_t::size);
	auto packetno_start = last_seen_live_meta_marker.current_marker.packetno_end;
	int64_t ret = move_to_packetno(packetno_start);
	if(ret < 0)
		return ret;
	is_timeshifted = false;
	ret = (int64_t)part_cursor.get_current_play_time();
	return ret/1000;
}


/*
	read up to outbytes bytes in outbuffer, while not reading more than inbytes bytes from the input stream
	The call may return earlier if not enough data is available
	Returns number of inputs bytes consumed and number of output bytes written
	Returns -1 on error or if must_exit
 */
std::tuple<int, int> playback_mpm_t::read_data_(char* outbuffer, int64_t outbytes) {
	if (error || outbytes == 0)
		return {0, 0};
	int tot_out{0};
	int tot_in{0};
	for (; outbytes > 0;) {
		auto [buffer, remaining_space, stream_change_] = part_cursor.get_read_range((int32_t)outbytes, live_mpm);
		if(stream_change_) {
				auto pmt_marker = part_cursor.get_pmt_marker();
				auto ss = stream_state.writeAccess();
				ss->current_pmt_marker = pmt_marker;
				update_pmt(*ss);
				continue;
		} else if(remaining_space==0) {
			//playing back recording and reaching eof
			return {tot_out, tot_in};
		}
		dttime_init();
		dttime(100);

		if (must_exit)
			return {-1, -1};
#ifdef PMTREWRITE
		if (remaining_space < ts_packet_t::size)
			break; // enough data which is known to be available to continue processing
#endif
		assert(remaining_space <= outbytes);
		auto [num_bytes_out, num_bytes_in] = copy_filtered_packets(outbuffer, buffer, outbytes, remaining_space);
		tot_out += num_bytes_out;
		tot_in += num_bytes_in;
#if 0
		{
			static FILE* fp=fopen("/tmp/sss.ts", "w");
			fwrite(buffer, num_bytes_in, 1, fp);
			fflush(fp);
			assert(num_bytes_in == num_bytes_out);
			assert(memcmp(buffer, outbuffer, num_bytes_out)==0);
		}
#endif
		outbuffer += num_bytes_out;
		part_cursor.advance(num_bytes_in);
		outbytes -= num_bytes_out;
		assert(outbytes >=0);
		if(tot_out> 0)
			break;
	}
	return {tot_out, tot_in};
}

static void make_null_packet(ss::bytebuffer<512>& buffer)
{
	buffer.resize(188);
	auto* buff = &buffer[0];
	memset(buff, 0xff, 188);
	buff[0]=0x47;
	buff[1]= 0x1f;
	buff[2]= 0xff;
	buff[3] = 0; //cccounter
}

/*
	read up to num_bytes data in output buffer.
	Returns ret, have_pmt
	  ret=-1: error
	  ret=0:  end of stream

 */
std::tuple<int64_t, bool> playback_mpm_t::read_data(char* outbuffer, uint64_t num_bytes) {
	uint64_t num_bytes_orig{num_bytes};
	if(part_cursor.has_error() ||  num_bytes == 0)
		return {0, have_pmt};
	int num_bytes_read{0};
	/*below, read_data_live_ and read_data_nonlive_ can read 0 bytes
		for two reasons: 1) due to pmt filtering, no real data may be available yet
		2) or real data may still be available, but the reading code has reached the point
		where pmt data should be sent.

		The loop is therefore needed, to retry the read.
	*/

	if(num_generated_bytes_to_send > 0) {
		auto num_bytes_sent  = read_generated_data(outbuffer + num_bytes_read, num_bytes);
		num_bytes -= num_bytes_sent;
		num_bytes_read += num_bytes_sent;
		if(num_bytes> 0) {
			/*the generated data is not fully sent; this must be becasuse mppv
				called us with too little room to write it all.
			*/
			return {num_bytes_read, have_pmt};
		}
		assert(num_generated_bytes_to_send ==0);
	}

	if(must_exit)
		return {0, have_pmt};
	assert(num_bytes >=0);

	if(num_bytes > 0 ) {
		auto [num_bytes_out, num_bytes_in] = read_data_(outbuffer + num_bytes_read, num_bytes);

		num_bytes_read += num_bytes_out;
		num_bytes -= num_bytes_out;

		/*in rare cases, our caller may ask for less than 188 bytes. We will never be able to provide this,
			because we always return multiples of 188 bytes and the result would be 0 in this case,
			which mpv interprets as end of stream.
			As a workaround we send part of null packet. The rest of this packet will be sent on the next mpv read
		*/
		if (num_bytes_read ==0 && num_bytes < ts_packet_t::size) {
			assert(have_pmt);
			assert (num_generated_bytes_to_send ==0); //otherwise num_bytes_read cannot be zero
			dtdebugf("Returning null packet data to fill partial packet: read={} left={} org={}",
							 num_bytes_read, num_bytes, num_bytes_orig);
			make_null_packet(this->generated_ts);
			num_generated_bytes_to_send =  generated_ts.size();
			assert(num_generated_bytes_to_send>=0);
			dtdebugf("setting num_generated_bytes_to_send={}", num_generated_bytes_to_send );
			assert(num_generated_bytes_to_send > 0);
			auto ret  = read_generated_data(outbuffer + num_bytes_read, num_bytes);
			assert(ret>0);
			num_bytes_read += ret;
			num_bytes -= ret;
			assert(num_bytes_read >0);
		}
	}
	assert(!live_mpm || num_bytes_read != 0); /*live_mpm will lead to blocking (ok), but otherwise we have a problem;
																						 num_bytes_read < 0 is ok; indicates and error
																							 num_bytes_read > 0 is also ok; indicates progress
																					 */
	return { (must_exit || error) ? -1 : num_bytes_read, have_pmt};
}

milliseconds_t playback_mpm_t::get_current_play_time() const {
	return part_cursor.get_current_play_time();
}

void playback_mpm_t::register_language_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb) {
	assert((int) subscription_id >= 0);
	assert(cb != nullptr);
	auto ls = stream_state.writeAccess();
	dtdebugf("Register audio_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->language_change_callbacks.size());
	ls->language_change_callbacks[subscription_id] = cb;
}

void playback_mpm_t::unregister_language_changed_callback(subscription_id_t subscription_id) {
	assert((int) subscription_id >= 0);
	auto ls = stream_state.writeAccess();
	dtdebugf("Unregister audio_changed_cb subscription_id={:d} s={:d}", (int) subscription_id,
					 (int)ls->language_change_callbacks.size());
	ls->language_change_callbacks.erase(subscription_id);
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
	return ls->current_pmt_marker.audio_langs;
}

ss::vector_<chdb::language_code_t> playback_mpm_t::subtitle_languages() {
	auto ls = stream_state.readAccess();
	return ls->current_pmt_marker.subtitle_langs;
}

active_service_t* playback_mpm_t::active_service() const {
	return live_mpm ? live_mpm->active_service : nullptr;
}

int64_t  playback_mpm_t::read_generated_data(char* outbuffer, uint64_t num_bytes) {
	assert(num_generated_bytes_to_send>=0);
	if(num_generated_bytes_to_send > 0) {
		auto n = std::min(num_bytes, (uint64_t)num_generated_bytes_to_send);
		assert(n > 0);
		auto num_generated_bytes_already_sent = generated_ts.size() - num_generated_bytes_to_send;
		memcpy(outbuffer,
					 generated_ts.buffer() + num_generated_bytes_already_sent, n);
		num_generated_bytes_to_send -= n;
		assert(num_generated_bytes_to_send>=0);
		if(num_generated_bytes_to_send == 0)
			generated_ts.clear();
		return n;
	}
	return 0;
}


#ifdef PMTREWRITE
/*
	copy full packets to the mpv buffer, discarding pmt packets
	inbytes = number of bytes that are available and allowed to read in inbuffer
	outbytes = number of bytes available in outbuffer
	Returns number of bytes placed in outbuffer,  and number of bytes read from inbuffer
	Both can be smaller than min(inbytes, outbytes) if last input packet
	is not yet complete and/or because some input packets are discarded
	The number of packets read/written can also equal zero
 */
std::tuple<int,int> playback_mpm_t::copy_filtered_packets(char* outbuffer, uint8_t* inbuffer,
																													int64_t outbytes, int64_t inbytes)
{
	inbytes -= inbytes % ts_packet_t::size;
	outbytes -= outbytes % ts_packet_t::size;
	auto ls = stream_state.readAccess();

	if(inbytes<=0)
		return {0, 0};
	int num_read{0};
	auto *inptr = inbuffer;
	auto *inptr_end = inptr + inbytes;
	auto *outptr = outbuffer;
	auto *outptr_end = outptr + outbytes;

	for (; inptr < inptr_end && outptr < outptr_end; inptr +=  dtdemux::ts_packet_t::size) {
		int pid = (((uint16_t)(inptr[1] & 0x1f)) << 8) | inptr[2];
		if (pid == current_pmt.pmt_pid
				|| pid == 0 /*pat*/
			)
			continue;
		if(pid != current_pmt.video_pid && pid != current_audio_pid && pid != current_subtitle_pid)
			continue;
		memcpy(outptr, inptr, dtdemux::ts_packet_t::size);
		outptr += dtdemux::ts_packet_t::size;
		num_read += dtdemux::ts_packet_t::size;
	}
	return {num_read, inptr - inbuffer};
}

#else
/*
	copy full packets to the mpv buffer, discarding pmt packets
	inbytes = number of bytes that are available and allowed to read in inbuffer
	outbytes = number of bytes available in outbuffer
	Returns number of bytes placed in outbuffer,  and number of bytes read from inbuffer
 */
std::tuple<int,int> playback_mpm_t::copy_filtered_packets(char* outbuffer, uint8_t* inbuffer,
																													int64_t outbytes, int64_t inbytes)
{
	auto ls = stream_state.readAccess();

	if(inbytes<=0)
		return {0, 0};
	auto *inptr = inbuffer;
	auto *outptr = outbuffer;

	auto n = std::min(outbytes, inbytes);
	memcpy(outptr, inptr, n);
	return {n, n};
}
#endif



/*
	Possible states:
	1) num_pmt_bytes_to_send>0: then return pmt bytes to reader until  num_pmt_bytes_to_send=0
	2) num_pmt_bytes_to_send==0 and  packetno < ls->next_pmt_marker.packetno_start:
     then we are sending filtered stream data: pmt packets are removed, other packets
		 are passed to reader
	3)) num_pmt_bytes_to_send==0 and  packetno == ls->next_pmt_marker.packetno_start:
     then we must call pmt call backs and we also must also go to state 1, with  num_pmt_bytes_to_send set
		 to the size of the newly active pmt


   Overall play state:
    current_pmt_marker: current pmt_data + byte at which this became active
    next_streams: next pmt_data + byte at which this becomes active. packetno_start = -1 means: there is no new pmt
		currently_playing_file: descriptor of mpm file wich is currently mapped
		current_byte_pos: position of next byte to read from mpm recording (live or non live)
		num_pmt_bytes_to_send: if larger than 0, number of pmt bytes still to send before reading from actual recorded stream
		current_pmt_pid: -> part of current_pmt_marker: pmt_pid used to send pmt data
		currently_playing_recording: overall information about recording (like epg data)



 */
