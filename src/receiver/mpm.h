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

#pragma once
#include <filesystem>
#include "filemapper.h"
#include "streamparser/packetstream.h"
#include "streamparser/streamwriter.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "util/safe/safe.h"
#include "recmgr.h"
#include "mpm_cursor.h"


namespace fs = std::filesystem;

class active_service_t;
class active_mpm_t;

enum class stream_status_t {
	UNKNOWN,
	STARTING,
	ACTIVE,
	INACTIVE,
	NODATA,
	ENCRYPTED,
	ERROR
};

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
	stream_status_t stream_status;
};

struct playback_epg_state_t {
	int last_seen_recdb_txn_id{-1};
	std::optional<epgdb::epg_record_t> current_epg;

	inline void update(receiver_t& receiver, int recdb_txn_id, playback_info_t& ret);
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
	mutable std::condition_variable cv;
	int last_seen_txn_id =-1;
	stream_status_t stream_status;
	int64_t num_bytes_safe_to_read = 0; //counted from the start of tuning to service (active_mpm only)

	recdb::file_t current_file_record{}; /* the file corresponding to the mpm-part
																					being played back (playback_mpm) or modified (active_mpm)
																				*/
	recdb::marker_t current_marker{};  /*position in current file being played back or last modified*/
/*
 In a playback_mpm, current_marker is updated from live_mpm if playing back the most recent (growing) file.
 It is also updated from the database when starting playback or opening a new file. It then points to the
 first packet to be played from that file*/
	system_time_t livebuffer_start_time{};
	system_time_t livebuffer_end_time{};
	milliseconds_t livebuffer_stream_time_start{};
	recdb::pmt_marker_t current_pmt_marker; //points to database record containing newest current pmt and such

	std::vector<playback_mpm_t*> playback_clients; /*for an active_mpm_t: filenos currently being played back
																									by any passive mpms coupled to it
																									all packetno are relative to the start of service tuning
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
	void wait_for_update(meta_marker_t& other, std::mutex& mutex, int64_t byte_pos_to_read);
	std::tuple<int32_t, int64_t, int32_t> wait_for_update(std::mutex& mutex, int64_t min_byte_pos, std::optional<recdb::pmt_marker_t>* ppmt_ret);

	void interrupt() {
		was_interrupted = true;
		cv.notify_all();
	}

};


struct stream_state_t {
	typedef std::function<void(const chdb::language_code_t& lang, int pos, bool for_subtitles)> callback_t;
	chdb::language_code_t current_audio_language;
	pid_t current_audio_pid{0x1fff};
	chdb::language_code_t current_subtitle_language;
	pid_t current_subtitle_pid{0x1fff};
	recdb::pmt_marker_t current_pmt_marker;
	ss::vector<chdb::language_code_t,4> audio_pref;
	ss::vector<chdb::language_code_t,4> subtitle_pref;
	std::map<subscription_id_t, callback_t> language_change_callbacks;

	int set_language_pref(int idx, bool for_subtitles);
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
	bool have_pmt{false};
	active_mpm_t* live_mpm = nullptr; /*if non-null the mpm is still growing; needed to prevent live_mpm
																			from deleting old data which we are reading
																		*/
	file_record_t currently_playing_file {};
	//recdb::marker_t end_of_recording_marker_record{};    //end of file (needed for GUI; to show status

	mutable playback_epg_state_t epg_state;
	meta_marker_t last_seen_live_meta_marker; //only used when playing a live buffer
	part_cursor_t part_cursor;
	pid_t current_audio_pid{0x1fff};
	pid_t current_subtitle_pid{0x1fff};
	bool is_timeshifted{false};
	recdb::rec_t currently_playing_recording{};
	ss_t stream_state;
	dtdemux::pmt_info_t current_pmt;
	ss::bytebuffer<512> generated_ts;
	int num_generated_bytes_to_send{0}; /* If this is non-zero then we do not send data
																					from the stream (and current_byte_pos
																					will not change), but instead pmt data.
																			 */
	dtdemux::pat_writer_t pat_writer; //rewritten pat-stream
	std::unique_ptr<dtdemux::pmt_writer_t> pmt_writer; //rewritten pmt-stream

public:
	const subscription_id_t subscription_id;

private:
	std::tuple<int,int> copy_filtered_packets(char* outbuffer, uint8_t* inbuffer, int64_t outbytes, int64_t inbytes);
	int64_t read_generated_data(char* outbuffer, uint64_t num_bytes);
	std::tuple<int, int> read_data_(char* outbuffer, int64_t outbytes);
	std::tuple<bool, int64_t> currently_playing_file_status();
	playback_info_t get_recording_program_info() const;
	void update_pmt(stream_state_t& stream_state);
public:
	EXPORT active_service_t* active_service () const;
	EXPORT void register_language_changed_callback(subscription_id_t subscription_id, stream_state_t::callback_t cb);
	EXPORT void unregister_language_changed_callback(subscription_id_t subscription_id);

	EXPORT void open_recording(const char* dirname);
	//void init();


	playback_mpm_t(receiver_t& receiver, subscription_id_t subscription_id,  const char* dirname, const char* idx_dirname);
	playback_mpm_t(active_mpm_t& other, const chdb::service_t& live_service, subscription_id_t subscription_id);
	playback_mpm_t& operator=(const playback_mpm_t& other) = delete;


	EXPORT std::tuple<int64_t,bool> read_data(char* buffer, uint64_t numbytes);
	EXPORT int move_to_time(milliseconds_t start_play_time);
	EXPORT int move_to_packetno(int32_t packetno);
	EXPORT int64_t move_to_bytepos(int64_t bytepos);
	EXPORT int64_t move_to_live();
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

	inline milliseconds_t play_time_for_byte_pos(int64_t byte_pos) {
		return part_cursor.mpm_cursor.play_time_for_byte_pos(byte_pos);
	}
};

class stream_buffer_t {
public:
	int64_t num_bytes_read{0};  //since start of receiving this channel
	int64_t num_bytes_decrypted{0}; /*since tuning this service*/

	dtdemux::ts_stream_t stream_parser;
	active_service_t* active_service{nullptr};

	stream_buffer_t(active_service_t* active_service, neumodb_t* idxdb =nullptr)
		:	stream_parser(idxdb) //TODO: move event_handler to parent class
		, active_service(active_service)
		{}

	inline bool has_encrypted_packets() const {
		return this->stream_parser.num_encrypted_packets > 0;
	}

	dtdemux::ts_stream_t*  get_ts_stream() {
		return &this->stream_parser;
	}

	virtual inline void set_stream_status(stream_status_t status) {};
	virtual void close()=0;
	virtual bool process_service_data(int num_bytes_decrypted_now) = 0;
	virtual int get_write_buffer(uint8_t*& buffer_ret) =0;
	virtual int advance() = 0;
	virtual void set_start_time(system_time_t creation_time) {};
	virtual void advance_write_pointer(int extra)  = 0;

	virtual void advance_decrypt_pointer(int extra)  = 0;

	virtual int bytes_to_decrypt(uint8_t*& buffer_ret) = 0;

	virtual void set_buffer(int num_bytes_decrypted_now)  = 0;
	virtual void housekeeping(system_time_t now)  = 0;

	inline virtual ~stream_buffer_t() = default; //essential

	virtual void register_parser_pid(int service_id, const dtdemux::pid_info_t& pidinfo) =0;
	inline virtual void save_pmt(system_time_t now_, const dtdemux::pmt_info_t& pmt_info,
															 const ss::bytebuffer<256>& pmt_sec_data) {}

	inline void set_marker_offsets(time_t real_time, recdb::marker_t marker) {
		stream_parser.set_marker_offsets(real_time, marker);
	}
};

class active_mpm_t : public mpm_t, public stream_buffer_t
{
	static constexpr  size_t  default_file_size = 127827968; //length of a single part, multiple of 4096 and 188 ; approx 121 MByte
	int next_recid = -1;
	int current_fileno = -1;
	system_time_t last_epg_check_time{};
	periodic_t periodic;
public:

	mm_t meta_marker;
	int num_recordings_in_progress = 0;
	size_t initial_file_size = default_file_size;
	size_t mmap_size = default_file_size;
	std::chrono::seconds file_time_limit{300s};//30; //if >0, then a new file will be started after approx. this many seconds


	int64_t first_available_byte{0}; /* when the start of the timeshift buffer is being
																			erases, this will be incremented to point to
																			the first available (decrypted) byte for reading
																	 */

	//information about the current file being streamed to
	//int64_t current_file_stream_time_start = 0; //since start of receiving this channel; play_time
	system_time_t current_file_time_start; //real time at which the current file was started (in seconds)
	int64_t current_file_stream_packetno_start{0};



private:
	bool  file_used_by_recording(const recdb::file_t& file) const;
	bool next_key(int parity);
	void transfer_filemap(int fd, int64_t num_bytes_in_final_mmap); //helper

  /*!
		create the directory structure, including the database
		opens the index database
	*/

	/*!
		create a new live buffer, using an appropriate name
	*/
	void mkdir(const char*  dirname);

 public:
	virtual void set_start_time(system_time_t creation_time) override;
	virtual inline void set_stream_status(stream_status_t status) override {
		auto w = meta_marker.writeAccess();
		w->stream_status = status;
	}
	void update_pmt(const dtdemux::pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data,
									bool is_new, bool ca_changed, bool service_changed);

	virtual void housekeeping(system_time_t now) final;
	virtual void save_pmt(system_time_t now_, const dtdemux::pmt_info_t& pmt_info,
												const ss::bytebuffer<256>& pmt_sec_data) override;
	void create(const recdb::live_service_t& live_service);
	active_mpm_t(active_service_t* parent, const recdb::live_service_t& live_service);
	~active_mpm_t();

  /*!
		create a new empty data file, open it and map it to memory;
		if old file and map exist, then it is closed and unmapped
	*/
	int next_data_file(system_time_t now);
	int new_data_file(const recdb::file_t& current_file_record, const char* mode);

	virtual void close() override;

	void start_live_recording(db_txn& parent_txn, system_time_t now, int duration);

	EXPORT playback_info_t get_current_program_info() const;

	recdb::rec_t
	start_recording(subscription_id_t subscription_id, recdb::rec_t rec /*not a reference!*/);

	int stop_recording(const recdb::rec_t& rec_in, mpm_copylist_t& copy_command);
	void forget_recording_in_livebuffer(const recdb::rec_t& r);

	void delete_recording(db_txn& parent_txn, uint32_t event_id, system_time_t now);
	void update_recording(recdb::rec_t&rec, const chdb::service_t& service,
											 const epgdb::epg_record_t& epgrec);
	void update_recordings(db_txn& parent_txn, system_time_t now);
	void delete_old_data(db_txn& parent_txn,  system_time_t now);

	void wait_for_update(meta_marker_t& other, int64_t byte_pos_to_read);
	std::tuple<int32_t, int64_t, int32_t>wait_for_update(int64_t min_byte_pos, std::optional<recdb::pmt_marker_t>* ppmt_ret);
	void destroy();

	inline virtual int get_write_buffer(uint8_t*& buffer_ret) override {
		return this ->filemap.get_write_buffer(buffer_ret);
	}

	inline virtual int advance()  override {
		return this->filemap.advance();
	}

	inline virtual void advance_write_pointer(int extra) override {
		this->filemap.advance_write_pointer(extra);
	}

	virtual void advance_decrypt_pointer(int extra) override {
		assert(filemap.decrypt_pointer+extra <= filemap.write_pointer);
		filemap.decrypt_pointer+=extra;
		assert(filemap.decrypt_pointer<=filemap.map_len);
	}



	inline virtual int bytes_to_decrypt(uint8_t*& buffer_ret) override {
		return this->filemap.bytes_to_decrypt(buffer_ret);
	}

	inline virtual void set_buffer(int num_bytes_decrypted_now) override {
		this->stream_parser.set_buffer(filemap.buffer + filemap.decrypt_pointer, num_bytes_decrypted_now);
	}

	virtual bool process_service_data(int num_bytes_decrypted_now) override;
	virtual void register_parser_pid(int service_id, const dtdemux::pid_info_t& pidinfo) final;

	std::unique_ptr<playback_mpm_t> make_playback_mpm(subscription_id_t subscription_id);
};

int finalize_recording(db_txn& livebuffer_idxdb_rtxn, mpm_copylist_t& copy_command, mpm_index_t* db);
int close_last_mpm_part(db_txn& idx_txn, const ss::string_& dirname);
