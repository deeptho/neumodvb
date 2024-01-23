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
#include <limits>
#include <algorithm>
#include <vector>

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

enum class opentv_table_type_t {
	SKY_UK,
	SKY_IT,
	SKY_NZ,
};

class huff_entry_t {
public:
	uint32_t code;
	uint8_t num_bits;
	uint8_t data_len;
	const char* data;

	constexpr huff_entry_t() : code(0), num_bits(0), data_len(0), data("") {}


	constexpr huff_entry_t(uint32_t code, uint8_t num_bits, uint8_t data_len, const char* data) :
		code(code), num_bits(num_bits), data_len(data_len), data(data) {}

	inline bool operator<(const huff_entry_t&other) const {
		return code < other.code;
	}

	inline bool operator == (const huff_entry_t&other) const {
		return code == other.code;
	}

};

class multi_huff_entry_t {
public:
	uint8_t num_bits; //total number of code bits corresponding to the string in data
	uint8_t data_len; /*total length of the string in data (without the terminating zero)*/
	uint16_t first_piece_idx; /*where to find the first huffman code*/
	union {const char i[12];
		const char* p;
	} data; /*string of output data corresponding to the first numbits bits,
						except if the string is too long to store. In that case
						a zero-lebgth string is stored*/
	inline const char* get_data() const {
		return likely(data_len < sizeof(data.i)) ? data.i: data.p;
	}
};

struct huff_data_t {
	std::vector<huff_entry_t> entries;
	std::vector<uint32_t> codes;
	int index_of_entry(const huff_entry_t& e) const {
		return &e - &entries[0];
	}

	__attribute__((optnone))
	huff_data_t(std::initializer_list<huff_entry_t> entries_)
		: entries(entries_) {
		int16_t j{0};
		int last_idx{-1};
		codes.resize(entries.size());
		for (const auto& e: entries) {
			codes[j] = e.code;
			++j;
		}
		entries.push_back({0xffffffff, 0, 0, (const char*)nullptr}); //avoids the need for some bounds checking
	}

	inline const huff_entry_t& decode_piece(uint32_t code) const;
};

struct bit_buffer_t {
	const uint8_t* data;
	int num_bytes;
	int num_bytes_left; //total number of bytes in data that have not yet been moved into code
	int num_bits_left_in_code; /*total number of valid bits in code; we always
															 try keep this in [32, 64], unless at the end of data processing
														 */
	uint64_t code;  /*most significant bit is always the next bit to process*/
	bool error{false};

//	__attribute__((optnone))
	bit_buffer_t(const uint8_t* data, int num_bytes)
		: data(data)
		, num_bytes(num_bytes)
		, num_bytes_left (std::max(num_bytes-8,0))
		, num_bits_left_in_code(std::min(64, num_bytes*8))
		, code(likely(num_bytes>=8) ? htobe64(*(uint64_t*) &data[0]) : 0)
		{
			if(unlikely(num_bytes<8)) {
				int shift = (8-1)*8;
				for(int i=0; i < num_bytes; ++i, shift -= 8)
					code |= ((uint64_t)data[i]) << shift;
			}
		}

	/*
		remove the first n bits from the buffer, and move an equal number of bits
		into the buffer; if there are fewer available, then move as many as available bits
		into the buffer
	*/
//__attribute__((optnone))
	inline int discard_bits(int n) {
		if (unlikely(n > num_bits_left_in_code))
			return -1;
		n = std::min(n, num_bits_left_in_code);

		code <<= n; /* remove first num_bits  bits and move the remaning ones to
									 the front of code, leaving num_bits zero bits at the end of code
											 */
		num_bits_left_in_code -= n;

		if(num_bits_left_in_code >= 32)
			return num_bits_left_in_code;  /*the lower half of code contains 32 valid bits, except at the end of processin
																			 where the lower half of code contains all remaining valid bits
																		 */
		if(unlikely(num_bytes_left <= 0))
			 return num_bits_left_in_code;

		/*nb is number of bytes to shift into code to replace some of the bits removed from code.
			We aim for 4 bytes at a time
		*/
		int nb = std::min(4 , num_bytes_left);
		uint64_t update{0};
		if(unlikely(nb < 4)) {
			int shift = (4-1)*8;
			for(int i= num_bytes-num_bytes_left; i < num_bytes-num_bytes_left + nb; ++i, shift -= 8)
				update |= ((uint32_t)data[i]) << shift;
		} else
			update = htobe32(*(uint32_t*) &data[num_bytes - num_bytes_left]);

		num_bytes_left -= nb;
		//assert(num_bytes_left >=0);
		/*
			At this point, code is structured as follows (v is valid bit):
			v....v0...0 0...00...0
			|     |    + bit position nb*8
			|     +bit position 32
			+bit position num_bits_left_in_code

			update is structured as follows (x is a valid bit)
			0....00...0 x...x0...0
			|     |    + bit position nb*8
			|     +bit position 32
			+bit position num_bits_left_in_code
		*/

		code |= (update << (32 - num_bits_left_in_code)); /*all bits are now in the correct place.
																												In general, we have shifted in 4 new bytes.
																												and there will be at least 32 valid bits in
																												code, except at the end of processing
																											*/
		num_bits_left_in_code += 8*nb;

		return num_bits_left_in_code;
	}

/*
	return the next 32 bits to process. In case fewer are left, then some of the least
	significant bits will not have valid data (filled with zeros)
*/
	//__attribute__((optnone))
	inline uint32_t get_bits() {
		return code >> 32; //return the most signifant bits
	}
};


extern multi_huff_entry_t sky_uk_multi_data[65536];
extern multi_huff_entry_t sky_it_multi_data[65536];
extern multi_huff_entry_t sky_nz_multi_data[65536];

extern huff_data_t sky_uk_single_data;
extern huff_data_t sky_it_single_data;
extern huff_data_t sky_nz_single_data;

template <class ForwardIt, class T>
//__attribute__((optnone))
inline constexpr ForwardIt sb_lower_bound(
      ForwardIt first, ForwardIt last, const T& value) {
   int length = last - first;
	 auto saved=first;
   while (length > 1) {
		 int half = length / 2;
		 first += half *(first[half] < value);
		 length -= half;
   }
   return first + (length==1 && (first[0] < value));
}

//__attribute__((optnone))
inline const huff_entry_t& huff_data_t::decode_piece(uint32_t code) const {
	static huff_entry_t invalid{0x00000000, 0, 0, ""};
#ifdef NEW
	int idx = (uint8_t)(code >> key_shift);
	auto first = boundsx[idx];
	//assert(first+1 < entries.size());
	auto& firste = entries[first];
	auto& laste = entries[first+1];
	bool multiple_matches = !((firste.code ^ laste.code) & 0xff000000);
	if(! multiple_matches) [[likely]] {
		return firste;
	}

	auto last = (idx <255)  ? boundsx[idx+1] : entries.size();
#endif

	auto tofind = code;
#ifdef NEW
	auto found = sb_lower_bound(codes.begin()+first, codes.begin()+last, tofind);
#else
	auto found = sb_lower_bound(codes.begin(), codes.end(), tofind);
#endif

	//assert(found -entries.begin() >0);
	found -= (*found != tofind); //entry now points at tofind, or to a prefix of tofind
	auto &e = entries[found- codes.begin()];
	unsigned int mask=std::numeric_limits<uint32_t>::max();
	mask >>= e.num_bits;
	mask = ~mask;
	auto ret = (code&mask) == (e.code&mask);
	if(!ret)
		return invalid;
	return e;
}
