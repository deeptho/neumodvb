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
#include <filesystem>
#include <map>

#include "dbdesc.h"
#include "neumodb/schema/schema_db.h"

/*
	These conversions could be eliminated because the two structs are the same: this would
	involve importing schema/db.h everywhere and would require stripping this file (and similar files)
	to its bare minimum
*/

static void convert(const field_desc_t& in, schema::neumo_schema_record_field_t& out) {
	// both records are basically the same!
	out.field_id = in.field_id;
	out.type_id = in.type_id;
	out.serialized_size = in.serialized_size;
	out.type = in.type;
	out.name = in.name;
}

static void convert(const schema::neumo_schema_record_field_t& in, field_desc_t& out) {
	// both records are basically the same!
	out.field_id = in.field_id;
	out.type_id = in.type_id;
	out.serialized_size = in.serialized_size;
	out.type = in.type;
	out.name = in.name;
}

static void convert(const record_desc_t& in, schema::neumo_schema_record_t& out) {
	// both records are basically the same!
	out.type_id = in.type_id;
	out.record_version = in.record_version;
	out.name = in.name;

	for (auto& field : in.fields) {
		schema::neumo_schema_record_field_t f;
		convert(field, f);
		out.fields.push_back(f);
	}
#if 0 //TODO does not work yet
	for (auto& in_index : in.indexes) {
		schema::neumo_schema_index_record_t out_index;
		out_index.type_id = in_index.type_id;
		out_index.index_id = in_index.index_id;
		out_index.name = in_index.name;
		for (auto& in_field : in_index.fields) {
			schema::neumo_schema_index_field_t out_field;
			out_field.name = in_field.name;
			out_index.fields.push_back(out_field);
		}
	}
#endif
}

static void convert(const schema::neumo_schema_record_t& in, record_desc_t& out) {
	// both records are basically the same!

	out.type_id = in.type_id;
	out.name = in.name;
	out.record_version = in.record_version;
	for (auto& field : in.fields) {
		field_desc_t f;
		convert(field, f);
		out.fields.push_back(f);
	}
}

void convert_schema(const ss::vector_<record_desc_t>& in, ss::vector_<schema::neumo_schema_record_t>& out) {
	for (const auto& i : in) {
		schema::neumo_schema_record_t o;
		convert(i, o);
		out.push_back(o);
		//count++;
	}
}

void convert_schema(const ss::vector_<schema::neumo_schema_record_t>& in, ss::vector_<record_desc_t>& out) {
	for (const auto& i : in) {
		record_desc_t o;
		convert(i, o);
		out.push_back(o);
	}
}

/*
	record_schema is what is stored in the database file
	current_schema is the schema used by the code
*/
void record_data_t::init(const record_desc_t& record_schema, const record_desc_t& current_schema) {
	int32_t offset = 0;
	auto num_fields = current_schema.fields.size();
	for (int i = 0; i < (int)num_fields; ++i)
		field_offsets[i] = NOTFOUND;
	int i = -1; // index of variable field
	for (auto& foreign_field : record_schema.fields) {
		if (foreign_field.serialized_size > 0) {
			auto* field = current_schema.find_field(foreign_field.field_id, foreign_field.type_id);
			if (field) {
				auto idx = field - current_schema.fields.begin();
				if (idx < (int)num_fields)
					field_offsets[idx] = offset;
			}
			offset += foreign_field.serialized_size;
		} else {
			if (offset >= 0) { // first variable field encoutered
				variable_size_fields_start_offset = offset;
			} else
				i--; // e.g., -3 is third variable size field
			auto* field = current_schema.find_field(foreign_field.field_id, foreign_field.type_id);
			if (field) {
				auto idx = field - current_schema.fields.begin();
				if (idx < (int)num_fields)
					field_offsets[idx] = i;
			}
		}
	}
	// at this point the offsets are stored for all fields in the current c++ structure
}

void dbdesc_t::init(const ss::vector_<record_desc_t>& sw_schema) {
	// init with what is defined by the current code; may be overrriden by
	// calling dbdesc_t::init with a schema retrieved from the database file
	schema_map.clear();
	index_map.clear();
	for (const auto& sw_record_schema : sw_schema) {
		// store a default, current schema
		schema_map.try_emplace(sw_record_schema.type_id, sw_record_schema);
		for(const auto& index: sw_record_schema.indexes)
			index_map.try_emplace(index.index_id, index);
	}
}

void dbdesc_t::init(const all_schemas_t& all_sw_schemas) {
	// init with what is defined by the current code; may be overrriden by
	// calling dbdesc_t::init with a schema retrieved from the database file

	this->p_all_sw_schemas = &all_sw_schemas; // save in case it is later needed
	schema_map.clear();
	index_map.clear();
	for (const auto& a : all_sw_schemas) {
		if(this->schema_version >=0 && this->schema_version != a.schema_version) {
			dterrorf("All schemas of all databases should have the same version but found versions {:d} and {:d}",
							 this->schema_version, a.schema_version);
		}
		this->schema_version = std::max(this->schema_version, a.schema_version);
		assert(a.pschema);
		auto& sw_schema = *a.pschema;
		for (const auto& sw_record_schema : sw_schema) {
			// store a default, current schema);
			auto [it, inserted] = schema_map.try_emplace(sw_record_schema.type_id, sw_record_schema);
			if (!inserted) {
				/*
					This test can fail in case type_ids overlap between e.g., epgdb and chdb
					@todo: the requirement could be relaxed to "only toplevel records, i.e., the ones
					stored in the database, must have distinct type_ids". The reason is that within a record
					type_ids are defined by the code (they are not stored in thedata) and we could detect
					to which database type (chdb, epgdb...) they relate
				*/
				dterrorf("several records have same  type_id 0x{:x}", sw_record_schema.type_id);
			}
			for(const auto& index: sw_record_schema.indexes)
				index_map.try_emplace(index.index_id, index);
		}
	}
}

void dbdesc_t::init(const all_schemas_t& all_sw_schemas, const ss::vector_<record_desc_t>& stored_schema) {
	init(all_sw_schemas);
	for (const auto& stored_record_schema : stored_schema) {
		// look up the slot for stored_record_schema.type_id for the currently processed struct
		auto* metadata = metadata_for_type(stored_record_schema.type_id);

		if (metadata != nullptr) {
			const auto& current_record_schema = metadata->record_desc;
			// store the schema info about this struct, and also compute offsets
			schema_map.at(stored_record_schema.type_id) = record_data_t(stored_record_schema, current_record_schema);
		}
	}
}

bool field_desc_t::operator==(const field_desc_t& other) const {
	if (field_id != other.field_id)
		return false;

	if (type_id != other.type_id)
		return false;

	if (serialized_size != other.serialized_size)
		return false;
	return true;
}

bool record_desc_t::operator==(const record_desc_t& other) const {
	if (type_id != other.type_id)
		return false;

	if (record_version != other.record_version)
		return false;
	int idx = 0;
	if (fields.size() != other.fields.size())
		return false;
	for (auto& field : fields) {
		if (idx >= other.fields.size())
			return false;
		auto& other_field = other.fields[idx++];
		if (field != other_field)
			return false;
	}
#if 0 //TODO does not work yet
	idx = 0;
	if(indexes.size() != other.indexes.size())
		return false;
	for (auto& index : indexes) {
		if (idx >= other.indexes.size())
			return false;
		auto& other_index = other.indexes[idx++];
		if (index != other_index)
			return false;
	}
#endif
	return true;
}


bool index_field_desc_t::operator==(const index_field_desc_t& other) const {
	if (name != other.name) //@todo: should be replaced with type based lookup (field_id)
		return false;
	return true;
}


bool index_desc_t::operator==(const index_desc_t& other) const {
	if (type_id != other.type_id)
		return false;

	if (index_id != other.index_id)
		return false;
	int idx = 0;
	if (fields.size() != other.fields.size())
		return false;
	for (auto& field : fields) {
		if (idx >= other.fields.size())
			return false;
		auto& other_field = other.fields[idx++];
		if (field != other_field)
			return false;
	}
	return true;
}


/*
	compare a schema "stored_schema" stored in a database
	and compare it to the current database schema as stoed in the source code
*/

bool check_schema(const dbdesc_t& stored, const dbdesc_t& current) {
	bool ret = true;
	if(current.schema_version != stored.schema_version)
		return false;
	for (const auto& [type_id, current_desc] : current.schema_map) {
		auto* stored_desc = stored.schema_for_type(type_id);
		if (!stored_desc) {
			dtdebugf("No descriptor for type {} in database\n", type_id);
			ret = false;
		} else {
			if (*stored_desc != current_desc.record_desc) {
				ret = false;
				dtdebugf("type {} has changed", type_id);
			}
		}
	}
	return ret;
}
