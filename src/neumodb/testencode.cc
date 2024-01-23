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

#include "deserialize.h"
#include "serialize.h"
#include <map>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <map>
#include <vector>

template <typename T> void testit(std::vector<T> xx) {

	for (auto x : xx) {
		bytebuffer<128> ser;
		encode_ascending(ser, x);
		auto y = x;
		int index = 0;
		index = decode_ascending(y, ser, index);
		ieee754_double fx;
		ieee754_double fy;
		fx.d = x;
		fy.d = y;
		assert(index == sizeof(x));
		assert(x == y);
		// printf("x=%4d: ",x);
	}

	for (auto i : xx) {
		bytebuffer<128> seri;
		encode_ascending(seri, i);
		for (auto j : xx) {
			bytebuffer<32> serj;
			encode_ascending(serj, j);
			assert(seri.size() == serj.size());
			if (i < j) {
				ieee754_double fi;
				ieee754_double fj;
				fi.d = i;
				fj.d = j;
				assert(memcmp(seri.buffer(), serj.buffer(), seri.size()) < 0);
			}
		}
	}
}

int main(int argc, char** argv) {
	ss::string<32> x{"testpesttestpestabcde"};
	ss::string_ y;

	bytebuffer<128> ser;
	encode_ascending(ser, x);
	int index = 0;
	index = decode_ascending(y, ser, index);
	assert(index == (int)strlen(x.c_str()) + 1);
	assert(x == y);

	testit(std::vector<int16_t>{-1, 32767, -32767, 1, 2, 258, -5});
	testit(std::vector<uint16_t>{65535, 32767, 32768, 0, 1, 2, 258, 5});

	testit(std::vector<int32_t>{-1, 2147483647, -2147483648, 32767, -32767, 0, 1, 2, 258, -5});
	testit(std::vector<uint32_t>{4294967295, 2147483647, 32767, 32768, 0, 1, 2, 258, 5});

	float q = 1 / 0.;
	float p = -1 / 1.;
	testit(std::vector<float>{1,
			2147483647.,
			2147483648.,
			32767,
			32767.00001,
			0,
			1,
			2,
			258,
			5,
			-p,
			-q,
			-1,
			0,
			1.40129846e-45,
			-1.40129846e-45,
			2147483647.,
			-2147483648,
			32767,
			-32767.00001,
			0,
			1,
			2,
			258,
			-5,
			p,
			q});

	double dq = 1 / 0.;
	double dp = -1 / 1.;
	testit(std::vector<double>{1,
			4.9406564584124654e-324,
			-4.9406564584124654e-324,
			2147483647,
			2147483648,
			32767,
			32767.00001,
			0,
			1,
			2,
			258,
			5,
			-p,
			-q,
			-dp,
			-dq,
			-1,
			0,
			1.40129846e-45,
			-1.40129846e-45,
			2147483647,
			-2147483648,
			32767,
			-32767.00001,
			0,
			1,
			2,
			258,
			-5,
			dp,
			dq});

#if 0
	bytebuffer<32> ser;
	service_key_t k;
	k.mux.sat_pos=5;
	k.mux.network_id= 0xab;
	k.mux.ts_id= 0xef;
	k.mux.extra_id= 0x77;
	k.service_id =0xcd;


	encode_ascending(ser, k);
	for(auto i: ser) {
		printf("%02x ", (uint8_t)i);
	}
	printf("\n");
#endif
}
