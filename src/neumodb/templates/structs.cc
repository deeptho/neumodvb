/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#include "neumodb/dbdesc.h"
#include "neumodb/{{dbname}}/{{dbname}}_db.h"
#include "neumodb/db_keys_helper.h"
#include "neumodb/screen_impl.h"
#define todo(x)
#define UNUSED __attribute__((unused))




{%if struct.is_table %}
namespace {{dbname}} {
		/*
			if a key starts with a specific subfield, then we can use this key
			for sorting (disregarding any other secondary fields  for sorting)
		*/
	dynamic_key_t  {{struct.class_name}}::validate_sort_order (const dynamic_key_t& sort_order)  {
		if(sort_order.fields.size()==0)
			return dynamic_key_t((uint8_t) keys_t::{{struct.primary_key.index_name}});
		auto s = subfield_t(sort_order.fields[0]);
		switch(s) {
		case subfield_t::none:
			//this must be a keys_t:: field, not a subfield_t::; this is encoded as 0,0,0, key, with key!=0
			if(sort_order.fields[1]!=0 || sort_order.fields[2] !=0 || //means
				 sort_order.fields[3] == 0 ) {
				dterror("Illegal sort_order none; using default");
				return dynamic_key_t((uint8_t) subfield_t::DEFAULT);
			}
		return sort_order;
			break;
				{% for field in struct.subfields %}
				{% if True or field.key  %}
			case subfield_t::{{field.name.replace('.','_')}}:
			return dynamic_key_t({(int)s, 0, 0,0});
			break;
			{% endif %}
			{% endfor %}
			default:
				return dynamic_key_t((uint8_t) subfield_t::DEFAULT);
			}
		}
};
{% endif %}

{%if struct.is_table %}
namespace {{dbname}} {
		/*
			if a key starts with a specific subfield, then we can use this key
			for sorting (disregarding any other secondary fields  for sorting)
		*/
	{{struct.class_name}}::keys_t {{struct.class_name}}::key_for_sort_order(const dynamic_key_t& sort_order)  {
		if(sort_order.fields.size()==0)
			return keys_t::{{struct.primary_key.index_name}};
		auto s = subfield_t(sort_order.fields[0]);
		switch(s) {
		case subfield_t::none:
			//thismust be a keys_t:: field, not a subfield_t::
			assert (sort_order.fields[1]==0);
			assert (sort_order.fields[2]==0);
			if(sort_order.fields[3]==0) {
				dterror("Illegal sort_order none; using default");
				return keys_t::{{struct.primary_key.index_name}};
			}
			return keys_t(sort_order.fields[3]);
			break;
			{% for field in struct.subfields %}
				{% if field.key  %}
			case subfield_t::{{field.name.replace('.','_')}}:
			return keys_t::{{field.key}};
			break;
			{% endif %}
			{% endfor %}
			default:
				return keys_t::{{struct.primary_key.index_name}};
			}
		}
};
{% endif %}


{% if struct.is_table %}
namespace {{dbname}} {
	{{struct.class_name}}::keys_t {{struct.class_name}}::key_for_prefix (
		const {{struct.class_name}}::partial_keys_t key_prefix)  {
		switch(key_prefix) {
			{% for key in struct.keys %}
			{% if key.primary %}
		case partial_keys_t::all:
		case partial_keys_t::none:
			return keys_t::{{key.index_name}};
			break;
			{% endif %}
		{% for prefix in key.key_prefixes %}
		{% if not prefix.duplicate %}
    case partial_keys_t::{{prefix.prefix_name}}:
		return keys_t::{{key.index_name}};
		break;
		{% endif %}
		{% endfor %}
		{% endfor %}
		default:
			return keys_t::none;
		}
		}
}; // namespace {{dbname}}
		{% endif %} {#struct.is_table #}

{% if struct.is_table %}
namespace {{dbname}} {
		/*
			if a key starts with a specific subfield, then we can use this key
			for sorting (disregarding any other secondary fields  for sorting)
		*/
		uint32_t {{struct.class_name}}::subfield_from_name (const char* subfield_name)  {
				{% for field in struct.subfields %}
				if(strcmp(subfield_name, "{{field.name}}")==0)
					return uint32_t(subfield_t::{{field.name.replace('.','_')}});
			{% endfor %}
			dterror("Illegal subfield name =" << subfield_name);
			return uint32_t(subfield_t::none);
		}
};
{% endif %}

{%if struct.is_table %}
template<>
bool screen_t<{{dbname}}::{{struct.class_name}}>::is_primary(const dynamic_key_t& order)
{
	switch(uint32_t(order)) {
		{% for key in struct.keys %}
	case uint32_t({{dbname}}::{{struct.class_name}}::keys_t::{{key.index_name}}):
	{
		{% if key.primary  %}
		return true;
		{% else  %}
		return false;
		{% endif %}
	}
	break;
	{% endfor %}
	default:
		return false;
		break;
	}
	return false;
}
{% endif %}


/*{{struct.class_name}} deserialization code - to be used when stored data is not known to be in exactly the right format
 */
int deserialize_field_safe
(const ss::bytebuffer_& ser, const field_desc_t& foreign_field, int offset,
 {{dbname}}::{{struct.class_name}}& rec, const dbdesc_t& db);


/*!{{struct.class_name}} deserialization code - to be used when stored data is known to be in exactly the right format

 */

template<>
int deserialize<{{dbname}}::{{struct.class_name}}>(
	const ss::bytebuffer_ & ser,  {{dbname}}::{{struct.class_name}}& rec, int offset)  {
	using namespace {{dbname}};
	{%for f in struct.fields %}
	{%-if f['type'].startswith('ss::') %}
	rec.{{f.name}}.clear();
	offset = deserialize(ser, rec.{{f.name}}, offset);
	{% elif f.is_variant %}
	{
		uint32_t type_id; //read {{f.type}} record
		offset = deserialize(ser, type_id, offset);
		if(offset<0)
			return offset;
	switch(type_id) {
		{% for variant_type in f.variant_types %}
	case data_types::data_type<{{variant_type}}>(): //current type for this field
	{
		{{variant_type}} content;
		offset = deserialize(ser, content, offset);
		if(offset>=0) {
			rec.{{f.name}} = content;
		}
		return offset;
	}
	break;
	{% endfor %}
	default:
		// this field_type does not exist currently and should be ignored
				return offset+foreign_field.serialized_size;
				break;
	}
	}
	{%else%}
	offset = deserialize(ser, rec.{{f.name}}, offset);
	{%endif%}
	if(offset<0)
		return offset;
	{%endfor %}
	return offset;
};

//!{{struct.class_name}} serialized size
template<>
int serialized_size<{{dbname}}::{{struct.class_name}}>(
	const {{dbname}}::{{struct.class_name}}& in)  {
	using namespace {{dbname}};
	/*
		serialize only needs to be called on fields which contain allocated data
	*/
	int ret = 0;
	{%for f in struct.fields %}
	{% if f.is_variant %}
	{
		auto ret = sizeof(uint32_t);
		for(;;) {
		{% for variant_type in f.variant_types %}
		if (std::holds_alternative<{{variant_type}}>(in.{{f.name}})) {
			ret += serialized_size(*std::get_if<{{variant_type}}>(&in.{{f.name}}));
			break;
		}
		{% endfor %}
		break;
		}
	}
	{% else %}
	ret += serialized_size(in.{{f.name}});
	{% endif%}
	{%endfor %}
	return ret;
};



namespace {{dbname}} {

     /*{{struct.class_name}} binary descriptor of record content
     */

	const record_desc_t&  {{struct.class_name}}::descriptor() {
     static const record_desc_t desc = {
       .type_id = {{struct.class_name}}::type_id_, //type_id
       .record_version = {{struct.record_version}}, //record_version
			 .name = "{{struct.name}}", //name
			 .fields = {//fields
          {%for f in struct.fields %}
             {
              .field_id = {{f.field_id}}, //field_id, unique identifier for this field within struct {{struct.class_name}}
							.type_id =
              {%if f.is_vector  %}
              data_types::vector|  //This is a vector type
              {%else%}
              {%endif%}
              (uint32_t)
							{% if f.is_variant %}
							data_types::variant, //type_id_, uniquely identifies data type
							{% else %}
							data_types::data_type<{{f.namespace}}{{f.scalar_type}}>(), //type_id_, uniquely identifies data type
							{% endif %}
							.serialized_size =
              {%if f.has_variable_size%}
              -1, // serialized_size: -1 means that this is a variable size record
              {%else%}
							 compile_time_serialized_size<{{f.type}}>(),
							 //sizeof({{struct.class_name}}::{{f.name}}), //serialized size
              {%endif%}
              .type = "{{f.type}}",   //type as seen in c++
              .name = "{{f.name}}"  //name as seen in c++
              }
           {%- if not loop.last -%}
                ,
           {% endif %}
           {%endfor %}
			 },

			 .indexes = {//indexes
          {%for key in struct.keys %}
					{%if key.index_id != None  %}
					{
						.type_id = {{struct.class_name}}::type_id_,
						.index_id = {{key.index_id}},
						.name = "{{key.index_name}}",
						.fields = {
          {%for field in key.fields %}
					{
						.name = "{{field.name}}"
					}
					{%- if not loop.last -%}
                ,
					{% endif %}
					{% endfor %}
							}
					}
					{%- if not loop.last -%}
                ,
					{% endif %}
					{% endif %}
					{%endfor %}
					}
		 };
		 return desc;
	}


     /*{{struct.class_name}} precomputed offsets
     */
     ss::vector<int32_t, {{struct.fields|length}}> {{struct.class_name}}::compute_static_offsets() {
     ss::vector<int32_t, {{struct.fields|length}}> out = {
          {%for f in struct.fields %}
              {%if not f.has_variable_size%}
              sizeof({{struct.class_name}}::{{f.name}})
              {%- else %}
               -1
              {%- endif -%}
           {%- if not loop.last -%}
                ,  //serialized size
           {% else %}
                  //serialized size
           {%- endif -%}
           {%endfor %}

       {{ '}' }};
     int offset = 0;
					for(int i=0; i< (int) out.size(); ++i) {
       std::swap(offset, out[i]);
       if(offset<0)
          break;
       if(i>0)
         offset += out[i];
     }
     return out;
     }



		 {%if false %} //if we wish to enable getters for individual fields
    {%for f in struct.fields %}
		{{f.type}}
		{{struct.class_name}}::get_{{f.name}}
		(ss::bytebuffer_ &ser, const dbdesc_t& db, const record_data_t* record_data) {
			using namespace {{dbname}};
			  {{f.type}} ret{ {%-if f.default is not none %}{{f.default}}{%-endif%}};

				auto offset = record_data->field_offset_for_index(ser, {{loop.index-1}});
				if (offset == dbdesc_t::NOTFOUND)
					return ret;
					if constexpr (data_types::is_builtin_type<{{f.namespace}}{{f.scalar_type}}>()) {
							//scalar builtin or vector of builtins
							if(deserialize(ser, ret, offset)<0)//calls the serialization code on stackstring.h
								throw std::runtime_error("deserialisation failed");
						} else {
						auto& subschema = record_data->schema;
					{%if f.is_vector -%}
							//vector of user defined structures
							uint32_t size;
							offset = deserialize(ser, size, offset);
							if(offset< 0)
								throw std::runtime_error("deserialisation failed");
							auto limit = offset + size;
							for (int i=0; offset<limit; ++i) {
								offset = deserialize_safe(ser, subschema, ret[i], db, offset);
								if(offset<0)
									throw std::runtime_error("deserialisation failed");
							}
          {%else%}
							//user defined structure
					{
						auto sta =deserialize_safe(ser, subschema,  ret, db, offset);
						if(sta<0)
							throw std::runtime_error("deserialisation failed");
					}
					{%endif%}

								}
       return ret;
      }
    {%endfor %}
		//if we wish to enable getters for individual fields
		{% endif %}

}  //end of namespace {{dbname}}


{%if struct.is_table %}
		{%for key in struct.keys %}
		{%if not key.primary %}
namespace {{dbname}} {
	namespace {{struct.name}} {
	static void update_secondary_key_{{key.index_name}}
	(
		db_tcursor_index<{{struct.class_name}}>& idx, bool exists, {{struct.class_name}}& oldrecord,
	 const ss::bytebuffer_& primary_key, const {{struct.class_name}}& newrecord) {
		if(exists) { //record existed in primary index, so must also exist in secondary index
			auto new_secondary_key =
				{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																				{{struct.class_name}}::partial_keys_t::all, &newrecord);
			if (!cmp_{{key.index_name}}_key(oldrecord, newrecord)) {
				//The fields needed for the keys differ in the old and new record
				auto old_secondary_key =
					{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																					{{struct.class_name}}::partial_keys_t::all, &oldrecord);

				if(old_secondary_key != new_secondary_key) {
					/*This can happen, e.g., if a name changes case: names are different but key is same
					 */
					bool was_present = idx.del_kv(old_secondary_key, primary_key);
#pragma unused (was_present)
					bool new_record = idx.put_kv(new_secondary_key, primary_key);
#pragma unused (new_record)
          //record was moved to another index place
				} else {
					/*Even though the key's fields differ, the key turned out to be the same (e.g.,
						because the field is case insensitive
						//TODO: implement two different cmp_ functions, with the new one providing
						consistent results for keys
					*/
				}
			} else {
				//record remains in same place in index, but has changed (otherwise no need to update this index)
			}
		} else { // new record
			auto new_secondary_key =
				{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																				{{struct.class_name}}::partial_keys_t::all, &newrecord);
			bool new_record = idx.put_kv(new_secondary_key, primary_key);
			#pragma unused (new_record)
		}
	}
	}   //	namespace {{struct.name}}
}    //	namespace {{dbname}}
{% endif %}
{% endfor %}
{% endif %}


{%if struct.is_table %}
namespace {{dbname}} {

	static void update_secondary_key_dynamic(
		const dynamic_key_t& order,
		db_tcursor_index<{{struct.class_name}}>& idx, bool exists, {{struct.class_name}}& oldrecord,
	 const ss::bytebuffer_& primary_key, const {{struct.class_name}}& newrecord)
	{
		if(exists) { //record existed in primary index, so must also exist in secondary index
			auto new_secondary_key =
				{{struct.class_name}}::make_key(order,
																				{{struct.class_name}}::partial_keys_t::all, &newrecord);
			auto old_secondary_key =
				{{struct.class_name}}::make_key(order,
																				{{struct.class_name}}::partial_keys_t::all, &oldrecord);
			bool different = old_secondary_key !=new_secondary_key;
			if (different) {
				bool was_present = idx.del_kv(old_secondary_key, primary_key);
#pragma unused (was_present)
				bool new_record = idx.put_kv(new_secondary_key, primary_key);
#pragma unused (new_record)
			} else {
			}
		} else { // new record
			//toremove
			auto new_secondary_key =
				{{struct.class_name}}::make_key(order,
																				{{struct.class_name}}::partial_keys_t::all, &newrecord);
			bool new_record = idx.put_kv(new_secondary_key, primary_key);
#pragma unused (new_record)
		}
	}
}; //namespace {{dbname}}
{% endif %} {# struct.is_table #}



{%if struct.is_table %}
namespace {{dbname}} {
	template<>
		void update_secondary_keys<{{struct.class_name}}>
			(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, const {{struct.class_name}}& newrecord) {
			{{struct.class_name}} oldrecord;
			bool exists = get_record_at_key(tcursor, primary_key, oldrecord);
#pragma unused (exists)
			{%if struct.keys|length %}
			auto idx = tcursor.txn.pdb->tcursor_index<{{struct.class_name}}>(tcursor.txn);
			{% endif %}
			if(!tcursor.txn.pdb->is_temp) {
			{% for key in struct.keys %}
				{%if not key.primary %}
				{{struct.name}}::update_secondary_key_{{key.index_name}}
				(idx, exists, oldrecord, primary_key, newrecord);
				{% endif %}
			{%endfor%}
			}
			if(tcursor.txn.pdb->use_dynamic_keys)
				for(auto order: tcursor.txn.pdb->dynamic_keys) {
					update_secondary_key_dynamic(
						order, idx, exists, oldrecord, primary_key, newrecord);
				}

		}
}; //namespace {{dbname}}
{%endif%}



int deserialize_field_safe
(const ss::bytebuffer_& ser, const field_desc_t& foreign_field, size_t offset,
 {{dbname}}::{{struct.class_name}}& rec, const dbdesc_t& dbdesc)  {
	using namespace {{dbname}};
	auto field_id = foreign_field.field_id;
	switch(field_id) {
		{%for f in struct.fields %}
	case {{f.field_id}}: { // {{f.name}}
		auto current_type_id =
			{%if f.is_vector  %}
			data_types::vector|  //This is a vector type
			{%else%}
		{%endif%}
		{% if f.is_variant %}
		data_types::variant; //current type for this field
		{% else %}
		data_types::data_type<{{f.namespace}}{{f.scalar_type}}>(); //current type for this field
    {% endif %}
		if(foreign_field.type_id == current_type_id) { //currently, conversion is limited to literal copying
			{% if f.is_variant %}
			uint32_t type_id;
			offset = deserialize(ser, type_id, offset);
			if(offset<0)
				return offset;
			switch(type_id) {
				{% for variant_type in f.variant_types %}
			case data_types::data_type<{{variant_type}}>(): //current type for this field
			{
				{{variant_type}} content;
			  offset = deserialize(ser, content, offset);
				if(offset>=0) {
					rec.{{f.name}} = content;
				}
				return offset;
			}
			break;
				{% endfor %}
			default:
				// this field_type does not exist currently and should be ignored
				return offset+foreign_field.serialized_size;
				break;
			}
			{% else %}
			if constexpr (data_types::is_builtin_type<{{f.namespace}}{{f.scalar_type}}>()) {
					//scalar builtin or vector of builtins
					return deserialize(ser, rec.{{f.name}}, offset); //calls the serialization code on stackstring.h
				} else {
				auto* subschema = dbdesc.schema_for_type(current_type_id);
				if (subschema) {
					{%if f.is_vector -%}
					//vector of user defined structures
					uint32_t size;
					offset = deserialize(ser, size, offset);
					if(offset< 0)
						return -1;
					auto limit = offset + size;
					for (int i=0; offset<limit; ++i) {
						offset = deserialize_safe(ser, *subschema, rec.{{f.name}}[i], dbdesc, offset);
						if(offset<0)
							return -1;
					}
					return offset;
					{%else%}
					//user defined structure
					{
						return deserialize_safe(ser, *subschema,  rec.{{f.name}}, dbdesc, offset);
					}
					{%endif%}
				}
			}
			{% endif %} {#is_variant#}
		} else {
			{% if f.is_int %}
			using namespace data_types;
			if (is_builtin_type(foreign_field.type_id)) {
				//int={{f.is_int}}
				if(is_int_type(foreign_field.type_id)  &&
					 is_int_type(current_type_id)) {
					offset = deserialize_int(ser, rec.{{f.name}}, foreign_field.type_id, offset);
				}
			} else
			{% endif %}
				offset += foreign_field.serialized_size;
		}
		return offset;
	}
	break;
	{%endfor %}
	default:
		// this field does not exist currently and should be ignored
		if(foreign_field.serialized_size > 0)
			return offset+foreign_field.serialized_size;
		else {
			uint32_t size;
			if (deserialize(ser, size, offset) < 0) // skip over the variable size record. Safe even for foreign fields
				return -1;
			offset += sizeof(size) + size;
			return offset;
		}
		break;
	}
	return -1;
}



/*{{struct.class_name}} deserialization code - to be used when stored data is not known to be in exactly the right format
	The procedure is to loop over the foreign record, while computing offsets and calling the proper field
				serialization function
*/
template<>
 int deserialize_safe<{{dbname}}::{{struct.class_name}}>(
	const ss::bytebuffer_ &ser, const record_desc_t& foreign_record_desc,
	{{dbname}}::{{struct.class_name}}& rec, const dbdesc_t& db, size_t offset) {
	using namespace {{dbname}};
	for (auto& field: foreign_record_desc.fields) {
		auto new_offset = deserialize_field_safe(ser, field, offset, rec, db);
		if(new_offset<0)
			return -1;
#if 1
		offset = new_offset;
#else
		if (field.serialized_size >0)
			offset += field.serialized_size;
		else {
			uint32_t size;
			if(deserialize(ser, size, offset)<0) //skip over the variable size record. Safe even for foreign fields
				return -1;
			offset += sizeof(size) + size;
		}
#endif
	}
	return offset;
}

 int deserialize_safe
(const ss::bytebuffer_& ser, {{dbname}}::{{struct.class_name}}& rec, const dbdesc_t& dbdesc)
{
	auto type_id = {{dbname}}::{{struct.class_name}}::type_id_;
	auto it = dbdesc.schema_map.find(type_id);
	if(it != dbdesc.schema_map.end()) {
		return deserialize_safe(ser, it->second.record_desc, rec, dbdesc);
	}
	return -1;
}

/*!{{struct.class_name}} serialization code
 */
template<>
 void serialize<{{dbname}}::{{struct.class_name}}>(
	ss::bytebuffer_ &out, const {{dbname}}::{{struct.class_name}}& in)  {
	using namespace {{dbname}};
	{%for f in struct.fields %}
	{%-if f['type'].startswith('ss::') %}
	serialize(out, in.{{f.name}});
	{%elif f.is_variant%}
	{
		for(;;) {
		{% for variant_type in f.variant_types %}
		if (std::holds_alternative<{{variant_type}}>(in.{{f.name}})) {
			serialize(out, *std::get_if<{{variant_type}}>(&in.{{f.name}}));
			break;
		}
		{% endfor %}
		break;
		}
	}
	{%else%}
	serialize(out, in.{{f.name}});
	{%endif%}
	{%endfor %}
};

/*!{{struct.class_name}} encoding code
 */
template<>
 void encode_ascending<{{dbname}}::{{struct.class_name}}>(
	ss::bytebuffer_ &ser, const {{dbname}}::{{struct.class_name}}& in)  {
	using namespace {{dbname}};
	{% if struct.is_table %}
	//encode only primary key
	{%for f in struct.primary_key.fields %}
	  {%- if f.is_vector %}
	  for(const auto& v: in.{{f.name}}) {
		  encode_ascending(ser, v);
	  }
	  {%- else %}
	  encode_ascending(ser, in.{{f.name}});
	  {% endif %}
	  {%endfor %}
	{% else %}
  	{%for f in struct.fields %}
	  {%- if f.is_vector %}
	  for(const auto& v: in.{{f.name}}) {
		  encode_ascending(ser, v);
	  }
	  {%- else %}
	  encode_ascending(ser, in.{{f.name}});
	  {% endif %}
	  {%endfor %}
	 {% endif %}
};


{% if struct.is_table %}
/*!{{struct.class_name}} generic key encoding (used for temporary database storing sorted records)
	Note that this is  different from serialisation: for strings and vectors no size field is stored
	prior to the encoded data. As such, the encoding is irreversable
 */
template<>
void {{dbname}}::encode_subfields<{{dbname}}::{{struct.class_name}}>(
	ss::bytebuffer_ &out, const {{dbname}}::{{struct.class_name}}& in,
	const ss::vector_<uint8_t>&subfields)  {
	int count =0;
	for(auto field_: subfields) {
		/*Prevent long keys from being generated in the first place
			An example occurs when sorting lnbs by network. IN one example the key size is 594 because
			there are many networks.
			The "problem" occurs when using vectors. Often it would be enough to sort their string representation,
			but that requires a string representation to exist
		*/
			if(out.size() > 64) {
				out.resize(64);
				return;
			}

		auto field = ({{struct.class_name}}::subfield_t) field_;
		switch(field) {

	{%for f in struct.subfields %}
	{% if f.is_variant %}
	assert(0); //Implementation error. Fields of variants cannot be used as indices (fields not always available)
	{% if false %}
		case {{struct.class_name}}::subfield_t::{{f.name.replace('.','_')}}:
			{
		auto ret = sizeof(uint32_t);
		for(;;) {
		{% for variant_type in f.variant_types %}
		if (std::holds_alternative<{{variant_type}}>(in.{{f.name}})) {
			auto& content = *std::get_if<{{variant_type}}>(&in.{{f.name}});
			encode_subfields<{{variant_type}}>(content);
			break;
		}
		{% endfor %}
		break;
		}
	}
 {% endif %}
		break;
	{% else %}
		case {{struct.class_name}}::subfield_t::{{f.name.replace('.','_')}}:
		{% if f.is_string%}
		{ auto tmp = ss::tolower(in.{{f.name}});
		encode_ascending(out, tmp);
		}
		{%elif f.is_vector%}
			for(const auto &v :  in.{{f.name}}) {
				encode_ascending(out, v);
			}
		{%else%}
		encode_ascending(out, in.{{f.name}});
		{%endif%}
		break;
	{% endif %}
	{%endfor %}
		case {{struct.class_name}}::subfield_t::none:
			assert(count>0); //first set field should be nonzero
			return;
		default:
			assert(0);
		}
		count++;
	}
};
{% endif %}


{%if struct.is_table %}
namespace {{dbname}} {
	namespace {{struct.name}} {
	static void delete_secondary_key_dynamic(
		const dynamic_key_t& order,
		db_tcursor_index<{{struct.class_name}}>& idx, const {{struct.class_name}}& oldrecord,
		const ss::bytebuffer_& primary_key)
	{
     //record existed in primary index, so must also exist in secondary index
			auto secondary_key =
				{{struct.class_name}}::make_key(order,
																				{{struct.class_name}}::partial_keys_t::all, &oldrecord);

			bool was_present = idx.del_kv(secondary_key, primary_key);
#pragma unused (was_present)
	}
}; //	namespace {{struct.name}}
}; //namespace {{dbname}}
{% endif %} {# struct.is_table #}




{%if struct.is_table %}

namespace {{dbname}} {

		template<>
			 void delete_secondary_keys<{{struct.class_name}}>
		(db_tcursor<{{struct.class_name}}>& tcursor, const ss::bytebuffer_& primary_key, const {{struct.class_name}}& record) {
			auto idx = tcursor.txn.pdb->tcursor_index<{{struct.class_name}}>(tcursor.txn);
			{%for key in struct.keys%}
			{%if not key.primary %}
			if(!tcursor.txn.pdb->is_temp)
				{
					auto secondary_key =
						{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																				{{struct.class_name}}::partial_keys_t::all, &record);
					bool was_present = idx.del_kv(secondary_key, primary_key);
#pragma unused (was_present)
				}
				{%endif%}
			{%endfor%}
			if(tcursor.txn.pdb->use_dynamic_keys)
				for(auto k: tcursor.txn.pdb->dynamic_keys) {
					{{struct.name}}::delete_secondary_key_dynamic(
						k, idx, record, primary_key);
				}
		}

}; //namespace {{dbname}}
{%endif%}



using namespace {{dbname}};
//struct {{struct.class_name}}
  {%if struct.is_table %}
  {%set key = struct.primary_key%}
namespace {{dbname}} {

  /**
   * Positions this cursor at the first record PRIMARY of this type.
   */
	template<>
		 db_tcursor<{{struct.class_name}}> find_first<{{struct.class_name}}>(db_txn& txn) {
		//create a key which has only the type field set
		auto key_prefix =
			{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																			{{struct.class_name}}::partial_keys_t::none);
		assert(key_prefix.size() == (int)sizeof(uint32_t));
		auto idx = txn.pdb->tcursor<{{struct.class_name}}>(txn, key_prefix);
		if(!idx.find(key_prefix, MDB_SET_RANGE)) {
			idx.close();
			return idx;
		}

		return idx;
	}


  /**
   * Positions this cursor at the last record PRIMARY of this type.
   */
	template<>
		 db_tcursor<{{struct.class_name}}> find_last<{{struct.class_name}}>(db_txn& txn) {

		//create a key which has only the type field set
		const bool next_type = true; // point after
		auto key_prefix =
			{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																			{{struct.class_name}}::partial_keys_t::none);
		auto key_prefix_next =
			{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																			{{struct.class_name}}::partial_keys_t::none, nullptr, next_type);
		assert(key_prefix_next.size() == (int)sizeof(uint32_t));
		auto idx = txn.pdb->tcursor<{{struct.class_name}}>(txn/*, key_prefix*/);
		if(!idx.find(key_prefix_next, MDB_SET_RANGE)) {
			//There is no record type stored after the current one, so position cursor at very last record,
			//which is possibly of the desired type
			auto idx = txn.pdb->tcursor<{{struct.class_name}}>(txn, key_prefix);
			if(!idx.find(key_prefix, MDB_LAST)) {
				//database is empty
				idx.close();
				return idx;
			}
			/*database must be pointed either at the maximum record of the desired type or
				there is no record of the desired type and it points at some other type
			*/
			if(idx.is_valid()) {
				//we have reached a record of the desired type and it must be the last one
				return idx;
			}
			//there is no record of the desired type
			idx.close();
			return idx;
		}
		//we have found the first record of the "next" type
		idx.prev();
		idx.set_key_prefix(key_prefix);
		return idx;
	}

}//		namespace {{dbname}}

  {%endif%}


//struct {{struct.class_name}}
  {%if struct.is_table %}
	namespace {{dbname}}::{{struct.name}} {
		{%for key in struct.keys%}
  	{%if not key.primary %}

  /**
   * Positions this cursor at the first SECONDARY key of this type.
   */
		db_tcursor_index<{{struct.class_name}}>  find_first_sorted_by_{{key.index_name}}
	(db_txn& txn) {

		auto secondary_key_prefix =
			{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																			{{struct.class_name}}::partial_keys_t::none);

		assert(secondary_key_prefix.size() == (int)sizeof(uint32_t));

		auto idx = txn.pdb->tcursor_index<{{struct.class_name}}>(txn, secondary_key_prefix);

		if(!idx.find(secondary_key_prefix, MDB_SET_RANGE)) {
			idx.close();
			return idx;
		}
		{
			lmdb::val k{}, v{};
			bool ret = idx.get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
#pragma unused (ret)
			assert(ret);
			if((int)k.size() < secondary_key_prefix.size() || memcmp(k.data(), secondary_key_prefix.buffer(),
																													secondary_key_prefix.size())) {
				idx.close(); // we could not find a key of the correct type
			}
			if(!idx.maincursor.get(v, nullptr, MDB_SET))
				idx.close();
		}

		return idx;
	}

  	{%endif%}
	{%endfor%}
}//		namespace {{dbname}}::{{struct.name}}
  {%endif%}


{% if struct.is_table %}
{% for key in struct.keys %}
{% for prefix in key.key_prefixes %}
{% if not prefix.duplicate %}

{% if key.primary %}
  /**
   * Positions this cursor at the given PRIMARY key.
   */
{% set cursor_type = "db_tcursor" %}
{% set key_name = 'primary' %}
{% set key_type = 'primary_key_t' %}
{% else %}
  /**
   * Positions this cursor at the given SECONDARY key.
   */
{% set cursor_type = "db_tcursor_index" %}
{% set key_name = 'secondary' %}
{% set key_type = 'secondary_key_t' %}
{% endif %}
/*!
	Find a record by a full or partial key specified by the arguments.
	The returned cursor points to the record if find_type==find_eq, or to a nearby
	record otherwise.

	key_prefix: restricts the valid range based on a specific prefix part of the key
	find_prefix: restricts the lookup to a specific prefix part of the key.
  if find_prefix smaller then the key itself then  find_type may not equal find_eq.
 */
{{cursor_type}}<{{struct.class_name}}> {{struct.class_name}}::find_by_{{key.index_name}}
(db_txn& txn, {%- for field in prefix.fields %}
		const {{field.namespace}}{{field.scalar_type}}& {{field.short_name}},
	{%endfor -%}
		find_type_t find_type,  {{struct.class_name}}::partial_keys_t key_prefix
{% if prefix.is_full_key %}, {{struct.class_name}}::partial_keys_t find_prefix {% endif %})
{
   {{struct.class_name}} temp;
	 {%for field in prefix.fields %}
	temp.{{field.name}} = {{field.short_name}};
	{%endfor%}
	{% if not prefix.is_full_key %}
	/*This is a partial key. There is no point in searching for equality.
		This is a code error. The caller should use find_geq instead.
	*/
	assert(find_type!= find_type_t::find_eq || ("Incorrect code" == nullptr));
	auto find_prefix = {{struct.class_name}}::partial_keys_t::{{prefix.prefix_name}};
	{% else %}
	//This is is a full key
	assert(find_type!= find_type_t::find_eq ||
				 find_prefix == {{struct.class_name}}::partial_keys_t::{{prefix.prefix_name}} ||
				 find_prefix == {{struct.class_name}}::partial_keys_t::all || ("Incorrect code" == nullptr));
	{% endif %}
	auto key_prefix_ =
		{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																		key_prefix,  &temp);
	const auto& {{key_name}}_key = {{struct.class_name}}::make_key
														({{struct.class_name}}::keys_t::{{key.index_name}},
														 find_prefix, &temp);

  assert({{key_name}}_key.size() >= (int)sizeof(uint32_t));

  auto ret= {{key_type}}::find_by_serialized_key<{{struct.class_name}}>(txn, {{key_name}}_key, key_prefix_,
																																			find_type);
//@todo The make_key can be avoided in some cases
	ret.set_key_prefix(key_prefix_);
	return ret;
	}

{% endif %}
{% endfor %}
{% endfor %}
{% endif %}



/////////////////

{% if struct.is_table %}
namespace {{dbname}} {
	ss::bytebuffer<32>
	{{struct.class_name}}::make_key(
		{{struct.class_name}}::keys_t order, {{struct.class_name}}::partial_keys_t prefix,
	 const {{dbname}}::{{struct.class_name}}* ref,
		bool next_type)
	{
		ss::bytebuffer<32> out;

		switch(order) {
		default:
			dterrorx("Illegal key: %d; using default instead", (int)order);
			prefix =  {{struct.class_name}}::partial_keys_t::none;
		{% for key in struct.keys %}
		// key {{key.index_name}}
		case {{struct.class_name}}::keys_t::{{key.index_name}}:
		{
			//encode type identifier
			{% if key.primary %}
			encode_ascending(out, data_types::data_type<{{struct.class_name}}>());
			{% else %}
			encode_ascending(out, (uint32_t) {{key.index_id}});
			{% endif %}

			for(;;) {
				if (prefix=={{struct.class_name}}::partial_keys_t::none)
					break;
				if(ref == nullptr) {
					dterror("ref parameter must be specified");
					assert(0);
				}
				{% for prefix in key.key_prefixes | reverse %}
				{% set field = prefix['last_field'] %}
				{% if field.fun %}
			   {
					 auto temp= {{field.fun}}(ref->{{field.name}});
					 encode_ascending(out, temp);
				 }
		    {%else%}
			   encode_ascending(out, ref->{{field.name}});
		    {%endif%}
			if (prefix=={{struct.class_name}}::partial_keys_t::{{prefix.prefix_name}})
					 break;
		    {%endfor%}
			if(prefix == {{struct.class_name}}::partial_keys_t::all)
				break;
				dterror("illegal prefix for this call");
				assert(0);
			 }
			 break;
		}
	{% endfor %}
		}
		if(next_type) {
			auto* p = (uint8_t *) out.buffer();
			int i=out.size()-1;
			for(; i>=0; --i) {
				p[i]++;
				if(p[i]!=0) //0 means we need to progate the carry
					break;
			}
			if(i<0) {
				dterrorx("Implementation error");
			}
		}
		return out;
	}

	ss::bytebuffer<32>
	{{struct.class_name}}::make_key(
		const dynamic_key_t& order, {{struct.class_name}}::partial_keys_t prefix,
	 const {{dbname}}::{{struct.class_name}}* ref,
		bool next_type)
	{
		if(order.is_predefined()) {
			// shortcut if we detect predefined key
			auto neworder = ({{struct.class_name}}::keys_t) (uint8_t)(uint32_t)order;
			return make_key(neworder, prefix, ref, next_type);
		}
		ss::bytebuffer<32> out;
		/*
			This will always have MSB!=0 and so should not clash with predefined orders
		 */
		auto i = (uint32_t) order;
		assert((i&0xff000000)!=0);
		encode_ascending(out, i);
		if(ref)
			encode_subfields(out, *ref, order.fields);

		if(next_type) {
			auto* p = (uint8_t *) out.buffer();
			int i=out.size()-1;
			for(; i>=0; --i) {
				p[i]++;
				if(p[i]!=0) //0 means we need to progate the carry
					break;
			}
			if(i<0) {
				dterrorx("Implementation error");
			}
		}
		return out;
	}


}; //end namespace  {{dbname}}::{{struct.name}}

{% endif %}



{% if struct.is_table %}
namespace {{dbname}} {
	ss::bytebuffer<32>
	{{struct.class_name}}::make_log_key(size_t txn_id, bool next_type)
	{
		ss::bytebuffer<32> out;
		//encode type identifier
		encode_ascending(out, data_types::data_type<{{struct.class_name}}>());
		//encode txn id
		encode_ascending(out, txn_id);

		if(next_type) {
			auto* p = (uint8_t *) out.buffer();
			int i=out.size()-1;
			for(; i>=0; --i) {
				p[i]++;
				if(p[i]!=0) //0 means we need to progate the carry
					break;
			}
			if(i<0) {
				dterrorx("Implementation error");
			}
		}
		return out;
	}


}; //end namespace  {{dbname}}::{{struct.name}}
{% endif %}


//struct {{struct.class_name}}
{%if struct.is_table %}
namespace {{dbname}} {
	/*!
		compare two records and return true if they agree in all requested fields.
		flags is a logical or of
  {% for f in struct.subfields -%} {{f.name}},  {%- endfor %}
	 */
	inline bool matches(const {{struct.class_name}}& a, const {{struct.class_name}}& b,
												 const ss::vector<field_matcher_t>& field_matchers) {
		for(const auto & fm: field_matchers) {
			switch({{struct.class_name}}::subfield_t(fm.field_id)) {
			case {{struct.class_name}}::subfield_t::none:
				break;
				{% for f in struct.subfields -%}
				{%if f.is_vector -%}
			case {{struct.class_name}}::subfield_t::{{f.name.replace('.','_')}}:
				{
					const auto& aa = a.{{f.name}};
					const auto& bb = b.{{f.name}};
					if(aa.size() != bb.size())
						return false;
					for(int i=0; i < aa.size(); ++i) {
						if (aa[i] != bb[i])
							return false;
					}
				}
				break;
				{%else%}
			case {{struct.class_name}}::subfield_t::{{f.name.replace('.','_')}}:
			switch(fm.match_type) {
			default:
				continue;
			case field_matcher_t::match_type_t::EQ:
				if(!(a.{{f.name}} == b.{{f.name}}))
					return false;
				break;
				{%if f.is_string -%}
			case field_matcher_t::match_type_t::STARTSWITH:
				if(strncasecmp(a.{{f.name}}.c_str(), b.{{f.name}}.c_str(),
									 b.{{f.name}}.size()) !=0)
					return false;
				break;
				{%else%}
			case field_matcher_t::match_type_t::GEQ:
				if(a.{{f.name}} < b.{{f.name}})
					return false;
				break;
			case field_matcher_t::match_type_t::LEQ:
				if(a.{{f.name}} > b.{{f.name}})
					return false;
				break;
			case field_matcher_t::match_type_t::GT:
				if(a.{{f.name}} <= b.{{f.name}})
					return false;
				break;
			case field_matcher_t::match_type_t::LT:
				if(a.{{f.name}} >= b.{{f.name}})
					return false;
				break;
				{%endif%}
			}
			break;
			{%endif%}
				{%- endfor %}
			default:
				return true;
			}
		}
		return true;
	}

}//end namespace {{dbname}}
///////////////////////
{%endif%}


{% if struct.is_table %}
namespace {{dbname}}::{{struct.name}} {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
	template<typename cursor_t>
static void fill_list_db_(
	db_txn& txn,
	int pos_top, //return records starting from this position
	function_view<bool (const {{struct.class_name}}&)> match_fn,
#ifdef USE_END_TIME
	const ss::bytebuffer_& serialized_end_key,
#endif
	cursor_t& c,
	db_txn& wtxn,
	monitor_t* monitor = nullptr,
	dynamic_key_t* sort_order = nullptr)
	{
		assert(pos_top==0);
	//n is total number of records

	int n=0; //number of records
	if(monitor) {
		monitor->state.pos_top = -1; //position of top entry on screen, or -1 if screen is empty
		monitor->state.list_size = 0;
	}

	auto cw = wtxn.pdb->tcursor<{{struct.class_name}}>(wtxn/*, key_prefix*/);
	cw.drop(false);
	auto cwi = wtxn.pdb->tcursor_index<{{struct.class_name}}>(wtxn/*, key_prefix*/);
	cwi.drop(false);
	auto cwl = wtxn.pdb->tcursor_log<{{struct.class_name}}>(wtxn/*, key_prefix*/);
	cwl.drop(false);

	for (;c.is_valid(); c.next()) {
		auto x = c.current();
#ifdef USE_END_TIME
		//the following will check the secondary key for db_tcursor_index and the primary_key for db_tcursor
		if(serialized_end_key.size()>0 &&
			 cmp(c.current_serialized_key(), serialized_end_key) >=0)
			break;
#endif
		if (match_fn(x)) {
			/*@TODO: this  code allows initialising monitor.reference, avoiding a second full scan when later set_reference(const record_t&)  is called
				but it has not been tested yet
			*/
			if(sort_order) {
				const bool is_removal = false;
				ss::bytebuffer<32> secondary_key;
				make_secondary_key(secondary_key, *sort_order, x);
				auto primary_key = c.current_serialized_primary_key();
				monitor->reference.update(secondary_key, primary_key, is_removal);
			}
			put_record(cw, x);
			n++;
		}
	}
	if(monitor)
		monitor->state.list_size = n;
	return;
}
}; //end namespace  {{dbname}}::{{struct.name}}
{% endif %}



{%if struct.is_table %}
/*
	Fill a temporary database containing records of one specific type, sorted in arbitrary order.
	For use in GUI data screens
	txn: write transation
 */
template<>
void screen_t<{{dbname}}::{{struct.class_name}}>::fill_list_db
(db_txn&txn, db_txn& wtxn,
	 int num_records, //desired number of records to retrieve
 int pos_top,  //return num_records starting at position pos_top from top
 ss::vector_<field_matcher_t>& field_matchers,
 const {{struct.class_name}} * match_data,
 ss::vector_<field_matcher_t>& field_matchers2,
 const {{struct.class_name}} * match_data2,
 const {{struct.class_name}} * reference
) {
	using namespace {{dbname}};
	using namespace {{dbname}}::{{struct.name}};

	if(reference) {
		make_primary_key(monitor.reference.primary_key, *reference);

		make_secondary_key(monitor.reference.secondary_key, sort_order, *reference);

		monitor.reference.row_number = -1; //reset to find first record
		monitor.auxiliary_reference.row_number = -1; //reset
	}
	assert(match_data || field_matchers.size()==0);

	ss::bytebuffer_& start_key = limits.key_lower_limit;
#ifdef USE_END_TIME
	ss::bytebuffer_& end_key = limits.key_upper_limit;
#endif
	auto some_match_fn = [&field_matchers, &match_data](const {{struct.class_name}}& record) {
		return matches(record, *match_data, field_matchers);
	};

	auto all_match_fn = [](const {{struct.class_name}}& record) {
		return true;
	};



	if(is_primary(dynamic_key_t((uint8_t)limits.index_for_key_prefix))) {
		auto c = primary_key_t::find_by_serialized_key<{{struct.class_name}}>(txn, start_key,
																																					limits.key_prefix,
																																					find_type_t::find_geq);
			c.set_key_prefix(limits.key_prefix);

			if(field_matchers.size() ==0)
			{{struct.name}}::fill_list_db_(txn, pos_top,
																		 all_match_fn,
#ifdef USE_END_TIME
																		 end_key,
#endif
																		 c, wtxn,
																		 &monitor,
																		 reference ? &this->sort_order : nullptr
				);
			else
			{{struct.name}}::fill_list_db_(txn, pos_top,
																		 some_match_fn,
#ifdef USE_END_TIME
																		 end_key,
#endif
																		 c, wtxn,
																		 &monitor,
																		 reference ? &this->sort_order : nullptr
				);

	} else {
			auto c = secondary_key_t::find_by_serialized_key<{{struct.class_name}}>(txn, start_key,
																																							limits.key_prefix,
																																							find_type_t::find_geq);
			c.set_key_prefix(limits.key_prefix);

			if(field_matchers.size() ==0)
			{{struct.name}}::fill_list_db_(txn, pos_top,
																		 all_match_fn,
#ifdef USE_END_TIME
																		 end_key,
#endif
																		 c, wtxn,
																		 &monitor,
																		 reference ? &this->sort_order : nullptr
				);
			else
			{{struct.name}}::fill_list_db_(txn, pos_top,
																		 some_match_fn,
#ifdef USE_END_TIME
																		 end_key,
#endif
																		 c, wtxn,
																		 &monitor,
																		 reference ? &this->sort_order : nullptr
				);

		}
}

{% endif %}




{%if struct.is_table %}
	/*
		1) ref==null retrieve an initial screen containing the top n entries of a list
		2) ref!=null retrieve an initial screen containing n entries of a list around a reference entry
		filter by setting filter_type and filter_value
		list size and position are computed
	*/
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic pop
/*
	A screen is a slice of a list, either the list part shown on the screen or a slightly larger slice.
	Initialize a screen with at most num_records, containing record "ref" (or containing a record sorted
	close to ref if ref does not exist).
	If offset!=0, the slice is shifted such that the first returned item will come offset (if offset>0)
	records after or -offset (offset<0)
  records before the reference. Will be ignored if num_records<0
	If num_records<0, return as many as possible records (offset is ignored)
*/
{% endif %} {# struct.is_table #}


{%if struct.is_table %}

template struct screen_t<{{dbname}}::{{struct.class_name}}>;

{%if dbname != 'schema' %}

{% endif %}
{% endif %}



{%if struct.is_table %}
/*!
		3) retrieve a screen containing n entries starting at a given position (scrolling using scrollbar)
		4) move the screen up or down relative to the current screen, by a small number of entries (scrolling
		using navigation keys)

	 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic pop
/*
	A screen is a slice of a list, either the list part shown on the screen or a slightly larger slice.
	Initialize a screen with at most num_records, starting at position offset in the list
	If num_records<0, return as many as possible records
 */


{% endif %}





/////////////////
{%if true %}
{%for key in struct.keys%}
namespace {{dbname}}::{{struct.name}} {
{% if key.primary %}
  /**
   * List all by given PRIMARY key.
   */
{% set index_type = 'primary_key_t' %}
{% else %}
  /**
   * List all by given SECONDARY key.
   */
{% set index_type = 'secondary_key_t' %}
{% endif %}

EXPORT ss::vector_<{{struct.class_name}}> list_all_by_{{key.index_name}}(db_txn& txn)
{
	ss::vector_<{{struct.class_name}}> records;

	auto key_prefix_type = {{struct.class_name}}::partial_keys_t::none;
	auto sort_order = dynamic_key_t((uint8_t){{struct.class_name}}::keys_t::{{key.index_name}});
	bool next_type = false;
	auto key_prefix = {{struct.class_name}}::make_key(sort_order, key_prefix_type,
																										nullptr, next_type);

	::list<{{index_type}},  {{struct.class_name}}>(txn, key_prefix, key_prefix, records);
	return records;

}

}; //end namespace  {{dbname}}::{{struct.name}}
{% endfor %}
{% endif %}


/////////////////
{%if struct.is_table %}

namespace {{dbname}}::{{struct.name}} {

EXPORT ss::vector_<{{struct.class_name}}> list_all(db_txn& txn, uint32_t order, bool use_index)
																	 {
																		 using namespace {{dbname}};
#ifdef TO_IMPLEMENT
	/*in case the first column is also the first column of an index, we use the index and
		ignore the secondary sort columns.

		Otherwise, we open a temporary database, write the sorted records into it,
		and then start reading from the secondary database.
	*/
	auto sort_order = {{struct.class_name}}::validate_sort_order(dynamic_key_t(order));

	screen_t<{{struct.class_name}}> screen(txn, sort_order, use_index);
	return std::move(screen.records);
#else
	using namespace {{dbname}}::{{struct.name}};
		ss::vector_<{{struct.class_name}}> records;
	if(!use_index) {
		//@TODO: in this case we could create a screen  and return records from that
		assert(0);
		return records; //return an empty list
	}

	auto key_prefix_type = {{struct.class_name}}::partial_keys_t::none;
	auto sort_order = {{struct.class_name}}::validate_sort_order(dynamic_key_t(order));
	bool next_type = false;
	auto key_prefix = {{struct.class_name}}::make_key(sort_order, key_prefix_type,
																										nullptr, next_type);
	if(screen_t<{{struct.class_name}}>::is_primary(sort_order))
		::list<primary_key_t,  {{struct.class_name}}>(txn, key_prefix, key_prefix, records);
	else
		::list<secondary_key_t,  {{struct.class_name}}>(txn, key_prefix, key_prefix, records);
	return records;
#endif
}
}; //end namespace  {{dbname}}::{{struct.name}}

{% endif %}


/////////////////
{%if struct.is_table %}

namespace {{dbname}}::{{struct.name}} {
/*
	find a record in a vector of records, by comparing primary keys
	Returns -1 if not found
 */
	int index_in_vector(const ss::vector_<{{struct.class_name}}>& haystack, const {{struct.class_name}}& needle)
{
	using namespace {{dbname}};
	int i=0;
	for(auto& record: haystack) {
		if(cmp_{{struct.primary_key.index_name}}_key(record,needle))
			return i;
		++i;
	}
	return -1;
}

}; //end namespace  {{dbname}}::{{struct.name}}

{% endif %}
