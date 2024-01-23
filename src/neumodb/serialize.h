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

#include "encode.h"
//serialized size of a simple primitive type
template<typename T>
inline int serialized_size(const T& val);

//serialized size of a databuffer
template<typename data_t>
inline int serialized_size(const ss::databuffer_<data_t>& data);


//serialized size of a vector
template<typename data_t, int buffer_size>
inline int serialized_size(const ss::vector<data_t, buffer_size>& vec);

//serialized size of a string - case 1
inline int serialized_size(const ss::string_& str);

//serialized size of a string - case 2
template<int buffer_size>
inline int serialized_size(const ss::string<buffer_size>& str);

	//serialized size of a simple primitive type
template<>
inline int serialized_size(const bool& val)  {
	uint8_t val_{};
	return ss::bytebuffer_::append_raw_size(val_);
}

	//serialized size of a simple primitive type
template<typename T>
inline int serialized_size(const T& val)  {
	static_assert(std::is_fundamental<T>::value || std::is_enum<T>::value);
	return ss::bytebuffer_::append_raw_size(val);
}

template<>
inline int serialized_size(const std::monostate& val)  {
	return 0;
}

//serialized size of a bytebuffer
template<typename data_t>
inline int serialized_size(const ss::databuffer_<data_t>& data)  {
	if(std::is_fundamental<data_t>::value || std::is_enum<data_t>::value)
		return ss::bytebuffer_::append_raw_size(data.buffer(), data.size()) + sizeof(uint32_t);
	else {
		auto ret = sizeof(uint32_t);
		for(auto&x : data)
			ret+=serialized_size(x);
		return ret;
	}
}


template<>
inline int serialized_size(const ss::bytebuffer_& data)  {
	return ss::bytebuffer_::append_raw_size(data.buffer(), data.size()) + sizeof(uint32_t);
}

template<int buffer_size>
inline int serialized_size(const ss::bytebuffer<buffer_size>& data)  {
	return ss::bytebuffer_::append_raw_size(data.buffer(), data.size()) + sizeof(uint32_t);
}

//serialized size of a vector
template<typename data_t, int buffer_size>
inline int serialized_size(const ss::vector<data_t, buffer_size>& vec)  {
	return serialized_size((const ss::databuffer_<data_t>&) vec);
}

//serialized size of a string - case 1
inline int serialized_size(const ss::string_& str)  {
	uint32_t size = str.size() + 1;
	return ss::bytebuffer_::append_raw_size(size)
		+ ss::bytebuffer_::append_raw_size(str.buffer(), size);
}

//serialized size of a string - case 2
template<int buffer_size>
inline int serialized_size(const ss::string<buffer_size>& str)  {
	return serialized_size((const ss::string_&) str);
}



//serialisation of a simple primitive type
template<typename T>
inline void serialize(ss::bytebuffer_ &ser, const T& val);

//serialisation of a bytebuffer
template<typename data_t>
inline void serialize(ss::bytebuffer_ &ser, const ss::databuffer_<data_t>& data);


//serialisation of a bytebuffer
template<>
inline void serialize(ss::bytebuffer_ &ser, const ss::bytebuffer_& data);


//serialize a string - case 1
template<typename data_t, int buffer_size>
inline void serialize(ss::bytebuffer_ &ser, const ss::vector<data_t, buffer_size>& str);


//serialize a string - case 2
void serialize(ss::bytebuffer_ &ser, const ss::string_& str);


//serialize a string - case 3
template<int buffer_size>
void serialize(ss::bytebuffer_ &ser, const ss::string<buffer_size>& str);


//serialize  a vector
template<typename data_t, int buffer_size>
inline void serialize(ss::bytebuffer_ &ser, const ss::vector<data_t, buffer_size>& vec);


//serialisation of a simple primitive type
template<typename T>
inline void serialize(ss::bytebuffer_ &ser, const T& val)  {
	static_assert(std::is_fundamental<T>::value || std::is_enum<T>::value);
	encode_ascending(ser, val);
}

template<>
inline void serialize(ss::bytebuffer_ &ser, const std::monostate& val)  {
	//do nothing
}

//serialisation of a simple primitive type
template<>
inline void serialize(ss::bytebuffer_ &ser, const bool& val)  {
	uint8_t val_ = val;
	encode_ascending(ser, val_);
}

//serialisation of a bytebuffer
template<typename data_t>
inline void serialize(ss::bytebuffer_ &ser, const ss::databuffer_<data_t>& data)  {
#ifndef NDEBUG
	auto oldsize = ser.size();
#pragma unused(oldsize)
#endif
		//in the case of variable length structures, size is unknown at this point.
		//So we write "0" as a placeholder and save the offset, so that we can write later
		auto size_offset = ser.size();
		uint32_t size = 0;
		ser.append_raw(size); //clumsy way to allocate size
		for(auto&x : data)
			serialize(ser, x);
		assert(ser.size() - size_offset >= (int) sizeof(size));
		size = ser.size() - size_offset - sizeof(size);
		//store the final value of size
		auto ss = encode_ascending(size);
		memcpy(ser.buffer() + size_offset, (void*)&ss, sizeof(ss));
#ifndef NDEBUG
	assert(ser.size() - oldsize == serialized_size(data));
#endif
}


//serialisation of a bytebuffer
template<>
inline void serialize(ss::bytebuffer_ &ser, const ss::bytebuffer_& data)  {
	serialize(ser, (const ss::databuffer_<char>&) data);
}

//serialisation of a bytebuffer
template<int buffer_size>
inline void serialize(ss::bytebuffer_ &ser, const ss::bytebuffer<buffer_size>& data)  {
	serialize(ser, (const ss::databuffer_<char>&) data);
}


//serialize  a vector
template<typename data_t, int buffer_size>
inline void serialize(ss::bytebuffer_ &ser, const ss::vector<data_t, buffer_size>& vec)  {
	serialize(ser, (const ss::databuffer_<data_t>&) vec);
}

//serialize a string - case 1
inline void serialize(ss::bytebuffer_ &ser, const ss::string_& str)  {
	uint32_t size = str.size() +1;
	encode_ascending(ser, size);
	ser.append_raw(str.buffer(), size);
}

//serialize a string - case 2
template<int buffer_size>
inline void serialize(ss::bytebuffer_ &ser, const ss::string<buffer_size>& str)  {
	serialize(ser, (const ss::string_&) str);
}

#if 0
template<typename T>
inline void serialize(ss::databuffer_<char, false> &ser, const T&t) {
	serialize((ss::bytebuffer_&) ser, t);
}
#endif

template<>
inline int serialized_size<milliseconds_t>(const milliseconds_t& val)
{
	return serialized_size(val.ms);
}

template<>
inline void serialize<milliseconds_t>(ss::bytebuffer_ &ser, const milliseconds_t& val)  {
	return serialize(ser, val.ms);
}
