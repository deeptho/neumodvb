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

#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/db_update.h"
#include "util/util.h"
#include <boost/program_options.hpp>

using namespace boost;
namespace po = boost::program_options;

struct options_t {
	std::string chdb{"/mnt/neumo/db/chdb.mdb/"};
	std::string logconfig{"config/neumo.xml"};
	int parse_options(int argc, char** argv);
};

options_t options;

int options_t::parse_options(int argc, char** argv) {
	po::options_description desc("NeumoDVB test program");
	try {
		desc.add_options()
			("usage,u", "show usage")
			("chdb,c", po::value<std::string>(&chdb)
			 ->default_value(config_path /chdb)
			 ,"Path to chdb database")
			;

		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
		po::notify(vm);
		if (vm.count("usage")) {
			std::cerr << desc << "\n";
			return -1;
		}

	} catch (std::exception& e) {
		std::cerr << e.what() << "\n";
		std::cerr << desc << "\n";
		return -1;
	}
	return 0;
}

int main(int argc, char** argv) {

	if (options.parse_options(argc, argv) < 0)
		return -1;

	auto log_path = config_path / "config/neumo.xml";

	set_logconfig(options.logconfig.c_str());

	chdb::chdb_t from_db;
	chdb::chdb_t to_db;
	from_db.open(options.chdb.c_str());
	to_db.open("/tmp/chdb.mdb");
	auto from_txn = from_db.rtxn();
	auto to_txn = to_db.wtxn();
	auto to_txnid = 0;
	update_from<chdb::service_t>(to_txn, from_txn, to_db, from_db, to_txnid);
	from_txn.commit();
	to_txn.commit();
}
