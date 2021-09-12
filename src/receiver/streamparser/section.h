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


#pragma once
#include <cstdlib>
#include "mpeg.h"
#include "substream.h"
#include "neumodb/chdb/chdb_extra.h"


namespace dtdemux {


	struct stored_section_t {
		const ss::bytebuffer<4096>& payload; //4096 = maximum size of any section
		int bytes_read{0};
		bool error{false};
		bool throw_on_error{false};
		bool has_error() const {
			return error;
		}


		void throw_bad_data() {
			error = true;
			if (throw_on_error)
				throw bad_data_exception();
		}

		stored_section_t(const ss::bytebuffer<4096>& payload_) :
			payload(payload_) {}

		int available() const {
			return payload.size() - bytes_read;
		}

		const uint8_t* current_pointer(int size)  {
			if(available() < size) {
				throw_bad_data();
				return nullptr;
			}
			auto ret = payload.buffer() + bytes_read;
			bytes_read += size;
			return ret;
		}

		int get_buffer(uint8_t* buffer, int toread) {
			if(toread > available()) {
				throw_bad_data();
				return -1;
			}
			memcpy(buffer, payload.buffer() + bytes_read, toread);
			bytes_read += toread;
			return toread;
		}

		template<typename T> T get() {
			typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
			get_buffer((uint8_t*)::std::addressof(storage), sizeof(T));
			return net_to_native((const T&) storage);
		}

		template<typename T> T get(const descriptor_t& desc, uint16_t stream_pid);

		//T is the structure to fill, U defines which substructure to fill
		//returns -1 on error
		template<typename U, typename T> int get_fields(T& ret);
		template<typename U, typename T> int get_fields(T& ret, const descriptor_t&desc);
		template<typename U, typename T, typename E> int get_fields(T& ret, const descriptor_t&desc, E extra);
		template<typename U, int size> int get_fields(ss::string<size>& ret) {
			return get_fields<U, ss::string_>(ret);
		}
		template<typename U, int size> int get_fields(ss::string<size>& ret, const descriptor_t&desc) {
			return get_fields<U, ss::string_>(ret, desc);
		}

		template<typename U, typename T, int size> int get_vector_fields(ss::vector<T,size>& ret, const descriptor_t&desc);


		int skip(int toskip) {
			if(toskip > available()) {
				throw_bad_data();
				return -1;
			}
			bytes_read += toskip;
				return 1;
		}


	};

};
