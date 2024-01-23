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

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif


/*
	maintain counts of how many records were deleted insterted between
	in [start, ref[ and [ref, end[ respectively
 */
#include "util/function_view.h"
#include "screen_monitor.h"



struct dynamic_key_t {
	ss::vector<uint8_t,4> fields;
	dynamic_key_t() = default;

	template<typename T>
	dynamic_key_t(std::initializer_list<T> args)
		{
			for(auto x: args)
				fields.push_back(uint8_t(x));
		}

	dynamic_key_t(ss::vector_<uint8_t> args)
		{
			for(auto x: args)
				fields.push_back(uint8_t(x));
		}

	explicit dynamic_key_t(uint32_t val) :
		dynamic_key_t({(uint8_t)(val>>24),(uint8_t)(val>>16),(uint8_t)(val>>8), (uint8_t)val})
		{}

	explicit dynamic_key_t(uint8_t val) :
		dynamic_key_t({(uint8_t)0,(uint8_t)0,(uint8_t)0, val})
		{}


	inline bool is_predefined() const {
		assert(fields.size()>=1);
		return fields.size()==4 && fields[0]==0 && fields[1] == 0 && fields[2]==0;
	}

	explicit operator uint32_t() const {
		uint32_t ret =0;
		for (auto f: fields) {
			ret = (ret<<8) | f;
		}
		auto extra = 4-fields.size();
		if(extra<=3)
			ret <<= (extra*8);
		return ret;
	}
};


struct field_matcher_t {
	enum match_type_t : uint8_t {
		EQ=1,
		GEQ,
		LEQ,
		GT,
		LT,
		STARTSWITH,
		CONTAINS,
	};

	int8_t field_id{-1};
	match_type_t match_type{match_type_t::EQ};
	inline bool operator == (const field_matcher_t& other) const {
		return field_id == other.field_id;
	}
};


template <typename record_t>
struct EXPORT screen_t {
private:
	using db_t = typename record_t::db_t;
	void init_(db_txn &txn,
       int pos_top, // return num_records starting at position pos_top from top
       int num_records // desired number of records to retrieve
		);
	monitor_t monitor;

	//db_t masterdb;
	std::shared_ptr<neumodb_t> tmpdb; //database used for temporary lists
public:
	enum index_type_t {
		primary,
		secondary,
		temp
	};

	/*
		Defines which range of records to consider in the primarry db to read records from.
		This is used to tlimit only epg entries for a certain service
		This is achieved by setting a key_prefix_type (which selects an index to read the primary db_
		and optionally key_prefix_data to further specify this range

		lower_linit and upper limit can be used to futher specify the range (currently not
		fully implemented/tested)
	 */
	struct HIDDEN limits_t {
		typedef ss::bytebuffer<32>  key_t;
		typename record_t::partial_keys_t key_prefix_type;
		typename record_t::keys_t index_for_key_prefix;
		//std::optional<record_t>  lower_limit_optional;
		key_t key_prefix{}; /*all relevant keys must start with this
												 */
		key_t key_lower_limit{}; /*all relevant keys are >= key_lower_limit; this is NOT the first key in the list
															 if filtering is active
														 */
		key_t key_upper_limit{};  //all relevant keys are < key_upper_limit
		index_type_t index_type{primary};
		limits_t(typename record_t::partial_keys_t key_prefix_type_ = record_t::partial_keys_t::none,
						 const record_t *key_prefix_data_ = nullptr, const record_t* lower_limit_ = nullptr,
						 const record_t* upper_limit_ = nullptr);

	};

	limits_t limits;
	dynamic_key_t sort_order;
	typename record_t::keys_t index_for_sorting;
	record_t primary_current_record;
	record_t auxiliary_current_record;
  int idxref = 0;  /*records[idxref] is the current reference record which will
										 be kept on screen when list changes
									 */
  int pos_top = 0; // positional index of top record on the screen
  int list_size() const {
		return monitor.state.list_size;
	} // number of entries in the complete list
	ss::vector<field_matcher_t> field_matchers;
	record_t match_data;
	ss::vector<field_matcher_t> field_matchers2;
	record_t match_data2;

	HIDDEN inline static bool is_primary(const dynamic_key_t &order);
private:
	HIDDEN inline db_tcursor_index<record_t> reference_cursor(db_txn& rtxn,
																										 monitor_t::reference_t& reference);
	HIDDEN inline db_tcursor_index<record_t> first_cursor(db_txn& rtxn);
	HIDDEN inline db_tcursor_index<record_t> last_cursor(db_txn& rtxn);


	HIDDEN inline bool put_screen_record(db_tcursor<record_t>& tcursor,
																const ss::bytebuffer_& primary_key, const record_t& record,
																unsigned int put_flags);

	HIDDEN inline void delete_screen_record(db_tcursor<record_t>& tcursor,
																	 const ss::bytebuffer_& primary_key);

	HIDDEN inline void update_screen_key(const dynamic_key_t& order,
																db_tcursor_index<record_t>& idx, bool exists,
																const record_t& oldrecord,
																const ss::bytebuffer_& primary_key, const record_t& newrecord);
	HIDDEN inline void delete_screen_key(const dynamic_key_t& order, db_tcursor_index<record_t>& idx, bool exists,
																const record_t& oldrecord,
																const ss::bytebuffer_& primary_key);

	/*
		Initialise the list and position the screen such that record "ref" appears at position
		"-offset" on screen (offset=0: top: offset=-1, second line ....)

	*/
  HIDDEN inline void init(db_txn &txn,
													int pos_top, // return num_records starting at position pos_top from top
													int num_records // desired number of records to retrieve
		);

  HIDDEN inline void init(db_txn &txn,
													std::shared_ptr<neumodb_t>& tmpdb,
													int pos_top, // return num_records starting at position pos_top from top
													int num_records // desired number of records to retrieve
		);

//public:

	HIDDEN void fill_list_db(db_txn& txn, db_txn& wtxn,
										int num_records, //desired number of records to retrieve
										int pos_top,  //return num_records starting at position pos_top from top
										ss::vector_<field_matcher_t>& field_matchers,
										const record_t * match_data,
										ss::vector_<field_matcher_t>& field_matchers2,
										const record_t * match_data2,
										const record_t * reference);

	/*
		 (key_prefix_type_, key_prefix_data): limits the list to records starting with the given
		 prefix, e.g., only muxes for a specific sat

		 lower_limit: limits the list to records >= lower_limit, e.g., epg records with a start_time
		 >= a given start_time
#ifdef USE_END_TIME
		 upper_limit: limits the list to records <= upper_limit, e.g., epg records with a start_time
		 <= a given start_time
#endif
		 "limit the list" means: any non-compliant records are considered not part of the rist
	 */

	HIDDEN inline screen_t(const dynamic_key_t &sort_order_,
					typename record_t::partial_keys_t key_prefix_type_ = record_t::partial_keys_t::none,
					const record_t *key_prefix_data_ = nullptr, const record_t* lower_limit_ = nullptr
#ifdef USE_END_TIME
												 , const record_t* upper_limit_ = nullptr
#endif
		)
		: limits(key_prefix_type_, key_prefix_data_, lower_limit_,
#ifdef USE_END_TIME
						 upper_limit_
#else
						 nullptr
#endif
			)
		, sort_order(sort_order_)
		, index_for_sorting(record_t::key_for_sort_order(sort_order_))
		{
  }
public:
	//used by gridepg_screen and chepg_screen
	screen_t(db_txn& txn, std::shared_ptr<neumodb_t>& tmpdb, uint32_t sort_order_,
					 typename record_t::partial_keys_t key_prefix_type_ = record_t::partial_keys_t::none,
					 const record_t *key_prefix_data_ = nullptr, const record_t* lower_limit_ = nullptr,
#ifdef USE_END_TIME
					 const record_t* upper_limit_ = nullptr,
#endif
					 const ss::vector_<field_matcher_t>* field_matchers_ =nullptr,
					 const record_t* match_data_ = nullptr,
					 const ss::vector_<field_matcher_t>* field_matchers2_ =nullptr,
					 const record_t* match_data2_ = nullptr
		);

	screen_t(db_txn& txn, uint32_t sort_order_,
					 typename record_t::partial_keys_t key_prefix_type_ = record_t::partial_keys_t::none,
					 const record_t *key_prefix_data_ = nullptr, const record_t* lower_limit_ = nullptr,
#ifdef USE_END_TIME
					 , const record_t* upper_limit_ = nullptr
#endif
					 const ss::vector_<field_matcher_t>* field_matchers_ =nullptr,
					 const record_t* match_data_ = nullptr,
					 const ss::vector_<field_matcher_t>* field_matchers2_ =nullptr,
					 const record_t* match_data2_ = nullptr
		);

//returns true if an update is needed
	EXPORT bool update_if_matches(db_txn& from_txn, 	function_view<bool(const record_t&)> match_fn);
	EXPORT bool update(db_txn& master_txn);

	EXPORT record_t record_at_row(int row_number);
	/*
		sets a reference arnd return a row number
	 */
	EXPORT int set_reference(const record_t & record);
	EXPORT int set_reference(int row_number);
	HIDDEN void drop_temp_table(bool del);
};
/*
	A screen is a view on a sorted database table.
	As the sorting can be on any combination of columns (currently max. 4),
	the records from the main database are copied to a sorted temp database.
	Periodically, the  the main database is checked for updated, and the temp
	database is updated accordingly.

	To avoid needless screen refreshes, two strategies may be used

	1. old screen. This creen maintains the secondary (sorted) index of the first and  last
	row currently displayed. It also maintains the row number of the top line of the screen
	and the number of lines in the table.

	When a record is added or removed this information is updated and also results in a decision
	on whether or not the screen should really be refreshed. This is the case if the record added
	appears between the old top and new line or if it was removed there. If the screen has not changed,
	then still the scrollbar should be adjusted.

	Records can also be updated. In this case there order can change (leading to a combination similar
	to delete folled by insert) or its value can change.

	This screen also has the concept of "focused entry", which is an entry that will always be kept on screen
	after a refres. E.g., if 1000 records are inserted on top of the screen, the refresh will scroll down
	such that the old focused entry remains visible if possible (e.g., when not deleted).

	This old screen causes some problems as from the GUI side, the relationship scrollbar vs list becomes
	complicated. Indeed, the old screen would provide the actual data for the gui as a small vector containing
	only the entries which should be displayed

	2. new screen. simpler but perhaps less efficient

	The screen will always be refreshed, even if the content remains unchanged.
	Instead the scren provides a lookup from row_number to record, but this lookup is made efficient
	by maintaining a reference_entry, for which both a secondary (sorted) key, primary and a row_number is stored.
	The assumption is that subsequent loopkups will be for "close" row numbers and can therefore be efficient.
	This requires minimal changes to the GUI code.

	When new data is entered in the sort temp database, we check how this affects row_number of focused_entry
	and update its value accordingly.

	Note that row_number is determined by the secondary (sorted) key, but that such keys are not unique
	(e.g., several records with the same channel_number)

	When a record is updated in the temp db, we should update the row_number corresponding to ref_entry
	and we may even need ref_entry to be updated.
	The following cases could be considred, with P the primary key of the record, O its old secondary key and
	N its new secondary key. With OP and NP, we denote a unique combination in the seconddary index

	In temp db, the following cases are possible

	P newly addded
      OP cannot be present
         => update row_number
	P updated
	    OP must be present and NP cannot be present
				  NP == OP
              => record update not affecting order
				  NP != OP => record has moved
               => update row_number (check secondary and primary key!)

	P deleted
        OP must ne present
					 => udate row_number



 */
