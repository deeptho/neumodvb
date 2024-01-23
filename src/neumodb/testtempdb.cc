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

#include "dbdesc.h"
#include "stackstring.h"
#include "stackstring_impl.h"
#include <map>
#include <time.h>

#include "neumodb/chdb/chdb_db.h"
#include "neumodb/chdb/chdb_extra.h"

struct d_t {
	ss::vector<uint8_t, 4> fields;

	template <typename T> d_t(std::initializer_list<T> q) {
		for (auto x : q)
			fields.push_back(uint8_t(x));
	}
#if 1
	d_t(uint8_t val) : d_t({(uint8_t)0, (uint8_t)0, (uint8_t)0, val}) {
	}
#endif
	template <typename T> d_t(T val) : d_t({(uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)val}) {
	}

	static d_t from_predefined_key(uint8_t val) { return d_t({(uint8_t)0, (uint8_t)0, (uint8_t)0, val}); }

	bool is_predefined() const {
		assert(fields.size() >= 1);
		return fields.size() == 4 && fields[0] == 0 && fields[1] == 0 && fields[2] == 0;
	}
	operator uint32_t() const {
		uint32_t ret = 0;
		for (auto f : fields) {
			ret = (ret << 8) | f;
		}
		auto extra = 4 - fields.size();
		if (extra <= 3)
			ret <<= (extra * 8);
		assert(ret);
		return ret;
	}
};

using namespace chdb;

int main(int argc, char** argv) {
	service_t s1;
	s1.k.mux.sat_pos = 12;
	s1.k.service_id = 99;
	s1.ch_order = 55;

	service_t s2;
	s2.k.mux.sat_pos = 14;
	s2.k.service_id = 11;
	s2.ch_order = 77;

	service_t s3;
	s3.k.mux.sat_pos = 12;
	s3.k.service_id = 11;
	s3.k.ts_id = 48;
	s3.ch_order = 77;

	chdb_t db(/*readonly*/ false, /*is_temp*/ true);
	db.add_dynamic_key(
		{service_t::subfield_t::ch_order, service_t::subfield_t::k_service_id, service_t::subfield_t::k_mux_sat_pos});
	db.open_temp("/tmp/tempdb.tmp");
	auto txn = db.wtxn();
	auto c = db.tcursor<service_t>(txn);
	c.drop(false);
	put_record(c, s1);
	put_record(c, s2);
	put_record(c, s3);

	auto lst = chdb::service::list_all(txn, service_t::keys_t::key);
	for (auto x : lst) {
		fmt::print("{:d} {:d} {:d} {}\n", x.ch_order, x.k.service_id, x.k.ts_id, x);
	}
	printf("/////////////////////////////////\n");

	auto z = db.dynamic_keys[0];
	auto lst1 = chdb::service::list_all(txn, (uint32_t)z);
	for (auto x : lst1) {
		fmt::print("{:d} {:d} {:d} {}\n", x.ch_order, x.k.service_id, x.k.ts_id, x);
	}

	txn.commit();
}
