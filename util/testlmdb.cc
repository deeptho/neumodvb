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


#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include <lmdb++.h>


/*example of how to further position cursor and return
 */
auto f(lmdb::dbi&dbi, lmdb::txn& txn, const ss::string_& key) {
	//auto txn = lmdb::txn::begin(env, nullptr/*parent*/, MDB_RDONLY);
	lmdb::cursor ret(lmdb::cursor::open(txn, dbi));
	ret.find(key, MDB_SET_RANGE);
	std::string key1, value;
	//lmdb::cursor h(lmdb::cursor::open(ret, dbi));
	return ret;
}



int main() {

  /* Create and open the LMDB environment: */
  auto env = lmdb::env::create();
	env.set_max_dbs((MDB_dbi)10);
  env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL); /* 1 GiB */
  env.open("./example.mdb", MDB_NOTLS, 0664);

	lmdb::txn txn(lmdb::txn::begin(env));
 	lmdb::dbi dbi(lmdb::dbi::open(txn, "zorro", MDB_CREATE));
	txn.commit();
	std::thread t1( [&dbi, &env] () {
										auto txn = lmdb::txn::begin(env);
										printf("thread 1 started\n");
										sleep(2);
										dbi.put(txn, "xxusername3", "jhacker");
										dbi.put(txn, "xxemail3", "jhacker@example.org");
										dbi.put(txn,"xxfullname3", "J. Random Hacker");
										//printf("TXNid=%d\n", lmdb::txn_id(h));
										txn.commit();
										printf("thread 1 ended\n");
									}
		);

	std::thread t2( [&dbi, &env] () {
  /* Fetch key/value pairs in a read-only transaction: */
										printf("thread 2 started\n");
										sleep(1);
										//auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
										//auto dbi = lmdb::dbi::open(rtxn, nullptr);
										//auto cursor = lmdb::cursor::open(rtxn, dbi);
										//auto h = f(env);
										//try
										{
#if 1
											auto txn = lmdb::txn::begin(env, nullptr/*parent*/, MDB_RDONLY);
											ss::string_ username("username");
											auto h = f(dbi, txn, username);
#else
											auto txn = lmdb::txn::begin(env, nullptr/*parent*/, MDB_RDONLY);
											lmdb::cursor h(lmdb::cursor::open(txn, dbi));
											h.find("email", MDB_SET_RANGE);
#endif
											sleep(3);
											ss::string_ key, value;
											while (h.cursor::get(key, value, MDB_NEXT)) {
													std::printf("key2: '%s', value: '%s'\n", key.buffer(), value.buffer());
													//h.get(nullptr, nullptr, MDB_NEXT);
												}
												h.close();
												//lmdb::cursor_txn(h).abort();
												//h.cursor::close();
												//cursor.lmdb::txn::abort();
												printf("thread 2 ended\n");
											}
										//catch(...) {printf("thread 2: TABLE zorro not present\n");}
									}
		);

  /* Insert some key/value pairs in a write transaction: */
  auto wtxn = lmdb::txn::begin(env);
  auto dbik = lmdb::dbi::open(wtxn, "zorro", MDB_CREATE);
  dbik.put(wtxn, "username", "jhacker");
  dbik.put(wtxn, "email", "jhacker@example.org");
  dbik.put(wtxn, "fullname", "J. Random Hacker");
  wtxn.commit();

  /* Fetch key/value pairs in a read-only transaction: */
  auto rtxn = lmdb::txn::begin(env,  nullptr, MDB_RDONLY);
	//dbi = lmdb::dbi::open(rtxn, "kamiel");
  auto cursor = lmdb::cursor::open(rtxn, dbi);
  ss::string_ key, value;
  while (cursor.get(key, value, MDB_NEXT)) {
    std::printf("key: '%s', value: '%s'\n", key.buffer(), value.buffer());
  }
  cursor.close();
  rtxn.abort();
	t1.join();
	t2.join();
	printf("EXITING\n");
  /* The enviroment is closed automatically. */

  return EXIT_SUCCESS;
}
