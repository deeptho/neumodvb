/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "serialize.h"
#include "decode.h"

//deserialization of a simple primitive type
template<typename T>
inline int deserialize(const ss::bytebuffer_ &ser, T& val, int offset=0);

//deserialization of a simple integer, but with possibly different word length
template<typename T>
inline std::enable_if_t<std::is_integral_v<T>, int>
deserialize_int(const ss::bytebuffer_ &ser, T& val, int foreign_type_id, int offset=0);

template<typename T>
inline std::enable_if_t<!std::is_integral_v<T>, int>
deserialize_int(const ss::bytebuffer_ &ser, T& val, int foreign_type_id, int offset=0)
{
	assert(0);
	return 0;
}

//deserialisation of a databuffer
template<typename data_t>
inline int deserialize(const ss::bytebuffer_ &ser, ss::databuffer_<data_t>& data, int offset=0);


//deserialize a a vector - case 1
template<typename data_t, int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::vector<data_t, buffer_size>& str, int offset=0);

//deserialize a a vector - case 2
template<typename data_t, int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::vector_<data_t>& str, int offset=0);

//deserialize a string - case 2
inline int deserialize(const ss::bytebuffer_ & ser, ss::string_& str, int offset=0);

//deserialize a string - case 3
template<int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::string<buffer_size>& str, int offset=0);

//deserialization of a simple primitive type
template<typename T>
inline int deserialize(const ss::bytebuffer_ &ser, T& val, int offset)  {
	static_assert(std::is_fundamental<T>::value || std::is_enum<T>::value);
	return decode_ascending(val, ser, offset);
}


//deserialization of a simple primitive type with different word length
template<typename T>
inline
std::enable_if_t<std::is_integral_v<T>, int>
deserialize_int(const ss::bytebuffer_ &ser, T& val, int foreign_type_id, int offset)  {
	int ret = -1;
	using namespace data_types;
	switch(foreign_type_id & ~data_types::enumeration & ~data_types::vector) {
	case uint8:
	case boolean: {
		uint8_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case int8: {
		int8_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case uint16: {
		uint16_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case int16: {
		int16_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case uint32: {
		uint32_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case int32: {
		int32_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case uint64: {
		uint64_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	case int64: {
		int64_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	default:
		assert (0);
	}

	return ret;
}

//deserialization of a simple primitive type with different word length
inline float32_t
deserialize_float(const ss::bytebuffer_ &ser, float32_t& val, int foreign_type_id, int offset)  {
	int ret = -1;
	using namespace data_types;
	switch(foreign_type_id & ~data_types::enumeration & ~data_types::vector) {
	case float32: {
		float32_t val_;
		ret = decode_ascending(val_, ser, offset);
		val = val_;
		return ret;
	}
	default:
		assert (0);
	}

	return ret;
}



//deserialisation of a databuffer
template<typename data_t>
inline int deserialize(const ss::bytebuffer_ &ser, ss::databuffer_<data_t>& data, int offset)
{
	using namespace ss;
	uint32_t size;
#ifndef NDEBUG
	auto oldsize = offset;
#endif
	assert(data.size()==0);

	offset = deserialize(ser, size, offset);
	if(offset< 0)
		return -1;
	auto limit = offset + (signed) size;
	for (int i=0; offset<limit; ++i) {
		offset = deserialize(ser, data[i], offset);
		if(offset< 0)
			return -1;
	}
#ifndef NDEBUG
	auto test_size = serialized_size(data);
	assert(offset - oldsize == test_size);
#endif
	return offset; // + size * array_item_size<data_t>();
}


//deserialisation of a bytebuffer
template<>
inline int deserialize(const ss::bytebuffer_ &ser, ss::bytebuffer_& data, int offset)
{
	return deserialize(ser, (ss::databuffer_<char>&) data, offset);
}

//deserialisation of a bytebuffer
template<int buffer_size>
inline int deserialize(const ss::bytebuffer_ &ser, ss::bytebuffer<buffer_size>& data, int offset)
{
	return deserialize(ser, (ss::databuffer_<char>&) data, offset);
}

//deserialize a vector - case 1
template<typename data_t, int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::vector<data_t, buffer_size>& vec, int offset)
{
	return deserialize(ser, (ss::databuffer_<data_t>&) vec, offset);
}

//deserialize a vector - case 2
template<typename data_t, int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::vector_<data_t>& vec, int offset)
{
	return deserialize(ser, (ss::databuffer_<data_t>&) vec, offset);
}

//deserialize a string - case 2
/*
	@todo: for all this code: implement robustness by checking buffer limits
 */
inline int deserialize(const ss::bytebuffer_ & ser, ss::string_& str, int offset)
{
	uint32_t size;
	offset = deserialize(ser, size, offset);
	if(offset< 0)
		return -1;
	if(size>0)
		size -= 1; //stored size includes traling zero
	if(size>0) {
		if(offset + (signed)size > ser.size()) {
			//printf("Insufficient data to deserialise\n");
			return -1;
		}
		str.copy_raw((const char*)ser.buffer()+offset, size);
	}
	return offset + size + 1;
}


template<int buffer_size>
inline int deserialize(const ss::bytebuffer_ & ser, ss::string<buffer_size>& str, int offset)
{
	return deserialize(ser, (ss::string_&) str, offset);
}


template<>
inline int deserialize<milliseconds_t>(const ss::bytebuffer_ &ser, milliseconds_t& val, int offset)  {
	return deserialize(ser, val.ms, offset);
}
