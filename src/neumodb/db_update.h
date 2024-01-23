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
#include "cursors.h"
#include "lmdb++.h"
#include "db_keys_helper.h"

inline static int key_cmp(const ss::bytebuffer_& a, const ss::bytebuffer_& b)
{
	auto x = memcmp((void*) a.buffer(), b.buffer(), (int)std::min(a.size(), b.size()));
	if(x!=0 || a.size() == b.size())
		return x;
	return (a.size() < b.size()) ? -1 : 1;
}


/*
	Important: the log only records which records where changed in a transaction but not how
	they were changed. If a record is deleted in afirst trasaction t1 and recretaed in a later transaction t2,
	then when processing tracaction t1 only, the records will NOT appear deleted.

	Therefore updating makes only sense when the update process is done until the very last
	transaction.


 */
template<typename record_t>
int update_from(db_txn& to_txn, db_txn& from_txn, neumodb_t& to_db, neumodb_t& from_db,
								 size_t to_txnid)
{
	int count =0;
	auto start_logkey = record_t::make_log_key(to_txnid);

	ss::bytebuffer<32> key_prefix;
	encode_ascending(key_prefix, data_types::data_type<record_t>());

	auto c = from_db.tcursor_log<record_t>(from_txn, key_prefix);
	find_by_serialized_secondary_key(c, start_logkey, key_prefix, find_type_t::find_geq);

	/*we cannot use c.range() because some secondary keys in
		log may point to deleted records and my not have a primary record
	*/
	auto done = !c.is_valid();
	for(; !done; done=!c.next()) {
		assert(c.is_valid());
		auto k = c.current_serialized_secondary_key();
		count++;
		record_t record;
		if(c.get_value(record)) {
			bool new_record = put_record(to_txn,record);
			if(new_record)
				printf("New record\n");
			else
				printf("Updated record\n");
		} else {
			delete_record(to_txn,record);
			printf("Print deleted record\n");
		}
	 }
	return count;
}
