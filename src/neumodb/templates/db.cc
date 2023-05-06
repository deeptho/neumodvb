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
#include <time.h>
#include "stackstring.h"
#include "stackstring_impl.h"
#include "neumodb/db_keys_helper.h"
#include "neumodb/neumodb_upgrade.h"
#include "{{dbname}}_db.h"
#include "neumodb/dbdesc.h"

{%for db in external_dbs %}
namespace {{db}} {
	extern schema_entry_t get_sw_schema();
}; //end namespace  {{db}}
{%endfor%}

namespace {{dbname}} {
  ss::vector<record_desc_t, {{structs|length}}> sw_schema = {
  {%for struct in structs %}
     	{{ struct.class_name }}::descriptor()
     	{%- if not (loop.last and loop.last) -%}
        ,
      {% endif %}
  {% endfor %}
  };

	EXPORT schema_entry_t get_sw_schema() {
		return {"{{dbname}}", neumo_schema_version, &sw_schema };
	}

ss::vector<schema_entry_t, {{1 + external_dbs|length}}> all_sw_schemas {
	{ {{dbname}}::get_sw_schema() } ,
{%for extdb in external_dbs %}
{ {{extdb}}::get_sw_schema() } ,
{%endfor%}
};

	{{dbname}}_t::{{dbname}}_t () :
	neumodb_t(false /*readonly*/, false /*is_temp*/, true /*autoconvert*/, false /*autoconvert_major_version*/) {
		init(all_sw_schemas);
	}

	{{dbname}}_t::{{dbname}}_t (bool readonly, bool is_temp, bool autoconvert, bool autoconvert_major_version) :
	neumodb_t(readonly, is_temp, autoconvert, autoconvert_major_version) {
		init(all_sw_schemas);
	}

	{{dbname}}_t::{{dbname}}_t(const neumodb_t& other) :
	neumodb_t(other) {
		init(all_sw_schemas);
	}


}; //namespace {{dbname}}


void {{dbname}}::{{dbname}}_t::clean_log(db_txn&txn, int to_keep)
{
  {%for struct in structs %}
	{%if struct.is_table %}
	::clean_log<{{ struct.class_name }}>(txn, to_keep);
	{% endif %}
  {% endfor %}
}


void {{dbname}}::{{dbname}}_t::open(const char* dbpath, bool allow_degraded_mode,
																		const char* table_name, bool use_log,
																		size_t mapsize)
{
	try {
		neumodb_t::open_(dbpath, allow_degraded_mode, table_name, use_log, mapsize);
	} catch(const db_needs_upgrade_exception& e) {
		{% if dbname != "schema" %}

		if(!autoconvert) {
			dterrorx("Database %s needs update, but autoconvert not allowed", dbpath);
			throw db_needs_upgrade_exception("Database needs update, but autoconvert not allowed");
		}
		bool needs_major_upgrade = db_version != neumo_schema_version;
		if(needs_major_upgrade && ! autoconvert_major_version) {
			dtdebugx("Database needs major upgrade from %d to %d\n", db_version, neumo_schema_version);
			throw db_upgrade_info_t{db_version, neumo_schema_version};
		}
		dterrorx("Auto upgrading database %s", dbpath);
		const char* backup_name = nullptr;
		bool force_overwrite = true;
		bool inplace_upgrade = true;
		bool dont_backup = false;
		envp->close();
		auto ret=neumodb_upgrade<{{dbname}}::{{dbname}}_t>(dbpath, backup_name, force_overwrite, inplace_upgrade, dont_backup);
		if(ret<0) {
			dterrorx("Auto upgrading database %s FAILED", dbpath);
			throw e;
		} else {
			dterrorx("Auto upgrading database %s SUCCESS", dbpath);
			autoconvert = false; // prevent  a conversion loop
			autoconvert_major_version = false;
			//envp = std::make_shared<lmdb::env>(lmdb::env::create());
			*envp = lmdb::env::create(); //recreate environment which was closed earlier
			{{dbname}}::{{dbname}}_t::open(dbpath, allow_degraded_mode, table_name, use_log, mapsize);
		}

		{% else %}
		throw db_needs_upgrade_exception("Database needs update, but autoconvert not possible");
		{% endif %}
	}
}
