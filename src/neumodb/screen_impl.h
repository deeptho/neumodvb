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
/*
	maintain counts of how many records were deleted insterted between
	in [start, ref[ and [ref, end[ respectively
 */
#include "screen.h"
#include "db_update.h"
//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#include "neumodb/neumodb.h"
#include "neumodb/epgdb/extra.h"
#include "neumodb/recdb/extra.h"
#include "util/dtassert.h"
#endif


template <typename record_t>
screen_t<record_t>::limits_t::limits_t(typename record_t::partial_keys_t key_prefix_type_,
														 const record_t *key_prefix_data_,
																			 const record_t* lower_limit_,
																			 const record_t* upper_limit_)
	: key_prefix_type(key_prefix_type_)
	, index_for_key_prefix(record_t::key_for_prefix(key_prefix_type_))
	, key_prefix(record_t::make_key(index_for_key_prefix, key_prefix_type, key_prefix_data_, false))
	, key_lower_limit(lower_limit_
								?record_t::make_key(index_for_key_prefix, record_t::partial_keys_t::all, lower_limit_, false)
								: key_prefix)
	, key_upper_limit(upper_limit_
								?record_t::make_key(index_for_key_prefix, record_t::partial_keys_t::all, upper_limit_, true)
								: record_t::make_key(index_for_key_prefix, key_prefix_type, key_prefix_data_, true))
{
#ifdef USE_END_TIME
#else
	//to implement
	assert(upper_limit_==nullptr);
#endif
}



/*
	A screen is a slice of a list, either the list part shown on the screen or a slightly larger slice.
	Initialize a screen with at most num_records, starting at position offset in the list
	If num_records<0, return as many as possible records
 */
template<typename record_t>
void screen_t<record_t>::init
	(db_txn& txn,
	 std::shared_ptr<neumodb_t>& tmpdb_,
	 int num_records, //desired number of records to retrieve
	 int pos_top  //return num_records starting at position pos_top from top
		)
{
	assert(tmpdb_);
	if(!tmpdb_.get()) {
		/* we create a temporary database with the sort order we desire and populate it with data.
			 Then we use the temp database.
			 As long as the sort column does not change, we keep the temp database around.
		*/
		tmpdb = std::make_shared<db_t>(/*readonly*/ false, /*is_temp*/ true);
		tmpdb->add_dynamic_key(this->sort_order);
		tmpdb->open_temp("/tmp/neumolists");
	} else
		tmpdb = tmpdb_;
	auto wtxn = tmpdb->wtxn();
	fill_list_db(txn, wtxn, -1, 0, field_matchers, &match_data, field_matchers2, &match_data2, nullptr);
	monitor.txn_id = txn.txn_id();
	wtxn.commit();
}


template<typename record_t>
void screen_t<record_t>::init
	(db_txn& txn,
	 int num_records, //desired number of records to retrieve
	 int pos_top  //return num_records starting at position pos_top from top
		)
{
		/* we create a temporary database with the sort order we desire and populate it with data.
			 Then we use the temp database.
			 As long as the sort column does not change, we keep the temp database around.
		*/
		tmpdb = std::make_shared<db_t>(/*readonly*/ false, /*is_temp*/ true);
		tmpdb->add_dynamic_key(this->sort_order);
		tmpdb->open_temp("/tmp/neumolists");
		auto wtxn = tmpdb->wtxn();

		fill_list_db(txn, wtxn, -1, 0, field_matchers, &match_data, field_matchers2, &match_data2, nullptr);
		monitor.txn_id = txn.txn_id();
		wtxn.commit();
}



/*
	update the secondary index of a temp database and adjust the reference
	records' row index if needed.

 */
template<typename record_t>
	inline void screen_t<record_t>::update_screen_key(
	const dynamic_key_t& order,
	db_tcursor_index<record_t>& idx, bool exists,
	const record_t& oldrecord,
	const ss::bytebuffer_& primary_key, const record_t& newrecord
	)
{
	if(exists) { //record existed in primary index, so must also exist in secondary index
			auto new_secondary_key =
				record_t::make_key(order, record_t::partial_keys_t::all, &newrecord);
			auto old_secondary_key =
				record_t::make_key(order, record_t::partial_keys_t::all, &oldrecord);
			//bool different = old_secondary_key !=new_secondary_key;
			bool different = old_secondary_key !=new_secondary_key;
			if (different) {
				if(!cmp(primary_key, monitor.reference.primary_key)
					 &&!cmp(old_secondary_key, monitor.reference.secondary_key))
					monitor.reference.reset(); //reference moves to different location
				else {
					monitor.reference.update(old_secondary_key, primary_key, true);
					monitor.reference.update(new_secondary_key, primary_key, false);
				}
				if(!cmp(primary_key, monitor.auxiliary_reference.primary_key)
					 &&!cmp(old_secondary_key, monitor.auxiliary_reference.secondary_key))
					monitor.auxiliary_reference.reset(); //reference moves to different location
				else {
					monitor.auxiliary_reference.update(old_secondary_key, primary_key, true);
					monitor.auxiliary_reference.update(new_secondary_key, primary_key, false);
				}
				bool was_present = idx.del_kv(old_secondary_key, primary_key);
#pragma unused (was_present)
				bool new_record = idx.put_kv(new_secondary_key, primary_key);
#pragma unused (new_record)
				assert(new_record);
				return;
			} else {
				return;
			}
	} else { // new record
		auto new_secondary_key =
			record_t::make_key(order, record_t::partial_keys_t::all, &newrecord);
		bool new_record = idx.put_kv(new_secondary_key, primary_key);
#pragma unused (new_record)
		monitor.reference.update(new_secondary_key, primary_key, false);
		monitor.auxiliary_reference.update(new_secondary_key, primary_key, false);
		monitor.state.list_size++;
		return;
	}
}


/*
	update the secondary index of a temp database and adjust the reference
	records' row index if needed.
	idx = unitialised index cursor (uses secondary key) into destination database

 */
template<typename record_t>
	inline void screen_t<record_t>::delete_screen_key(
	const dynamic_key_t& order,
	db_tcursor_index<record_t>& idx, bool exists,
	const record_t& oldrecord,
	const ss::bytebuffer_& primary_key)
{

	if(exists) { //record existed in primary index, so must also exist in secondary index
		auto old_secondary_key =
			record_t::make_key(order, record_t::partial_keys_t::all, &oldrecord);
			//bool different = old_secondary_key !=new_secondary_key;
		if(!cmp(primary_key, monitor.reference.primary_key)) {
			//reference itself is being deleted
			monitor.reference.reset();
		} else {
			monitor.reference.update(old_secondary_key, primary_key, true);
		}
		if(!cmp(primary_key, monitor.auxiliary_reference.primary_key)) {
			//reference itself is being deleted
			monitor.auxiliary_reference.reset();
		} else {
			monitor.auxiliary_reference.update(old_secondary_key, primary_key, true);
		}
		bool was_present = idx.del_kv(old_secondary_key, primary_key);
		assert( was_present);
		if(was_present)
			monitor.state.list_size--;
		assert(monitor.state.list_size>=0);
		return;
	} else {
		return;
	}
}


/*
	insert a new record in a tempdb, note if a record with the same primary key was already present,
	and if so if the new secodary key is larger or smaller than the old one
 */
template<typename record_t>
inline bool screen_t<record_t>::put_screen_record
(db_tcursor<record_t>& tcursor, const ss::bytebuffer_& primary_key, const record_t& record,
		 unsigned int put_flags)
{
	//using namespace {{dbname}};
	assert(!tcursor.is_index_cursor);
	assert(tcursor.txn.pdb->use_dynamic_keys);
	assert(tcursor.txn.pdb->dynamic_keys.size()==1);
	for(auto order: tcursor.txn.pdb->dynamic_keys) {
		record_t oldrecord;
		bool exists = get_record_at_key(tcursor, primary_key, oldrecord);
		auto idx = tcursor.txn.pdb->template tcursor_index<record_t>(tcursor.txn);
		update_screen_key(order, idx, exists, oldrecord, primary_key, record);
		break;
	}
	bool new_record=tcursor.put_kv(primary_key, record, put_flags);
	//auto update_type = new_record ? db_txn::update_type_t::added : db_txn::update_type_t::updated;

	return new_record;
}


/*
	tcursor points to the destination datebase and uses the primary key
 */
template<typename record_t>
inline void screen_t<record_t>::delete_screen_record
	(db_tcursor<record_t>& tcursor, const ss::bytebuffer_& primary_key) {
	assert(!tcursor.is_index_cursor);

	assert(tcursor.txn.pdb->use_dynamic_keys);
	assert(tcursor.txn.pdb->dynamic_keys.size()==1);

	record_t oldrecord;
	/*find the record in the destination database
	*/
	bool exists = get_record_at_key(tcursor, primary_key, oldrecord);
	if(exists) {
		for(auto order: tcursor.txn.pdb->dynamic_keys) {
			auto idx = tcursor.txn.pdb->template tcursor_index<record_t>(tcursor.txn);
			delete_screen_key(order, idx, exists, oldrecord, primary_key);
		}
		tcursor.del_k(primary_key);
	}
}

extern void print_hex(ss::bytebuffer_& buffer);


template <typename record_t>
bool screen_t<record_t>::update_if_matches(db_txn& from_txn, 	function_view<bool(const record_t&)> match_fn)
{
	auto to_txn = this->tmpdb->wtxn();
	assert(monitor.txn_id>=0);
	auto& from_db = *from_txn.pdb;

	auto to_txnid = monitor.txn_id+1;
	int count =0;

	//make a key containing (type_id, to_txn_id) as its value; this is a key in the log table
	auto start_logkey = record_t::make_log_key(to_txnid);

	//make a prefix, which will restrict the type of the records we will actually handle
	ss::bytebuffer<32> key_prefix;
	encode_ascending(key_prefix, data_types::data_type<record_t>());

	//initialize an index cursor. It will run over all log records with the correct type_id
	auto c = from_db.template tcursor_log<record_t>(from_txn, key_prefix);

	//position the log cursor at the first log record of interest
	find_by_serialized_secondary_key(c, start_logkey, key_prefix, find_type_t::find_geq);

	/*we cannot use c.range() because some secondary keys in
		log may point to deleted records and my not have a primary record
	*/
	auto done = !c.is_valid();
	//auto old_list_size = monitor.state.list_size;
	assert(to_txn.pdb->use_dynamic_keys);
	assert(to_txn.pdb->dynamic_keys.size()==1);
	//auto& order = to_txn.pdb->dynamic_keys[0];
	auto to_cursor = to_txn.pdb->template tcursor<record_t>(to_txn);

	for(; !done; done=!c.next()) {
		assert(c.is_valid());
		//k points to serialized (type_id, txn_id)
		bool has_been_deleted = !c.maincursor.is_valid(); //@todo: maybe too much of a hack
		ss::bytebuffer<32> old_secondary_key;
		/*check if we have already a record with the same primary key in to_db.
			If such a record exists, it may move to a different location in the secondary
			indexes of to_db*/
#ifdef DEBUG_PRINT
		auto x = c.current_serialized_secondary_key();
		assert(x.size()==12);
		size_t txnid_check;
		deserialize(x, txnid_check, 4); //only for testing. This code is not the inverse of encode_ascending
		//The following will produce an in correct result if primary_key is no longer in main db
#endif
		record_t record;
		auto primary_key = c.current_serialized_primary_key();
		if(primary_key.size() < limits.key_prefix.size() ||
			 memcmp(primary_key.buffer(), limits.key_prefix.buffer(), limits.key_prefix.size())!=0) {

#ifdef DEBUG_PRINT
			//using namespace chdb;
			//using namespace epgdb;
			//using namespace recdb;
		c.get_value(record);
		ss::string<32> rec_check;
		rec_check.format("{}", record);
		printf("BAD from_txn=%ld %s ref_row={:d}\n", txnid_check, rec_check.c_str(), monitor.reference.row_number);
#endif

			continue; //we do not need this record (e.g., epg for wrong service
		}

		if(!has_been_deleted) {
			auto found = c.get_value(record);
#pragma unused (found)
			assert(found);
			//In from_db the record is present, so this is not a deletion
			put_screen_record(to_cursor, primary_key, record, 0);
		if(match_fn(record))
			count++;
#ifdef DEBUG_PRINT

		ss::string<32> rec_check;
		rec_check.format("{}", record);
		printf("from_txn=%ld %s ref_row={:d}\n", txnid_check, rec_check.c_str(), monitor.reference.row_number);
#endif
		} else {
				delete_screen_record(to_cursor, primary_key);
				count++;
		}
	}
#if 0
	printf("result: txn={:d} -> {:d} changed={:d} moved={:d} resized={:d}\n",
				 monitor.txn_id,  from_txn.txn_id(),
				 monitor.state.screen_content_changed, monitor.state.content_moved,
				 old_list_size != list_size);
#endif
	monitor.txn_id = from_txn.txn_id();
	to_txn.commit();
	//from_txn.abort();
	return count>0;
}

template <typename record_t>
bool screen_t<record_t>::update(db_txn& from_txn)
{
	auto all_match_fn = [](const record_t& record) {
		return true;
	};

	auto * match_data = this->field_matchers.size() > 0 ?  & this->match_data : nullptr;
	auto * match_data2 = this->field_matchers2.size() > 0 ?  & this->match_data2 : nullptr;

	if (!match_data && !match_data2)
		return screen_t<record_t>::update_if_matches(from_txn, all_match_fn);
	else {
		auto some_match_fn = [this, &match_data, &match_data2](const record_t& record) {
			return (!match_data || matches(record, *match_data, field_matchers)) &&
				(!match_data2 || matches(record, *match_data2, field_matchers2));
		};
		return screen_t<record_t>::update_if_matches(from_txn, some_match_fn);
	}
}

/*
	returns a cursor pointing to the first record
*/
template <typename record_t>
inline db_tcursor_index<record_t> screen_t<record_t>::first_cursor(db_txn& rtxn)
{

	auto find_type =  find_type_t::find_geq;
	auto key_prefix =record_t::make_key(sort_order, record_t::partial_keys_t::none, nullptr);
	auto c = secondary_key_t::template find_by_serialized_key<record_t>(rtxn, key_prefix, key_prefix,
																																			find_type);
	c.set_key_prefix(key_prefix);
//! c.is_valid() can occur if the list is empty
	return c;
}

/*
	returns a cursor pointing to the last record
*/
template <typename record_t>
inline db_tcursor_index<record_t> screen_t<record_t>::last_cursor(db_txn& rtxn)
{

	auto find_type =  find_type_t::find_leq;
	auto key_prefix =record_t::make_key(sort_order, record_t::partial_keys_t::none, nullptr, false);
	auto upper_limit =record_t::make_key(sort_order, record_t::partial_keys_t::none, nullptr, true);
	auto c = secondary_key_t::template find_by_serialized_key<record_t>(rtxn, upper_limit,
																																			key_prefix,
																																			find_type);
	c.set_key_prefix(key_prefix);
	assert(c.is_valid());
	return c;
}



/*
	returns a cursor pointing to the current reference record
*/
template <typename record_t>
inline db_tcursor_index<record_t> screen_t<record_t>::reference_cursor
(db_txn& rtxn, monitor_t::reference_t& reference)
{
	auto key_prefix =record_t::make_key(sort_order, record_t::partial_keys_t::none, nullptr);
	if(reference.row_number<0) {
		//auto find_type =  find_type_t::find_geq;
		auto c = first_cursor(rtxn);
		//! c.is_valid() can occur if the list is empty
		if(c.is_valid()) {
			reference.row_number =0;
			reference.primary_key = c.current_serialized_primary_key();
		}
		return c;
	}
	auto c = rtxn.pdb->template tcursor_index<record_t>(rtxn);
	auto ret = c.find_both(reference.secondary_key, reference.primary_key, MDB_GET_BOTH);
	if(!ret)
		return c; //not found
	if(!c.maincursor.find(reference.primary_key, MDB_SET)) {
		c.close();
		return c;
	}
	c.set_key_prefix(key_prefix);
	assert(c.is_valid());
	if(!ret)
		c.close();
	return c;
}


/*
	sets a reference arnd return a row number
*/
template <typename record_t>
int screen_t<record_t>::set_reference(const record_t& record)
{
	auto rtxn = tmpdb->rtxn();
	ss::bytebuffer<32> primary_key;
	make_primary_key(primary_key, record);

	make_secondary_key(monitor.reference.secondary_key, sort_order, record);
	//auto secondary_key = record_t::key_for_sort_order(sort_order);

	int rowno=-1;
	//auto c = find_first<record_t>(rtxn);

	//
	monitor.reference.row_number = -1; //reset to find first record
	monitor.auxiliary_reference.row_number = -1; //reset
	auto c = this->reference_cursor(rtxn, monitor.reference);

	for(;c.is_valid(); c.next()) {
		rowno++;
			auto p = c.current_serialized_primary_key();
			auto v = cmp(p, primary_key);
			if(v==0)  {
				monitor.reference.primary_key = p;
				monitor.reference.row_number = rowno;
				//monitor.reference.secondary_key = secondary_key; already set above
				c.get_value(primary_current_record);
				return rowno;
			}
	}
	dterrorf("Asked for row number of non-existent record");
	return -1;
}

/*
	sets a reference to specifc row
*/
template <typename record_t>
int screen_t<record_t>::set_reference(int row_number)
{
	const int large_jump_threshold{50};
	auto rtxn = tmpdb->rtxn();

	/*
		wxGrid seems to request rows at the beginning of the table to
		perform useless repaints. This happens after calling set_reference(record_t&)
		and disturbs the caching implemented by the reference_cursor.
		Therefore, we use two reference cursors: the main one which is initialised by
		set_reference(record_t&) and can only move up/down by a very limited amount
		and a secondary one, which is used in other cases, and can move by any amount

		cursor_type returns 0 if the current "reference" is valid and close enough to
		attempt moving it to a desired row.
		All other values indicate that it should not be used: -1: not initialised; +1:
	 */
	auto cursor_type = [this, row_number, large_jump_threshold](auto& reference, bool limit_jump) {
		if (reference.row_number>=0 && (!limit_jump ||
																		std::abs(row_number - reference.row_number) < large_jump_threshold))
			return 0;
		if (row_number > monitor.state.list_size/2 +1)
			return 1;
		return -1;
	};

	auto * reference = &monitor.reference;

	auto startc = [this, &rtxn, &reference, &cursor_type]() {
		auto ct = cursor_type(*reference, true);
		if(ct == 0) {
			auto c = this->reference_cursor(rtxn, *reference);
			if(c.is_valid())
				return c;
			//Handle the case where reference may not exist in the database
			reference->row_number = 0;
			return first_cursor(rtxn);
		}

		reference = &monitor.auxiliary_reference;
		ct = cursor_type(*reference, false);
		if(ct==0) {
			auto c = this->reference_cursor(rtxn, *reference);
			if(c.is_valid())
				return c;
			//Handle the case where reference may not exist in the database
			reference->row_number = 0;
			return first_cursor(rtxn);
		}
		if(ct<0) {
			reference->row_number = 0;
			return first_cursor(rtxn);
		}
		reference->row_number = monitor.state.list_size-1;
		return last_cursor(rtxn);
	};


	auto c =  startc();

	int count= reference->row_number;
	if(std::abs(row_number - count)>=large_jump_threshold) {
		dtdebugf("LARGE JUMP: {} -> {} aux={}",  count, row_number, reference == &monitor.auxiliary_reference);
	}
	if(row_number >= count) {
		for(;c.is_valid(); c.next()) {
			if(count == row_number)
				break;
			++count;
		}
	} else {
		for(;c.is_valid(); c.prev()) {
			if(count == row_number)
				break;
			--count;
		}
	}

	if(count>=0) {
		assert(count == row_number);
		assert(count < monitor.state.list_size);
		if(c.is_valid()) {
			reference->row_number =row_number;
			reference->primary_key = c.current_serialized_primary_key();
			reference->secondary_key = c.current_serialized_secondary_key();
		} else {
			reference->row_number = 0;
			c = first_cursor(rtxn);
			count = 0;
		}
		if(c.is_valid())
			c.get_value((reference == &monitor.auxiliary_reference) ? auxiliary_current_record : primary_current_record);
	}
	return count;
}

//used by gridepg_screen and by channel epg screen
template <typename record_t>
screen_t<record_t>::screen_t
(db_txn& txn, std::shared_ptr<neumodb_t>& tmpdb, uint32_t sort_order_,
 typename record_t::partial_keys_t key_prefix_type_,
 const record_t *key_prefix_data_, const record_t* lower_limit_,
 #ifdef USE_END_TIME
 const record_t* upper_limit_,
 #endif
 const ss::vector_<field_matcher_t>* field_matchers_,
 const record_t* match_data_,
 const ss::vector_<field_matcher_t>* field_matchers2_,
 const record_t* match_data2_) :
	screen_t(dynamic_key_t(sort_order_), key_prefix_type_, key_prefix_data_,
					 lower_limit_
#ifdef USE_END_TIME
					 , upper_limit_
#endif
		)
{
	if(field_matchers_) {
		field_matchers = *field_matchers_;
		assert(match_data_);
		match_data = *match_data_;
	} else {
		assert(!match_data_);
	}
	if(field_matchers2_) {
		field_matchers2 = *field_matchers2_;
		assert(match_data2_);
		match_data2 = *match_data2_;
	} else {
		assert(!match_data2_);
	}
	this->init(txn, tmpdb, -1, 0);
}


template <typename record_t>
screen_t<record_t>::screen_t
(db_txn& txn, uint32_t sort_order_,
 typename record_t::partial_keys_t key_prefix_type_,
 const record_t *key_prefix_data_, const record_t* lower_limit_,
#ifdef USE_END_TIME
 , const record_t* upper_limit_
#endif
 const ss::vector_<field_matcher_t>* field_matchers_,
 const record_t* match_data_,
 const ss::vector_<field_matcher_t>* field_matchers2_,
 const record_t* match_data2_
	)
	: screen_t(dynamic_key_t(sort_order_), key_prefix_type_, key_prefix_data_,
						 lower_limit_
#ifdef USE_END_TIME
						 , upper_limit_
#endif
		)
{
	if ((uint32_t)sort_order ==0) {
		std::runtime_error("illegal sort order");
	}
	if(field_matchers_) {
		field_matchers = *field_matchers_;
		assert(match_data_);
		match_data = *match_data_;
	} else {
		assert(!match_data_);
	}
	if(field_matchers2_) {
		field_matchers2 = *field_matchers2_;
		assert(match_data2_);
		match_data2 = *match_data2_;
	} else {
		assert(!match_data2_);
	}
	this->init(txn, -1, 0);
}



template <typename record_t>
record_t screen_t<record_t>::record_at_row(int row_number)
{
#if 0
	if(row_number != monitor.reference.row_number &&
		 row_number != monitor.auxiliary_reference.row_number)
#endif
		set_reference(row_number);
	if(row_number == monitor.reference.row_number)
		return primary_current_record;
	else if(row_number == monitor.auxiliary_reference.row_number)
		return auxiliary_current_record;
	assert(0);
	//only for debugging
	set_reference(row_number);
	return primary_current_record;
}


template <typename record_t>
void screen_t<record_t>::drop_temp_table(bool del)
{
		return tmpdb->drop_table(del);
}
