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
#include <stdint.h>
#include <unistd.h>
#include "util/dtassert.h"

/*
	Represents one part of a multi-part-mpeg (mpm), stored as a separate file on disk
	and mapped to a a memory region on in ram.

	An mpm represents all the data received from a service from when it is was tuned until
	the current time.

 */
struct mmap_t {
	bool readonly = false;
	static const int64_t pagesize;
	int fd{-1}; //file descriptor of currently mapped file

	/*
		currenly we map the range [offset, offset + map_len] in memory.
		This may extend beyond the end of the file. Units are bytes
	 */
	int64_t offset{0}; /*file offset at which this part starts, relative to the start of the mpm,
											and expressed in bytes*/
	int64_t map_len{-1}; /* Current length of the mapped file area in bytes*/

	uint8_t* buffer{nullptr}; //address at which the part is mapped
	int64_t safe_read_len{-1}; /*number of bytes which are safe to read in the mapped part.
															 So buffer[0]... buffer[safe_read_len-1] is the range contiaining
															 valid data to read.

															 Even if more data, after  after buffer[safe_read_len-1], is also mapped,
															 that data may not be accessed (not even read) because it is space in
															 which FUTURE writes may occur, but wich may also still be
															 truncated away by the writer, which would then cause a BUS error when read.
														 */
	int64_t read_pointer{0};   /*points to the byte buffer[read_pointer] we will read next in the part.
															 This byte is byte offset+read_pointer wr.t.t the start of the mpm.
															 Valid range for read_pointer: [0, safe_read_len]
														 */
	int64_t write_pointer{0};  /*points to the byte buffer[write_pointer] in the file  we will write next.
															 This byte is byte offset+write_pointer w.r.t. the start if the mpm.
															 Valid range for write_pointer: [0, map_len]
														 */
	int64_t decrypt_pointer{0}; /*points to the byte buffer[decrypt_pointer] we will decrypt next
																in the part.
																This byte is byte offset+decrypt_pointer w.r.t. the start if the mpm.
																Valid range for decrypt_pointer: [0, write_pointer]
															*/

	void init();

	/*!
		Returns the total number of bytes decrypted so far
	 */
	int64_t get_decrypted_filesize() {
		return offset+decrypt_pointer;
	}

	int64_t get_write_buffer(uint8_t*& buffer_ret) {
		if(!buffer)
			return -1;
		buffer_ret=buffer+write_pointer;
		assert (write_pointer>= 0);
		assert (write_pointer<= map_len);
		return map_len - write_pointer;
	}

	int64_t get_read_buffer(uint8_t*& buffer_ret) {
		if(!buffer)
			return -1;
		buffer_ret = buffer + read_pointer;
		assert (read_pointer>= 0);
		assert (read_pointer<= safe_read_len);
		return safe_read_len - read_pointer;
	}

	/*
		Return the number of bytes which are available for decryption
	 */
	int64_t bytes_to_decrypt(uint8_t*& buffer_ret) {
		if(!buffer)
			return -1;
		buffer_ret = buffer + decrypt_pointer;
		assert (decrypt_pointer>= 0);
		assert (decrypt_pointer<= map_len);
		return ((write_pointer - decrypt_pointer)/188)*188;
	}

	void advance_write_pointer(int64_t extra) {
		write_pointer+=extra;
		assert(write_pointer<=map_len);
	}

	void advance_read_pointer(int64_t extra) {
		read_pointer += extra;
		assert(read_pointer <= safe_read_len);
	}

	void advance_decrypt_pointer(int64_t extra) {
		assert(decrypt_pointer+extra <= write_pointer);
		decrypt_pointer+=extra;
		assert(decrypt_pointer<=map_len);
	}

	void discard_non_decrypted() {
		write_pointer = decrypt_pointer;
	}

	/*!
		Move the mmaped range of the file to allow for more data
		Grow the current file if needed
	 */
	int advance();


mmap_t(int64_t map_len_, bool readonly_)
	: readonly(readonly_),
		map_len(map_len_ -map_len_ % pagesize) {
	}

	EXPORT void unmap();

	/* map a new file
	 */
	bool init(int _fd, off_t _offset, off_t end_read_offset=-1);

	/*map a different segment of the current file to a new offset; change filesize as needed*/
	int move_map(off_t _offset);

	int grow_map(off_t end_read_offset);

	EXPORT void close();

	mmap_t& operator=(const mmap_t& other);
	mmap_t& operator=(mmap_t&& other);

	~mmap_t() {
		unmap();
		close();
	}
};
