/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

void neumodb_t::init(const all_schemas_t& all_sw_schemas) {
	dbdesc = std::make_unique<dbdesc_t>();
	dbdesc->init(all_sw_schemas);
}

neumodb_t::neumodb_t(bool readonly_, bool is_temp_, bool autoconvert_)
	:  is_temp(is_temp_), readonly(readonly_), autoconvert(autoconvert_)
	, envp(std::make_shared<lmdb::env>(lmdb::env::create())) {
}

neumodb_t& neumodb_t::operator=(const neumodb_t& other) {
	envp = other.envp;
	dbdesc = other.dbdesc;
	return *this;
}

neumodb_t::neumodb_t(const neumodb_t& main)
	: is_temp(main.is_temp), readonly(main.readonly), autoconvert(main.autoconvert), envp(main.envp)
//, dbdesc(main.dbdesc) deliberately not copied, as the copy of the neumodb_t needs to be initialised
{}

neumodb_t::~neumodb_t() {
		close();
	}

inline auto decode_ascending(uint32_t x) {
	auto& encoded = (std::array<uint8_t, sizeof(uint32_t)>&)x;
	return (encoded[0] << 24) | (encoded[1] << 16) | (encoded[2] << 8) | (encoded[3]);
}

/*!
	read an old database record by record using from_txn, transforming the records to
	the latest format and storing them in to_txn

	put_flags can be set to MDB_APPEND for databases (e.g. recdb) which
	are always appended. This results in a lot of space saving as no free room
	is left between records.
*/
int convert_db(neumodb_t& from_db, neumodb_t& to_db, unsigned int put_flags) {
	try {
		// using namespace schema;
		auto from_txn = from_db.rtxn();
		auto from_cursor = from_db.generic_get_first(from_txn);
		auto to_txn = to_db.wtxn();

		/*Check if both databases are related; this does NOT compare if the stored
			schemas match, but rather that the programmer does not try to convert
			unrelated databases; the test is a partial test (checks pointers)
		*/
		assert(from_db.dbdesc->p_all_sw_schemas == to_db.dbdesc->p_all_sw_schemas);
		auto& current = *to_db.dbdesc;
		for (auto status = from_cursor.is_valid(); status; status = from_cursor.next()) {
			ss::bytebuffer<32> key;
			from_cursor.get_serialized_key(key);
			if (key.size() <= (int)sizeof(uint32_t)) {
				dterror("This key is too short");
				continue;
			}
			auto encoded_type_id = *(uint32_t*)key.buffer();
			auto type_id = decode_ascending(encoded_type_id);
			auto* desc = current.schema_for_type(type_id);
			if (desc == nullptr) {
				if (type_id == data_types::data_type<schema::neumo_schema_t>()) {
					dtdebug("schema record found");
				} else {
					dtdebugx("unrecognized type=0x%x", type_id);
				}
				continue;
			}
			to_db.convert_record(from_cursor, to_txn, type_id);
		}

		// add a schema to the output database
		to_db.store_schema(to_txn, put_flags);
		to_txn.commit();
	} catch (...) {
		dterror("EXCEPTION occurred");
		return -1;
	}
	return 1;
}

int neumodb_t::wait_for_activity(int old_txn_id) {
	std::unique_lock<std::mutex> lk(activity_mutex);
	activity_cv.wait(lk, [this, old_txn_id] { return last_txn_id > old_txn_id; });
	return last_txn_id;
}

void neumodb_t::open(const char* dbpath, bool allow_degraded_mode, const char* table_name, bool use_log,
										 size_t mapsize) {
	bool was_open = is_open_;
	is_open_ = true;
	try {
		if (!readonly) {
			mkpath(std::string(dbpath));
		}
		if (is_temp) {
			envp->set_max_dbs((MDB_dbi)128);
			envp->open(dbpath, MDB_NOTLS | MDB_NOSYNC | MDB_WRITEMAP, 0664); // MDB_NOTLS not needed?
		} else {
			envp->set_max_dbs((MDB_dbi)12);
			envp->open(dbpath, MDB_NOTLS | extra_flags, 0664); // MDB_NOTLS not needed?
		}
		envp->set_mapsize(mapsize);
		clean_stale_readers(dbpath);
		// Open the channel database in a transaction
		lmdb::txn txn(lmdb::txn::begin(*envp));
		/* all data is stored in a single table, for simplicity.
			 all index data is stored in a separate table to allow duplicate secondary keys
		*/
		if (table_name) {
			ss::string<16> name;
			name.sprintf("%s_data", table_name);
			dbi = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE));
			name.clear();
			name.sprintf("%s_index", table_name);
			dbi_index = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE | MDB_DUPSORT));
			name.clear();
			name.sprintf("%s_log", table_name);
			dbi_log = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE | MDB_DUPSORT));
		} else {
			dbi = lmdb::dbi(lmdb::dbi::open(txn, "data", MDB_CREATE));
			dbi_index = lmdb::dbi(lmdb::dbi::open(txn, "index", MDB_CREATE | MDB_DUPSORT));
			dbi_log = lmdb::dbi(lmdb::dbi::open(txn, "log", MDB_CREATE | MDB_DUPSORT));
		}
		txn.commit();
	} catch (...) {
		dterrorx("Fatal error opening lmdb database %s", dbpath);
		is_open_ = false;
		assert(0);
	}
	if (load_and_check_schema() < 0) {
		if (allow_degraded_mode) {
			dtdebug("opening database in degraded mode");
		} else {
			is_open_ = false;
			throw db_needs_upgrade_exception("Bad database, or database needs to be upgraded");
		}
	}

	this->use_log = use_log;

	/*
		TODO:
		1. load schema in db; if there is no schema, either abort or install a new schema,
		(based on whether the caller created the database?)
		The schema should be stored in the database itself with a special empty key correspinding to the scmae
		2. compare the two schemas. The smas are different if any type stored contains different
		field ids or field types
		3. abort if a difference is encountered or operate in readonly degraded mode.
	*/
}

void neumodb_t::close() {
	if (envp) {
		/*envp->close(); Do not call this because env may be used by other (secondary) databases
			destructor will take care of it
		*/
		envp.reset();
	}
}

/*
	Open a temporary database with a unique name, stored as a subdir of where
*/
void neumodb_t::open_temp(const char* where, bool allow_degraded_mode, const char* table_name, size_t mapsize) {
	is_open_ = true;
	assert(!readonly);
	try {
		ss::string<256> templ{"/tmp/neumo.mdb.XXXXXX"};
		char* path = mkdtemp(templ.c_str());
		if (!path) {
			dterrorx("mkdtemp %s failed: %s", templ.c_str(), strerror(errno));
			return;
		}
		open(path, allow_degraded_mode, table_name, mapsize);
		std::filesystem::remove_all(std::filesystem::path(path));
		use_log = false;
	} catch (...) {
		dterrorx("Fatal error opening temporary lmdb database in %s", where);
		is_open_ = false;
		use_log = false;
		assert(0);
	}
}

void neumodb_t::open_secondary(const char* table_name, bool allow_degraded_mode) {
	is_open_ = true;
	assert(table_name);
	try {
		// Open the channel database in a transaction
		lmdb::txn txn(lmdb::txn::begin(*envp));
		// all data is stored in a single table, for simplicity
		// all index data is stored in a separate table to allow duplicate secondary keys
		ss::string<16> name;
		name.sprintf("%s_data", table_name);
		dbi = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE));
		name.clear();
		name.sprintf("%s_index", table_name);
		dbi_index = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE | MDB_DUPSORT));
		name.clear();
		name.sprintf("%s_log", table_name);
		dbi_log = lmdb::dbi(lmdb::dbi::open(txn, name.c_str(), MDB_CREATE | MDB_DUPSORT));
		txn.commit();
	} catch (...) {
		dterrorx("Fatal error opening lmdb database %s", table_name);
		is_open_ = false;
		throw;
	}
	if (load_and_check_schema() < 0) {
		if (allow_degraded_mode) {
			dtdebug("opening database in degraded mode");
		} else {
			is_open_ = false;
			throw std::runtime_error("Bad database, or database needs to be upgraded");
		}
	}
	use_log = false;
}

void neumodb_t::drop_table(bool del) {
	lmdb::txn txn(lmdb::txn::begin(*envp));

	::mdb_drop(txn.handle(), dbi, del ? 1 : 0);
	::mdb_drop(txn.handle(), dbi_index, del ? 1 : 0);
	::mdb_drop(txn.handle(), dbi_log, del ? 1 : 0);
	txn.commit();
}

#if 0
void print_schema(schema::neumo_schema_t& s) {
	for (const auto& r : s.schema) {
		printf("record: type=%d vers=%d num_fields=%d\n", r.type_id, r.record_version, r.fields.size());
		for (const auto& i : r.fields) {
			printf("   field: id=%d type=%d ser_size=%d type=%s name=%s\n", i.field_id, i.type_id, i.serialized_size,
						 i.type.c_str(), i.name.c_str());
		}
	}
}
#endif

int neumodb_t::load_schema_(db_txn& txn) {
	using namespace schema;
	schema::neumo_schema_t s;
	schema_is_current = true;
	// TODO: find a way to upgrade a readonly txn to a write txn
	auto c = neumo_schema_t::find_by_key(txn, s.k, find_eq);
	if (c.is_valid()) {
		auto rec = c.current();
		db_type = rec.db_type;
#if 0
	 print_schema(stored);
#endif
		ss::vector_<record_desc_t> converted;
		convert_schema(rec.schema, converted);

		dbdesc_t stored;
		dbdesc_t current;
		current.init(*dbdesc->p_all_sw_schemas);
		stored.init(converted);
		if (check_schema(stored, current)) {
			schema_is_current = true;
			return 1;
		} else {
			dtdebug("database schema CHANGED\n");
			schema_is_current = false;
			/*
				Notify the code of the old schema so that record reading in degraded mode is possible
			*/
			dbdesc->init(*dbdesc->p_all_sw_schemas, converted);
			/*
				We operate in degraded mode
				1. it is not safe to attempt to find specific keys as all stored keys (primary/secondary)
				may have invalid formats and sizes.
				2. It is safe to iterated over all keys and decode the corresponding values,
				e.g., to convert a database
			*/
			return -1;
		}
	}
	return 0;
}

int neumodb_t::load_and_check_schema() {
	auto saved_use_dynamic_keys = use_dynamic_keys;
	use_dynamic_keys = false;
	using namespace schema;
	schema::neumo_schema_t s;
	{

		// TODO: find a way to upgrade a readonly txn to a write txn
		auto txn = this->rtxn();
		auto ret = load_schema_(txn);
		if (ret != 0) {
			use_dynamic_keys = saved_use_dynamic_keys;
			return ret;
		}
	}

	/*
		TODO The proper thing to do would be to only add a schema if we created the database
	*/
	auto txn = wtxn();
	store_schema(txn, 0);
	txn.commit();
	use_dynamic_keys = saved_use_dynamic_keys;
	return 0;
}

std::ostream& operator<<(std::ostream& os, field_matcher_t::match_type_t match_type) {
	typedef field_matcher_t::match_type_t m_t;
	switch (match_type) {
	case m_t::EQ:
		os << "EQ";
		break;
	case m_t::LEQ:
		os << "LEQ";
		break;
	case m_t::GEQ:
		os << "GEQ";
		break;
	case m_t::LT:
		os << "LT";
		break;
	case m_t::GT:
		os << "GT";
		break;
	case m_t::STARTSWITH:
		os << "STARTSWITH";
		break;
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const field_matcher_t& matcher) {
	os << matcher.match_type << "(" << ((int)matcher.field_id) << ")";
	return os;
}
