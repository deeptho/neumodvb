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
#include "stackstring.h"
//#include "serialize.h"
#include "../util/logger.h"
#include <map>
#include <stdint.h>
#include <vector>

struct zorro_t {
	int x{1};
	char y{2};
	double z{3};
	ss::string<0> alfa{"alfa"};
	int q{4};
};

ss::accu_t f(ss::string_& s) {
	ss::accu_t a(s);
	a << "a";
	return a;
}

int main(int argc, char** argv) {
	set_logconfig(nullptr);
	ss::string<16> dirname;
	dirname << "28.2E 10";

	ss::string<16> tmp;
	tmp = dirname;
	printf("s=%s\n", tmp.c_str());
	tmp << "11111111111111111111111111111111111111111111111111111111111111111";
	printf("s=%s\n", tmp.c_str());

	return 0;

#if 0
	ss::string<256> tst;
	tst << dirname << "/" << "index.mb";
	printf("res=%s\n", tst.c_str());
	exit(0);
#endif
#if 0
	ss::vector<int,2> x;
	for(int i=0; i<32;++i)
		x[i]=i;
	ss::vector<int,2> y;
	y=x;

	for(int i=0; i<32;++i)
		printf("[%d] =%d\nn", i, y[i]);


	exit(0);
#endif
	ss::string<32> s;

	subscription_id_t subscription_id = -1;
	s << "SUB[" << subscription_id << "] ";

	ss::string<0> zorro1{"adsa"}; // 1 1 0, ////0,0,0

	ss::string<20> ss1="hello there";
	int i;
	for (i = 0; i < 32; ++i) {
		ss1[i] = 'a';
		printf("[%d/%d]=0x%p = %s\n", ss1.size(), ss1.capacity(), ss1.c_str(), ss1.c_str());
	}

	printf("ss1 size %ld %d %d\n", sizeof(ss1), ss1.size(), ss1.capacity());
	ss1.clear(true);
	printf("ss1 size %ld %d %d\n", sizeof(ss1), ss1.size(), ss1.capacity());
	ss1.sprintf("%s", "zorro was here1234");
	ss1.shrink_to_fit();
	ss::vector<uint16_t, 4> vv;
	vv[0] = 12;

	ss::string<32> ss;
	ss.append_as_utf8("hello", ss1.size(), "FR");
	printf("ss1[%d/%d]=%s\n", ss1.size(), ss1.capacity(), ss1.c_str());
	printf("ss[%d/%d]=%s\n", ss.size(), ss.capacity(), ss.c_str());
	printf("sizes=%ld %ld\n", sizeof(ss1), sizeof(ss));

	printf("vv sizes=%ld %d %d\n", sizeof(vv), vv.size(), vv.capacity());
	printf("vv: allocated=%d\n", vv.is_allocated());

	vv[1] = 13;
	printf("vv sizes=%ld %d %d\n", sizeof(vv), vv.size(), vv.capacity());
	printf("vv: allocated=%d\n", vv.is_allocated());

	vv[6] = 14;
	printf("vv sizes=%ld %d %d\n", sizeof(vv), vv.size(), vv.capacity());
	printf("vv: allocated=%d\n", vv.is_allocated());

	vv[7] = 14;
	printf("vv sizes=%ld %d %d\n", sizeof(vv), vv.size(), vv.capacity());
	printf("vv: allocated=%d\n", vv.is_allocated());

	printf("vvv[]={%d %d %d %d %d %d %d %d}\n", vv[0], vv[1], vv[2], vv[3], vv[4], vv[5], vv[6], vv[7]);
	vv.clear(true);
	printf("vv sizes=%ld %d %d\n", sizeof(vv), vv.size(), vv.capacity());
	printf("qqq=%ld\n", sizeof(ss));
}
