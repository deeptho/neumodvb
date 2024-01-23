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
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <filesystem>
#include "stackstring.h"
#include "stackstring_impl.h"
#include "cursors.h"
#include "util/template_util.h"
#include "neumodb/neumodb.h"
#include "neumodb/schema/schema_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/statdb/statdb_extra.h"
#include "neumodb/epgdb/epgdb_extra.h"
#include "neumodb/recdb/recdb_extra.h"

namespace fs = std::filesystem;



template<typename db_t>
int neumodb_upgrade(const char* from_dbname, const char* to_dbname,
										bool force_overwrite, bool inplace_upgrade, bool dont_backup)
{
	std::error_code err;
	unsigned int put_flags = 0;
	db_t from_db;
	db_t to_db;
	ss::string<128> backup_name;
	if (!to_dbname) {
		backup_name  = from_dbname;
		for(; backup_name.size()>0 && backup_name[backup_name.size()-1]=='/'; ) {
			backup_name.resize_no_init(backup_name.size()-1);
		}
		if(backup_name.size()==0)
			return -1;
		backup_name.format(".");
		backup_name.format("%Y%m{:d}_%H:%M:%S", time(NULL));
		to_dbname = backup_name.c_str();
	}

	const bool allow_degraded_mode = true;
	from_db.open(from_dbname, allow_degraded_mode);


	/*
		first we save to backup_db. This will be later renamed to out_db or swapped with in_db
		in case of an inplace update
	 */
	if(!to_dbname) {
		fprintf(stderr, "to_dbname not valid\n");
		return -1;
	}
	auto path_to = fs::path(to_dbname);
	if(fs::exists(path_to, err)) {
		if(force_overwrite) {
			auto num_deleted = fs::remove_all(path_to, err);
			if(err || num_deleted==0) {
				fprintf(stderr, "Error removing %s\n", to_dbname);
				return -1;
			}
		} else {
		fprintf(stderr, "Refusing to overwrite %s\n", to_dbname);
		return -1;
		}
	} else if(err) {
		fprintf(stderr, "Error while deleting %s\n", to_dbname);
		return -1;
	}

	to_db.open_without_log(to_dbname);
	if(convert_db(from_db, to_db, put_flags)<0) {
		fprintf(stderr, "Conversion failed; cleaning up %s\n", to_dbname);
		auto num_deleted = fs::remove_all(path_to, err);
		if(err || num_deleted==0) {
			fprintf(stderr, "Error cleaning up (removing %s)\n", to_dbname);
		}
		return -1;
	}

	///////////specific for recdb ////////////////////
	if constexpr (is_same_type_v<db_t, recdb::recdb_t>) {

		epgdb::epgdb_t from_epgdb (from_db);
		epgdb::epgdb_t to_epgdb (to_db);
		from_epgdb.open_secondary("epg", allow_degraded_mode);
		to_epgdb.open_secondary("epg");
		if(convert_db(from_epgdb, to_epgdb, put_flags)<0) {
			fprintf(stderr, "Conversion (epg) failed; cleaning up %s\n", to_dbname);
			auto num_deleted = fs::remove_all(path_to, err);
			if(err || num_deleted==0) {
				fprintf(stderr, "Error cleaning up (epg) (removing %s)\n", to_dbname);
			}
			return -1;
		}

		chdb::chdb_t from_chdb (from_db);
		chdb::chdb_t to_chdb (to_db);
		from_chdb.open_secondary("service", allow_degraded_mode);
		to_chdb.open_secondary("service");
		if(convert_db(from_chdb, to_chdb, put_flags)<0) {
			fprintf(stderr, "Conversion (service) failed; cleaning up %s\n", to_dbname);
			auto num_deleted = fs::remove_all(path_to, err);
			if(err || num_deleted==0) {
				fprintf(stderr, "Error cleaning up (service) (removing %s)\n", to_dbname);
			}
			return -1;
		}

				//recdb has actually two recdbs: the main table and the table named "idx"
		recdb::recdb_t from_idxdb (from_db);
		recdb::recdb_t to_idxdb (to_db);
		from_idxdb.open_secondary("idx", allow_degraded_mode);
		to_idxdb.open_secondary("idx");
		if(convert_db(from_idxdb, to_idxdb, put_flags)<0) {
			fprintf(stderr, "Conversion (idx) failed; cleaning up %s\n", to_dbname);
			auto num_deleted = fs::remove_all(path_to, err);
			if(err || num_deleted==0) {
				fprintf(stderr, "Error cleaning up (idx) (removing %s)\n", to_dbname);
			}
			return -1;
		}

	}
	///////////end: specific for recdb ////////////////////
	if(inplace_upgrade) {
		//atomically replace input and output db
			if(file_swap(from_dbname, to_dbname)<0) {
				//fileswa- failed
				dterrorf("error renaming  {} to {}: {}",  from_dbname,
								 to_dbname, strerror(errno));
				fprintf(stderr, "Conversion failed; cleaning up %s\n", to_dbname);
				auto num_deleted = fs::remove_all(path_to, err);
				if(err || num_deleted==0) {
					fprintf(stderr, "Error cleaning up (removing %s)\n", to_dbname);
				}
				return -1;
		} else {
			//now we are left with an upgraded in_db and a backup
			if(dont_backup) {
				//note that path_to  now points to the input database, which has been renamed
				auto num_deleted = fs::remove_all(path_to, err);
				if(err || num_deleted==0) {
					fprintf(stderr, "Error cleaning up (removing %s)\n", to_dbname);
				}
				return -1;
			}
		}
	}
	return 0;
}

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif
