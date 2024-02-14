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

#include "devmanager.h"
#include "receiver.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/neumovariant.h"
#include "neumodb/devdb/tune_options.h"
#include <dirent.h>
#include <errno.h>
#include <functional>
#include <iomanip>
#include <map>
#include <libconfig.h++>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <bitset>

typedef int infd_t;
#define EVENT_SIZE (sizeof(struct inotify_event))

/*event name will be about 2 characaters. The extra NAME_MAX+1 ensures that even for a very long filename
	we will be able to read 1 event */

#define EVENT_BUF_LEN (4 * (EVENT_SIZE + 20) + NAME_MAX + 1)

static std::tuple<api_type_t, int>  get_dvb_api_type() {
	static std::mutex m;
	static std::tuple<api_type_t, int> cached = {api_type_t::UNDEFINED, -1};
	/*
		Note: multiple threads could simultaneously TRY to initialize the value, but only
		one will succeed.
	*/
	if (std::get<0>(cached) == api_type_t::UNDEFINED) {
		std::scoped_lock lck(m);
		if (std::get<0>(cached) != api_type_t::UNDEFINED) {
			// this could happen  if another thread beat us to it
			return cached;
		}
		using namespace libconfig;
		Config cfg;
		try {
			cfg.readFile("/sys/module/dvb_core/info/version");
		} catch (const FileIOException& fioex) {
			cached = {api_type_t::DVBAPI, 5000};
			return cached;
		} catch (const ParseException& pex) {
			return {api_type_t::DVBAPI, 5000};
		}

		try {
			std::string type = cfg.lookup("type");
			if (strcmp(type.c_str(), "neumo") != 0) {
				cached = {api_type_t::DVBAPI, 5000};
				return cached;
			}
		} catch (const SettingNotFoundException& nfex) {
			cached = {api_type_t::DVBAPI, 5000};
			return cached;
		}

		try {
			std::string version = cfg.lookup("version");
			dtdebugf("Neumo dvbapi detected; version={}", version);
			cached = { api_type_t::NEUMO, 1000*std::stof(version)};
			return cached;

		} catch (const SettingNotFoundException& nfex) {
			cached = { api_type_t::DVBAPI, 5000};
			return cached;
		}
	}
	return cached;
}


class dvbdev_monitor_t : public adaptermgr_t {
#if 0
	std::optional<db_txn> chdb_wtxn_;
#endif
	std::optional<db_txn> devdb_wtxn_;

	inline db_txn devdb_wtxn() {
		if(!devdb_wtxn_)
			devdb_wtxn_.emplace(receiver.devdb.wtxn());
		return devdb_wtxn_->child_txn();
	}

#if 0
	inline db_txn chdb_wtxn() {
		if(!chdb_wtxn_)
			chdb_wtxn_.emplace(receiver.chdb.wtxn());
		return chdb_wtxn_->child_txn();
	}
#endif

	inline void commit_txns() {
#if 0
		if (chdb_wtxn_) {
			chdb_wtxn_->commit();
			chdb_wtxn_.reset();
		}
#endif
		if (devdb_wtxn_) {
			devdb_wtxn_->commit();
				devdb_wtxn_.reset();
		}
	}

	int wd_dev{-1};
	int wd_dev_dvb{-1};

	std::map<infd_t, adapter_no_t> adapter_no_map;
	std::map<infd_t, std::shared_ptr<dvb_frontend_t>> frontend_map;

	char buffer[EVENT_BUF_LEN];

	bool frontend_exists(adapter_no_t adapter_no, frontend_no_t frontend_no);
	bool adapter_no_exists(adapter_no_t adapter_no);

	void on_new_frontend(adapter_no_t adapter_no, frontend_no_t frontend_no);
	void on_delete_frontend(struct inotify_event* event);
	void discover_frontends(adapter_no_t adapter_no);
	void on_new_adapter(int adapter_no);

	void on_delete_adapter(struct inotify_event* event);
	void discover_adapters();
	void on_new_dir(struct inotify_event* event);
	void on_new_file(struct inotify_event* event);
	void on_delete_dir(struct inotify_event* event);

	void disable_missing_adapters();
	bool renumber_cards();
	void update_lnbs();

public:

	dvbdev_monitor_t(receiver_t& receiver);
	~dvbdev_monitor_t();
	int run();
	int start();
	int stop();

	inline int get_dish_move_penalty() const {
		auto r = receiver.options.readAccess();
		return r->dish_move_penalty;
	}

	inline bool get_tune_may_move_dish() const {
		auto r = receiver.options.readAccess();
		return r->tune_may_move_dish;
	}

	inline int get_resource_reuse_bonus() const {
		auto r = receiver.options.readAccess();
		return r->resource_reuse_bonus;
	}

	void update_dbfe(const adapter_no_t adapter_no, const frontend_no_t frontend_no,
									 fe_state_t& t);

	void renumber_card(int old_number, int new_number);
};


dvbdev_monitor_t::~dvbdev_monitor_t() {
	dtdebugf("dvbdev_monitor_t destructor\n");
	if (wd_dev) {
		inotify_rm_watch(inotfd, wd_dev);
		wd_dev = -1;
	}
	if (wd_dev_dvb) {
		inotify_rm_watch(inotfd, wd_dev_dvb);
		wd_dev_dvb = -1;
	}

	/*closing the INOTIFY instance*/
	close(inotfd);
}

static void discover_helper(const char* directory, const char* pattern, int max_number, std::function<void(int)>(cb)) {
	DIR* dir;
	struct dirent* de;

	/*@todo: tuner_id need to be found by matching adapter and frond_end names, rather
		than just relying on a fixed order.

		The code below assumes a fixed order of adapters and handles removable adapters poorly

	*/

	if ((dir = opendir(directory))) {
		while ((de = readdir(dir))) {
			if (de->d_name[0] == '.')
				continue;
			int number = 0;
			if (sscanf(de->d_name, pattern, &number) == 1 && number >= 0 && number < max_number) {
				cb(number);
			}
		}
		closedir(dir);
	}
}

bool dvbdev_monitor_t::frontend_exists(adapter_no_t adapter_no, frontend_no_t frontend_no) {

	auto it = std::find_if(frontend_map.begin(), frontend_map.end(), [&](auto& x) {
		auto [wd, fe] = x;
		return fe->adapter_no == adapter_no  && fe->frontend_no == frontend_no;
	});
	return it != frontend_map.end();
}

bool dvbdev_monitor_t::adapter_no_exists(adapter_no_t adapter_no) {

	auto it = std::find_if(adapter_no_map.begin(), adapter_no_map.end(), [&](auto& x) {
		auto [wd, adapter_no_] = x;
		return adapter_no == adapter_no_;
	});
	return it != adapter_no_map.end();
}


/*
	update databases
*/
void dvbdev_monitor_t::update_dbfe(const adapter_no_t adapter_no, const frontend_no_t frontend_no,
																	 fe_state_t& t) {
	t.dbfe.adapter_no = int(adapter_no);

	auto devdb_wtxn = this->devdb_wtxn();
	auto c = devdb::fe_t::find_by_key(devdb_wtxn,
																		devdb::fe_key_t{t.dbfe.k.adapter_mac_address, (uint8_t)(int)frontend_no});
	auto dbfe_old = c.is_valid() ? c.current() : devdb::fe_t();
	dbfe_old.mtime = t.dbfe.mtime; //prevent this from being a change
	if (t.dbfe.card_no<0)
		t.dbfe.card_no = dbfe_old.card_no;
	bool changed = !c.is_valid() || (dbfe_old != t.dbfe);
	if(dbfe_old.sub.owner != -1 && dbfe_old.sub.owner != getpid() && kill((pid_t)dbfe_old.sub.owner, 0)) {
		dtdebugf("process pid={:d} has died\n", dbfe_old.sub.owner);
		t.dbfe.sub.owner = -1;
		t.dbfe.sub.subs.clear();
		changed = true;
	}

	if (changed) {
		t.dbfe.mtime = system_clock_t::to_time_t(now);
		t.dbfe.k.frontend_no = int(frontend_no);
		t.dbfe.enable_dvbs = dbfe_old.enable_dvbs;
		t.dbfe.enable_dvbc = dbfe_old.enable_dvbc;
		t.dbfe.enable_dvbt = dbfe_old.enable_dvbt;
		put_record(devdb_wtxn, t.dbfe, 0);
		devdb::lnb::update_lnb_adapter_fields(devdb_wtxn, t.dbfe);
	}
	devdb_wtxn.commit(); //commit child transaction
}


void dvbdev_monitor_t::on_new_frontend(adapter_no_t adapter_no, frontend_no_t frontend_no) {
	if (frontend_exists(adapter_no, frontend_no)) {
		dtdebugf("Frontend already exists!\n");
		return;
	}
	ss::string<128> fname;
	fname.format("/dev/dvb/adapter{:d}/frontend{:d}", (int)adapter_no, (int)frontend_no);
	int wd = -1;
	int count = 0;
	while (((wd = inotify_add_watch(inotfd, fname.c_str(), IN_OPEN | IN_CLOSE | IN_DELETE_SELF)) < 0) && (count < 100)) {
		usleep(20000);
		count++;
	}
	if (count > 0)
		dtdebugf("Count={:d}\n", count);
	if (wd < 0) {
		dtdebugf("ERROR: {}", strerror(errno));
		assert(0);
	}
	auto fe = dvb_frontend_t::make(this, adapter_no, frontend_no, api_type, api_version);
	{
		auto w = fe->ts.writeAccess();
		w->dbfe.present = true;
		w->dbfe.can_be_used = true;
		update_dbfe(fe->adapter_no, fe->frontend_no, *w);
	}
	{auto w = frontends.writeAccess();
		w->try_emplace({adapter_no, frontend_no}, fe);
	}

	// adapter->update_adapter_can_be_used();
	frontend_map.emplace(wd, fe);
	dtdebugf("new frontend adapter {:d} fe={:d} wd={:d} p={:p}",
					 (int)fe->adapter_no, (int)fe->frontend_no, wd, fmt::ptr(fe.get()));
}

void dvbdev_monitor_t::on_delete_frontend(struct inotify_event* event) {
// should be a frontend or demux is removed
	auto it = frontend_map.find(event->wd);
	if (it == frontend_map.end()) {
		dtdebugf("Could not find frontend wd={:d}\n", event->wd);
		// assert(0);
		return;
	}
	auto [wd, fe] = *it;
	assert(wd == event->wd);
	inotify_rm_watch(inotfd, wd);
	//@todo: stop all active muxes and active services
	fe->stop_frontend_monitor_and_wait();
	frontend_map.erase(it);
	dtdebugf("delete frontend adapter {:d} fe={:d} wd={:d}\n", (int)fe->adapter_no, (int)fe->frontend_no, event->wd);
	{
		auto w = fe->ts.writeAccess();
		w->dbfe.can_be_used = false;
		w->dbfe.present = false;
		w->dbfe.can_be_used = false;
		update_dbfe(fe->adapter_no, fe->frontend_no, *w);
	}
	{ auto w = frontends.writeAccess();
	w->erase({fe->adapter_no, fe->frontend_no});
	}
}

void dvbdev_monitor_t::discover_frontends(adapter_no_t adapter_no) {
	ss::string<256> fname;
	fname.format("/dev/dvb/adapter{:d}", (int) adapter_no);
	auto frontend_cb = [this, adapter_no](int frontend_no) {
		on_new_frontend(adapter_no, frontend_no_t(frontend_no)); };

	// scan /dev/dvb/adapterX for all frontends
	discover_helper(fname.c_str(), "frontend%d", 32, frontend_cb);
}

void dvbdev_monitor_t::on_new_adapter(int adapter_no) {
	ss::string<256> fname;
	if(adapter_no_exists(adapter_no_t(adapter_no))) {
		dtdebugf("Adapter {:d} already exists\n", adapter_no);
		return;
	}
	fname.format("/dev/dvb/adapter{:d}", adapter_no);
	auto wd = inotify_add_watch(inotfd, fname.c_str(), IN_CREATE | IN_DELETE_SELF);
	adapter_no_map.emplace(wd, adapter_no_t{adapter_no});
	dtdebugf("new adapter {:d} wd={:d}\n", adapter_no, wd);
	discover_frontends(adapter_no_t(adapter_no));
}

void dvbdev_monitor_t::on_delete_adapter(struct inotify_event* event) {
	auto it = adapter_no_map.find(event->wd);
	if (it == adapter_no_map.end()) {
		dtdebugf("Could not find adapter\n");
		assert(0);
	}
	auto [wd, adapter_no] = *it;
	assert(wd == event->wd);
	inotify_rm_watch(inotfd, wd);
	adapter_no_map.erase(wd);
	/*NOTE: the actual adapter deletion will be done when its last frontend is deleted
		This could happen later!
	*/
}
void dvbdev_monitor_t::discover_adapters() {

	auto adapter_cb = [this](int adapter_no) { on_new_adapter(adapter_no); };

	// scan /dev/dvb for all adapters
	discover_helper("/dev/dvb", "adapter%d", 64, adapter_cb);
}

void dvbdev_monitor_t::on_new_dir(struct inotify_event* event) {
	if (event->wd == wd_dev) { // new subdir of /dev/
		if (strcmp(event->name, "dvb") == 0) {
			dtdebugf("first adapter....\n");
			if (wd_dev_dvb >= 0) {
				inotify_rm_watch(inotfd, wd_dev_dvb);
				dtdebugf("unexpected: stil watching /dev/dvb\n");
				wd_dev_dvb = -1;
			}
			wd_dev_dvb = inotify_add_watch(inotfd, "/dev/dvb", IN_CREATE | IN_DELETE_SELF);
			if (wd_dev_dvb < 0) {
				dtdebugf("ERROR: {}", strerror(errno));
			}
			dtdebugf("Removing watch to /dev because /dev/dvb now exist\n");
			inotify_rm_watch(inotfd, wd_dev);
			wd_dev = -1;
			discover_adapters(); // needed because some adapters may already exist
		}
	} else if (event->wd == wd_dev_dvb) { // new subdir in /dev/dvb/
		int adapter_no;
		if (sscanf(event->name, "adapter%d", &adapter_no) == 1) {
			on_new_adapter(adapter_no);
		}
	} else
		dtdebugf("should not happen\n");
}

void dvbdev_monitor_t::on_new_file(struct inotify_event* event) {
// must be a file, so a frontend or a demux
	int frontend_no;
	if (sscanf(event->name, "frontend%d", &frontend_no) == 1) {
		auto it = adapter_no_map.find(event->wd);
		if (it == adapter_no_map.end()) {
			dtdebugf("Adapter not found");
			assert(0);
		}
		on_new_frontend(adapter_no_t(it->second), frontend_no_t(frontend_no));
	}
}

void dvbdev_monitor_t::on_delete_dir(struct inotify_event* event) {
	if (event->wd == wd_dev_dvb) { // subdir of /dev/
		inotify_rm_watch(inotfd, wd_dev_dvb);
		wd_dev_dvb = -1;
		dtdebugf("delete /dev/dvb\n");
		dtdebugf("Adding watch to /dev because /dev/dvb no longer exist\n");
		if (wd_dev < 0)
			wd_dev = inotify_add_watch(inotfd, "/dev", IN_CREATE);

	} else if (event->wd == wd_dev) {
		assert(0);
	} else {
		on_delete_adapter(event);
	}
}

dvbdev_monitor_t::dvbdev_monitor_t(receiver_t& receiver) : adaptermgr_t(receiver) {
		try {
		std::tie(api_type, api_version) = get_dvb_api_type();
	} catch(...) {
	}
}

int dvbdev_monitor_t::start() {

	{ //in case we exited uncleanly: mark all live stat_info_t records as none live
		auto wtxn = receiver.statdb.wtxn();
		statdb::clean_live(wtxn);
		wtxn.commit();
	}
	/*creating the INOTIFY instance*/
	inotfd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

	/*checking for error*/
	if (inotfd < 0) {
		dtdebugf("inotify_init");
		throw std::runtime_error("Cannot start dvbdev_monitor");
	}

	/*adding the “/tmp” directory into watch list. Here, the suggestion is to validate the existence
		of the directory before adding into monitoring list.*/
	wd_dev_dvb = inotify_add_watch(inotfd, "/dev/dvb/", IN_CREATE | IN_DELETE_SELF);
	if (wd_dev_dvb < 0) {
		dtdebugf("Adding watch to /dev because /dev/dvb does not exist\n");
		wd_dev = inotify_add_watch(inotfd, "/dev", IN_CREATE);
	}

	discover_adapters();
	renumber_cards();
	disable_missing_adapters();
	update_lnbs();
	this->commit_txns();

	/*read to determine the event change happens on “/tmp” directory. Actually this read blocks until the change event
	 * occurs*/
	return 0;
}

int dvbdev_monitor_t::stop() {
	// special type of for loop because monitors map will be erased and iterators are invalidated
	{ auto r = frontends.owner_read_ref();
		for (auto& [key, fe]: r) {
			fe->stop_frontend_monitor_and_wait();
		}
	}
	{ //mark all live stat_info_t records as none live
		auto wtxn = receiver.statdb.wtxn();
		statdb::clean_live(wtxn);
		wtxn.commit();
	}

	if (wd_dev_dvb) {
		inotify_rm_watch(inotfd, wd_dev_dvb);
		wd_dev_dvb = -1;
	}

	if (inotfd >= 0)
		if (::close(inotfd) != 0) {
			if (errno != EINTR)
				dterrorf("Error while close inotify");
		}
	return 0;
}

void dvbdev_monitor_t::disable_missing_adapters() {
	using namespace chdb;
	auto devdb_wtxn = this->devdb_wtxn();
	auto c = devdb::find_first<devdb::fe_t>(devdb_wtxn);
	for (auto fe : c.range()) {
		bool found{false};
		//check if an frontend with the correct key is present
		auto r = frontends.owner_read_ref();
		for (const auto& [k, present_fe]: r) {
			auto ts = present_fe->ts.readAccess();
			if(ts->dbfe.k ==  fe.k) {
				found = true;
				break;
			}
		}
		if(!found) {
			ss::string<128> adapter_name;
			adapter_name.format("A-- {:s}", fe.card_short_name);
			if(fe.present || fe.can_be_used || adapter_name != fe.adapter_name ||
				 fe.sub != devdb::fe_subscription_t()) {
				fe.can_be_used = false;
				fe.present = false;
				fe.adapter_no = -1;
				fe.sub = {};
				fe.adapter_name = adapter_name;
				put_record(devdb_wtxn, fe);
			}
		}
	}
	devdb_wtxn.commit(); //commit child transaction
}

bool dvbdev_monitor_t::renumber_cards() {
	using namespace chdb;
	std::map<card_mac_address_t, int> card_numbers;
	auto devdb_wtxn = this->devdb_wtxn();
	auto c = devdb::find_first<devdb::fe_t>(devdb_wtxn);
	auto saved = c.clone();
	bool changed = false;
	std::bitset<128> numbers_in_use;
	for (auto fe : c.range()) {
		auto card_no = std::min((int)fe.card_no, ((int) numbers_in_use.size()-1));
		card_numbers.try_emplace((card_mac_address_t)fe.card_mac_address,
														 (card_no < 0 || numbers_in_use[card_no]) ? -1 : card_no);
		if(fe.card_no <0)
			continue;
		numbers_in_use[fe.card_no] = true;
	}


	auto next_card_no = [&] {
		int ret=0;
		for(int n = 0; n < (int)numbers_in_use.size(); ++n) {
			if(!numbers_in_use[n]) {
				numbers_in_use[n] =  true;
				return ret;
			}
			ret++;
		}
		return -1;
	};


	for(auto& [card_mac_address, card_no]: card_numbers ) {
			if (card_no == -1)
				card_no = next_card_no();
	}

	c = saved;
	for (auto dbfe : c.range()) {
		auto card_no = card_numbers[(card_mac_address_t)dbfe.card_mac_address];
		assert(card_no >= 0);
		if (card_no != dbfe.card_no) {
			dbfe.card_no = card_no;
			changed = true;
			put_record(devdb_wtxn, dbfe);
		}
		auto [it, found] = find_in_safe_map_with_owner_read_ref(
			frontends, std::tuple{(adapter_no_t)dbfe.adapter_no, (frontend_no_t)dbfe.k.frontend_no});
		if(found)  {
			auto& fe =*it->second;
			auto w = fe.ts.writeAccess();
			w->dbfe.card_no = dbfe.card_no;
		}
	}
	devdb_wtxn.commit(); //commit child transaction
	return changed;
}

void dvbdev_monitor_t::renumber_card(int old_number, int new_number) {
	using namespace chdb;
	std::map<card_mac_address_t, int> card_numbers;
	auto devdb_wtxn = this->devdb_wtxn();
	auto c = devdb::find_first<devdb::fe_t>(devdb_wtxn);
	auto saved = c.clone();
	std::bitset<128> numbers_in_use;

	if(old_number == new_number)
		return;

	for (auto dbfe : c.range()) {
		auto card_no = std::min((int)dbfe.card_no, ((int) numbers_in_use.size()-1));
		card_numbers.try_emplace((card_mac_address_t)dbfe.card_mac_address,
														 (card_no < 0 || numbers_in_use[card_no]) ? -1 : card_no);
		if(dbfe.card_no <0)
			continue;
		numbers_in_use[dbfe.card_no] = true;
	}

	auto next_card_no = [&] {
		int ret=0;
		for(int n = 0; n < (int)numbers_in_use.size(); ++n) {
			if(!numbers_in_use[n]) {
				numbers_in_use[n] =  true;
				return ret;
			}
			ret++;
		}
		return -1;
	};

	c =saved;

	int next = -1;
	if (numbers_in_use[new_number]) {
		next = next_card_no();
	}

	for(auto dbfe : c.range()) {
		if (dbfe.card_no == new_number) {
			assert(next >= 0);
			dbfe.card_no  = next;
			put_record(devdb_wtxn, dbfe);
		} else if(dbfe.card_no == old_number) {
			dbfe.card_no = new_number;
			put_record(devdb_wtxn, dbfe);
		}
		auto [it, found] = find_in_safe_map_with_owner_read_ref(
			frontends, std::tuple{(adapter_no_t)dbfe.adapter_no, (frontend_no_t)dbfe.k.frontend_no});
		if(found)  {
			auto& fe =*it->second;
			auto w = fe.ts.writeAccess();
			w->dbfe.card_no = dbfe.card_no;
		}
	}

	devdb_wtxn.commit(); //commit child transaction
	this->commit_txns();
}

void dvbdev_monitor_t::update_lnbs() {
	auto devdb_wtxn = this->devdb_wtxn();
	devdb::lnb::update_lnbs(devdb_wtxn);
	devdb_wtxn.commit(); //commit child transaction
}

/*
	Returns -1 on error, 0 on no nore events, 1 on events processed
	runs in receiver_thread
*/
int dvbdev_monitor_t::run() {
	int num = 0;
	for (;;) {
		int length = read(inotfd, buffer, EVENT_BUF_LEN);
		if (length < 0) {
			switch (errno) {
			case EAGAIN:
				return num;
			case EINTR:
				continue;
			default:
				dterrorf("read error: {:s}", strerror(errno));
				return -1;
			}
		}
		/*checking for error*/
		if (length < 0)
			perror("read");

		/*actually read return the list of change events happens. Here, read the change event one by one and process it
		 * accordingly.*/
		for (int i = 0; i < length;) {
			struct inotify_event* event = (struct inotify_event*)&buffer[i];
			num++;
			if (event->mask & IN_CREATE) {
				if (event->mask & IN_ISDIR)
					on_new_dir(event);
				else
					on_new_file(event);
			} else if (event->mask & IN_DELETE) {
				assert(0);
			} else if (event->mask & IN_DELETE_SELF) {
				if (frontend_map.find(event->wd) != frontend_map.end())
					on_delete_frontend(event);
				else
					on_delete_dir(event);
			} else if (event->mask & IN_OPEN) {
				// TODO: 1) check if we opened ourself. If not, then disable can_be_used for adapter and frontend
			} else if (event->mask & IN_CLOSE) {
				// TODO: update can_be_used for adapter and frontend
			} else if (event->mask & IN_IGNORED) {
			} else
				dtdebugf("UNPROCESSED EVENT\n");
			i += EVENT_SIZE + event->len;
			// dtdebugf("new read\n");
		}
		commit_txns();
	}
}

void adaptermgr_t::renumber_card(int old_number, int new_number) {
	auto* self = dynamic_cast<dvbdev_monitor_t*>(this);
	return self->renumber_card(old_number, new_number);
}

int adaptermgr_t::run() {
	auto* self = dynamic_cast<dvbdev_monitor_t*>(this);
	return self->run();
}

int adaptermgr_t::start() {
	auto* self = dynamic_cast<dvbdev_monitor_t*>(this);
	return self->start();
}

int adaptermgr_t::stop() {
	auto* self = dynamic_cast<dvbdev_monitor_t*>(this);
	return self->stop();
}

std::shared_ptr<adaptermgr_t> adaptermgr_t::make(receiver_t& receiver) {
	return std::make_shared<dvbdev_monitor_t>(receiver);
}



bool fe_state_t::is_tuned_to(const chdb::dvbs_mux_t& mux, const devdb::rf_path_t* required_rf_path,
	bool ignore_t2mi_pid) const {
	if (required_rf_path && *required_rf_path != reserved_rf_path)
		return false;
	const auto* tuned_mux = std::get_if<chdb::dvbs_mux_t>(&reserved_mux);
	if (!tuned_mux)
		return false;
	if(!chdb::matches_physical_fuzzy(mux, *tuned_mux, true /*check_sat_pos*/)) //or stream_id or t2mi_pid does not match
		return false;
	if (tuned_mux->k.stream_id != mux.k.stream_id)
		return false;
	if (tuned_mux->k.stream_id >= 0 && !(tuned_mux->pls_code == mux.pls_code && tuned_mux->pls_mode == mux.pls_mode))
		return false;
	// note that we do not check t2mi_pid because that does not change mux
	return true;
}

bool fe_state_t::is_tuned_to(const chdb::dvbt_mux_t& mux, const devdb::rf_path_t* required_rf_path,
														 bool ignore_t2mi_pid) const {
	assert(!required_rf_path);
	const auto* tuned_mux = std::get_if<chdb::dvbt_mux_t>(&reserved_mux);
	if (!tuned_mux)
		return false;
	if(!chdb::matches_physical_fuzzy(mux, *tuned_mux, true /*check_sat_pos*/,
																	 ignore_t2mi_pid)) //or stream_id or t2mi_pid does not match
		return false;
	return true;
}

bool fe_state_t::is_tuned_to(const chdb::dvbc_mux_t& mux, const devdb::rf_path_t* required_rf_path,
														 bool ignore_t2mi_pid) const {
	assert(!required_rf_path);
	const auto* tuned_mux = std::get_if<chdb::dvbc_mux_t>(&reserved_mux);
	if (!tuned_mux)
		return false;
	if(!chdb::matches_physical_fuzzy(mux, *tuned_mux, true /*check_sat_pos*/,
																	 ignore_t2mi_pid)) //or stream_id or t2mi_pid does not match
		return false;
	return true;
}

bool fe_state_t::is_tuned_to(const chdb::any_mux_t& mux, const devdb::rf_path_t* required_rf_path,
														 bool ignore_t2mi_pid) const {
	bool ret;
	visit_variant(
		mux, [this, &ret, required_rf_path, ignore_t2mi_pid](const chdb::dvbs_mux_t& mux) {
			ret = this->is_tuned_to(mux, required_rf_path, ignore_t2mi_pid); },
		[this, &ret, required_rf_path, ignore_t2mi_pid](const chdb::dvbc_mux_t& mux) {
			ret = this->is_tuned_to(mux, required_rf_path, ignore_t2mi_pid); },
		[this, &ret, required_rf_path, ignore_t2mi_pid](const chdb::dvbt_mux_t& mux) {
			ret = this->is_tuned_to(mux, required_rf_path, ignore_t2mi_pid); });
	return ret;
}

std::tuple<std::string, int> adaptermgr_t::get_api_type() const {
	const char* api_type="";
	switch(this->api_type) {
	case api_type_t::NEUMO:
		api_type = "neumo";
		break;
	default:
		api_type = "dvbapi";
		break;
	}
	return { api_type, this->api_version};
}
