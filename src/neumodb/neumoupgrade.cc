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

#include "cursors.h"
#include "neumodb/neumodb.h"
#include "neumodb/neumodb_upgrade.h"
#include "stackstring.h"
#include "stackstring_impl.h"
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdio.h>
#include <time.h>

#include <boost/program_options.hpp>

using namespace boost;
namespace po = boost::program_options;
namespace fs = std::filesystem;

namespace devdb {
	struct devdb_t;
}

namespace chdb {
	struct chdb_t;
}

namespace epgdb {
	struct epgdb_t;
}

namespace statdb {
	struct statdb_t;
}

namespace recdb {
	struct recdb_t;
}

struct options_t {
	std::string in_db;
	std::string out_db;
	std::string backup_db;
	std::string db_type;
	bool inplace_upgrade = false;
	bool dont_backup = false;
	bool force_overwrite = false;
	options_t() = default;

	int parse_options(int argc, char** argv);
};

int options_t::parse_options(int argc, char** argv) {
	po::options_description desc("NeumoDVB database upgrader");
	try {
		desc.add_options()
			("usage,u", "show usage")
			("input,i", po::value<std::string>(&in_db)
			 ,"Name of database to convert")
			("output,o", po::value<std::string>(&out_db)
			 ->default_value("")
			 ,"Where to store output (default is upgrade in place)")
			("backup,b", po::value<std::string>(&backup_db)
			 ->default_value("")->implicit_value(""),"Name of backup created when upgrading in place "
			 "(default is to generate a datestamped name)")
			("no-backup,n",
			 "Skip creating a backup")
			("force-overwrite,f", po::value<bool>(&force_overwrite)
			 ->implicit_value(false), "Overwrite existing output or backup")
			("db-type,t", po::value<std::string>(&db_type)
			 ->implicit_value("chdb"), "database type")
			;

		po::positional_options_description pd;
		pd.add("input", 1);

		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);
		po::notify(vm);
		if (vm.count("usage")) {
			std::cerr << desc << "\n";
			return -1;
		}
		if (vm.count("input") == 0) {
			std::cerr << "missing argument input"
								<< "\n";
			std::cerr << desc << "\n";
			return -1;
		} else {
			in_db = fs::canonical(fs::path(in_db)); // clean trailing slashes and such
		}
		if (vm["output"].defaulted()) {
			inplace_upgrade = true;
			dont_backup = false;
		} else if (vm.count("output") > 0) {
			inplace_upgrade = false;
			dont_backup = true;
			out_db = fs::weakly_canonical(fs::path(out_db)); // clean trailing slashes and such
		}
		if (vm.count("no-backup") > 0)
			dont_backup = true;
		if (vm.count("force-overwrite") > 0)
			force_overwrite = true;
		if (backup_db.size() == 0) {
			// user specified -b, without a value
			ss::string<128> tmp;
			tmp = in_db.c_str();
			tmp.format(".");
			tmp.format(":%Y%m%d_%H:%M:%S", time(NULL));
			backup_db = tmp.c_str();
		} else {
			backup_db = fs::canonical(fs::path(backup_db)); // clean trailing slashes and such
		}
	} catch (std::exception& e) {
		std::cerr << e.what() << "\n";
		std::cerr << desc << "\n";
		return -1;
	}
	return 0;
}

int main(int argc, char** argv) {
	std::error_code err;

	options_t options;
	if (options.parse_options(argc, argv) < 0)
		return -1;
	auto* from_dbname = options.in_db.c_str();
	auto* to_dbname = options.inplace_upgrade ? options.backup_db.c_str() : options.out_db.c_str();
	bool force_overwrite = options.force_overwrite;
	bool inplace_upgrade = options.inplace_upgrade;
	bool dont_backup = options.dont_backup;
	if (options.db_type == "devdb") {
		return neumodb_upgrade<devdb::devdb_t>(from_dbname, to_dbname, force_overwrite, inplace_upgrade, dont_backup);
	} else  if (options.db_type == "chdb") {
		return neumodb_upgrade<chdb::chdb_t>(from_dbname, to_dbname, force_overwrite, inplace_upgrade, dont_backup);
	} else if (options.db_type == "statdb") {
		return neumodb_upgrade<statdb::statdb_t>(from_dbname, to_dbname, force_overwrite, inplace_upgrade, dont_backup);
	} else if (options.db_type == "epgdb") {
		;
		return neumodb_upgrade<epgdb::epgdb_t>(from_dbname, to_dbname, force_overwrite, inplace_upgrade, dont_backup);

	} else if (options.db_type == "recdb") {

		return neumodb_upgrade<recdb::recdb_t>(from_dbname, to_dbname, force_overwrite, inplace_upgrade, dont_backup);

	} else {
		fprintf(stderr, "Illegal db_type: %s\n", options.db_type.c_str());
	}
	return -1;
}
