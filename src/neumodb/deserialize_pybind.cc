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
#include "neumodb/cursors.h"
#include "neumodb/deserialize.h"
#include "neumodb/metadata.h"
#include "neumodb/dbdesc.h"
#include "neumodb/schema/schema.h"
#include <pybind11/pybind11.h>
#include <stdio.h>

namespace py = pybind11;

#define INVALID {py::cast<py::none>(Py_None), offset}




static inline
std::tuple<py::object,int> deserialize_int_to_python(const ss::bytebuffer_&ser, int foreign_type_id, int offset) {
	int64_t val;
	auto new_offset = deserialize_int(ser, val, foreign_type_id, offset);
	if(new_offset>=0) {
		py::int_ val_{val};
		return {val_, new_offset};
	}
	assert(0);
	return {py::cast<py::none>(Py_None), offset};
}

static inline
std::tuple<py::object,int> deserialize_float_to_python(const ss::bytebuffer_&ser, int foreign_type_id, int offset) {
	float32_t val;
	auto new_offset = deserialize_float(ser, val, foreign_type_id, offset);
	if(new_offset>=0) {
		py::float_ val_{val};
		return {val_, new_offset};
	}
	assert(0);
	return {py::cast<py::none>(Py_None), offset};
}

static inline
std::tuple<py::object,int> deserialize_boolean_to_python(const ss::bytebuffer_&ser, int foreign_type_id, int offset) {
	int64_t val;
	auto new_offset = deserialize_int(ser, val, foreign_type_id, offset);
	if(new_offset>=0) {
		py::bool_ val_{(bool)val};
		return {val_, new_offset};
	}
	assert(0);
	return {py::cast<py::none>(Py_None), offset};
}

static inline
std::tuple<py::object,int> deserialize_string_to_python(const ss::bytebuffer_&ser, int foreign_type_id, int offset) {
	ss::string<256> val;
	auto new_offset = deserialize(ser, val, offset);
	if(new_offset>=0) {
		py::str val_{val.c_str()};
		return {val_, new_offset};
	}
	assert(0);
	return {py::cast<py::none>(Py_None), offset};
}




std::tuple<py::object, int> deserialize_safe_to_python(const ss::bytebuffer_ &ser,
																											 const record_desc_t& foreign_record_desc,
																											 const dbdesc_t& db, size_t offset);


std::tuple<py::object, int> deserialize_builtin_safe_to_python
(const ss::bytebuffer_& ser, const dbdesc_t& db, uint32_t type_id, size_t offset)  {
	assert(data_types::is_builtin_type(type_id));
	if(data_types::is_int_type(type_id)) {
		//type_id &= ~  data_types::enumeration;
		return deserialize_int_to_python(ser, type_id, offset);
	} else if(data_types::is_float_type(type_id)) {
		return deserialize_float_to_python(ser, type_id, offset);
	} else 	if(data_types::is_boolean_type(type_id)) {
		//type_id &= ~  data_types::enumeration;
		return deserialize_boolean_to_python(ser, type_id, offset);
	}	 else 	if(data_types::is_string_type(type_id)) {
		//type_id &= ~  data_types::enumeration;
		return deserialize_string_to_python(ser, type_id, offset);
	}
	assert (0);
	return {py::cast<py::none>(Py_None), -1};
}

std::tuple<py::object, int> deserialize_safe_to_python(const ss::bytebuffer_ &ser,
																											 const record_desc_t& foreign_record_desc,
																											 const dbdesc_t& db, size_t offset);


std::tuple<py::object, int> deserialize_field_safe_to_python
(const ss::bytebuffer_& ser, const field_desc_t& foreign_field, size_t offset,  const dbdesc_t& dbdesc)  {
	auto type_id = foreign_field.type_id;
	auto* subschema = data_types::is_builtin_type(type_id) ? nullptr : dbdesc.schema_for_type(type_id);
	if (type_id == data_types::variant) {
		uint32_t variant_type_id;
		offset = deserialize(ser, variant_type_id, offset);
		if(offset<0)
			return INVALID;
		auto* subschema = dbdesc.schema_for_type(type_id);
		return  deserialize_safe_to_python(ser, *subschema, dbdesc, offset);
	} else if (type_id & data_types::vector) {
		uint32_t size;
		offset = deserialize(ser, size, offset);
		if(offset<0)
			return INVALID;
		auto limit = offset + size;
		py::list list;
		while (offset<limit) {
			auto [ val, new_offset] =
				subschema?
				deserialize_safe_to_python(ser, *subschema, dbdesc, offset) :
			deserialize_builtin_safe_to_python(ser, dbdesc, type_id, offset);
			offset = new_offset;
			if(offset<0)
				return INVALID;
			list.append(val);
		}
		return {list, offset};
	} else  if(data_types::is_builtin_type(type_id)) {
		return deserialize_builtin_safe_to_python(ser, dbdesc, type_id, offset);
	}  else if (subschema) { //user defined scalar type
		return deserialize_safe_to_python(ser, *subschema,  dbdesc, offset);
	} 	else {
		assert(0);
	}
	return {py::cast<py::none>(Py_None), -1};
}

std::tuple<py::object, int> deserialize_safe_to_python(const ss::bytebuffer_ &ser,
																											 const record_desc_t& foreign_record_desc,
																											 const dbdesc_t& db, size_t offset) {
	py::dict dict;
	for (auto& field: foreign_record_desc.fields) {
		auto [val, new_offset] = deserialize_field_safe_to_python(ser, field, offset, db);
		dict[field.name.c_str()] = val;
		if(new_offset<0)
			return { py::cast<py::none>(Py_None), -1};
		offset = new_offset;
	}
	return {dict, offset};
}


#if 0
py::object test(db_txn& txn) {
	ss::bytebuffer_ ser(val, sizeof(val));
	ss::bytebuffer_ serk(key, sizeof(key));
	uint32_t type_id =  0x630073;
	decode_ascending(type_id, serk, 0);
	auto& dbdesc = *txn.pdb->dbdesc;
	auto it = dbdesc.schema_map.find(type_id);
	if (it != dbdesc.schema_map.end()) {
		auto [val, new_offset] = deserialize_safe_to_python (ser, it->second.schema, dbdesc, 0);
		return val;
	}
	return py::dict{};
}
#endif


py::object degraded_export(db_txn& txn) {
	auto& db = *txn.pdb;
	py::list list;
	schema::neumo_schema_t s;
	auto cs = schema::neumo_schema_t::find_by_key(txn, s.k, find_eq);
	dbdesc_t stored_dbdesc;
	auto saved = txn.pdb->schema_is_current;
	//the following hackish line is needed to be able to read the schema
	txn.pdb->schema_is_current = true;
	if(cs.is_valid()) {
		auto rec = cs.current();
		ss::vector_<record_desc_t> converted;
		convert_schema(rec.schema, converted);
		stored_dbdesc.init(converted);
	}
	txn.pdb->schema_is_current = saved;
	auto c = db.generic_get_first(txn);
	for (auto status = c.is_valid(); status; status = c.next()) {
		ss::bytebuffer<32> key;
		c.get_serialized_key(key);
		if (key.size() <= (int)sizeof(uint32_t)) {
			dterrorf("This key is too short");
			continue;
		}
		uint32_t type_id;
		decode_ascending(type_id, key, 0);
		//read record
		auto it = stored_dbdesc.schema_map.find(type_id);
		if (it != stored_dbdesc.schema_map.end()) {
			ss::bytebuffer<256>  ser;
			c.get_serialized_value(ser);
			//const auto& tst = it->second;
			const auto& schema = it->second.record_desc;
			auto [val, new_offset] = deserialize_safe_to_python (ser, schema, stored_dbdesc, 0);
			py::dict rec;
			rec["type"] = schema.name.c_str();
			rec["data"] = val;
			list.append(rec);
		}
	}
	return std::move(list);
}


namespace schema {
	extern void export_structs(py::module& m);
};

#ifdef PURE_PYTHON
py::dict schema_map(db_txn& txn) {
	using namespace schema;
	using namespace data_types;
	neumo_schema_t s;
	py::dict ret;
	// TODO: find a way to upgrade a readonly txn to a write txn
	auto c = neumo_schema_t::find_by_key(txn, s.k, find_eq);
	if (c.is_valid()) {
		auto schema = c.current();
		//schema.k;
		ret["version"]=schema.version;
		ret["db_type"]= py::str(schema.db_type.c_str());
		py::dict records_dict;
		auto& schema_records =  schema.schema;
#if 0
		auto field_type_name = [&schema_records]( int32_t type_id) -> const char* {
			if(is_builtin_type(type_id))
				return typename_for_type_id(type_id).c_str();
			type_id &= ~data_types::vector & ~data_types::enumeration;
			for(const auto& record: schema_records) {
				if( record.type_id == type_id )
					return record.name.c_str();
			}
			return "";
		};
#endif
		for(const auto& schema_record: schema_records) {
			py::dict record_dict;
			record_dict["type_id"] = schema_record.type_id;
			record_dict["record_version"] = schema_record.record_version;
			record_dict["name"] = py::str(schema_record.name.c_str());
			py::list fields_list;
			for(const auto& field: schema_record.fields) {
				py::dict field_dict;
				field_dict["field_id"] = field.field_id;
				field_dict["type_id"] = field.type_id;
				field_dict["type"] = field.type.c_str();
				field_dict["serialized_size"] = field.serialized_size;
				field_dict["name"] = field.name.c_str();
				fields_list.append(field_dict);
			}
			record_dict["fields"] = fields_list;
			records_dict[schema_record.name.c_str()] = record_dict;
		}
		ret["records"] = records_dict;
		return ret;
	}
	return py::cast<py::none>(Py_None);
}
#endif

schema::neumo_schema_t schema_map(db_txn& txn) {
	using namespace schema;
	neumo_schema_t s;
	auto c = neumo_schema_t::find_by_key(txn, s.k, find_eq);
	if (c.is_valid()) {
		return c.current();
	}
	return {};
}

#ifdef PURE_PYTHON
std::string typename_for_type_id(int32_t type_id) {
	return data_types::typename_for_type_id(type_id);
}
#endif

void export_deserialize(py::module& m) {
	static bool called = false;
	if (called)
		return;
	called = true;
	//schema::export_structs(m);
	m.def("degraded_export", &degraded_export)
		.def("schema_map", &schema_map)
#ifdef PURE_PYTHON
		.def("schema_map2", &schema_map)
		.def("typename", &typename_for_type_id)
#endif
		;
}


PYBIND11_MODULE(pydeser, m) {
	m.doc() = R"pbdoc(
        Pybind11 channel database
        -----------------------

        .. currentmodule:: pydeser

        .. autosummary::
           :toctree: _generate

    )pbdoc";


	export_deserialize(m);
}
