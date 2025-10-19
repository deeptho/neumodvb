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

#include "mpm.h"

class mpm_index_t;
class active_mpm_t;
class part_cursor_t;

class mpm_cursor_t {
	friend class part_cursor_t;
	std::shared_ptr<mpm_index_t> db;
	bool error {false}; //if true, an error has occurred and nothing will work
	int64_t current_byte_pos = 0; /* overall global position in mpm file at which next byte will be read.
																	 This is relative to  when it was created, i.e., to when the channel was tuned
																	 if parts are removed from the file (old live buffer parts), the current_byte_pos
																	 is relative to the start of a deleted part....
																*/
	int32_t num_bytes_safe_to_read{0}; /* number of bytes starting from current_byte_pos until the end of the
																			 current part (current best estimate in case file is growing)

																			 This value may be outdated, but then the most uptodate value is larger
																		*/

	int64_t next_stream_change_{-1}; /* first byte position after current_byte_pos at which a pmt change will occur.
																			This is relative to  the first byte in the mpm, when the mpm was created,
																			i.e., to when the channel was tuned
																			if parts are removed from the file (old live buffer parts), the current_byte_pos
																			is relative to the start of a deleted part....

																			The special value -1 means no pmt change is known.

																			This value may be outdated, but only in the sense that -1 may be incorrect
																	 */
	std::optional<recdb::pmt_marker_t>  next_pmt_marker;

	int move_to_part(db_txn& idxdb_rtxn, int partno);
	int check_for_pmt_change(std::optional<db_txn>& idxdb_rtxn, int64_t last_pmt_packetno_start);

/*
		position current_bytepos at start_time milliseconds from the start of tuning,
		or as close as possible, such that it points to valid data.
		Also set current_part, which contains info about the mpm part containing that byte

		returns current_bytepos on success, and -1 on error
	 */
	int64_t seek_to_time_(db_txn& idxdb_txn, milliseconds_t start_time);
	inline int64_t seek_to_time_(milliseconds_t start_time);

public:
	recdb::file_t current_part {}; /*information about current part that contains current_bytepos*/

	recdb::file_t last_part {}; /*information about the last part in this mpm*/

	inline bool part_is_growing () const {
		return current_part.stream_packetno_end == std::numeric_limits<int64_t>::max();
	}

	/*
		set current_part such that it contains packet packetno, or if that is not possible
		to the next available packet

		Returns current_bytepos on success, and -1 on error
	 */
	int64_t seek_part_for_packetno(db_txn& idxdb_rtxn, int32_t packetno);


	std::optional<recdb::marker_t> get_marker_for_time(db_txn& idxdb_txn, milliseconds_t start_play_time);



	int init(db_txn& idxdb_rtxn);

	int init();

	/*
		position current_bytepos at byte_pos bytes since the start of tuning.
		Also set current_part, which contains info about the mpm part containing that byte
	 */
	int64_t seek_to_bytepos(db_txn& idxdb_txn, int64_t byte_pos);
	int64_t seek_to_bytepos(int64_t byte_pos);

	milliseconds_t play_time_for_byte_pos(db_txn& idxdb_txn, int64_t byte_pos);
	inline milliseconds_t play_time_for_byte_pos(int64_t byte_pos);

	inline int current_fileno() const {
		return current_part.fileno;
	}

	/*
		returns true if the cursor is placed in a live_part. This information can be outdated, but only in the sense
		that the most uptodate return value would be false, whereas true is returnded
	 */
	bool is_in_live_part() const {
		return (last_part.fileno == current_part.fileno && part_is_growing());
	}


	int64_t next_stream_change() const {
		return next_stream_change_;
	}


	int wait_for_update(active_mpm_t* live_mpm);

	std::tuple<int32_t, int64_t, int32_t, bool>  get_read_range(int32_t num_bytes, active_mpm_t* live_mpm);
	void advance(int32_t num_bytes);

	/*Move the cursor past a stream change, after the caller has handled the stream_change.
		This then allows further reading from the cursor
	 */
	std::optional<recdb::pmt_marker_t> get_pmt_marker();

	milliseconds_t get_current_play_time() const;

	mpm_cursor_t(const char* idx_dirname_) :
		db(std::make_shared<mpm_index_t>(idx_dirname_))
		{}
};

class part_cursor_t {
	constexpr static int default_map_len = 16ll*1024*1024;
	bool error{false};
	ss::string<64> dirname;
	int fd{-1}; //file descriptor of currently opened part
	int part_no{-1}; //currently mapped part
	int64_t offset{-1}; //offset w.r.t. part of currently mapped part
	uint8_t* mapped{nullptr}; //currently mapped range
	int32_t map_len{default_map_len}; //size of currently mapped part, which may be larger than the current file size!

	EXPORT void close_current_part();

	void open_current_part();

	void unmap();

	uint8_t* map(int64_t start);

	/*
		test if the currently mapped data range contains data from a specific part
		part_no: index of the part we
		start: offset within the part (start of part is 0)
		num_bytes: desired number of bytes

		returns nullptr if desired range is not fully mapped currently,
		otherwise a pointer to the first desired byte
	 */
	inline uint8_t* get_buffer(int part_no, int64_t start, int32_t num_bytes) {
		assert(part_no == this->part_no);
		bool still_growing =  mpm_cursor.is_in_live_part();
		if (still_growing) {
			auto ret = (mapped && (this->part_no == part_no && start >= offset
												 && start + num_bytes <= offset + map_len))
				? mapped + (start -offset) : nullptr;

			assert(ret - mapped + num_bytes  <= map_len);
			return ret;
		} else {
			auto ret = (mapped && (this->part_no == part_no && start >= offset
												 && start + num_bytes <= mpm_cursor.current_part.stream_packetno_end * dtdemux::ts_packet_t::size
												 && start + num_bytes <= offset + map_len))
				? mapped + (start -offset) : nullptr;
			assert(ret - mapped + num_bytes  <= map_len);
			return ret;
		}
	}

public:
	//mmap_t mmap;
	mpm_cursor_t mpm_cursor;


	part_cursor_t(const char* dirname_, const char* idx_dirname_)
		: dirname(dirname_)
		, mpm_cursor(idx_dirname_)
		{}

	~part_cursor_t() {
		close_current_part();
	}

	int init();
	int seek_to_time(milliseconds_t start_time);
	int seek_to_bytepos(int64_t byte_pos);

	inline std::optional<recdb::pmt_marker_t> get_pmt_marker() {
		return mpm_cursor.get_pmt_marker();
	}

	/*returns a pointer to a range of data to be read of at most num_bytes bytes
	 */
	std::tuple<uint8_t*, int32_t, bool>
	get_read_range(int32_t num_bytes, active_mpm_t* live_mpm);

	inline void advance(int32_t num_bytes) {
		mpm_cursor.advance(num_bytes);
	}

	inline bool has_error() const {
		return this->error;
	}

	inline milliseconds_t get_current_play_time() const {
		return this->mpm_cursor.get_current_play_time();
	}

};
