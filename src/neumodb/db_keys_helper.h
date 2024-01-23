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
#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif


  /**
   * Positions this cursor at the given PRIMARY key.
   */
template<typename record_t>
inline void
find_by_serialized_primary_key(db_tcursor<record_t>& idx, const ss::bytebuffer_& primary_key,
															 const ss::bytebuffer_& key_prefix,
															 find_type_t find_type)
{

		if (find_type == find_eq) {
			if(!idx.find(primary_key))
				idx.close(); 			//we have reached the end of the database
			return; //we have found the specific record
		}

		if(idx.find(primary_key, MDB_SET_RANGE)) {
			if(find_type == find_geq) {
				if(! idx.has_key_prefix(key_prefix))
					idx.close();
				return; // we found a record
			}
			assert(find_type == find_leq);
			//we have found a record >= desired key, but we need <=
			lmdb::val k{}, v{};
			bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
#pragma unused (ret)
			assert(ret); //we have not yet reached the end of the database
			if(idx.has_key(primary_key)) {
				//the current record has the same primary key as the desired key
				return;
			} else {
				if (!idx.prev() || ! idx.has_key_prefix(key_prefix))
					idx.close();
				return;
			}
		} else {
			//we have reached the end of the database
			if(find_type == find_geq) {
				idx.close(); //there are no records
				return;
			}
			assert(find_type == find_leq);
			//position at last entry in database
			lmdb::val k{}, v{};
			bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_LAST);
			if(!ret) {
				//empty database
				idx.close();
				return;
			}
			//if the last record is for a different key type, move to the previous one; if that is not ok
			//then give up
			if(idx.has_key_prefix(key_prefix))
				return;
			if(idx.prev() && idx.has_key_prefix(key_prefix))
				return;
			idx.close();
			return; //last key is the one we want
		}
}

template<typename record_t>
inline
db_tcursor<record_t>
find_by_serialized_primary_key(db_txn& txn, const ss::bytebuffer_& primary_key,
															 const ss::bytebuffer_& key_prefix,
															 find_type_t find_type)
{

	auto idx = txn.pdb->tcursor<record_t>(txn);
	find_by_serialized_primary_key(idx, primary_key, key_prefix, find_type);
	return idx;
}


/**
 * Positions this cursor at the given SECONDARY key.

 @todo: find_by_serialized_secondary_key is almost the same as find_by_serialized_primary_key
 => integrate

 cursor version is useful, so we wuld like to keep if (and is used in db_update.h)
 */

template<typename cursor_t>
inline void
find_by_serialized_secondary_key(cursor_t& idx, const ss::bytebuffer_& secondary_key,
																 const ss::bytebuffer_& key_prefix, find_type_t find_type)
{
	assert(secondary_key.size() >= (int) sizeof(uint32_t));
	ss::bytebuffer<8> empty;
	if (find_type == find_eq) {
		if(!idx.find(secondary_key))
			idx.close();
		else {
			lmdb::val k{}, v{};
			bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
			if(!ret) {
				idx.close(); //we could not find anything at all, not even a record after
				return;
			}
			auto primary_key = idx.current_serialized_primary_key();
			idx.maincursor.find(primary_key, v);
		}
		return;
	}

	if(idx.find(secondary_key, MDB_SET_RANGE)) {

		if(find_type == find_leq) {
			lmdb::val k{}, v{};
			bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
			if(!ret) {
				idx.close(); //we could not find anything at all, not even a record after
				return;
			}
			if(idx.has_key(secondary_key))
			{
				//exact match; can always be returned
				auto primary_key = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
				idx.maincursor.find(primary_key, v);
				return;
			}
			/*there is no matching key, so we reached a higher one, but we were asked to position the cursor before
				this point (as there was no exact match.
			*/

			if (!idx.prev() || ! idx.has_key_prefix(key_prefix))
				idx.close();
			else {
				auto primary_key = idx.current_serialized_primary_key();
				idx.maincursor.find(primary_key, v);
			}
			return;
		} else {
			if(!idx.has_key_prefix(key_prefix))
				idx.close();
			else {
				lmdb::val k{}, v{};
				bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
				if(!ret) {
					idx.close(); //we could not find anything at all, not even a record after
					return;
				}
				auto primary_key = idx.current_serialized_primary_key();
				idx.maincursor.find(primary_key, v);
			}
			return;
		}

	} else {
		if(find_type != find_leq) {
			//we have reached the end of the database
			idx.close();
			return;
		}
		//position at last entry in database
		lmdb::val k{}, v{};
		bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_LAST);
		if(!ret) {
			//empty database
			idx.close();
			return;
		}
			//if the last record is for a different key type, move to the previous one; if that is not ok
			//then give up
		if(idx.has_key_prefix(key_prefix)) {
			auto primary_key = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
				idx.maincursor.find(primary_key, v);
			return;
		}
		if(idx.prev() && idx.has_key_prefix(key_prefix)) {
			//auto primary_key = ss::bytebuffer_::view(v.data(), v.size(), v.size());
			auto primary_key = idx.current_serialized_primary_key();
			idx.maincursor.find(primary_key, v);

			return;
		}
		idx.close();
		return; //last key is the one we want
	}
	assert(0);
	return;
}



struct primary_key_t {
	template<typename record_t>
inline static
db_tcursor<record_t>
find_by_serialized_key(db_txn& txn, const ss::bytebuffer_& primary_key,
															 const ss::bytebuffer_& key_prefix,
															 find_type_t find_type) {
	return ::find_by_serialized_primary_key<record_t>(txn, primary_key, key_prefix, find_type);
}

};

/**
 * Positions this cursor at the given SECONDARY key.
 */

struct secondary_key_t {
	template<typename record_t>
	inline static
	db_tcursor_index<record_t>
	find_by_serialized_key(db_txn& txn, const ss::bytebuffer_& secondary_key,
																	 const ss::bytebuffer_& key_prefix, find_type_t find_type) {
		auto idx = txn.pdb->tcursor_index<record_t>(txn);
		::find_by_serialized_secondary_key(idx, secondary_key, key_prefix, find_type);
		return idx;

	}


};


template <typename data_t, typename cursor_t>
ss::vector_<data_t> get_range_helper(cursor_t& cursor, int before, int num_records)
{
	ss::vector_<data_t> ret;
	#if 0
	ret.reserve(num_records);
	for(;before>0 && c.is_valid(); --before)  {
		n++;
		c.prev();
	}

	if(!c.is_valid())  {
		n--;
		{% if key.primary %}
		c = find_first(txn);
		{%else %}
		c = find_first_sorted_by_{{key.index_name}}(txn);
		{%endif%}
	}
	n+= 1 + after; // n is now the total number of records to get
	n =100000;
	if(c.is_valid()) {
		for (auto const &x : c.range()) {
			data.push_back(x);
			if(--n <=0)
				break;
		}
	}
#endif
	return ret;
}




template<typename key_t, typename record_t>
monitor_t::state_t list
(db_txn& txn,
 const dynamic_key_t& order, //Defines order in which to list record (and which index to use
 /*
	 Note that order is always a predefined key in list_primary,
	 but can be a dynamic key in list_secondary
 */
 typename record_t::partial_keys_t key_prefix_, //restrict to portion of database (efficient; uses key)
 record_t* ref, //record serving as reference; used by offset
 int num_records, //desired number of records, return all if num_records<0
 int offset, /*first returned item will come offset (if offset>0) records after or -offset (offset<0)
							 before the reference. Will be ignored if num_records<0
						 */
 bool estimate_pos_top, //estimate reference position (requires scanning whole list!)
 bool estimate_list_size, //estimate list size (requires scanning whole list!)
 const ss::bytebuffer_& key_prefix,
 const ss::bytebuffer_& key_lower_limit,
 ss::vector_<record_t>& data)
{
	monitor_t::state_t state;

	if(num_records<0) {
		//all records will be returned
		offset = 0;
		ref=nullptr;
	}
	if(estimate_list_size)
		estimate_pos_top = true;

	ss::bytebuffer<32> ref_key;
 	assert(key_prefix.size() >= (int)sizeof(uint32_t));
	if(ref) {
		const bool next_type = false;
		ref_key = record_t::make_key(
			order, record_t::partial_keys_t::all, ref, next_type);
	} else {
		ref_key = key_prefix;
	}
	auto& start_key = estimate_pos_top? key_lower_limit : ref_key;
	auto c = key_t::template find_by_serialized_key<record_t>(txn, start_key, key_prefix,
																														find_type_t::find_geq);
	c.set_key_prefix(key_prefix);
	int ref_pos = -1; //will be the position of the last entry <ref, which also matches, or -1 if no such record exists
	if(estimate_pos_top && ref) {
		//we need to seek to the specified reference entry, but also count the number of records
		while(c.is_valid()) {
			auto x = c.current();
			ss::bytebuffer<16> key;
			c.get_serialized_key(key);
			if(key == ref_key) { //@todo check if this test works
				break;
			}
				ref_pos++;
			c.next();
		}
	}
	if(num_records<0) {
		//all records will be returned
		data.reserve(1024);
		num_records = std::numeric_limits<int>::max();
	} else  {
		//use data as rolling buffer. cur_idx points to next write location
		//n is total number of records
		data.reserve(num_records);
	}
	int n=0; //number of records
	if(estimate_pos_top)
		state.pos_top = -1; //position of top entry on screen, or -1 if screen is empty
	if(estimate_list_size)
		state.list_size = 0;

	if(offset<0) {
		int cur_idx = num_records-1;  //start at the end of the screen (records will be moved afterwards)
		//retrieve -offset records before ref
		auto c1 = c.clone(); // preserve original cursor
		for(; n+offset<0 && c1.is_valid(); )  {
			auto x = c1.current();
			if(matches(x)) {
				if(state.key_top.size()==0)
					 c1.get_serialized_key(state.key_top); //TODO: check if we get the secondary key
				c1.get_serialized_key(state.key_bottom); //TODO: check if we get the secondary key
				if(estimate_pos_top) {
					if(state.pos_top<0) { //initialisation
						state.pos_top = ref_pos+1;
					} else
						state.pos_top--;
					if(estimate_list_size)
						state.list_size++;
				}
				n++;
				assert(cur_idx>=0);
				data[cur_idx] = x;
				--cur_idx;
			}
			c1.prev();
		}
		assert(cur_idx == num_records -1 -n);
		/*
			data are now at indices cur_idx+1 ... num_records -1
      which is also num_records - n .... num_records -1

			If we found record (n>0),
			rotate buffer such that these records end up at 0 ... n-1
		*/
		if(n>0) { //if no records were found, there is no need to rotate
			rotate(data, cur_idx);
		}
	}
	/*all retrieved records are now at start of screen, which means that "ref"
		might be higher on the screen than requested by the caller*/
	for (; n<num_records && c.is_valid();) {
		auto x = c.current();
		if (matches(x)) {
			if(state.key_top.size()==0)
				c.get_serialized_key(state.key_top); //TODO: check if we get the secondary key
			c.get_serialized_key(state.key_bottom); //TODO: check if we get the secondary key
			data.reserve(n); //ensure enough room; only needed if returining all records
			data[n++] = x;
			if(estimate_list_size)
				state.list_size++;
		}
		c.next();
	}
	data.resize(n); //shrink vector in case fewer records are found than desired
	/*continue processing records in the list, only to count them*/
	if(estimate_list_size) {
		for (; c.is_valid();) {
			auto x = c.current();
			if (matches(x)) {
				state.list_size++;
			}
			c.next();
		}
	}

	return state;
}

template<typename record_t> HIDDEN void clean_log(db_txn& txn, int num_keep)
{
	auto end_id = txn.txn_id();
	if (end_id<=num_keep)
		return;
	end_id -= num_keep;
	decltype(end_id) start_id{0};

	auto start_logkey = record_t::make_log_key(start_id);
	auto end_logkey = record_t::make_log_key(end_id);
	//make a prefix, which will restrict the type of the records we will actually handle
	ss::bytebuffer<16> key_prefix;
	//encode_ascending(key_prefix, data_types::data_type<record_t>());

	//initialize an index cursor. It will run over all log records with the correct type_id
	auto c = txn.pdb->template tcursor_log<record_t>(txn, key_prefix);

	//position the log cursor at the first log record of interest
	find_by_serialized_secondary_key(c, start_logkey, key_prefix, find_type_t::find_geq);
	auto done = !c.is_valid();
	for(; !done; done=!c.next()) {
		assert(c.is_valid());
		auto x = c.current_serialized_secondary_key();
		assert(x.size() == end_logkey.size());
		if(memcmp(x.buffer(), end_logkey.buffer(), x.size())>=0)
			break;
		c.cursor_del();
	}
}


template<typename key_t, typename record_t>
void list(
	db_txn& txn,
	const ss::bytebuffer_& key_prefix,
  const ss::bytebuffer_& key_lower_limit,

	ss::vector_<record_t>& data) {

	ss::bytebuffer<32> ref_key;
 	assert(key_prefix.size() >= (int)sizeof(uint32_t));
	auto& start_key = key_lower_limit;
	auto c = key_t::template find_by_serialized_key<record_t>(txn, start_key, key_prefix,
																														find_type_t::find_geq);
	c.set_key_prefix(key_prefix);

	data.clear();
	for (; c.is_valid(); c.next()) {
		auto x = c.current();
		data.push_back(x);
	}
}
