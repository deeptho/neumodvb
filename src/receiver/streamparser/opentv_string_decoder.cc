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

#include "opentv_string_decoder.h"
#include "opentv_huffman.h"

#include "stackstring.h"
#include <vector>
#include <endian.h>
void tst();


static inline auto& get_table(opentv_table_type_t t) {
	switch (t) {
	default:
	case opentv_table_type_t::SKY_UK:
		return sky_uk_single_data;
	case opentv_table_type_t::SKY_IT:
		return sky_it_single_data;
	case opentv_table_type_t::SKY_NZ:
		return sky_nz_single_data;
	}
}


/*
	data[dat_len] is a byte buffer of bits to be processed by a huff man encoder. Within a byte
	the first bit to be processed is the msb (1<<7) and the last one the lst (1<<0).

	The code below groups bytes into integers. However, the x86_64 little endian storage order
	has the (in this case annoying property) that the least significant byte is stored at the lowest
	memry address, i.e., treating buf[0..3] as one integer leads the first  byte to be processed to the
	the least siugnificant byte, whereas the first bit to be processed in that byte is the most significant
	bit.

	Below the integer "code" is therefore best thought of as a Big Endian integer. In that case bits and bytes
	are always consumed by the coder in order of decreasing significance.

*/


#if 0
//__attribute__((optnone))
INLINE multi_huff_entry_t* tstxxx(uint32_t code, int num_bits)
{
//	multi_huff_entry_t* pres1 =nullptr;
	int n = 1<<(16-num_bits);
	uint16_t base = (code >> 16) & ~(n -1);
	n+= base;
	for(int i=base; i < n; ++i) {
		auto pres = &sky_uk_data_new[i];
		if(likely(pres->num_bits <= num_bits && pres->num_bits > 0))
			return pres;
	}
	return nullptr;
}
#endif

//__attribute__((optnone)) //12 us /call
bool opentv_decode_string(ss::string_& ret, unsigned char* data, unsigned int n, opentv_table_type_t t) {
	//tst();
	unsigned char* p = data; // current input byte
	int pbits = 8;					 // number of bits still present in p[0]
	unsigned char* pend = p + n;
#if 0
	uint32_t end_of_string_code{0xe36f0000};
#endif
	//uint32_t end_of_string_code{0x00006fe3};
	auto bb = bit_buffer_t(data, n
#if 0
												 , end_of_string_code
#endif
		);
	//auto lastbb=bb;
	auto done= bb.discard_bits(2) <=0;
	while (!done) {
		static_assert(sizeof(sky_uk_multi_data)/sizeof(sky_uk_multi_data[0])==65536);
		uint32_t code = bb.get_bits();
		uint16_t prefix = (code>>16);
		multi_huff_entry_t* pres=nullptr;
		pres = &sky_uk_multi_data[code>>16];
		/*At end of data the trailing bits are garbage and we may be pointing to an entry
			that has correct string data, follwed by incorrect data determined by the garbage.
			In this case, pres->first_piece_idx points to an entry which is guaranteed to
			only have string data for the non garbage bits*/

		if(unlikely(pres->num_bits==0)) {
			auto& table = get_table(t);
			auto& res = table.decode_piece(code);
			auto r = bb.discard_bits(res.num_bits);
			if(likely(r>=0))
				ret.append(res.data, res.data_len);
			done = r<=0;
			continue;
		}
		if (unlikely(bb.num_bits_left_in_code < pres->num_bits))
			pres = &sky_uk_multi_data[pres->first_piece_idx];
		auto r = bb.discard_bits(pres->num_bits);
		if(likely(r>=0))
			ret.append(pres->get_data(), pres->data_len);
		done = r<=0;
	}
	if(!strcmp(ret.buffer() ,"Diary of a Wimpy Kid: The Long.")) {
		ss::string<128> tst;
		static int called{0};
		if(!called) {
			called=1;
			opentv_decode_string(tst, data, n, t);
			called=0;
		}
	}
	return true;
}


__attribute__((optnone))
std::tuple<int, int> opentv_decode_stringxxx(ss::string_& ret, unsigned char* data, unsigned int n, auto&table,
																						 int num_bits_limit) {
	int num_bits_consumed{0};
	unsigned char* p = data; // current input byte
	int pbits = 8;					 // number of bits still present in p[0]
	unsigned char* pend = p + n;
	unsigned int code = 0;
#if 0
	uint32_t end_of_string_code{0};
#endif
	auto bb = bit_buffer_t(data, n
#if 0
												 , end_of_string_code
#endif
		);
	//auto done= bb.discard_bits(2) <=0;
	bool done{false};
	int first_num_bits{0};
	while (!done) {
		uint32_t code = bb.get_bits();
		auto& res = table.decode_piece(code);
		if(!first_num_bits)
			first_num_bits = res.num_bits;
		if (!res.num_bits)
			return {num_bits_consumed, first_num_bits}; // something is wrong because we don't make progress

		if(res.num_bits + num_bits_consumed > num_bits_limit)
			break;
		ret.append(res.data, res.data_len);
		done = bb.discard_bits(res.num_bits) <= 0;
		num_bits_consumed += res.num_bits;
	}
	return {num_bits_consumed, first_num_bits};
}

static void fprintf_escaped(FILE*fp, ss::string_& s)
{
	for(int i=0; i < s.size();++i) {
		if (s[i]=='"')
			fprintf(fp, "\\\"");
		else if (s[i]=='\\')
			fprintf(fp, "\\\\");
		else
			fprintf(fp, "%c", s[i]);
	}
}

#if 0
__attribute__((optnone))
void tst1()
{
	static int ttt{0};
	ttt++;
	char fname[128];
	sprintf(fname, "/tmp/codeb%d.c", ttt);
	uint16_t xcode;
	int max_len=0;
	int max_num_codes=0;
	auto t = opentv_table_type_t::SKY_UK;
	auto& table = get_table(t);
	const int key_shift=8;
	FILE *fp=fopen(fname, "w");
	fprintf(fp, "#include \"opentv_string_decoder.h\"\n");
	fprintf(fp, "#include \"opentv_huffman.h\"\n");
	fprintf(fp, "huff_data_t sky_uk_single_data={{:d}, {\n", key_shift);

	for(auto& e: table.entries) {
		if(e.num_bits<=16)
			continue;
		ss::string<128> out(e.data, e.data_len);
		fprintf(fp, "{0x%08x, {:d}, {:d}, \"", e.code, e.num_bits, out.size());
		fprintf_escaped(fp, out);
		fprintf(fp, "\"},\n");
	}
	fprintf(fp, "}};\n");
	fclose(fp);
}
#endif

#if 0
__attribute__((optnone))
void tst()
{
	static int ttt{0};
	ttt++;
	char fname[128];
	sprintf(fname, "/tmp/code{:d}.c", ttt);
	uint16_t xcode;
	int max_len=0;
	int max_num_codes=0;
	auto t = opentv_table_type_t::SKY_UK;
	auto& table = get_table(t);
	FILE *fp=fopen(fname, "w");
#if 0
	uint32_t end_of_string_code{0xe36f0000};
#else
	const uint16_t end_of_string_code{0xe36f};
#endif
	fprintf(fp, "#include \"opentv_string_decoder.h\"\n");
	fprintf(fp, "#include \"opentv_huffman.h\"\n");
	fprintf(fp, "multi_huff_entry_t sky_uk_multi_data[65536]={\n");
	for(xcode=0;; ++xcode) {
		uint64_t code = (((xcode&0xff)<<8) | (xcode>>8));
		//code=0xc9ff7217;
		//code=0xb86a80e8; 		code=0x000080e8;
		ss::string<256> out;
		auto [num_bits, first_num_bits] =
			opentv_decode_stringxxx(out, (unsigned char*)&code, 8, table, 16);
		uint16_t prefix_code =
			((xcode >> (16- first_num_bits))<<(16-first_num_bits)) |
			(end_of_string_code >> first_num_bits);
		if(out.size() >= sizeof(multi_huff_entry_t::data.i)) {
			fprintf(fp, "{{:d}, {:d}, 0x%04x, {.p=\"", num_bits, out.size(), prefix_code);
			fprintf_escaped(fp, out);
			fprintf(fp, "\"}},\n");
		} else {
			fprintf(fp, "{{:d}, {:d}, 0x%04x, \"", num_bits, out.size(), prefix_code);
			fprintf_escaped(fp, out);
			fprintf(fp, "\"},\n");
		}
		if(xcode==0xffff)
			break;
		max_len =std::max(out.size(), max_len);
	}
	fprintf(fp, "};\n");
	printf("max len={:d}\n", max_len);
	fclose(fp);
	tst1();
}
#endif
