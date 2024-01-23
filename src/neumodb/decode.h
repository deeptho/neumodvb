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

#include <ieee754.h>
#include "stackstring.h"
#include "neumotime.h"
#include "metadata.h"

template<typename T>
inline int decode_ascending(T& x , const ss::bytebuffer_&ser, int offset);


template<> inline int decode_ascending<uint8_t>(uint8_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > (signed) ser.size())
		return -1;
	out = (uint8_t)ser.buffer()[offset];
	return ret;
}

template<> inline int decode_ascending<int8_t>(int8_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > (signed) ser.size())
		return -1;
	out = (int8_t) (((uint8_t)ser.buffer()[offset]) ^ 0x80);
	return ret;
}

template<> inline int decode_ascending<char>(char&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > (signed) ser.size())
		return -1;
	out = (int8_t) (((uint8_t)ser.buffer()[offset]) ^ 0x80);
	return ret;
}


template<> inline int decode_ascending<uint16_t>(uint16_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out = (p[0]<<8) | p[1];
	return ret;
}

template<> inline int decode_ascending<int16_t>(int16_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out = ((p[0]^ 0x80)<<8) | p[1];
	return ret;
}


template<> inline int decode_ascending<uint32_t>(uint32_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out = (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
	return ret;
}

template<> inline int decode_ascending<int32_t>(int32_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out = ((p[0]^0x80)<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
	return ret;
}

template<> inline int decode_ascending<uint64_t>(uint64_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out =
		(((uint64_t)p[0]) << 56) |
		(((uint64_t)p[1]) << 48) |
		(((uint64_t)p[2]) << 40) |
		(((uint64_t)p[3]) << 32) |
		(((uint64_t)p[4]) << 24) |
		(((uint64_t)p[5]) << 16) |
		(((uint64_t)p[6]) << 8)  |
		(((uint64_t)p[7]));
	return ret;
}


template<> inline int decode_ascending<int64_t>(int64_t&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();
	out =
		(((uint64_t)p[0]^0x80)  << 56) |
		(((uint64_t)p[1]) << 48) |
		(((uint64_t)p[2]) << 40) |
		(((uint64_t)p[3]) << 32) |
		(((uint64_t)p[4]) << 24) |
		(((uint64_t)p[5]) << 16) |
		(((uint64_t)p[6]) << 8)  |
		(((uint64_t)p[7]));
	return ret;
}


template<> inline int decode_ascending<float>(float&out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();

	static_assert(std::numeric_limits<float>::is_iec559);
	ieee754_float y;
	y.ieee.negative = 0x1 ^ (p[0]>>7);
	y.ieee.exponent = ((p[0]&0x7f)<<1) | (p[1]>>7);
	if(y.ieee.negative)
		y.ieee.exponent = (y.ieee.exponent&0xff) ^ 0xff;
	y.ieee.mantissa = ((p[1]&0x7f) << 16) | (p[2]<<8) | p[3];
	out = y.f;
	return ret;
}

template<> inline int  decode_ascending<double>(double& out, const ss::bytebuffer_ &ser, int offset) {
	int ret = offset + sizeof(out);
	if(ret > ser.size())
		return -1;
	auto *p = offset + (uint8_t*) ser.buffer();

	static_assert(std::numeric_limits<float>::is_iec559);
	ieee754_double y;
	y.ieee.negative = 0x1 ^ (p[0]>>7);
	y.ieee.exponent = ((p[0]&0x7f)<<4) | (p[1]>>4);
	y.ieee.mantissa0 = ((p[1]&0x0f)<<16) | (p[2]<<8) | p[3];
	y.ieee.mantissa1 = (p[4]<<24) | (p[5]<<16) | (p[6]<<8) | p[7];
	if(y.ieee.negative)
		y.ieee.exponent = (y.ieee.exponent&0x7ff) ^ 0x7ff;
	out = y.d;
	return ret;
}




//decoding of a class enum
template<typename T>
inline int decode_ascending(T& out, const ss::bytebuffer_ &ser, int offset) {
	return decode_ascending((typename std::underlying_type<T>::type&) out, ser, offset);
}

//decoding of a simple primitive type
template<>
inline int decode_ascending(bool& out, const ss::bytebuffer_ &ser, int offset)  {
	uint8_t x;
	int ret= decode_ascending(x, ser, offset);
	out = x;
	return ret;
}



//decode a string - case 1
inline int decode_ascending(ss::string_& out, const ss::bytebuffer_ &ser, int offset) {
	/*The following sorts according to content,
		and then according to size. note that key size is limited anyway in the database. If
		we do decounter a very long key, the sort order will be strange
	*/
	//we must scan for terminating zero byte
	for(;offset < ser.size(); ++offset)  {
		char x = ser[offset];
		if(x==0)
			return offset+1;
		out.push_back(x);
	}
	//no trailing zero byte found
	return -1;
}



//decode a string - case 2
template<int buffer_size>
inline int decode_ascending(ss::string<buffer_size>& out, const ss::bytebuffer_ &ser, int offset) {
	/*The following sorts according to content,
		and then according to size. note that key size is limited anyway in the database. If
		we do decounter a very long key, the sort order will be strange
	*/
	//we must scan for terminating zero byte
	for(;offset < ser.size(); ++offset)  {
		auto x = ser[offset];
		if(x==0)
			return offset+1;
		out.push_back(x);
	}
	//no trailing zero byte found
	return -1;
}

#if 0
template<int bytebuffer_size, typename T>
inline int decode_ascending(const T&out, const ss::bytebuffer<bytebuffer_size> &ser, int offset) {
	return decode_ascending(out, (ss::bytebuffer_&) ser, offset);
}
#endif
