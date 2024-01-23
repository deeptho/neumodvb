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
#include <stdint.h>
#include <unistd.h>
#include "util/dtassert.h"


struct mmap_t {
	bool readonly = false;
	static const int pagesize;
	int fd{-1}; //file descriptor of currently mapped file

	/*
		currenly we map the range [offset, offset + map_len] in memory.
		This may extend beyond the end of the file. Units are bytes
	 */
	off_t offset{0}; /*file offset at which this mmap starts, within current file, in bytes*/
	int map_len{-1}; /* Current length of the mapped file area in bytes*/

	uint8_t* buffer{nullptr}; //address at which this mapping is mapped
	int safe_read_len{-1}; /*number of bytes which are safe to read in the mapped part of the
													 currently mapped file.
													 So the safe read range starts at byte offset in the file and ends
													 at byte offset + safe_read_len -1 in the file.

													 Even if data after this range is also mapped, that data may not be accessed
														(not even read) because it is space in which FUTURE writes may occur, but this space
														may also be truncated away by the writers.
												*/
	int read_pointer{0}; /*points to the first byte in te mapped file region we will read next, which corresponds to byte
												 offset + read_pointer of the file.
												 valid range for read_pointer: [0, safe_read_len]
											 */
	int write_pointer{0}; /*points to the first byte in te mapped file region we will write next,
													which corresponds to byte offset + write_pointer of the file.
													valid range for write_pointer: [0, map_len]
												*/
	int decrypt_pointer{0}; /*points to the first byte in te mapped file region we will decrypt next,
														which corresponds to byte offset + decrypt_pointer of the file.
														valid range for decrypt_pointer: [0, write_pointer]
													*/

	void init();

	/*!
		Returns the total number of bytes decrypted so far
	 */
	off_t get_decrypted_filesize() {
		return offset+decrypt_pointer;
	}

	int get_write_buffer(uint8_t*& buffer_ret) {
		if(!buffer)
			return -1;
		buffer_ret=buffer+write_pointer;
		assert (write_pointer>= 0);
		assert (write_pointer<= map_len);
		return map_len - write_pointer;
	}

	int get_read_buffer(uint8_t*& buffer_ret) {
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
	int bytes_to_decrypt(uint8_t*& buffer_ret) {
		if(!buffer)
			return -1;
		buffer_ret = buffer + decrypt_pointer;
		assert (decrypt_pointer>= 0);
		assert (decrypt_pointer<= map_len);
		return ((write_pointer - decrypt_pointer)/188)*188;
	}

	void advance_write_pointer(int extra) {
		write_pointer+=extra;
		assert(write_pointer<=map_len);
	}

	void advance_read_pointer(int extra) {
		read_pointer += extra;
		assert(read_pointer <= safe_read_len);
	}

	void advance_decrypt_pointer(int extra) {
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


mmap_t(int map_len_, bool readonly_)
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
