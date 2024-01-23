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

//older data
extern huff_data_t sky_uk_data;
extern huff_data_t sky_it_data;
extern huff_data_t sky_nz_data;



static inline auto& get_table(opentv_table_type_t t) {
	switch (t) {
	default:
	case opentv_table_type_t::SKY_UK:
		return sky_uk_data;
	case opentv_table_type_t::SKY_IT:
		return sky_it_data;
	case opentv_table_type_t::SKY_NZ:
		return sky_nz_data;
	}
}

/*
	Decode a string starting at bit 0 (as opposed to in opentv_string_decoder, where parsing starts
	at bit 2
 */
__attribute__((optnone))
std::tuple<int, int> opentv_decode_string(ss::string_& ret, unsigned char* data, unsigned int n, auto&table,
																						 int num_bits_limit) {
	int num_bits_consumed{0};
	unsigned char* p = data; // current input byte
	int pbits = 8;					 // number of bits still present in p[0]
	unsigned char* pend = p + n;
	unsigned int code = 0;
	auto bb = bit_buffer_t(data, n);
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

/*
	Print a c string possibly containing backslasg or escaped quotes and such
*/
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

/*
	Create a subset of huffman codes, all of which are longer than 16 bits
 */
void create_single_opentv(FILE* fp, opentv_table_type_t t, const char*name)
{

	uint16_t xcode;
	int max_len=0;
	int max_num_codes=0;
	auto& table = get_table(t);
	fprintf(fp, "\n");
	fprintf(fp, "huff_data_t %s_single_data={\n", name);

	for(auto& e: table.entries) {
		if(e.num_bits<=16)
			continue;
		ss::string<128> out(e.data, e.data_len);
		fprintf(fp, "{0x%08x, %d, %d, \"", e.code, e.num_bits, out.size());
		fprintf_escaped(fp, out);
		fprintf(fp, "\"},\n");
	}
	fprintf(fp, "};\n");
}


void create_multi_opentv(FILE* fp, opentv_table_type_t t, const char* name)
{
	static int ttt{0};
	ttt++;
	uint16_t xcode;
	int max_len=0;
	int max_num_codes=0;
	auto& table = get_table(t);
	const uint16_t end_of_string_code{0xe36f};
	fprintf(fp, "\n");
	fprintf(fp, "multi_huff_entry_t %s_multi_data[65536]={\n", name);
	for(xcode=0;; ++xcode) {
		uint64_t code = (((xcode&0xff)<<8) | (xcode>>8));
		ss::string<256> out;
		auto [num_bits, first_num_bits] =
			opentv_decode_string(out, (unsigned char*)&code, 8, table, 16);
		uint16_t prefix_code =
			((xcode >> (16- first_num_bits))<<(16-first_num_bits)) |
			(end_of_string_code >> first_num_bits);
		if(out.size() >= sizeof(multi_huff_entry_t::data.i)) {
			fprintf(fp, "{%d, %d, 0x%04x, {.p=\"", num_bits, out.size(), prefix_code);
			fprintf_escaped(fp, out);
			fprintf(fp, "\"}},\n");
		} else {
			fprintf(fp, "{%d, %d, 0x%04x, \"", num_bits, out.size(), prefix_code);
			fprintf_escaped(fp, out);
			fprintf(fp, "\"},\n");
		}
		if(xcode==0xffff)
			break;
		max_len =std::max(out.size(), max_len);
	}
	fprintf(fp, "};\n");
}


int main(int argc, char**argv)
{
	char fname[128];
	sprintf(fname, "/tmp/huffman_opentv_multi.cc");
	FILE *fp=fopen(fname, "w");
	fprintf(fp, "#include \"opentv_huffman.h\"\n\n");

	create_multi_opentv(fp, opentv_table_type_t::SKY_UK, "sky_uk");
	create_multi_opentv(fp, opentv_table_type_t::SKY_IT, "sky_it");
	create_multi_opentv(fp, opentv_table_type_t::SKY_NZ, "sky_nz");

	fclose(fp);

	sprintf(fname, "/tmp/huffman_opentv_single.cc");
	fp=fopen(fname, "w");
	fprintf(fp, "#include \"opentv_huffman.h\"\n\n");

	create_single_opentv(fp, opentv_table_type_t::SKY_UK, "sky_uk");
	create_single_opentv(fp, opentv_table_type_t::SKY_IT, "sky_it");
	create_single_opentv(fp, opentv_table_type_t::SKY_NZ, "sky_nz");
	fclose(fp);
}
