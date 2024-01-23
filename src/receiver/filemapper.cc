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

#include "filemapper.h"
#include "util/logger.h"
#include "util/util.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

const int mmap_t::pagesize = sysconf(_SC_PAGESIZE);

/*!
	use other as a template, to create a non-mapped version
*/
mmap_t& mmap_t::operator=(const mmap_t& other) {
	/*
		To implement thsi correctly, we have to decide what to do with already created
		memory maps and opened file descriptors:
		one approach is to assume that the result of operator= is unmapped.
		Then safe_read_len, offset, map_len, buffer
		should remain at default values
	*/
	readonly = other.readonly;
	map_len = other.map_len;
	fd = -1;
	offset = 0;
	buffer = nullptr;
	read_pointer = 0;
	write_pointer = 0;
	decrypt_pointer = 0;
	return *this;
}

mmap_t& mmap_t::operator=(mmap_t&& other) {
	/*
		To implement thsi correctly, we have to decide what to do with already created
		memory maps and opened file descriptors:
		The approach is to steal all resouces
		steal all resources
	*/
	readonly = other.readonly;
	fd = other.fd;
	other.fd = -1;

	offset = other.offset;
	other.offset = 0;
	map_len = other.map_len;
	other.map_len = -1;

	buffer = other.buffer;
	other.buffer = nullptr;

	safe_read_len = other.safe_read_len;
	other.safe_read_len = -1;

	read_pointer = other.read_pointer;
	other.read_pointer = 0;

	write_pointer = other.write_pointer;
	other.write_pointer = 0;

	decrypt_pointer = other.decrypt_pointer;
	other.decrypt_pointer = 0;

	return *this;
}

/*!
	Map a different range of the file, resizing the file if needed
	the new map length will be the same as the old map length, so in effect the mapping
	will move forward by start bytes into the file.

	start must be a multiple of pagesize
*/
int mmap_t::move_map(off_t start) {
	assert(fd >= 0);
	assert(start >= 0);

	bool isfirst = !buffer;
	if (buffer)
		unmap();
	assert(map_len>0);
	offset = start;
	if (map_len % pagesize != 0) {
		dterrorf("map_len%%pagesize != 0 map_len={:d} pagesize={:d}", map_len, pagesize);
	}

	auto current_size = filesize_fd(fd);
	if (!readonly) {
		if (!isfirst) {
			auto new_size = (((start + map_len) + pagesize - 1) / pagesize) * pagesize; // always grow in units of map_len
			/*this is needed in case decryption is running behind.
				It ensures that the file always grows by a minimal amount
			*/
			while (new_size <= current_size) {
				new_size += map_len;
			}

			if (ftruncate(fd, new_size) < 0) {
				dterrorf("Error while truncating: {}", strerror(errno));
				return -1;
			}
			map_len = new_size - start;
			assert(map_len % pagesize == 0);
			assert(map_len>0);
		}
		assert(map_len>0);
	} else {
		map_len = current_size - start;
		if (map_len % pagesize != 0)
			map_len += pagesize - (map_len % pagesize);
		if (map_len % pagesize != 0) {
			dterrorf("map_len%%pagesize != 0 map_len={:d} current_size={:d} start={:d} pagesize={:d}", map_len, current_size, start,
							 pagesize);
		}

		assert(map_len > 0);
	}
	dtdebugf("MMAP {:d} {:d}", start, map_len);
	uint8_t* mem = (uint8_t*)mmap(NULL, map_len, readonly ? PROT_READ : (PROT_READ | PROT_WRITE), MAP_SHARED, fd, start);
	if (mem == (uint8_t*)-1) {
		dterrorf("Error in mmap: {}", strerror(errno));
		assert(0);
		return -1;
	}
	buffer = mem;

	return 1;
}

/*!
	Resize the mapped range to min(new_map_len, file_size) ir something smaller when the map size grows
	too long
	and/or sets the new end_read_offset
	Returns:
	1 if file was remapped (there was growth)
	0 if file was not remapped but safe_read_len increased
	-1 if nothing was done

	end_read_offset: number of bytes which can safely be read in this file according to caller
	this number is relative to the start of the file, not to the start of the mapped part

*/
int mmap_t::grow_map(off_t end_read_offset) {
	/*
		compute offset in currently mapped part of the file
		in case beginning of file is no longer mapped, this will be different from end_read_offset
	*/
	auto new_safe_read_len = end_read_offset - offset;

	// first see if we can simply get more data by increasing safe_read_len:
	assert(new_safe_read_len >= 0);

	if (new_safe_read_len <= safe_read_len)
		return -1;

	if (new_safe_read_len < map_len) {
		safe_read_len = new_safe_read_len; // we still have enough
		return 0;
	}

	// how many pages should minimally be mapped to be able to read until new_safe_read_len?
	auto min_map_len = new_safe_read_len - new_safe_read_len % pagesize;
	if (min_map_len < new_safe_read_len)
		min_map_len += pagesize;
	assert(min_map_len >= map_len); // otherwise we should have returned earlier

	/*we will need to map additional pages, but we may as well map more than we
		need to avoid making many small remaps. As long as we don;t read from any non-existing
		bytes, this will be no problem
	*/
	auto new_map_len = std::max(min_map_len, 128 * 1024 * 1024L + map_len); // grow by 128 MByte

	if (new_map_len % pagesize != 0)
		new_map_len += pagesize - (new_map_len % pagesize);

	assert(buffer);

	void* mem = mremap(buffer, map_len, new_map_len, MREMAP_MAYMOVE);
	dtdebugf("MEMREMAP: map_len = {:d} -> {:d} buffer={:p} -> {:p}", map_len, new_map_len,
					 fmt::ptr(buffer), fmt::ptr(mem));
	if (mem == (void*)-1) {
		dterrorf("Error in mremap: {}", strerror(errno));
		return -1;
	}
	buffer = (uint8_t*)mem;
	assert(new_safe_read_len >= 0);
	assert(new_safe_read_len > safe_read_len);
	safe_read_len = new_safe_read_len;
	assert(safe_read_len <= new_map_len);
	map_len = new_map_len;
	assert(new_map_len % pagesize == 0);
	return 1;
}

/*
	map a different open file into memory
	start_offset is the first byte where reading/writing//... will start
	This is w.r.t. start of tuning
*/
bool mmap_t::init(int fd_, off_t start_offset, off_t end_read_offset) {
	unmap();
	if (fd != fd_) {
		close();
		fd = fd_;
	}
	auto page_offset = (start_offset / pagesize) * pagesize;

	safe_read_len = -1;
	auto ret = move_map(page_offset) > 0;
	if (!ret) {
		dterrorf("move_map failed fd={:d} fd_={:d}", fd, fd_);
	}
	assert(offset == page_offset);
	if (readonly) {
		assert(end_read_offset >= 0);
		read_pointer = start_offset - page_offset;
		safe_read_len = std::min((off_t)map_len, (off_t)(end_read_offset - page_offset));
		assert(safe_read_len >= 0);
		write_pointer = 0;
		decrypt_pointer = 0;
	} else {
		write_pointer = start_offset - page_offset;
		decrypt_pointer = write_pointer;
		read_pointer = 0;
		safe_read_len = 0;
	}
	return ret;
}

void mmap_t::unmap() {
	if (!buffer)
		return;
	dtdebugf("UNMAP: {:p} {:d}", fmt::ptr(buffer), map_len);
	if (buffer && munmap(buffer, map_len) < 0) {
		dterrorf("Error while unmapping: {}", strerror(errno));
	}
	offset = -1;
	buffer = nullptr;
}

void mmap_t::close() {
	while (fd >= 0 && ::close(fd) < 0) {
		if (errno != EINTR) {
			dterrorf("Error closing file: {:s}", strerror(errno));
			break;
		}
	}
	fd = -1;
}

/*!
	grow the mmaped file by adding (approximately) map_len to its size
	Then move the mmap region  forward over the file as much as possible
*/
int mmap_t::advance() {
	// decrypt_pointer must be within the mapped area
	auto safe_to_discard = readonly ? read_pointer : std::min(decrypt_pointer, write_pointer);
	off_t extra = (safe_to_discard / pagesize) * pagesize;
	int new_decrypt_pointer = decrypt_pointer - extra;
	int new_write_pointer = write_pointer - extra;
	int new_read_pointer = read_pointer - extra;
	int new_safe_read_len = safe_read_len - extra;
	assert(!readonly || new_read_pointer >= 0);
	assert(!readonly || new_safe_read_len >= 0);
	assert(readonly || new_write_pointer >= 0);
	assert(readonly || new_decrypt_pointer >= 0);
	auto ret = move_map(offset + extra);
	if (readonly) {
		assert(new_safe_read_len >= 0);
		safe_read_len = new_safe_read_len;
		read_pointer = new_read_pointer;
	} else {
		write_pointer = new_write_pointer;
		decrypt_pointer = new_decrypt_pointer;
	}
	return ret;
}

void mmap_t::init() {
	if (buffer)
		unmap();
	*this = mmap_t(map_len, readonly);
}
