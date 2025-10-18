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

using namespace recdb;

static int64_t pagesize = sysconf(_SC_PAGESIZE);

std::optional<recdb::marker_t> mpm_cursor_t::get_marker_for_time(db_txn& idxdb_rtxn, milliseconds_t start_play_time) {
	auto c = recdb::marker_t::find_by_key(idxdb_rtxn, recdb::marker_key_t(start_play_time), find_geq);
	if (!c.is_valid()) {
		dtdebugf("Could not obtain marker for time {}", start_play_time);

		c = find_last<recdb::marker_t>(idxdb_rtxn);
		if (!c.is_valid()) {
			dtdebugf("Could not find last marker");
			return {};
		}
		return c.current();
	}
	return c.current();
}



/*
	Position the file pointer to the start of the file containing packet packetno
	unless packetno is not in the file. In that case, position it at the next availalable packet
 */
int64_t mpm_cursor_t::seek_part_for_packetno(db_txn& idxdb_rtxn, int32_t packetno) {
	// find a file which starts at start_time, or if none exists, which contains start_time
	using namespace recdb;
	auto c = file_t::find_by_stream_packetno_start(idxdb_rtxn, packetno, find_leq); /* Find part with a stream_packetno_start smaller than or
																																											equal to the requested one*/
	if (!c.is_valid()) {
		/*
			This can happen if the oldest parts of the mpm have already been deleted
		*/
		user_errorf("Could not find file containing packet  {}", packetno);
		c = file_t::find_by_stream_packetno_start(idxdb_rtxn, packetno, find_geq);
		if (!c.is_valid()) {
			//this can only happen if everything has been deleted (ie., never)
			user_errorf("Could not find start");
			return -1;
		}
	}
	// store info about the currently playing file
	current_part = c.current();
	auto cf = find_last<file_t>(idxdb_rtxn);
	if(!cf.is_valid())
		return -1;
	last_part = cf.current();
	num_bytes_safe_to_read = this->part_is_growing() ? 0 :  current_part.stream_packetno_end * ts_packet_t::size;
#if 0
	dtdebugf("set num_bytes_safe_to_read={} part_no={}", num_bytes_safe_to_read, current_part.fileno);
#endif
	return num_bytes_safe_to_read;
}

int mpm_cursor_t::init() {
	db->open_index();
	auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto ret = init(idxdb_rtxn);
	idxdb_rtxn.abort();
	return ret;
}


int mpm_cursor_t::init(db_txn& idxdb_rtxn) {
	auto c = find_first<file_t>(idxdb_rtxn);
	auto saved = error;
	error = true;
	if(!c.is_valid())
		return -1;
	current_part = c.current();
	c = find_last<file_t>(idxdb_rtxn);
	if(!c.is_valid())
		return -1;
	last_part = c.current();
	error = saved;
	return -1;
}


//called by playback_mpm
inline int mpm_cursor_t::seek_to_time_(milliseconds_t start_time) {
	auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto ret =  seek_to_time_(idxdb_rtxn, start_time);
	idxdb_rtxn.abort();
	return ret;
}

int mpm_cursor_t::seek_to_time_(db_txn& idxdb_rtxn, milliseconds_t start_time) {

	auto current_markerp = get_marker_for_time(idxdb_rtxn, start_time);
	if(!current_markerp) { //can happen at start of tuning, when no index records have yet been written
		num_bytes_safe_to_read = 0;
		dtdebugf("set num_bytes_safe_to_read={}", num_bytes_safe_to_read);

		auto ret = seek_part_for_packetno(idxdb_rtxn, 0);
		if(ret < 0)
			return ret;

		return 0;
	}

	auto & current_marker = *current_markerp;

	auto ret = seek_part_for_packetno(idxdb_rtxn, current_marker.packetno_start);
	if(ret < 0)
		return ret;

	this->current_byte_pos = current_marker.packetno_start * ts_packet_t::size;
	dtdebugf("set current_byte_pos={} part_no={}", this->current_byte_pos, this->current_part.fileno);
	assert(this->current_byte_pos >=0);
	assert(this->current_byte_pos >= current_part.stream_packetno_start * (int64_t)ts_packet_t::size);
	assert(this->part_is_growing() ||
				 this->current_byte_pos < current_part.stream_packetno_end * (int64_t)ts_packet_t::size);
	if(this->part_is_growing()) {
		auto c = find_last<recdb::marker_t>(idxdb_rtxn);
		if (!c.is_valid()) {
			dterrorf("Could not obtain last marker");
			dtdebugf("set num_bytes_safe_to_read={}", num_bytes_safe_to_read);
			num_bytes_safe_to_read = 0;
			return 0;
		}
		auto end_marker = c.current();
		num_bytes_safe_to_read = end_marker.packetno_end * ts_packet_t::size - this->current_byte_pos;
		dtdebugf("set num_bytes_safe_to_read={} current_byte_pos={}", num_bytes_safe_to_read, this->current_byte_pos);
	} else {
		//num_bytes_safe_to_read = current_marker.packetno_end * ts_packet_t::size - this->current_byte_pos;
		dtdebugf("num_bytes_safe_to_read={} current_byte_pos={}", num_bytes_safe_to_read, this->current_byte_pos);
	}
	return 0;
}


milliseconds_t mpm_cursor_t::play_time_for_byte_pos(db_txn& idxdb_rtxn, int64_t byte_pos) {
	auto c = recdb::marker_t::find_by_packetno(idxdb_rtxn, (uint32_t) (this->current_byte_pos / ts_packet_t::size), find_leq);
	if (!c.is_valid()) {
		auto c = find_first<recdb::marker_t>(idxdb_rtxn);
		if(!c.is_valid())
			return milliseconds_t(0);
		auto m = c.current();
		return m.k.time;
	}
	auto m = c.current();
	return m.k.time;
}

milliseconds_t mpm_cursor_t::play_time_for_byte_pos(int64_t byte_pos) {
	auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto ret =  play_time_for_byte_pos(idxdb_rtxn, byte_pos);
	idxdb_rtxn.abort();
	return ret;
}

int64_t mpm_cursor_t::seek_to_bytepos(int64_t byte_pos) {
	auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto ret =  seek_to_bytepos(idxdb_rtxn, byte_pos);
	idxdb_rtxn.abort();
	return ret;
}

int64_t mpm_cursor_t::seek_to_bytepos(db_txn& idxdb_rtxn, int64_t byte_pos) {
	this->current_byte_pos = byte_pos;
	return this->seek_part_for_packetno(idxdb_rtxn, (uint32_t) (byte_pos / ts_packet_t::size));
}

std::optional<recdb::pmt_marker_t>  mpm_cursor_t::get_pmt_marker() {
	assert(this->current_byte_pos == next_stream_change_); //otherwise there is no reason to call this
	if(this->current_byte_pos != next_stream_change_)
		return {};
	auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto c = recdb::pmt_marker_t::find_by_key(idxdb_rtxn, 1 + (uint32_t) (this->current_byte_pos / ts_packet_t::size),
																						find_geq);
	if(!c.is_valid()) {
		//no known stream change yet in database, but there may be one in live_mpm
	} else {
		auto pmt_marker = c.current();
		next_pmt_marker = pmt_marker;
		next_stream_change_ = pmt_marker.packetno_start *  ts_packet_t::size;
		assert(next_stream_change_ > this->current_byte_pos);
	}
	idxdb_rtxn.abort();
	auto ret = next_pmt_marker;
	next_pmt_marker.reset();
	next_stream_change_ = -1;
	return ret;
}

/*
	Move to a specific part and update information about the last part in case mpm is growing.
 */
int mpm_cursor_t::move_to_part(db_txn& idxdb_rtxn, int partno)
{
	//update our stale view
	if(!idxdb_rtxn)
		idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
	auto c = find_last<file_t>(idxdb_rtxn);
	if(!c.is_valid()) {
		dterrorf("Cannot find last part\n");
		error = true;
		return -1;
	}
	last_part = c.current();

	//figure out the correct last byte of the current file
	auto cf = recdb::file_t::find_by_fileno(idxdb_rtxn, partno, find_eq);
	assert(cf.is_valid());
	auto old_current_part = current_part;
	current_part = cf.current();

	num_bytes_safe_to_read = this->part_is_growing() ? 0 :  current_part.stream_packetno_end * ts_packet_t::size;
	dtdebugf("set num_bytes_safe_to_read={} partnp={}", num_bytes_safe_to_read, current_part.fileno);
	return 0;
}

int mpm_cursor_t::check_for_pmt_change(std::optional<db_txn>& idxdb_rtxn, int64_t last_pmt_packetno_start)
{
	if(next_stream_change_ == -1 && (last_pmt_packetno_start  > this->current_byte_pos
																	 || this->current_byte_pos==0)) {
		/*a stream change must be pending, but beware: there may have been multiple pmt_changes.
			In that case the returned  last_pmt_packetno_start is not for the next stream change,
			but possible for a later one (which would be rare!)
		*/

		if(!idxdb_rtxn)
			idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
		auto c = recdb::pmt_marker_t::find_by_key(*idxdb_rtxn, (uint32_t) (this->current_byte_pos / ts_packet_t::size),
																							find_geq);
		if(!c.is_valid()) { /* Note that last_pmt_packetno_start>=0 in this case as current_byte_pos >=0.
													 Therefore we already found out earlier that there is at least one pmt present
													 The above test last_pmt_packetno_start  >= current_byte_pos
													 shows that it is located beyond current_byte_pos.
													 Therefore the find_geq test must succeed.
												*/
			error = true;
			dterrorf("Cannot find expected pmt current_byte_pos={}\n", this->current_byte_pos);
			return -1;
		}
		auto pmt_marker = c.current();
		assert(pmt_marker.packetno_start <= last_pmt_packetno_start);
		next_stream_change_ = pmt_marker.packetno_start *  ts_packet_t::size;
		assert(next_stream_change_ >= this->current_byte_pos);
		next_pmt_marker = pmt_marker;
	}
	return 0;
}

int mpm_cursor_t::wait_for_update(active_mpm_t* live_mpm) {
	/*
		wait until there are more bytes available to read
	*/
	assert(num_bytes_safe_to_read==0); //otherwise there is no need for an update
	auto [last_fileno, max_bytes_pos, last_pmt_packetno_start]
	 = live_mpm->wait_for_update(this->current_byte_pos + num_bytes_safe_to_read);

	auto new_num_bytes_safe_to_read = max_bytes_pos - this->current_byte_pos;
	assert(new_num_bytes_safe_to_read >= num_bytes_safe_to_read);
	num_bytes_safe_to_read = new_num_bytes_safe_to_read;
#if 0
	dtdebugf("set num_bytes_safe_to_read={}", num_bytes_safe_to_read);
#endif
	std::optional<db_txn> idxdb_rtxn;

	if(last_fileno != last_part.fileno) {
		if(!idxdb_rtxn)
			idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
		//update our stale view by re-seeking
		auto ret = this->move_to_part(*idxdb_rtxn, current_part.fileno);
		if (ret<0)
			return ret;
	}

	auto ret = check_for_pmt_change(idxdb_rtxn, (int64_t)last_pmt_packetno_start * ts_packet_t::size);
	if(idxdb_rtxn)
		idxdb_rtxn->abort();
	if(ret<0)
		return ret;
	return 0;
}



/*
	increment num_bytes_to_read by at least 0 and at most num_bytes bytes
	This is is done such that current_part_does not change, unless current_byte_pos points past its end;
	in the later case, current_part is moved to a next part.

	Hence, the result alway equals a number of bytes that are safe to read in the current mpm_part, but possibly
	less than the requested number of bytes "num_bytes".

	Also, the number of bytes returned will always be limited until the next stream_change

	Special cases are
	-when the last part is still growing, the call will block until data is available
	-when the last part is not growing (a recording), the value 0 will be returned, which should be interpreted
	by the caller as "end of file reached".
  -when a stream change (pmt change) prevents returning at least one bye, also the value 0 will be returned.


	returns:
	 fileno:     number of the part in which data can be read
	 start_pos:  byte in this part (relative to the start of the file) at which the first byte can be read
	 num_bytes:  number of bytes that can be read
	 stream_change: true when the reason for returning num_bytes==0 is a stream change
 */
std::tuple<int32_t, int64_t, int32_t, bool> mpm_cursor_t::get_read_range(int32_t num_bytes, active_mpm_t* live_mpm) {
	assert(num_bytes>0);
	auto n = std::min(num_bytes_safe_to_read, num_bytes);
	assert(n>=0);
	auto still_growing = live_mpm && this->part_is_growing();
	if(!still_growing) {
		int maxbytes  = (int64_t)this->current_part.stream_packetno_end* ts_packet_t::size - this->current_byte_pos;
		n = std::min(maxbytes, n);
		assert(n>=0);
	}

	bool stream_change{false};

	if(next_stream_change_ >=0) {
		stream_change = (next_stream_change_ == this->current_byte_pos);
		n = std::min(n, (int32_t)(next_stream_change_ - this->current_byte_pos));
	}
	assert(n>=0);
	if(n  == 0 && !stream_change) {
		while (n==0 && ! stream_change) {
			auto still_growing = live_mpm && this->part_is_growing();
			if(still_growing) {
				assert(num_bytes_safe_to_read == 0);
				auto ret = wait_for_update(live_mpm);
				assert(ret>=0);
			} else {
				std::optional<db_txn> idxdb_rtxn;
				idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
				//move to next part
				auto ret = move_to_part(*idxdb_rtxn, current_part.fileno+1);
				if(ret<0) {
					n=0; //error
					break;
				}
				ret = check_for_pmt_change(idxdb_rtxn, this->current_byte_pos);
				if(ret<0) {
					n=0; //error
					break;
				}
				idxdb_rtxn->abort();
			}
			still_growing = this->part_is_growing(); //may have changed now
			auto maxbytes  = num_bytes_safe_to_read;
			if(!still_growing) {
				maxbytes  = (int64_t)this->current_part.stream_packetno_end* ts_packet_t::size - this->current_byte_pos;
				assert(maxbytes >=0);
			}
			n = std::min(maxbytes, num_bytes);
			stream_change = (next_stream_change_ == this->current_byte_pos);
			if(next_stream_change_ >=0)
				n = std::min(n, (int32_t)(next_stream_change_ - this->current_byte_pos));
			assert(n>=0);
			assert(!stream_change || (n==0));
		}
	}
	assert(n>=0);
	assert(this->current_byte_pos >= current_part.stream_packetno_start * ts_packet_t::size);
	return {
		current_part.fileno,
		this->current_byte_pos - current_part.stream_packetno_start * ts_packet_t::size,
		n,
		stream_change};
}


void mpm_cursor_t::advance(int32_t num_bytes) {
	assert(num_bytes <= num_bytes_safe_to_read);
	num_bytes_safe_to_read -= num_bytes;
#if 0
	dtdebugf("set num_bytes_safe_to_read={} part_no={}", num_bytes_safe_to_read, this->current_part.fileno);
#endif
	this->current_byte_pos += num_bytes;
#if 0
	dtdebugf("advance: set current_byte_pos={} part_no={}", this->current_byte_pos, this->current_part.fileno);
#endif
	assert(this->current_byte_pos >=0);
	assert(next_stream_change_<0 || this->current_byte_pos <= next_stream_change_);
}

void part_cursor_t::close_current_part() {
	unmap();
	while (fd >= 0 && ::close(fd) < 0) {
		if (errno != EINTR) {
			dterrorf("Error closing file: {:s}", strerror(errno));
			break;
		}
	}
	fd = -1;
}

/*
	open the file that contains the current part, closing whatever was opened before

 */
void part_cursor_t::open_current_part() {
	if(this->part_no == mpm_cursor.current_part.fileno && this->fd >= 0)
		return; //already open
	if(this->fd >=0)
		close_current_part();

	ss::string<128> current_filename;
	current_filename.format("{:s}/{:s}", this->dirname, mpm_cursor.current_part.filename);
	// open the file, setting fd>=0 on success, otherwise -1
	for (; this->fd < 0;) {
		dtdebugf("Opening {}", current_filename);
		this->fd = ::open(current_filename.c_str(), O_RDONLY);
		if (this->fd < 0) {
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
	this->part_no =  mpm_cursor.current_part.fileno;
	error |= (this->fd < 0);
}


void part_cursor_t::unmap() {
	if (!mapped)
		return;
	dtdebugf("UNMAP: {:p} {:d}", fmt::ptr(mapped), map_len);
	if (mapped && munmap(mapped, map_len) < 0) {
		dterrorf("Error while unmapping: {}", strerror(errno));
	}
	offset = -1;
	mapped = nullptr;
}

	/*map a piece of the part corresponding to to mpm_cursor.current_part. The piece starts
		at offset from the start of the part and ends at min(offset+map_len, actual part file size)

		return nullptr on failure
	*/
uint8_t* part_cursor_t::map() {
	if(mapped)
		unmap();
	open_current_part();
	offset = this->mpm_cursor.current_byte_pos - this->mpm_cursor.current_part.stream_packetno_start*ts_packet_t::size;
	assert(offset >= 0);
	offset = (offset / pagesize) * pagesize;

	dtdebugf("MMAP current_byte_pos={} offset={} map_len={} part_no={}", this->mpm_cursor.current_byte_pos,
					 offset, map_len, this->part_no);
	uint8_t* mem = (uint8_t*) mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, offset);
	if (mem == (uint8_t*)-1) {
		dterrorf("Error in mmap: {}", strerror(errno));
		assert(0);
		error = true;
		return nullptr;
	}
	mapped = mem;
	//todo
	return mapped;
}


int part_cursor_t::init()
{
	mpm_cursor.init();
	error = false;
	return 0;
}

int part_cursor_t::seek_to_time(milliseconds_t start_time)
{
	auto old_fileno = mpm_cursor.current_fileno();
	auto ret = mpm_cursor.seek_to_time_(start_time);
	error |= (ret < 0);
	dtdebugf("Seek to time error={}", error);
	if(mpm_cursor.current_fileno() != old_fileno || !mapped) {
		map();
	}
	return ret;
}

//called by playback_mpm (move_to_live)
int part_cursor_t::seek_to_bytepos(int64_t byte_pos)
{
	auto old_fileno = mpm_cursor.current_fileno();
	auto ret = mpm_cursor.seek_to_bytepos(byte_pos);
	error |= (ret < 0);
	dtdebugf("Seek to bytepos error={}", error);
	if(mpm_cursor.current_fileno() != old_fileno) {
		map();
	}
	return (int32_t) byte_pos;
}


/*
	returns a range of data to be read of at most num_bytes bytes
	returns:
	 -buffer: pointer to first byte that can be read
	 -n: number of bytes that can be read, which will be >=0 and <= num_bytes
	 -if n==0, an stream_change==false we have reached the end of a (non-growing) mpm
	 -if n==0, an stream_change==true the caller must handle a stream_change (e.g., select a new audio stream),
	  then call skip_stream_change, and repeat get_read_range to retrieve a valid buffer

 */
std::tuple<uint8_t*, int32_t, bool>
part_cursor_t::get_read_range(int32_t num_bytes, active_mpm_t* live_mpm)
{
	if(error)
		return {nullptr, 0, false};
	auto [part_no_, current_byte_pos, len_, stream_change] =
		mpm_cursor.get_read_range(num_bytes, live_mpm);
	bool need_mapping = !mapped || 	this->part_no != part_no_;
	assert(current_byte_pos>=0);
	assert(len_>=0 && len_ <=num_bytes);
	if(stream_change)
		return {nullptr, 0, stream_change};

	if(need_mapping) {
		map();
	}
	this->part_no = part_no_;
	auto* buffer = get_buffer(part_no_, current_byte_pos, len_);
	assert(buffer +len_ - mapped  <= map_len);
	if(!buffer) {
		map();
		buffer = get_buffer(part_no_, current_byte_pos, len_);
		assert(buffer || (stream_change && len_==0));
	}
	assert(buffer || (stream_change && len_==0));
#if 0
	dtdebugf("part_no={} offset={} buffer={}, len={} num_bytes_safe_to_read={}",
					 part_no, offset, buffer - mapped + offset, len_, mpm_cursor.num_bytes_safe_to_read);
#endif
	assert(buffer +len_ - mapped  <= map_len);
	return {buffer, len_, stream_change};
}

//called by playback_mpm
milliseconds_t mpm_cursor_t::get_current_play_time() const {
	{
		auto current_byte_pos = this->current_byte_pos;
		auto idxdb_rtxn = db->mpm_rec.idxdb.rtxn();
		auto c = recdb::marker_t::find_by_packetno(idxdb_rtxn, (uint32_t) (current_byte_pos / ts_packet_t::size), find_leq);
		if (!c.is_valid())
			return milliseconds_t(0);
		auto m = c.current();
		idxdb_rtxn.abort();
		return m.k.time;
	}
}
