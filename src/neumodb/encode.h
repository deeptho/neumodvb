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

/*
	Map fields of data structures onto binary data which is byte order independent and
	can be compared with memcmp

 */
#include <ieee754.h>
#include "stackstring.h"
#include "neumotime.h"
#include <array>
#include <variant>

inline auto encode_ascending(uint8_t x) {
	return std::array<uint8_t, sizeof(uint8_t)> { x };
}

inline auto encode_ascending(int8_t x) {
	return std::array<uint8_t, sizeof(uint8_t)> { (uint8_t) (x ^ 0x80)  };
}

inline auto encode_ascending(char x) {
	return std::array<uint8_t, sizeof(uint8_t)> { (uint8_t) (x ^ 0x80)  };
}

inline auto encode_ascending(uint16_t x) {
	return std::array<uint8_t, sizeof(uint16_t)> { (uint8_t) ((x>>8) & 0xff), (uint8_t) (x & 0xff)  };
}

inline auto encode_ascending(int16_t x) {
	return std::array<uint8_t, sizeof(uint16_t)> { (uint8_t) (((x>>8) & 0xff) ^ 0x80), (uint8_t) (x & 0xff)  };
}


inline auto encode_ascending(uint32_t x) {
	return std::array<uint8_t, sizeof(uint32_t)>
		{ (uint8_t) ((x>>24) & 0xff),
			 (uint8_t) ((x>>16) & 0xff),
			 (uint8_t) ((x>>8) & 0xff),
			 (uint8_t) (x & 0xff)  };
}

inline auto encode_ascending(int32_t x) {
	return std::array<uint8_t, sizeof(uint32_t)>
		{ (uint8_t) (((x>>24) & 0xff) ^ 0x80),
			 (uint8_t) ((x>>16) & 0xff),
			 (uint8_t) ((x>>8) & 0xff),
			 (uint8_t)  (x & 0xff) };
}


inline auto encode_ascending(uint64_t x) {
	return std::array<uint8_t, sizeof(uint64_t)>
		{ (uint8_t) ((x>>56) & 0xff),
			 (uint8_t) ((x>>48) & 0xff),
			 (uint8_t) ((x>>40) & 0xff),
			 (uint8_t) ((x>>32) & 0xff),
			 (uint8_t) ((x>>24) & 0xff),
			 (uint8_t) ((x>>16) & 0xff),
			 (uint8_t) ((x>>8) & 0xff),
			 (uint8_t) (x & 0xff)
			 };
}

inline auto encode_ascending(int64_t x) {
	return std::array<uint8_t, sizeof(uint64_t)>
		{ (uint8_t) (((x>>56) & 0xff) ^ 0x80),
			 (uint8_t) ((x>>48) & 0xff),
			 (uint8_t) ((x>>40) & 0xff),
			 (uint8_t) ((x>>32) & 0xff),
			 (uint8_t) ((x>>24) & 0xff),
			 (uint8_t) ((x>>16) & 0xff),
			 (uint8_t) ((x>>8) & 0xff),
			 (uint8_t) (x & 0xff)
			 };
}

inline auto encode_ascending(float x) {
	static_assert(std::numeric_limits<float>::is_iec559);
	ieee754_float y;
	y.f = x;
	uint8_t exponent = x<0 ? (y.ieee.exponent ^0xff) :  y.ieee.exponent ;
	return std::array<uint8_t, sizeof(float)>
	{ (uint8_t)
			(((y.ieee.negative ^ 0x1)<<7) |
			 (exponent>>1)),
			(uint8_t) (((exponent & 0x1) << 7) | (y.ieee.mantissa>>16)),
			(uint8_t) ((y.ieee.mantissa>>8) & 0xff),
			(uint8_t) (y.ieee.mantissa & 0xff)
			};

}

inline auto encode_ascending(double x) {
	static_assert(std::numeric_limits<double>::is_iec559);
	ieee754_double y;
	y.d = x;
	uint16_t exponent = x<0 ? (y.ieee.exponent ^0x7ff) :  y.ieee.exponent ;
	return std::array<uint8_t, sizeof(double)>
	{ (uint8_t) (((y.ieee.negative ^0x1) << 7) | (exponent>>4)), // exponent = 7 bit  4 bit; extract 7bit by right shifting by 4
				(uint8_t) (((exponent << 4 ) &0xff) | (y.ieee.mantissa0>>16)),
				(uint8_t) ((y.ieee.mantissa0 >> 8) & 0xff),
				(uint8_t) (y.ieee.mantissa0 & 0xff),
				(uint8_t) (y.ieee.mantissa1 >> 24),
				(uint8_t) ((y.ieee.mantissa1 >> 16) & 0xff),
				(uint8_t) ((y.ieee.mantissa1 >> 8) & 0xff),
				(uint8_t) (y.ieee.mantissa1 & 0xff)
	};
}

//encoding of a class enum
template<typename T>
requires(std::is_enum<T>::value)
inline auto encode_ascending(T x) {
	return encode_ascending((typename std::underlying_type<T>::type)x);
}

//encoding of a simple primitive type
template<typename T>
requires(std::is_fundamental<T>::value || std::is_enum<T>::value)
inline void encode_ascending(ss::bytebuffer_ &ser, const T& val)  {
	static_assert(std::is_fundamental<T>::value || std::is_enum<T>::value);
	auto xx = encode_ascending((T&)val);
	for (auto x: xx)
		ser.push_back(x);
}

inline void encode_ascending(ss::bytebuffer_ &ser, const std::monostate& val)  {
	// do nothing
}

//encoding of a simple primitive type
inline void encode_ascending(ss::bytebuffer_ &ser, const bool& val)  {
	uint8_t val_ = val;
	auto xx = encode_ascending(val_);
	for (auto x: xx)
		ser.push_back(x);
}


/*encoding of a bytebuffer is not useful in general as bytebuffer can contain zero bytes,
	which results in strange sorting.
	we need to keep this function for tempdata code
*/
inline void encode_ascending(ss::bytebuffer_ &ser, const ss::bytebuffer_& data) {
	ser.append_raw(data.buffer(), data.size()); //do not include the 0 byte
}

/*encoding of a bytebuffer is not useful in general as bytebuffer can contain zero bytes,
	which results in strange sorting.
	we need to keep this function for tempdata code
*/
template<int buffer_size>
	inline void encode_ascending(ss::bytebuffer_ &ser, const ss::bytebuffer<buffer_size>& data) {
		ser.append_raw(data.buffer(), data.size()); //do not include the 0 byte
}

template<typename T>
inline void encode_ascending(ss::bytebuffer_ &ser, const ss::databuffer_<T>& data) {
	ser.append_raw(data.buffer(), data.size()); //do not include the 0 byte
}

//encode a string - case 1
inline void encode_ascending(ss::bytebuffer_ &ser, const ss::string_& str) {
	/*The following sorts according to content,
		and then according to size. note that key size is limited anyway in the database. If
		we do encounter a very long key, the sort order will be strange
	*/
	ser.append_raw(str.buffer(), str.size()+1); //include the 0 byte
	//encode_ascending(ser, (uint16_t) str.size());
}



//encode a string - case 2
template<int buffer_size>
inline void encode_ascending(ss::bytebuffer_ &ser, const ss::string<buffer_size>& str) {
	/*The following sorts according to content,
		and then according to size. note that key size is limited anyway in the database. If
		we do encounter a very long key, the sort order will be strange
	*/
	ser.append_raw(str.buffer(), str.size()+1); //include the 0 byte
	//encode_ascending(ser, (uint16_t)  str.size());
}

inline void encode_ascending(ss::bytebuffer_ &ser, const milliseconds_t& val)  {
	return encode_ascending(ser, val.ms);
}
