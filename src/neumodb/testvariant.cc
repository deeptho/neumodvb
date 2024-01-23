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
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/devdb_extra.h"
#include "util/dtassert.h"
#include <map>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <map>
#include <vector>
#ifdef NDEBUG
#undef NDEBUG
#endif

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
	using namespace chdb;
	devdb::subscription_data_t d0;

	devdb::subscription_data_t d1;
	service_t service;
	service.k.service_id=123;

	d1.v = service;

	devdb::subscription_data_t d2;
	band_scan_t band_scan;
	band_scan.scan_id.subscription_id=735;
	d2.v = band_scan;

	bytebuffer<128> ser0;
	serialize(ser0, d0);

	bytebuffer<128> ser1;
	serialize(ser1, d1);

	bytebuffer<128> ser2;
	serialize(ser2, d2);

	int offset0 = 0;
	devdb::subscription_data_t y0;
	offset0 = deserialize(ser0, y0, offset0);
	//auto& m = *std::get_if<service_t>(&y0.v); //monotype

	int offset1 = 0;
	devdb::subscription_data_t y1;
	offset1 = deserialize(ser1, y1, offset1);
	auto& s = *std::get_if<service_t>(&y1.v);
	if(s.k.service_id != service.k.service_id) {
		printf("problem\n");
	}

	assert(s.k.service_id == service.k.service_id);

	int offset2 = 0;
	devdb::subscription_data_t y2;
	offset2 = deserialize(ser2, y2, offset2);
	auto& b = *std::get_if<band_scan_t>(&y2.v);
	if(b.scan_id.subscription_id != band_scan.scan_id.subscription_id) {
		printf("problem\n");
	}

	assert(b.scan_id.subscription_id == band_scan.scan_id.subscription_id);

}
