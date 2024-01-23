/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 *
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

#include "{{dbname}}_db.h"
#include "{{dbname}}_keys.h"
#include "neumodb/cursors.h"
#include "neumodb/dbdesc.h"
#include "neumodb/schema/schema_db.h"
#include <uuid/uuid.h>


namespace {{dbname}} {
/*
		helper function for database conversion
*/
	int {{dbname}}_t::convert_record(db_cursor& from_cursor, db_txn& to_txn, uint32_t type_id,
																	 unsigned int put_flags)
	{
		switch(type_id) {
    {%for struct in structs %}
		{%if struct.is_table %}
		case {{struct.type_id}}: { //
			{{dbname}}::{{struct.class_name}} record;
			bool ret=from_cursor.get_value(record);
			//assert(ret);
			if(!ret)
				return -1; // record not found
			put_record(to_txn, record, put_flags);
		}
		break;
		{%endif %}
  {% endfor %}
		default:
			return -1; //unknown record
		}
		return -1;
  };
}; //namespace {{dbname}}

namespace {{dbname}} {
/*
		helper function for database conversion
*/
	void {{dbname}}_t::store_schema(db_txn& txn, unsigned int put_flags)
	{
		schema::neumo_schema_t s;
		s.version = neumo_schema_version;
		uuid_t uuid;
		uuid_generate(uuid);
		s.uuid.append_raw(&uuid[0], sizeof(uuid));
		s.db_type="{{dbname}}";
		for(auto& schema_entry: *dbdesc->p_all_sw_schemas) {
			convert_schema(*schema_entry.pschema, s.schema);
		}
		put_record(txn, s, put_flags);
  };
}; //namespace {{dbname}}




namespace {{dbname}} {

	template <typename record_t>
		inline void update_log(db_txn& txn, const ss::bytebuffer_& primary_key, db_txn::update_type_t update_type) {
		if(!txn.use_log)
			return;
		auto txn_id = txn.txn_id();
		auto idxlog = txn.pdb->tcursor_log<record_t>(txn);
			auto new_secondary_key =
				record_t::make_log_key(txn_id);
			idxlog.put_kv(new_secondary_key, primary_key);
	}
}; //namespace {{dbname}}


namespace {{dbname}} {
	{%for struct in structs %}
  {%if struct.is_table %}

	 /*
 returns true if a new record was added, false if an old record was updated
 */
	template<>
		bool put_record<{{struct.class_name}}>
		(db_tcursor<{{struct.class_name}}>& tcursor, const {{struct.class_name}}& record,
		 unsigned int put_flags)
		{
			using namespace {{dbname}};
			assert(!tcursor.is_index_cursor);
			auto primary_key =
				{{struct.class_name}}::make_key(
					{{struct.class_name}}::keys_t::{{struct.primary_key.index_name}},
					{{struct.class_name}}::partial_keys_t::all, &record);
			update_secondary_keys<{{struct.class_name}}>(tcursor, primary_key, record);
			bool new_record=tcursor.put_kv(primary_key, record, put_flags);
			auto update_type = new_record ? db_txn::update_type_t::added : db_txn::update_type_t::updated;
			update_log<{{struct.class_name}}>(tcursor.txn, primary_key, update_type);
			return new_record;
		}
		{%if false %}
	 template<>
		 void put_record_at_key<{{struct.class_name}}>
		 (db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, const {{struct.class_name}}& record)
		 {
			 assert(!tcursor.is_index_cursor);
			 update_secondary_keys<{{struct.class_name}}>(tcursor, primary_key, record);
			 tcursor.put_kv(primary_key, record);
			 auto update_type = db_txn::update_type_t::updated;
			 update_log<{{struct.class_name}}>(tcursor.txn, primary_key, update_type);
		 }
		 {% endif %}
		 /*
			 update record at cursor assuming its primary key was not changed
			*/
	 template<>
		 void update_record_at_cursor<{{struct.class_name}}>
		 (db_tcursor<{{struct.class_name}}>& tcursor, const {{struct.class_name}}& record)
		 {
			 assert(!tcursor.is_index_cursor);
			 auto primary_key = tcursor.current_serialized_primary_key();
			 update_secondary_keys<{{struct.class_name}}>(tcursor, primary_key, record);
			 tcursor.put_kv_at_cursor(record);
			 auto update_type = db_txn::update_type_t::updated;
			 update_log<{{struct.class_name}}>(tcursor.txn, primary_key, update_type);
		 }

/*
	 delete a record whose primary key matches the one in the "record" argument.
	 The rest of the record argument does not need to match the record being deleted
*/
	template<>
		void delete_record<{{struct.class_name}}>(db_tcursor<{{struct.class_name}}>& tcursor, const {{struct.class_name}}& record) {
		using namespace {{dbname}};
		assert(!tcursor.is_index_cursor);
		auto primary_key =
			{{struct.class_name}}::make_key(
				{{struct.class_name}}::keys_t::{{struct.primary_key.index_name}},
				{{struct.class_name}}::partial_keys_t::all, &record);
		if(!tcursor.find(primary_key))
			return; //there is no record with the primary key
		/*note that we must use the actual record, not the one passed as an argument, because that one may
			may differ in all fields which are not part of the primary key
		*/
		delete_secondary_keys<{{struct.class_name}}>(tcursor, primary_key, tcursor.current());
			bool was_present = tcursor.del_k(primary_key);
			if(was_present) {
				update_log<{{struct.class_name}}>(tcursor.txn, primary_key, db_txn::update_type_t::deleted);
			}
	}

	template<>
		void delete_record_at_cursor<{{struct.class_name}}>(db_tcursor<{{struct.class_name}}>& tcursor) {
		assert(!tcursor.is_index_cursor);
		auto primary_key = tcursor.current_serialized_primary_key();
		auto record = tcursor.current();
		delete_secondary_keys<{{struct.class_name}}>(tcursor, primary_key, record);
		tcursor.del_k(primary_key);
		update_log<{{struct.class_name}}>(tcursor.txn, primary_key, db_txn::deleted);

	}
  {%endif%}
	{%endfor%}


	}; //namespace {{dbname}}
