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
	ss::bytebuffer_ ser;

	for (auto x : xx) {
		serialize(ser, x);
	}

	int index = 0;
	for (auto x : xx) {
		auto y = x;
		index = deserialize(ser, y, index);
		assert(y == x);
	}
	assert(index == ser.size());
}

int main(int argc, char** argv) {
	ss::string<32> x{"testpesttestpestabcde"};
	ss::string_ y;

	bytebuffer<128> ser;
	serialize(ser, x);
	int index = 0;
	index = deserialize(ser, y, index);
	assert(index == (int)ser.size());
	assert(x == y);

	testit(std::vector<int16_t>{32767, -1, -32767, 1, 2, 258, -5});
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
			-2147483648.,
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
}
