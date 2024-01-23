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

#include <map>
#include "stackstring.h"
#include "neumodb/metadata.h"
#include "neumodb/cursors.h"
#include "enums.h"
#include "neumodb/{{dbname}}/data_types.h"
#ifndef null_pid
#define null_pid 0x1fff
#endif

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif



{%for fname in includes_by_file %}
{% for include in includes_by_file[fname] %}
#include "{{include.include}}"
{%endfor%}
{%endfor%}

{%for fname in structs_by_file %}
#include "{{fname}}.h"
{%endfor%}

namespace {{dbname}} {
  //extern ss::vector<record_desc_t, {{structs|length}}> current_schema;

	class EXPORT {{dbname}}_t : public neumodb_t {
	public:
		 {{dbname}}_t();

		 virtual void open(const char* dbpath, bool allow_degraded_mode = false,
											 const char* table_name = NULL, bool use_log =true, size_t mapsize = 512*1024u*1024u) final;

		 {{dbname}}_t(bool readonly, bool is_temp=false, bool autoconvert=false, bool autoconvert_major_version=false);

		 {{dbname}}_t(const neumodb_t& other);

		 {{dbname}}_t(const {{dbname}}_t& other) :
		       {{dbname}}_t((const neumodb_t&) other)
							{}

		HIDDEN virtual int convert_record(db_cursor& from_cursor, db_txn& to_txn, uint32_t type_id, unsigned int put_flags=0);
		HIDDEN virtual void store_schema(db_txn& txn, unsigned int put_flags=0);

		static void clean_log(db_txn& txn, int to_keep=10000);
	};

}; //namespace {{dbname}}

namespace {{dbname}} {
	/*
		These values correspond to the prefix of the primary key
	 */
  enum class table_t : uint32_t  {
  {%for struct in structs %}
	{% if struct.is_table %}
	{{struct.name}} = data_types::data_type<{{struct.class_name}}>(),
	{% endif %}
  {% endfor %}
  };

}; //namespace {{dbname}}


#include "neumodb/{{dbname}}/{{dbname}}_keys.h"
