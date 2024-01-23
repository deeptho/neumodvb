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
#include <filesystem>
#include "filemapper.h"
#include "streamparser/packetstream.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "util/safe/safe.h"
#include "recmgr.h"

#include "dvbcsa.h"


namespace fs = std::filesystem;

class active_service_t;
class active_mpm_t;

struct playback_info_t {
	chdb::service_t service;
	std::optional<epgdb::epg_record_t> epg;
	chdb::language_code_t audio_language;
	chdb::language_code_t subtitle_language;
	system_time_t start_time{}; //time of first available byte in livebuffer
	system_time_t end_time{};   //time of end of program (or now if there is no program)
	system_time_t play_time{};  //current playback time
	bool is_recording{false}; //Is this a recording or a live channel (possibly in timeshift mode)
	bool is_timeshifted{false};
};


namespace epgdb
{
	struct epg_record_t;
};



/*
	returns a todo list for the tuner thread, to be executed by the recmgr thread

*/
struct mpm_copylist_t {
	fs::path src_dir;
	fs::path dst_dir;
	//ss::vector<ss::string<128>, 32> filenames;
	recdb::rec_t rec;
	int fileno_offset{0};
	mpm_copylist_t() = default;
	mpm_copylist_t(const fs::path& src_dir_, const fs::path& dst_dir_, const recdb::rec_t& rec_)
		: src_dir(src_dir_)
		, dst_dir(dst_dir_)
		, rec(rec_)
		{}

	void run(db_txn& txn);
	void run();
};


/*
	muti-part mpeg*/
class mpm_index_t {
public:
	ss::string<128> idx_dirname;
	mpm_recordings_t mpm_rec;

	mpm_index_t(const char* idx_dirname ="");
	mpm_index_t(mpm_index_t& other) = delete;
	mpm_index_t(mpm_index_t&& other) = delete;

	mpm_index_t operator=(const mpm_index_t& other) = delete;


	void open_index();

};

class playback_mpm_t;

/*!Records the current state of playback or livebuffer recording

 */
class meta_marker_t {
	bool was_interrupted = false;
public:
	bool started = false;
	mutable std::condition_variable cv;
	int last_seen_txn_id =-1;
	int64_t num_bytes_safe_to_read = 0; //counted from the start of tuning to service (active_mpm only)
 	recdb::file_t current_file_record{}; //file being played back or modified (active_mpm only)
	recdb::marker_t current_marker{};  /*position in current file being played back or last modified*/
/*
 In a playback_mpm, current_marker is updated from live_mpm if playing back the most recent (growing) file.
 It is also updated from the database when starting playback or opening a new file. It then points to the
 first packet to be played from that file*/
	system_time_t livebuffer_start_time{};
	system_time_t livebuffer_end_time{};
	milliseconds_t livebuffer_stream_time_start{};
	recdb::stream_descriptor_t last_streams; //points to database record containing newest current pmt and such

	std::vector<playback_mpm_t*> playback_clients; /*for an active_mpm_t: filenos currently being played back
																									by any passive mpms coupled to it
																								*/
	meta_marker_t() {
		//needed to distinguish an uninitialized record from one with start==0
		assert(current_marker.packetno_start == std::numeric_limits<uint32_t>::max());
		init(now);
	}
	meta_marker_t(meta_marker_t&& other) = delete;
	void init(system_time_t now);
	bool need_epg_update(system_time_t play_time) const;
	void register_playback_client(playback_mpm_t* client);
	void unregister_playback_client(playback_mpm_t* client);
	int playback_clients_newest_fileno() const;

/*
	waits for a change in this meta_marker compared to "other" and then
	updates other; mutex should be locked prior to calling this function
*/
	void wait_for_update(meta_marker_t& other, std::mutex& mutex);
	void interrupt() {
		was_interrupted = true;
		cv.notify_all();
	}
	/*
		first and last record from database (for non live).
		This data is assumed to remain constant
	*/
	int update_from_db(db_txn& txn, recdb::marker_t& end_marker);


	int update_from_db(db_txn& txn, recdb::marker_t& end_marker, milliseconds_t start_play_time,
										 bool need_file_record);

};


struct stream_state_t {
	typedef std::function<void(const chdb::language_code_t& lang, int pos)> callback_t;
	chdb::language_code_t current_audio_language;
	chdb::language_code_t current_subtitle_language;
	recdb::stream_descriptor_t current_streams;
	recdb::stream_descriptor_t next_streams;
	ss::vector<chdb::language_code_t,4> audio_pref;
	ss::vector<chdb::language_code_t,4> subtitle_pref;
	std::map<subscription_id_t, callback_t> audio_language_change_callbacks;
	std::map<subscription_id_t, callback_t> subtitle_language_change_callbacks;
};


using mm_t = safe::Safe<meta_marker_t, std::mutex>;
using file_record_t = safe::Safe<recdb::file_t, std::mutex>;
using ss_t = safe::Safe<stream_state_t, std::mutex>;


class mpm_t {
protected:
	static constexpr  size_t  default_file_size = 127827968; //length of a single part, multiple of 4096 and 188 ; approx 121 MByte
	size_t mmap_size = default_file_size;

public:
	std::shared_ptr<mpm_index_t> db;

	mmap_t filemap;
	ss::string<128> dirname;
	bool error = false;

	//filename of the currently opened transport stream part
	ss::string<128> current_filename;

	mpm_t(mpm_t&&other) = delete;
	mpm_t(bool readonly);
	mpm_t(mpm_t& other);
	mpm_t(active_mpm_t& other, bool readonly);

	//void init(const char* dirname);
};

class active_mpm_t;
class active_playback_t;


class playback_mpm_t : public mpm_t {
	//active_playback_t* active_playback = nullptr; //if non null, then this is a live mpm
	receiver_t& receiver;
	bool must_exit = false;
	active_mpm_t* live_mpm = nullptr; /*if non-null the mpm is still growing; needed to prevent live_mpm
																			from deleting old data which we are reading
																		*/
	file_record_t currently_playing_file {};
	//recdb::marker_t end_of_recording_marker_record{};    //end of file (needed for GUI; to show status


	int64_t current_byte_pos = 0; /* overall global position in mpm file, relative to  when it was created,
																	 i.e., to when the channel was tuned:
																	 if parts are removed from the file (old live buffer parts), the current_byte_pos
																	 is relative to the start of a deleted part....
																	 Also, this refers to input bytes prior to filtering
																*/
	int num_pmt_bytes_to_send{-1}; /* If this is non-zero then we do not send data from the stream (and current_byte_pos
																		will not change), but instead pmt data.
																		-1 means: need initialisation
																 */
	meta_marker_t last_seen_live_meta_marker; //only used when playing a live buffer

	bool is_timeshifted{false};
	recdb::rec_t currently_playing_recording{};
	ss_t stream_state;
	dtdemux::pmt_info_t current_pmt;
	ss::bytebuffer<128> preferred_streams_pmt_ts;
	int64_t next_stream_change_{-1}; //cache
	int next_stream_change(); //byte at which new pmt becomes active (coincides with end of old pmt)
	inline void clear_stream_state() {
		next_stream_change_ = -1 ; //clear cache; will force a reload
		auto w = stream_state.writeAccess();
		w->current_streams = {};
		w->next_streams = {};
	}

public:
	const subscription_id_t subscription_id;

private:
	void find_current_pmts(int64_t bytepos);
	int get_end_marker_from_db(db_txn& txn, recdb::marker_t& end_marker);
	int get_marker_for_time_from_db(db_txn& idxdb_txn, recdb::marker_t& current_marker, milliseconds_t start_play_time);

	//int refresh_markers_(db_txn& txn);
	//int refresh_markers_(db_txn& txn, milliseconds_t milliseconds);
	//int refresh_current_file_record_(db_txn& txn);
	int open_(db_txn& idxdb_txn, milliseconds_t start_time);

	int open_file_containing_time(db_txn& recdb_txn, milliseconds_t start_time);

	int open_next_file();
	int64_t copy_filtered_packets(char* outbuffer, uint8_t* inbuffer, int64_t numbytes);
	std::tuple<int,int> copy_filtered_packets(char* outbuffer, uint8_t* inbuffer, int outbytes, int inbytes);
	int64_t read_pmt_data(char* outbuffer, uint64_t numbytes);
	int64_t read_data_from_current_file(uint8_t*& buffer);
	std::tuple<int, int> read_data_(char* outbuffer, int outbytes, int inbytes);
	std::tuple<bool, int64_t> currently_playing_file_status();
	playback_info_t get_recording_program_info() const;
	void update_pmt(stream_state_t& stream_state);
public:
	EXPORT active_service_t* active_service () const;
	EXPORT void register_audio_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb);
	EXPORT void unregister_audio_changed_callback(subscription_id_t subscription_id);

	EXPORT void register_subtitle_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb);
	EXPORT void unregister_subtitle_changed_callback(subscription_id_t subscription_id);

	EXPORT void open_recording(const char* dirname);
	//void init();


	playback_mpm_t(receiver_t& receiver, subscription_id_t subscription_id);
	playback_mpm_t(active_mpm_t& other, const chdb::service_t& live_service,
								 const recdb::stream_descriptor_t& streamdesc, subscription_id_t subscription_id);
	playback_mpm_t& operator=(const playback_mpm_t& other) = delete;


	EXPORT int64_t read_data(char* buffer, uint64_t numbytes);
	EXPORT int move_to_time(milliseconds_t start_play_time);
	EXPORT int move_to_live();
	//int open(int fileno=0); //find and open file
	EXPORT void close();
	EXPORT milliseconds_t get_current_play_time() const;
	EXPORT void force_abort();
	int current_fileno() const {
		return currently_playing_file.readAccess()->fileno;
	}
	EXPORT playback_info_t get_current_program_info() const;
	EXPORT int set_language_pref(int idx, bool for_subtitles);
	inline int set_audio_language(int audio_idx) {
		return set_language_pref(audio_idx, false);
	}
	inline int set_subtitle_language(int subtitle_idx) {
		return set_language_pref(subtitle_idx, true);
	}
	EXPORT chdb::language_code_t get_current_audio_language();
	EXPORT chdb::language_code_t get_current_subtitle_language();
	EXPORT ss::vector_<chdb::language_code_t> audio_languages();
	EXPORT ss::vector_<chdb::language_code_t> subtitle_languages();

};



class active_mpm_t : public mpm_t
{
	static constexpr  size_t  default_file_size = 127827968; //length of a single part, multiple of 4096 and 188 ; approx 121 MByte
	int next_recid = -1;
	int current_fileno = -1;
	system_time_t last_epg_check_time{};
public:
	active_service_t* active_service = nullptr; //if non null, then this is a live mpm
	mm_t meta_marker;
	int num_recordings_in_progress = 0;
	size_t initial_file_size = default_file_size;
	size_t mmap_size = default_file_size;
	std::chrono::seconds file_time_limit{300s};//30; //if >0, then a new file will be started after approx. this many seconds


	int64_t num_bytes_read{0};  //since start of receiving this channel

	int64_t first_available_byte{0}; /* when the start of the timeshift buffer is being
																			erases, this will be incremented to point to
																			the first available (decrypted) byte for reading
																	 */
	int64_t num_bytes_decrypted{0}; /*since tuning this service*/

	//information about the current file being streamed to
	//int64_t current_file_stream_time_start = 0; //since start of receiving this channel; play_time
	system_time_t current_file_time_start; //real time at which the current file was started (in seconds)
	const system_time_t creation_time;
	uint32_t current_file_stream_packetno_start{0};

	dvbcsa_t dvbcsa;

	dtdemux::ts_stream_t stream_parser;



private:
	bool  file_used_by_recording(const recdb::file_t& file) const;
	static ss::string<128> make_dirname(active_service_t*parent, system_time_t start_time);
	bool next_key(int parity);
	void transfer_filemap(int fd, int64_t new_num_bytes_safe_to_read); //helper

  /*!
		create the directory structure, including the database
		opens the index database
	*/

	/*!
		create a new live buffer, using an appropriate name
	*/
	void mkdir(const char*  dirname);

 public:

	void create();
	active_mpm_t(active_service_t* parent, system_time_t now);

  /*!
		create a new empty data file, open it and map it to memory;
		if old file and map exist, then it is closed and unmapped
	*/
	int next_data_file(system_time_t now, int64_t new_num_bytes_safe_to_read);

	void process_channel_data();

//low_data_rate: force decryption to use smaller buffers for a faster response
	int decrypt_channel_data(bool low_data_rate);

	void close();


	void start_live_recording(db_txn& parent_txn, system_time_t now, int duration);


	recdb::rec_t
	start_recording(subscription_id_t subscription_id, recdb::rec_t rec /*not a reference!*/);

	int stop_recording(const recdb::rec_t& rec_in, mpm_copylist_t& copy_command);
	void forget_recording_in_livebuffer(const recdb::rec_t& r);

	void delete_recording(db_txn& parent_txn, uint32_t event_id, system_time_t now);
	void update_recording(recdb::rec_t&rec, const chdb::service_t& service,
											 const epgdb::epg_record_t& epgrec);
	void update_recordings(db_txn& parent_txn, system_time_t now);
	void delete_old_data(db_txn& parent_txn,  system_time_t now);
	void self_check(meta_marker_t& meta_marker);
	void wait_for_update(meta_marker_t& other);
	void destroy();
};

int finalize_recording(db_txn& livebuffer_idxdb_rtxn, mpm_copylist_t& copy_command, mpm_index_t* db);
int close_last_mpm_part(db_txn& idx_txn, const ss::string_& dirname);
