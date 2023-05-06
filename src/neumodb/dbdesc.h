/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "metadata.h"
#include "serialize.h"
#include "deserialize.h"
#include <map>

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

/* fully  equivalent to neumo_schema_record_field_t;
	 should be replaced at some point with  neumo_schema_record_field_t

	 Describes a field (=member) of a struct (=record)
 */
struct field_desc_t {
	uint32_t field_id;  //unique (within a structure) identifier for this field
	uint32_t type_id;  //should never be changed for an existing field, even when data type changes
	                   //in a way that conversion is possible can consist of data_type_t, or-ed with container_type_t
	int32_t serialized_size; //size of data_type or -1 in case of variable size
	ss::string<16> type; //descriptive name (for error reporting; same as c field type)
	ss::string<16> name; //descriptive name (for error reporting; same as c filed name)
	//field_desc_t& operator=(const field_desc_t& other) = default;
	//field_desc_t& operator=(field_desc_t&& other) = default;

	bool operator== (const field_desc_t& other) const;
	bool operator!= (const field_desc_t& other) const {
		return ! (*this == other);
	}

};

struct index_field_desc_t {
	ss::string<16> name;
	bool operator== (const index_field_desc_t& other) const;
	bool operator!= (const index_field_desc_t& other) const {
		return ! (*this == other);
	}

};

struct index_desc_t {
	uint32_t type_id;  //unique (within a structure) identifier for this index
	uint32_t index_id;  //unique (within a structure) identifier for this index
	ss::string<16> name; //descriptive name (for error reporting; same as c filed name)
	//field_desc_t& operator=(const field_desc_t& other) = default;
	//field_desc_t& operator=(field_desc_t&& other) = default;

	ss::vector<index_field_desc_t, 4> fields;

	bool operator== (const index_desc_t& other) const;
	bool operator!= (const index_desc_t& other) const {
		return ! (*this == other);
	}
};


/*
 fully  equivalent to  neumo_schema_record_t
 describes a struct (=record) and the fields it contains
*/
struct record_desc_t {
	uint32_t type_id;
	uint32_t record_version;
	ss::string<32> name;
	ss::vector<field_desc_t> fields;
	ss::vector<index_desc_t> indexes;
	template<typename record_t> const field_desc_t* get_field_desc(record_t&record, const char*name);
	template <typename field_t, typename record_t> field_t& _get_field_ref(record_t&record, const char*name);
/*
	Return the value of a field given its name.
	This is used to read records from an an old record version, when
	translating the record to the current version. The schema of the record must
	be known
	TODO: in order to make this practical from python, we should make a function which returns
	a variant to python
 */
#define get_field_ref(record, name) _get_field_ref<decltype(record.name)>(record,#name)


	const field_desc_t* find_field(uint32_t field_id, uint32_t type_id) const {
		for(auto& field: fields)
			if(field.field_id==field_id) {
				if(field.type_id == type_id)
					return &field;
				else
					return nullptr;
			}
		return nullptr;
	}

	/*
		 compute offsets where fields are stored in a serialized buffer
	 */


	/*
		Fill in the offsets of fixed size fields. Note that these
		occur first in the list of fields.
		Negative ones need to be (re) computed
	 */
//	static ss::vector<int32_t, 32> compute_static_offsets();

	bool operator== (const record_desc_t& other) const;
	bool operator!= (const record_desc_t& other) const {
		return ! (*this == other);
	}
};

struct schema_entry_t {
	ss::string<16> name;
	int schema_version{-1};
	ss::vector_<record_desc_t>* pschema{nullptr};
};


using all_schemas_t = ss::vector_<schema_entry_t>;

/*
	a record_desc_t but with added field offset information
 */
class record_data_t
{
	friend class dbdesc_t;
public:
	static constexpr int32_t NOTFOUND{std::numeric_limits<int32_t>::max()};
	record_desc_t record_desc;
	/*
		Fixed size fields are stored at start of serialized data; for each of them, field_offsets[i] is the offset in the
		serialialized record where the i-th fixed size field can be found, or NOTFOUND

		Variable size fields are always stored last in the serialized record (but not necessarily
		in the c++ structures); for each of them field_offsets[i] will contain -(1+idx),
		where idx=0 for the first variable field, 1 for the second ...
	*/
	ss::vector<int32_t, 16> field_offsets; // 16 is a heuristic value

	int variable_size_fields_start_offset=0;

	/*
		initialise fixed_size_field_offsets from schema read from database
	*/

	record_data_t() {
	}


	void init(const record_desc_t& schema_map, const record_desc_t& current_schema);

	record_data_t(const record_desc_t& schema_map, const record_desc_t& current_schema) {
		record_desc = schema_map;
		init(schema_map, current_schema);
	}

	record_data_t(const record_desc_t& schema_map) {
		record_desc = schema_map;
		init(schema_map, schema_map);
	}

	size_t field_offset_for_index(const ss::bytebuffer_ &ser, int idx) const;

	template<typename field_t> bool get_field(const ss::bytebuffer_ &ser, int idx, field_t& f) {
		assert(idx>=0 && idx<=field_offsets.size());
		auto offset = field_offsets[idx];
		if(offset == NOTFOUND)
			return false;
		if(offset <0) {
			int n = -offset -1;
			offset = variable_size_fields_start_offset;
			//field is variable size; compute how many bytes to skip
			for(;n>0; n--) {
				uint32_t size;
				deserialize(ser, size, offset);
				offset += sizeof(size) + size;
			}
		}
		deserialize(ser, f, offset);
		return true;
	}

	template<typename field_t> field_t get_field(const ss::bytebuffer_ &ser, int idx) {
		field_t f;
		get_field<field_t>(ser, idx, f);
		return f;
	}
};

inline	size_t record_data_t::field_offset_for_index(const ss::bytebuffer_ &ser, int idx) const {
	auto offset = field_offsets[idx];
	if(offset >= 0 || offset == NOTFOUND)
		return offset;
	auto n = - offset -1;
	offset = variable_size_fields_start_offset;
	//field is variable size; compute how many bytes to skip
	for(;n>0; n--) {
		uint32_t size;
		deserialize(ser, size, offset);
		offset += sizeof(size) + size;
	}
	return offset;
}

/*
	Contains all runtime metadata for a database.
 */
class dbdesc_t {

public:

	static constexpr int32_t NOTFOUND{std::numeric_limits<int32_t>::max()};
	int schema_version{-1};
	const all_schemas_t* p_all_sw_schemas = nullptr; //as defined by the code

	std::map<uint64_t, record_data_t> schema_map; //as stored in the database file, but indexed by type_id
	std::map<uint64_t, index_desc_t> index_map; //as stored in the database file, but indexed by type_id
	const record_data_t* metadata_for_type(int type_id) const;
	inline const index_desc_t* metadata_for_index_type(int type_id) const;

	const record_desc_t* schema_for_type(int type_id) const {
		auto* p = metadata_for_type(type_id);
		return p ? &(p->record_desc): NULL;
	}

	const index_desc_t* index_desc_for_index_type(int type_id) const {
		auto* p = metadata_for_index_type(type_id);
		return p;
	}


	dbdesc_t() {}
	void init(const all_schemas_t& current_schemas, const ss::vector_<record_desc_t>& stored_schema);
	void init(const all_schemas_t& current_schemas);
	void init(const ss::vector_<record_desc_t>& sw_schema);
};


inline const record_data_t* dbdesc_t::metadata_for_type(int type_id) const {
	type_id &= ~(data_types::vector|data_types::enumeration);
	//look up in schema....
	auto it = schema_map.find(type_id);
	if (it == schema_map.end())
		return NULL;
	return &(it->second);
}


inline const index_desc_t* dbdesc_t::metadata_for_index_type(int type_id) const {
	type_id &= ~(data_types::vector|data_types::enumeration);
	//look up in schema....
	auto it = index_map.find(type_id);
	if (it == index_map.end())
		return NULL;
	return &(it->second);
}


template<typename record_t>
EXPORT int deserialize_safe(const ss::bytebuffer_ & ser, const record_desc_t& foreign_record_desc,
												record_t& rec, const dbdesc_t&db, size_t offset=0);

namespace schema {
	struct neumo_schema_record_t;
};

bool check_schema(ss::vector_<record_desc_t>& stored_schema, ss::vector_<record_desc_t>& current_schema);
void convert_schema(const ss::vector_<record_desc_t>& in, ss::vector_<schema::neumo_schema_record_t>& out);
void convert_schema(const ss::vector_<schema::neumo_schema_record_t>& in, ss::vector_<record_desc_t>& out);
