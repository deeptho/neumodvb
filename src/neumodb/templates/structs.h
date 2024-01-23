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
#include <variant>
#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif

namespace {{dbname}} {
	struct {{struct.class_name}};
};
namespace data_types {
	template<> constexpr uint32_t data_type<{{dbname}}::{{struct.class_name}}>() {
		//defined in dbdefs.py
		return {{struct.type_id}};
	}
};

//forward declarations and data type helpers

namespace {{dbname}} {
	class  {{dbname}}_t;
	struct EXPORT {{struct.class_name}} {
		{% if struct.is_table %}
		using db_t = {{dbname}}_t;

		// enums

		enum class subfield_t : uint8_t {
		none=0, //needed when composing dynamic key names
			{% for field in struct.subfields %}
			{{field.name.replace('.','_')}},
		{% if loop.index == 1 -%}
		DEFAULT = {{field.name.replace('.','_')}},
		{%- endif %}
			{% endfor %}
		};

		enum class partial_keys_t : uint8_t {
		none,
		all,
//Partial index names
		{% for key in struct.keys %}
		{% for prefix in key.key_prefixes %}
		{% if not prefix.duplicate %}
    {{prefix.prefix_name}}, //prefix of an available index, with duplicates removed
		{% endif %}
		{% endfor %}
		{% endfor %}
		};

		enum class keys_t : uint8_t {
///Index names
			none,  // used as return value for key_for_subfield
			{% for key in struct.keys %}
			{{key.index_name}},
			{% for variant in key.variants %}
			{%if variant.name != key.index_name %}
			{{variant.name}} = {{key.index_name}},
			{% else %}
			//{{variant.name}} = {{key.index_name}}, key_name coincides with field name
		    {% endif %}
			{% endfor %}
			{% endfor %}
		};
		{% if struct.filter_fields|length %}
		/*
			used for the function matches, to define a high level matching
			function, which is limited to strict equality. This may be too limited (e.g. prefix match would be
			better for transponder frequency, case insensitive prefix match would be better for channel names,
			...
			It may be better to implement this in python
		*/
		enum class filter_fields_t :uint64_t {
			none= 0, //reserve
			{% for field in struct.filter_fields %}
			{{field.name}}=1ULL<< ((uint64_t){{loop.index}}-1), //{{field.key}}
			{% endfor %}
		};

		{% endif %} {#struct.filter_fields|length#}

		/*
			if a key starts with a specific subfield, then we can use this key
			for sorting (disregarding any other secondary fields  for sorting)
		*/
		HIDDEN static dynamic_key_t validate_sort_order (const dynamic_key_t& sort_order);
		static {{struct.class_name}}::keys_t key_for_sort_order (const dynamic_key_t& sort_order);

		HIDDEN static {{struct.class_name}}::keys_t key_for_prefix (const {{struct.class_name}}::partial_keys_t key_prefix);

		static uint32_t subfield_from_name (const char* subfield_name);

		{% endif %} // struct.is_table

		//data members
    {%for f in struct.fields %}
		 {{ f['type'] }} {{ f.name }} { {%-if f.default is not none %}{{f.default}}{%-endif%}};
    {% endfor %}

		//default constructor
		  {{struct.class_name}}() {
		}
		//move constructor

		//{{struct.class_name}}({{struct.class_name}}&&other ) =delete;

		 {{struct.class_name}}(const {{struct.class_name}}& other ) = default;

		//copy constructor
		 {{struct.class_name}}& operator=(const {{struct.class_name}}& other ) = default;

		//full constructor
		 {{struct.class_name}}(
		    {%for f in struct.fields %}
			{%if f.is_simple %}
				{{ f['type'] }} {{ f.name }}{%-if not loop.last  %}, {%-endif%}
			{% else %}
				const {{ f['type'] }}& {{ f.name }}{%-if not loop.last  %}, {%-endif%}
			{% endif %}
    {% endfor %}
			);

		//methods
		HIDDEN inline static ss::vector<int32_t, {{struct.fields|length}}> compute_static_offsets();
		{%if false %} //if we wish to enable getters for individual fields
    {%for f in struct.fields %}
		static {{f.type}} get_{{f.name}} (ss::bytebuffer_ &ser, const dbdesc_t& db, const record_data_t* record_data);
    {%endfor %}
		//if we wish to enable getters for individual fields
		{% endif %}

			//!static data
      constexpr static uint32_t type_id_  = data_types::data_type<{{struct.class_name}}>();

      /*! binary descriptor of record content
       */
			~{{struct.class_name}}() {
			}
			HIDDEN static const record_desc_t& descriptor();
			{% if struct.is_table %}
			 static  ss::bytebuffer<32>
				make_log_key(size_t txn_id, bool next_type =false);
			 static 	ss::bytebuffer<32>
				make_key({{struct.class_name}}::keys_t order, {{struct.class_name}}::partial_keys_t prefix,
								 const {{dbname}}::{{struct.class_name}}* ref=nullptr, bool next_type =false);
			 static 	ss::bytebuffer<32>
				make_key(const dynamic_key_t& order, {{struct.class_name}}::partial_keys_t prefix,
								 const {{dbname}}::{{struct.class_name}}* ref=nullptr, bool next_type =false);
			{% endif %}

{% if struct.is_table %}
{% for key in struct.keys %}
{% for prefix in key.key_prefixes  %}
{% if not prefix.duplicate %}

{% if key.primary %}
  /**
   * Positions this cursor at the given PRIMARY key.
   */
{% set cursor_type = "db_tcursor" %}
{% set key_name = 'primary' %}
{% else %}
  /**
   * Positions this cursor at the given SECONDARY key.
   */
{% set cursor_type = "db_tcursor_index" %}
{% set key_name = 'secondary' %}
{% endif %}

 static
{{cursor_type}}<{{struct.class_name}}> find_by_{{key.index_name}}
(db_txn& txn, {%- for field in prefix.fields %}
		const {{field.namespace}}{{field.scalar_type}}& {{field.short_name}},
	{%endfor -%}
		find_type_t find_type
   {% if prefix.is_full_key %}
   = find_eq
   {%endif%} ,
       {{struct.class_name}}::partial_keys_t key_prefix ={{struct.class_name}}::partial_keys_t::none
      {% if prefix.is_full_key %}, {{struct.class_name}}::partial_keys_t find_prefix =
{{struct.class_name}}::partial_keys_t::{{prefix.prefix_name}}
{% endif %}
	);

{% endif %}
{% endfor %}
{% endfor %}
{% endif %}

	 {% if struct.primary_key == None %}
	 {% elif struct.primary_key.fields|length >= 1 %}
      inline bool has_same_key(const {{struct.class_name}}& other) {
		    {%for f in struct.primary_key.fields %}
				if(!({{f.name}} == other.{{f.name}}))
					return false;
				{% endfor%}
				return true;
				}
   {% endif %}

	}; //end 	struct {{struct.class_name}}
		inline bool operator==(const {{struct.class_name}}& a, const {{struct.class_name}}& b) {
    {%for f in struct.fields  %}
		{%if f.name not in struct.ignore_for_equality_fields %}
		if (!(a.{{f.name}} == b.{{f.name}}))
			return false;
		{%else%}
		//field {{f.name}} ignored for equality test
		{%endif%}
		{%endfor%}
		return true;
		}

		inline bool operator!= (const {{struct.class_name}}& a, const {{struct.class_name}}& b) {
			return ! operator==(a,b);
		}

} //end of namespace {{dbname}}


{%if false %}//if we wish to enable getters for individual fields
class dbdesc_t;
class record_data_t;
//if we wish to enable getters for individual fields
{% endif %}


template<>
EXPORT int deserialize<{{dbname}}::{{struct.class_name}}>
(const ss::bytebuffer_ & ser,  {{dbname}}::{{struct.class_name}}& rec, int offset);


namespace {{dbname}} {

		//full constructor
			 inline {{struct.class_name}}::{{struct.class_name}}(
		    {%for f in struct.fields %}
				{%if f.is_simple %}
				{{ f['type'] }} {{ f.name }}{%-if not loop.last  %}, {%-endif%}
				{% else %}
				const {{ f['type'] }}& {{ f.name }}{%-if not loop.last  %}, {%-endif%}
				{% endif %}
    {% endfor %}
			) :
		    {%for f in struct.fields %}
		{{ f.name }}({{f.name}}){%-if not loop.last  %}, {%-endif%}
    {% endfor %}
		{
		}

} //end of namespace {{dbname}}




/*!{{struct.class_name}} deserialization code - to be used when stored data is known to be in exactly the right format

 */

template<>
int deserialize<{{dbname}}::{{struct.class_name}}>(
	const ss::bytebuffer_ & ser,  {{dbname}}::{{struct.class_name}}& rec, int offset);



/*!{{struct.class_name}} deserialization code - to be used when stored data is not known
	to be in exactly the right format
 */
EXPORT int deserialize_safe
(const ss::bytebuffer_& ser, {{dbname}}::{{struct.class_name}}& rec, const dbdesc_t& db);



/*!{{struct.class_name}} serialization code
 */
template<>
EXPORT void serialize<{{dbname}}::{{struct.class_name}}>(
	ss::bytebuffer_ &out, const {{dbname}}::{{struct.class_name}}& in);;

//!{{struct.class_name}} serialized size
template<>
EXPORT int serialized_size<{{dbname}}::{{struct.class_name}}>(
	const {{dbname}}::{{struct.class_name}}& in);


/*!{{struct.class_name}} encoding code
 */
EXPORT void encode_ascending(ss::bytebuffer_ &ser, const {{dbname}}::{{struct.class_name}}& in);

//struct {{struct.class_name}}
namespace {{dbname}} {
  {%for key in struct.keys%}
	{%if true %}
	inline bool cmp_{{key.index_name}}_key(const {{struct.class_name}}& oldrecord, const {{struct.class_name}}& newrecord) {
		{%for field in key.fields %}
		if(!(oldrecord.{{field.name}} == newrecord.{{field.name}}))
			return false;
		{%endfor%}
		return true;
	}
	{%endif%}
	{%endfor%}
}; //namespace {{dbname}}


{%if struct.is_table %}

namespace {{dbname}}::{{struct.name}} {


	ss::vector_<{{struct.class_name}}> list(db_txn& txn);

/*!
	lists records in the dabase.
	The records are returned in a selected order, which must match
	one of the indexes in the database:
  		{% for key in struct.keys -%}
		   {% for variant in key.variants -%}
		    {%if variant.name != key.index_name %}
    		  {{variant.name}} = {{key.index_name}},
		    {% endif %}
       {% endfor %}
       {% endfor %}

	Records are returned only if they are in the database range defined by key_prefix:
		none, all,
		{%- for key in struct.keys -%}
		{%- for prefix in key.key_prefixes -%}
		{%- if not prefix.duplicate -%}
    {{prefix.prefix_name}},
		{%- endif -%}
		{%- endfor -%}
		{%- endfor %}


	and if they match with the reference record ref_fields if one is provided in fields indicated by	field_filter,
	which is a logical or of:
      {% for f in struct.filter_fields -%} {{f.name}},  {%- endfor %}

	At most num_records records are returned if num_records>0.
	The first returned one is ref if ref!=nullptr, else it is the first
	record in the database range
	If offset!=0 then the returned records are those with index offset, offset+1.... where offset==0
  corresponds to ref if ref!=nullptr, else to the start of the range
 */
	ss::vector_<{{struct.class_name}}>
	list(db_txn& txn,
			 {{struct.class_name}}::keys_t order, //Defines the order in which to list record (and which index to use
			 {{struct.class_name}}::partial_keys_t key_prefix_, //restrict to portion of database (efficient; uses key)
			 {{struct.class_name}}* ref =nullptr, //record serving as reference; used by offset
			 uint64_t field_filter = 0, //restrict to records with matching field(s); inefficient
			 {{struct.class_name}}* ref_filter =nullptr, //record serving as reference; used by field_filter
			int num_records =-1, //desired number of records
			int offset =0 /*first returned item will come offset (if offset>0) records after or -offset (offset<0)
											before the reference
										*/
		);


  {%for key in struct.keys%}
  	{%if not key.primary%}
  /*!
    Positions this cursor at the first SECONDARY key of this type.
   */
	db_tcursor_index<{{struct.class_name}}>  find_first_sorted_by_{{key.index_name}}(db_txn& txn);

  	{%endif%}
	{%endfor%}

};

{%endif%}



/////////////////
{%for key in struct.keys%}
namespace {{dbname}}::{{struct.name}} {
{% if key.primary %}
  /*!
		List all by given PRIMARY key.
   */
{% else %}
  /*!
		List all by given SECONDARY key.
   */
{% endif %}
 EXPORT ss::vector_<{{struct.class_name}}> list_all_by_{{key.index_name}}(db_txn& txn);
}; //end namespace  {{dbname}}::{{struct.name}}
	{% endfor  %}

/////////////////
{%if struct.is_table %}

namespace {{dbname}}::{{struct.name}} {


	 EXPORT ss::vector_<{{struct.class_name}}> list_all(db_txn& txn, uint32_t order, bool use_index=false);
	  std::shared_ptr<screen_t<{{struct.class_name}}>> make_screen(db_txn& txn, uint32_t order, bool use_index);

	  EXPORT inline ss::vector_<{{struct.class_name}}> list_all(
		db_txn& txn, {{struct.class_name}}::keys_t order, bool use_index=false)
  {
		return list_all(txn, uint32_t(order), use_index);
  }

	 EXPORT int index_in_vector(const ss::vector_<{{struct.class_name}}>& haystack, const {{struct.class_name}}& needle);

}; //end namespace  {{dbname}}::{{struct.name}}
{% endif %}


/////////////////
template<>
 constexpr inline int32_t compile_time_serialized_size<{{dbname}}::{{struct.class_name}}>()  {
	int32_t ret =0;
	using namespace {{dbname}};
		{%for f in struct.fields %}
		{
			{%if f.has_variable_size%}
			  return -1;
			{%else%}
				auto x = compile_time_serialized_size<{{f.type}}>();
			ret += x;
			{%endif%}
		}
		{% endfor %}
		return ret;
}




{% if struct.is_table %}
{% for key in struct.keys %}
{% if key.primary %}
namespace {{dbname}} {
/*!
	Find a record by a full or partial key specified by the arguments.
	The returned cursor points to the record if find_type==find_eq, or to a nearby
	record otherwise.
	The cursors key_prefix is set to all records for the specified key (so if a partial key is
	specified, the cursor can be moved to records which do not start with the cursors key_prefix
 */
	inline void make_primary_key(ss::bytebuffer_& primary_key, const {{struct.class_name}} & record)
{
	primary_key =
		{{struct.class_name}}::make_key({{struct.class_name}}::keys_t::{{key.index_name}},
																		{{struct.class_name}}::partial_keys_t::all,
																		&record);
}

	inline void make_secondary_key(ss::bytebuffer_& secondary_key, dynamic_key_t sort_order,
																 const {{struct.class_name}} & record)
{
	secondary_key =
		{{struct.class_name}}::make_key(sort_order,
																		{{struct.class_name}}::partial_keys_t::all,
																		&record);
}
}; //end namespace  {{dbname}}::{{struct.name}}
{% endif %}
{% endfor %}
{% endif %}
