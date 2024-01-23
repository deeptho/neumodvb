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
//#include "neumodb/{{dbname}}/db.h"

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif



namespace {{dbname}} {

	template<typename record_t>
		 EXPORT db_tcursor<record_t> find_first(db_txn& txn);

	template<typename record_t>
		EXPORT db_tcursor<record_t> find_last(db_txn& txn);


	template<typename record_t>
		EXPORT bool put_record(db_tcursor<record_t>& tcursor, const record_t& record, unsigned int put_flags=0);

	{% if false %}
	template<typename record_t>
		EXPORT void put_record_at_key(db_tcursor<record_t>& tcursor, const ss::bytebuffer_& primary_key, const record_t& record);
	{% endif %}

	template<typename record_t>
		EXPORT void update_record_at_cursor(db_tcursor<record_t>& tcursor, const record_t& record);

	template<typename record_t>
		EXPORT bool get_record_at_key(db_tcursor<record_t>& tcursor, const ss::bytebuffer_& primary_key, record_t& ret);

	template<typename record_t>
		EXPORT void delete_record(db_tcursor<record_t>& tcursor, const record_t& record);

	template<typename record_t>
		EXPORT void delete_record_at_cursor(db_tcursor<record_t>& tcursor);
#if 0
	template<typename record_t>
		inline void delete_key(db_tcursor<record_t>& tcursor, const ss::bytebuffer_& key);
#endif

	template <typename record_t>
		inline void update_log(db_txn& txn, const ss::bytebuffer_& primary_key, db_txn::update_type_t update_type);

	template <typename record_t>
		EXPORT inline int update_secondary_key_dynamic
		(const dynamic_key_t& order,
		 db_tcursor_index<record_t>& idx, bool exists, const record_t& oldrecord,
		 const ss::bytebuffer_& primary_key, const record_t& newrecord);

	template <typename record_t>
		 EXPORT inline void update_secondary_keys(db_tcursor<record_t> &tcursor, const ss::bytebuffer_ &primary_key,
																			const record_t& newrecord);

	template <typename record_t>
		 EXPORT inline void delete_secondary_keys(db_tcursor<record_t> &tcursor, const ss::bytebuffer_ &primary_key,
																			const record_t& record);
	template <typename record_t>
		void encode_subfields(ss::bytebuffer_ &out, const record_t& in, const ss::vector_<uint8_t>& subfields);

} //end namespace {{dbname}}





//////primary keys
	{%for struct in structs %}
//struct {{struct.class_name}}
  {%if struct.is_table %}
  {%set key = struct.primary_key%}


 template<>
 inline ss::bytebuffer_ lowest_primary_key<{{dbname}}::{{struct.class_name}}>() {
		using namespace {{dbname}};
		return {{struct.class_name}}::make_key(
			{{struct.class_name}}::keys_t::{{key.index_name}},
			{{struct.class_name}}::partial_keys_t::none);
	 }

  {%endif%}
	{%endfor%}




namespace {{dbname}} {

	{%for struct in structs %}
  {%if struct.is_table %}

  //struct {{struct.class_name}}

	template<>
		inline bool get_record_at_key<{{struct.class_name}}>
		(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, {{struct.class_name}}& ret) {
			assert(!tcursor.is_index_cursor);
			lmdb::val v;
			bool found = tcursor.find(primary_key, v);
			if(found) {
				auto  serialized_val = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
				if(deserialize<{{struct.class_name}}>(serialized_val, ret)<0)
					return false;
			} else {
				ret = {{struct.class_name}}{};
			}
			return found;
		}

	template<>
		void delete_secondary_keys<{{struct.class_name}}>
		(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, const {{struct.class_name}}& record);

		template<>
		 void update_secondary_keys<{{struct.class_name}}>
			(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, const {{struct.class_name}}& newrecord);


#if 0
	template<typename record_t>
		inline void delete_key(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& key);
#endif

	{%endif%}
	{%endfor%}

}; //end namespace {{dbname}}
