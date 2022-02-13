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

#include "adapter.h"
#include "receiver.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/neumovariant.h"
#include <dirent.h>
#include <errno.h>
#include <functional>
#include <iomanip>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

typedef int infd_t;
#define EVENT_SIZE (sizeof(struct inotify_event))

/*event name will be about 2 characaters. The extra NAME_MAX+1 ensures that even for a very long filename
	we will be able to read 1 event */

#define EVENT_BUF_LEN (4 * (EVENT_SIZE + 20) + NAME_MAX + 1)

struct sat_reservation_t {
	int sat_pos{sat_pos_none};						 // current official sat_pos
	dvb_frontend_t* exclusive_fe{nullptr}; /*if exclusive, some subscription (the one with exclusive_fe) may move
																					 the positioner and strange things can happen*/
	use_count_t use_count;
};

class dvbdev_monitor_t : public adaptermgr_t {
	mutable std::map<int, sat_reservation_t> dish_reservation_map; // indexed by dish_id
	int wd_dev{-1};
	int wd_dev_dvb{-1};

	std::map<infd_t, dvb_adapter_t*> adapter_map;
	std::map<infd_t, std::tuple<dvb_adapter_t*, dvb_frontend_t*>> frontend_map;

	char buffer[EVENT_BUF_LEN];

	bool frontend_exists(dvb_adapter_t* adapter, frontend_no_t frontend_no);

	bool adapter_exists(int adapter_no);
	void on_new_frontend(dvb_adapter_t* adapter, frontend_no_t frontend_no);
	void on_delete_frontend(struct inotify_event* event);
	void discover_frontends(dvb_adapter_t* adapter);
	void on_new_adapter(int adapter_no);

	void on_delete_adapter(struct inotify_event* event);
	void discover_adapters();
	void on_new_dir(struct inotify_event* event);
	void on_new_file(struct inotify_event* event);
	void on_delete_dir(struct inotify_event* event);

	void mark_all_adapters_not_present();
	std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t, int, int>
	find_lnb_for_tuning_to_mux_(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
															const dvb_adapter_t* adapter_to_release, bool blindscan,
															int required_adapter_no) const;
	std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t>
	find_slave_tuner_for_tuning_to_mux(db_txn& txn,
																		 const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
																		 const dvb_adapter_t* adapter_to_release, bool blindscan) const;


public:
	int reserve_dish_exclusive(dvb_frontend_t* fe, int dish_id);
	int release_dish_exclusive(dvb_frontend_t* fe, int dish_id);
	int reserve_sat(int dish_id, int sat_pos);
	void change_sat_reservation_sat_pos(int dish_id, int sat_pos);
	int release_sat(dvb_frontend_t* fe, int dish_id, int sat_pos);
	int reserve_master(adapter_no_t adapter_no, const chdb::dvbs_mux_t& mux);
	int release_master(adapter_no_t adapter_no);
	const sat_reservation_t& sat_reservation(int dish_id) const;
	sat_reservation_t& sat_reservation(int dish_id);

	dvbdev_monitor_t(receiver_t& receiver);
	~dvbdev_monitor_t();
	int run();
	int start();
	int stop();

	inline chdb::usals_location_t get_usals_location() const {
		auto r = receiver.options.readAccess();
		return r->usals_location;
	}

	inline int get_dish_move_penalty() const {
		auto r = receiver.options.readAccess();
		return r->dish_move_penalty;
	}

	void dump(FILE* fp) const;

	template <typename mux_t>
	std::shared_ptr<dvb_frontend_t> find_adapter_for_tuning_to_mux(db_txn& txn, const mux_t& mux,
																																 const dvb_adapter_t* adapter_to_release,
																																 bool blindscan) const;
	std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t>
	find_lnb_for_tuning_to_mux(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
														 const dvb_adapter_t* adapter_to_release, bool blindscan) const;

	std::shared_ptr<dvb_frontend_t> find_fe_for_lnb(const chdb::lnb_t& lnb, const dvb_adapter_t* adapter_to_release,
																									bool need_blindscan, bool need_spectrum) const;

	void update_dbfe(const adapter_no_t adapter_no, const frontend_no_t frontend_no, dvb_frontend_t::thread_safe_t& t);
};

/*
 *reserving a mux on an adapter prevents that mux from being detuned.
 It needs to be done on all adapters

 *a sat reservation can only be placed on a dish on a ROTOR_MASTER_USALS or ROTOR_MASTER_DISEQC12 lnb.
 After the reservation has been taken, the dish cannot be moved if use_count_dish>1, i.e., only
 the first sat subscription can move the positioner, All other subscriptions can use it, but
 only on the current sat

 sat reservations must be placed by all lnbs on the dish, including the master lnb itself.

 Currently only a master lnb can actually move the positioner. slave lnbs can never do it.
 In principle it would be possible for a slave lnb to temporarily  reserve a master lnb,
 move the positioner, and then remove the reservation on the master lnb, but this is too complicated.


 Mormally there should be only 1 master lnb on a dish, but one counter example would be a dish with 2 lnbs
 connected to a diseqc switch AFTER the positioner. In this case positioner commands sent to either lnb
 will control the positioner. Reservations must be placed on ALL masters for the reservation system to work

 *a polband  reservation can only be placed on a master lnb, by a slave lnb.
 a master lnb can only switch polarisation or band if use_count_polband==0. A slave lnb
 can do so if it use_count_polband==1

 Note that multiple simultaneous subscriptions are serialized: one subscription only changes
 after thr previous one has been made.

*/
int dvbdev_monitor_t::reserve_master(adapter_no_t adapter_no, const chdb::dvbs_mux_t& mux) {
	auto it_adapter = adapters.find(adapter_no);
	if (it_adapter == adapters.end()) {
		dtdebug("Master adapter is not available");
		return -1;
	}
	auto& [adapter_no_, master_adapter] = *it_adapter;
	auto w = master_adapter.reservation.writeAccess();
	assert(!w->exclusive);
	return w->use_count_polband.register_subscription();
}

int dvbdev_monitor_t::release_master(adapter_no_t adapter_no) {
	auto it_adapter = adapters.find(adapter_no);
	if (it_adapter == adapters.end()) {
		dtdebug("Master adapter is not available");
		return -1;
	}
	auto& [adapter_no_, adapter] = *it_adapter;
	auto w = adapter.reservation.writeAccess();
	w->exclusive = false;
	return w->use_count_polband.unregister_subscription();
}

int dvbdev_monitor_t::reserve_dish_exclusive(dvb_frontend_t* fe, int dish_id) {
	auto& res = dish_reservation_map[dish_id];
	assert(!res.exclusive_fe);
	res.exclusive_fe = fe;
	return res.use_count.register_subscription();
}

int dvbdev_monitor_t::release_dish_exclusive(dvb_frontend_t* fe, int dish_id) {
	auto& res = dish_reservation_map[dish_id];
	if(!res.exclusive_fe)
		return 0;
	res.exclusive_fe = nullptr;
	return res.use_count.unregister_subscription();
}

int dvbdev_monitor_t::reserve_sat(int dish_id, int sat_pos) {
	auto& res = dish_reservation_map[dish_id];
	assert(res.sat_pos = sat_pos || res.use_count() == 0);
	if (res.exclusive_fe)
		dterrorx("A subscription is starting while another might start moving the positioner");
	res.sat_pos = sat_pos;
	return res.use_count.register_subscription();
}

int dvbdev_monitor_t::release_sat(dvb_frontend_t* fe, int dish_id, int sat_pos) {
	auto& res = dish_reservation_map[dish_id];
	if (fe == res.exclusive_fe) {
		res.exclusive_fe = nullptr;
	} else {
		/*The following assertion is incorrect: res.sat_pos is the dish position, bbut
			sat_pos is from SI of mux which can have changed*/
		// assert(res.sat_pos == sat_pos);
	}
	return res.use_count.unregister_subscription();
}

void dvbdev_monitor_t::change_sat_reservation_sat_pos(int dish_id, int sat_pos) {
	auto& res = dish_reservation_map[dish_id];
	assert(res.use_count() <= 1);
	res.sat_pos = sat_pos;
}

const sat_reservation_t& dvbdev_monitor_t::sat_reservation(int dish_id) const {
	auto& res = dish_reservation_map[dish_id];
	return res;
}

sat_reservation_t& dvbdev_monitor_t::sat_reservation(int dish_id) {
	return const_cast<sat_reservation_t&>(const_cast<const dvbdev_monitor_t*>(this)->sat_reservation(dish_id));
}

dvbdev_monitor_t::~dvbdev_monitor_t() {
	dtdebug("dvbdev_monitor_t destructor\n");
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

bool dvbdev_monitor_t::frontend_exists(dvb_adapter_t* adapter, frontend_no_t frontend_no) {
	auto it = std::find_if(frontend_map.begin(), frontend_map.end(), [&](auto& x) {
		auto [wd, y] = x;
		auto [a, f] = y;
		return a == adapter && f->frontend_no == frontend_no;
	});
	return it != frontend_map.end();
}

bool dvbdev_monitor_t::adapter_exists(int adapter_no) {
	auto it = std::find_if(adapter_map.begin(), adapter_map.end(), [&](auto p) {
		auto [wd, a] = p;
		return a->adapter_no == adapter_no_t(adapter_no);
	});
	return it != adapter_map.end();
}

void dvb_adapter_t::update_adapter_can_be_used() {
	this->can_be_used = true;
	for (auto [frontend_no, fe] : this->frontends) {
		auto t = fe->ts.writeAccess();
		if (!t->can_be_used) {
			t->can_be_used = false;
			break;
		}
	}
}

chdb::usals_location_t dvb_adapter_t::get_usals_location() const { return adaptermgr->get_usals_location(); }

void adaptermgr_t::start_frontend_monitor(dvb_frontend_t* fe) {
	auto p = fe_monitor_thread_t::make(receiver, fe);
	auto [it, inserted] = monitors.insert({fe, p});
	assert(inserted);
}

void adaptermgr_t::stop_frontend_monitor(dvb_frontend_t* fe) {
	auto it = monitors.find(fe);
	assert(it != monitors.end());
	auto* monitor = it->second.get();
	assert(monitor);
	/*We need to wait, otherwise a race can occur, opening the frontend which still is being closed.
		The alternative is to maintain a record (list of futures) of frontends in the process of
		being closed
	*/
	//bool wait = true;
	monitor->stop_running(true);
	auto& reservation = fe->adapter->reservation;
	reservation.writeAccess()->reserved_fe = nullptr;
	monitors.erase(it);
}

/*
	update database
*/
void dvbdev_monitor_t::update_dbfe(const adapter_no_t adapter_no, const frontend_no_t frontend_no,
																	 dvb_frontend_t::thread_safe_t& t) {
	auto txn = receiver.chdb.rtxn();
	auto k = chdb::fe_key_t(int(adapter_no), int(frontend_no));
	auto c = chdb::fe_t::find_by_key(txn, k);
	auto dbfe_old = c.is_valid() ? c.current() : chdb::fe_t();

	bool changed = (dbfe_old.card_name != t.dbfe.card_name) || (dbfe_old.adapter_name != t.dbfe.adapter_name) ||
		(dbfe_old.card_address != t.dbfe.card_address) ||
		(dbfe_old.adapter_address != t.dbfe.adapter_address) || c.is_valid() || t.dbfe.present != true ||
		t.dbfe.can_be_used != t.can_be_used;

	//	auto tst =dump_caps((chdb::fe_caps_t)fe_info.caps);
	//	dterror("CAPS: " << tst);
	changed |= dbfe_old.delsys != t.dbfe.delsys;
	txn.abort();
	if (changed) {
		t.dbfe.k = k;
		t.dbfe.mtime = system_clock_t::to_time_t(now);
		t.dbfe.present = true;
		t.dbfe.enabled = dbfe_old.enabled;
		t.dbfe.can_be_used = t.can_be_used;
		auto txn = receiver.chdb.wtxn();
		put_record(txn, t.dbfe, 0);
		txn.commit();
	}
}

void dvbdev_monitor_t::on_new_frontend(dvb_adapter_t* adapter, frontend_no_t frontend_no) {
	if (frontend_exists(adapter, frontend_no)) {
		dtdebugx("Frontend already exists!\n");
		return;
	}
	char fname[256];
	sprintf(fname, "/dev/dvb/adapter%d/frontend%d", (int)adapter->adapter_no, (int)frontend_no);
	int wd = -1;
	int count = 0;
	while (((wd = inotify_add_watch(inotfd, fname, IN_OPEN | IN_CLOSE | IN_DELETE_SELF)) < 0) && (count < 100)) {
		usleep(1000);
		count++;
	}
	if (count > 0)
		dtdebugx("Count=%d\n", count);
	if (wd < 0) {
		dtdebugx("ERROR: %s\n", strerror(errno));
		assert(0);
	}
	auto fe = dvb_frontend_t::make(adapter, frontend_no);
	auto t = fe->ts.writeAccess();
	update_dbfe(adapter->adapter_no, frontend_no, *t);

	adapter->frontends.try_emplace(frontend_no, fe);

	// adapter->update_adapter_can_be_used();
	frontend_map.emplace(wd, std::make_tuple(adapter, &*fe));
	dtdebugx("new frontend adap=%d fe=%d wd=%d\n", (int)adapter->adapter_no, (int)frontend_no, wd);
}

void dvbdev_monitor_t::on_delete_frontend(struct inotify_event* event) { // should be a frontend or demux is removed
	auto it = frontend_map.find(event->wd);
	if (it == frontend_map.end()) {
		dtdebugx("Could not find frontend wd=%d\n", event->wd);
		// assert(0);
		return;
	}
	auto [wd, p] = *it;
	assert(wd == event->wd);
	auto [adapter, fe] = p;
	inotify_rm_watch(inotfd, wd);
	//@todo: stop all active muxes and active services
	stop_frontend_monitor(fe);
	frontend_map.erase(it);
	dtdebugx("delete frontend adap=%d fe=%d wd=%d count=%ld\n", (int)adapter->adapter_no, (int)fe->frontend_no, event->wd,
					 adapter->frontends.size());
	if (fe == adapter->reservation.readAccess()->reserved_fe) {
		// This should not happen in regular operation
		dterrorx("Unexpected\n");
	}
	auto t = fe->ts.writeAccess();
	t->can_be_used = false;
	t->dbfe.can_be_used = false;
	{
		auto txn = receiver.chdb.wtxn();
		put_record(txn, t->dbfe, 0);
		txn.commit();
	}

	adapter->frontends.erase(fe->frontend_no);
	if (adapter->frontends.size() == 0) {
		adapters.erase(adapter->adapter_no);
		dtdebugx("delete adapter %d\n", (int)adapter->adapter_no);
	}
	// adapter->update_adapter_can_be_used();
}

void dvbdev_monitor_t::discover_frontends(dvb_adapter_t* adapter) {
	char fname[256];
	sprintf(fname, "/dev/dvb/adapter%d", (int)adapter->adapter_no);
	auto frontend_cb = [this, adapter](int frontend_no) { on_new_frontend(adapter, frontend_no_t(frontend_no)); };

	// scan /dev/dvb/adapterX for all frontends
	discover_helper(fname, "frontend%d", 32, frontend_cb);
}

void dvbdev_monitor_t::on_new_adapter(int adapter_no) {
	char fname[256];
	if (adapter_exists(adapter_no)) {
		dtdebugx("Adapter already exists\n");
		return;
	}
	sprintf(fname, "/dev/dvb/adapter%d", adapter_no);
	auto wd = inotify_add_watch(inotfd, fname, IN_CREATE | IN_DELETE_SELF);
	auto [it, inserted] = adapters.try_emplace(adapter_no_t(adapter_no), this, adapter_no);
	adapter_map.emplace(wd, &it->second);
	dtdebugx("new adapter %d wd=%d\n", adapter_no, wd);
	discover_frontends(&it->second);
}

void dvbdev_monitor_t::on_delete_adapter(struct inotify_event* event) {
	auto it = adapter_map.find(event->wd);
	if (it == adapter_map.end()) {
		dtdebugx("Could not find adapter\n");
		assert(0);
	}
	auto [wd, adapter] = *it;
	assert(wd == event->wd);
	inotify_rm_watch(inotfd, wd);
	adapter_map.erase(wd);
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
			dtdebugx("first adapter....\n");
			if (wd_dev_dvb >= 0) {
				inotify_rm_watch(inotfd, wd_dev_dvb);
				dtdebugx("unexpected: stil watching /dev/dvb\n");
				wd_dev_dvb = -1;
			}
			wd_dev_dvb = inotify_add_watch(inotfd, "/dev/dvb", IN_CREATE | IN_DELETE_SELF);
			if (wd_dev_dvb < 0) {
				dtdebugx("ERROR: %s\n", strerror(errno));
			}
			dtdebugx("Removing watch to /dev because /dev/dvb now exist\n");
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
		dtdebugx("should not happen\n");
}

void dvbdev_monitor_t::on_new_file(struct inotify_event* event) { // must be a file, so a frontend or a demux
	int frontend_no;
	if (sscanf(event->name, "frontend%d", &frontend_no) == 1) {
		auto it = adapter_map.find(event->wd);
		if (it == adapter_map.end()) {
			dtdebugx("Adapter not found");
			assert(0);
		}
		on_new_frontend(it->second, frontend_no_t(frontend_no));
	}
}

void dvbdev_monitor_t::on_delete_dir(struct inotify_event* event) {
	if (event->wd == wd_dev_dvb) { // subdir of /dev/
		inotify_rm_watch(inotfd, wd_dev_dvb);
		wd_dev_dvb = -1;
		dtdebugx("delete /dev/dvb\n");
		dtdebugx("Adding watch to /dev because /dev/dvb no longer exist\n");
		if (wd_dev < 0)
			wd_dev = inotify_add_watch(inotfd, "/dev", IN_CREATE);

	} else if (event->wd == wd_dev) {
		assert(0);
	} else {
		on_delete_adapter(event);
	}
}

dvbdev_monitor_t::dvbdev_monitor_t(receiver_t& receiver) : adaptermgr_t(receiver) {}

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
		dtdebug("inotify_init");
		throw std::runtime_error("Cannot start dvbdev_monitor");
	}

	/*adding the “/tmp” directory into watch list. Here, the suggestion is to validate the existence
		of the directory before adding into monitoring list.*/
	wd_dev_dvb = inotify_add_watch(inotfd, "/dev/dvb/", IN_CREATE | IN_DELETE_SELF);
	if (wd_dev_dvb < 0) {
		dtdebugx("Adding watch to /dev because /dev/dvb does not exist\n");
		wd_dev = inotify_add_watch(inotfd, "/dev", IN_CREATE);
	}
	mark_all_adapters_not_present();
	discover_adapters();
	/*read to determine the event change happens on “/tmp” directory. Actually this read blocks until the change event
	 * occurs*/
	return 0;
}

int dvbdev_monitor_t::stop() {
	// special type of for loop because monitors map will be erased and iterators are invalidated
	for (auto it = monitors.begin(); it != monitors.end();) {
		auto& [fe, mon] = *it++; // it is incremened because it will be invalidated
		stop_frontend_monitor(fe);
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
				dterror("Error while close inotify");
		}
	return 0;
}

void dvbdev_monitor_t::mark_all_adapters_not_present() {
	auto txn = receiver.chdb.wtxn();
	using namespace chdb;
	auto c = find_first<chdb::fe_t>(txn);
	for (auto fe : c.range()) {
		fe.present = false;
		fe.can_be_used = false;
		put_record(txn, fe, 0);
	}
	txn.commit();
}

/*
	Returns -1 on error, 0 on no nore events, 1 on events processed
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
				dterrorx("read error: %s", strerror(errno));
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
				dtdebugx("UNPROCESSED EVENT\n");
			i += EVENT_SIZE + event->len;
			// dtdebugx("new read\n");
		}
	}
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

void adaptermgr_t::dump(FILE* fp) const {
	auto* self = static_cast<const dvbdev_monitor_t*>(this);
	self->dump(fp);
}

void dvbdev_monitor_t::dump(FILE* fp) const {
	fprintf(fp, "***********DUMP ALL ADAPTERS*************\n");
	for (auto& [adapter_no, a] : adapters) {
		for (auto& [frontend_no, fe] : a.frontends) {
			auto r = a.reservation.readAccess();
			auto use_count_mux = r->reserved_fe == fe.get() ? r->use_count_mux() : 0;
			auto use_count_polarisation_and_band = r->reserved_fe == fe.get() ? r->use_count_polband() : 0;
			fprintf(fp, "adapter=%d frontend=%d use_count: mux=%d pol_band=%d\n", int(adapter_no), int(frontend_no),
							use_count_mux, use_count_polarisation_and_band);
		}
	}
	fprintf(fp, "***********DUMP DISH RESERVATIONS*************\n");
	for (auto& [dish_id, sat_reservation] : dish_reservation_map) {
		auto use_count_dish = sat_reservation.use_count();
		fprintf(fp, "dish=%d sat_pos=%d use_count=%d\n", dish_id, sat_reservation.sat_pos, use_count_dish);
	}
	fprintf(fp, "***********   END    *************\n");
}

std::shared_ptr<dvb_frontend_t> dvb_adapter_t::fe_for_delsys(chdb::fe_delsys_t delsys) const {
	auto it = std::find_if(frontends.begin(), frontends.end(), [&delsys](auto& x) {
		auto& [frontend_no, pfe] = x;
		auto t = pfe->ts.readAccess();
		return std::find_if(t->dbfe.delsys.begin(), t->dbfe.delsys.end(), [&delsys](const auto& delsys_) {
				return delsys_to_type(delsys) == delsys_to_type(delsys_);
			}) != t->dbfe.delsys.end();
	});
	return it == frontends.end() ? nullptr : it->second;
}

int dvb_adapter_t::reserve_fe(dvb_frontend_t* fe, const chdb::lnb_t& lnb, bool wont_move_dish) {
	// auto reservation_type = dvb_adapter_t::reservation_type_t::mux;
	int ret = 0;
	{
		auto w = reservation.writeAccess();
		assert(!w->exclusive);
		w->exclusive = true;
		if (!w->reserved_fe) {
			w->reserved_fe = fe;
			assert(w->use_count_mux() == 0);

			w->reserved_mux = {};
			w->reserved_lnb = lnb;
			adaptermgr->start_frontend_monitor(w->reserved_fe);
		} else {
			assert(w->reserved_fe == fe);
			assert(fe->ts.readAccess()->fefd >= 0);
		}
		if (!wont_move_dish) {
			adaptermgr->reserve_dish_exclusive(fe, lnb.k.dish_id);
		}
	}
	auto master_adapter = fe->ts.readAccess()->dbfe.master_adapter;
	bool is_slave = (master_adapter >= 0);
	assert(!is_slave);

	return ret;
}

int dvb_adapter_t::reserve_fe(dvb_frontend_t* fe, const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux
															/*, dvb_adapter_t::reservation_type_t reservation_type*/) {
	// auto reservation_type = dvb_adapter_t::reservation_type_t::mux;
	int ret = 0;
	{
		auto w = reservation.writeAccess();
		assert(!w->exclusive);
		if (!w->reserved_fe) {
			w->reserved_fe = fe;
			assert(w->use_count_mux() == 0);

			w->reserved_mux = mux;
			w->reserved_lnb = lnb;
			adaptermgr->start_frontend_monitor(w->reserved_fe);
		} else {
			assert(w->reserved_fe == fe);
			assert(fe->ts.readAccess()->fefd >= 0);
		}
		if (chdb::on_rotor(lnb))
			adaptermgr->reserve_sat(lnb.k.dish_id, mux.k.sat_pos);
		ret = w->use_count_mux.register_subscription();
	}
	auto master_adapter = fe->ts.readAccess()->dbfe.master_adapter;
	bool is_slave = (master_adapter >= 0);
	if (is_slave) {
		adaptermgr->reserve_master(adapter_no_t(master_adapter), mux);
		// todo: what if this fails? previous line returns negative
	}

	return ret;
}

int dvb_adapter_t::reserve_fe(dvb_frontend_t* fe, const chdb::dvbc_mux_t& mux) {
	auto w = reservation.writeAccess();
	assert(!w->exclusive);
	if (!w->reserved_fe) {
		w->reserved_fe = fe;
		assert(w->use_count_mux() == 0);

		w->reserved_mux = mux;
		w->reserved_lnb = chdb::lnb_t();
		adaptermgr->start_frontend_monitor(w->reserved_fe);
	} else {
		assert(w->reserved_fe == fe);
		assert(fe->ts.readAccess()->fefd >= 0);
	}
	return w->use_count_mux.register_subscription();
}

int dvb_adapter_t::reserve_fe(dvb_frontend_t* fe, const chdb::dvbt_mux_t& mux) {
	auto w = reservation.writeAccess();
	assert(!w->exclusive);
	if (!w->reserved_fe) {
		w->reserved_fe = fe;
		assert(w->use_count_mux() == 0);

		w->reserved_mux = mux;
		w->reserved_lnb = chdb::lnb_t();
		adaptermgr->start_frontend_monitor(w->reserved_fe);
	} else {
		assert(w->reserved_fe == fe);
		assert(fe->ts.readAccess()->fefd >= 0);
	}
	return w->use_count_mux.register_subscription();
}

int dvb_adapter_t::change_fe(dvb_frontend_t* fe, const chdb::lnb_t& lnb, int sat_pos) {
	auto w = reservation.writeAccess();
	assert(w->exclusive);
	assert(w->reserved_fe == fe);
	assert(w->reserved_fe->ts.readAccess()->fefd >= 0);
	w->reserved_mux = {};
	w->reserved_lnb = lnb;

	if (chdb::lnb::dish_needs_to_be_moved(lnb, sat_pos)) {
		/*upgrade our dish reservation from nonexclusive to exclusive ;
			can cause problems on other subscriptions
		*/
		adaptermgr->reserve_dish_exclusive(fe, lnb.k.dish_id);
		adaptermgr->change_sat_reservation_sat_pos(lnb.k.dish_id, sat_pos);
	};

	return 0;
}

int dvb_adapter_t::change_fe(dvb_frontend_t* fe, const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
	auto w = reservation.writeAccess();
	assert(w->reserved_fe == fe);
	assert(w->reserved_fe->ts.readAccess()->fefd >= 0);
	w->reserved_mux = mux;
	w->reserved_lnb = lnb;
	if (chdb::lnb::dish_needs_to_be_moved(lnb, mux.k.sat_pos)) {
		adaptermgr->change_sat_reservation_sat_pos(lnb.k.dish_id, mux.k.sat_pos);
	};

	return 0;
}

int dvb_adapter_t::change_fe(dvb_frontend_t* fe, const chdb::dvbt_mux_t& mux) {
	auto w = reservation.writeAccess();
	w->reserved_mux = mux;
	w->reserved_lnb = chdb::lnb_t();
	assert(w->reserved_fe == fe);
	assert(w->reserved_fe->ts.readAccess()->fefd >= 0);
	return 0;
}

int dvb_adapter_t::change_fe(dvb_frontend_t* fe, const chdb::dvbc_mux_t& mux) {
	auto w = reservation.writeAccess();
	w->reserved_mux = mux;
	w->reserved_lnb = chdb::lnb_t();
	assert(w->reserved_fe == fe);
	assert(w->reserved_fe->ts.readAccess()->fefd >= 0);
	return 0;
}

int dvb_adapter_t::release_fe() {
	dvb_frontend_t* fe{nullptr};
	int ret = 0;
	{

		auto w = reservation.writeAccess();

		fe = w->reserved_fe;
		auto master_adapter = fe->ts.readAccess()->dbfe.master_adapter;
		bool is_slave = (master_adapter >= 0);
		if (is_slave) {
			adaptermgr->release_master(adapter_no_t(master_adapter));
			// todo: what if this fails? previois line returns negative
		}

		dtdebugx("releasing frontend_monitor: fefd=%d\n", w->reserved_fe->ts.readAccess()->fefd);
		if (w->exclusive) {
			adaptermgr->release_dish_exclusive(fe, w->reserved_lnb.k.dish_id);
		} else { // implies that we did not reserve mux
			ret = w->use_count_mux.unregister_subscription();
			const auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&w->reserved_mux);
			if (dvbs_mux) {
				if (chdb::on_rotor(w->reserved_lnb))
					adaptermgr->release_sat(fe, w->reserved_lnb.k.dish_id, dvbs_mux->k.sat_pos);
			}
		}
		w->exclusive = false;
	}
	if (ret == 0) {
		adaptermgr->stop_frontend_monitor(fe);
		// reservation.reserved_fe->close_device();
	}
	return ret;
}

bool adapter_reservation_t::is_tuned_to(const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb) const {
	if (!use_count_mux())
		return false;
	if (required_lnb && required_lnb->k != reserved_lnb.k)
		return false;
	const auto* tuned_mux = std::get_if<chdb::dvbs_mux_t>(&reserved_mux);
	if (!tuned_mux)
		return false;
	if (tuned_mux->k.sat_pos != mux.k.sat_pos)
		return false;
	if (tuned_mux->pol != mux.pol)
		return false;
	if (tuned_mux->stream_id != mux.stream_id)
		return false;
	if (tuned_mux->stream_id >= 0 && !(tuned_mux->pls_code == mux.pls_code && tuned_mux->pls_mode == mux.pls_mode))
		return false;
	// note that we do not check t2mi_pid because that does not change mux
	int tolerance = std::max(mux.symbol_rate, tuned_mux->symbol_rate) / 3000;
	int delta = std::abs((int)tuned_mux->frequency - (int)mux.frequency);
	return delta <= tolerance;
}

bool adapter_reservation_t::is_tuned_to(const chdb::dvbt_mux_t& mux, const chdb::lnb_t* required_lnb) const {
	assert(!required_lnb);
	const auto* tuned_mux = std::get_if<chdb::dvbt_mux_t>(&reserved_mux);
	if (!use_count_mux())
		return false;
	if (!tuned_mux)
		return false;
	if (tuned_mux->k.sat_pos != mux.k.sat_pos)
		return false;
	int tolerance = 3000;
	int delta = std::abs((int)tuned_mux->frequency - (int)mux.frequency);
	return delta <= tolerance;
}

bool adapter_reservation_t::is_tuned_to(const chdb::dvbc_mux_t& mux, const chdb::lnb_t* required_lnb) const {
	assert(!required_lnb);
	const auto* tuned_mux = std::get_if<chdb::dvbc_mux_t>(&reserved_mux);
	if (!use_count_mux())
		return false;
	if (!tuned_mux)
		return false;
	if (tuned_mux->k.sat_pos != mux.k.sat_pos)
		return false;
	int tolerance = std::max(mux.symbol_rate, tuned_mux->symbol_rate) / 3;
	int delta = std::abs((int)tuned_mux->frequency - (int)mux.frequency);
	return delta <= tolerance;
}

bool adapter_reservation_t::is_tuned_to(const chdb::any_mux_t& mux, const chdb::lnb_t* required_lnb) const {
	bool ret;
	visit_variant(
		mux, [this, &ret, required_lnb](const chdb::dvbs_mux_t& mux) { ret = this->is_tuned_to(mux, required_lnb); },
		[this, &ret, required_lnb](const chdb::dvbc_mux_t& mux) { ret = this->is_tuned_to(mux, required_lnb); },
		[this, &ret, required_lnb](const chdb::dvbt_mux_t& mux) { ret = this->is_tuned_to(mux, required_lnb); });
	return ret;
}


/*
	adapter_will_be_released indicates an the adapter is currently reserved but that
	reservation will be released by retuning, because it is no longer
	need by the subscription

	Return the frontend which can be used or a nullptr
*/
std::shared_ptr<dvb_frontend_t> adapter_reservation_t::can_tune_to(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
																																	 bool adapter_will_be_released,
																																	 bool blindscan) const {
	if ((!adapter_will_be_released ? use_count_mux() : use_count_mux() - 1) > 0)
		return nullptr; // this adapter is reserved by another subscription and will remain reserved; we cannot use it
	const bool disregard_networks{false};
	if (!chdb::lnb_can_tune_to_mux(lnb, mux, disregard_networks))
		return nullptr;
	/*
		We now know that this lnb is of the right type (e.g., Universal) and is configured for the desired network
	*/
	bool is_auto = mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_AUTO;
	auto fe = adapter->fe_for_delsys(is_auto ? chdb::fe_delsys_t::SYS_DVBS2 : (chdb::fe_delsys_t)mux.delivery_system);
	if (!fe)
		return nullptr; // this adapter has no suitable frontend

	auto t = fe->ts.readAccess();
	if ((blindscan || is_auto) && !t->dbfe.supports.blindscan)
		return nullptr;
	if (!t->can_be_used			// adapter is in use by an external program
			|| !t->dbfe.enabled // adapter is disabled by user configuration
			/*@todo: DVB-S2X: drivers support this if one passes DVB-S2 in stead
				but this approach makes it impossible to tell if the mux we are handling here
				requires DVB-S2X and if we should reject tuners which do not support DVB-S2X
			*/
			|| (mux.stream_id >= 0 && !t->dbfe.supports.multistream) // multistream is needed but not supported
			|| lnb.k.adapter_no != t->dbfe.k.adapter_no							 // lnb is not connected to this adapter
		)
		return nullptr;

	assert(!is_tuned_to(mux, nullptr)); // caller should not call us in this case
	bool is_slave = (t->dbfe.master_adapter >= 0);
	if (is_slave) {
		// slave adapters can only be used if their master adapter is already tuned t the proper band and polarisation
		auto* master_adapter = adaptermgr->find_adapter(t->dbfe.master_adapter);
		if (!master_adapter) {
			dterrorx("Could not find master adapter %d", t->dbfe.master_adapter);
			return nullptr;
		}

		auto mr = master_adapter->reservation.readAccess();
		auto* master_mux = std::get_if<chdb::dvbs_mux_t>(&mr->reserved_mux);
		assert(master_mux);
		if (mr->exclusive || mr->use_count_mux() == 0) {
			// master adapter is not in use, so we should not use the slave either
			return nullptr;
		}
		// master adapter is in use; check it is tuned to the correct sat/polarisation/band
		if (master_mux->k.sat_pos != mux.k.sat_pos || master_mux->pol != mux.pol)
			return nullptr; // wrong polarisation
		auto [master_band, master_voltage, master_freq] = chdb::lnb::band_voltage_freq_for_mux(mr->reserved_lnb, *master_mux);
		auto [slave_band, slave_voltage, slave_freq] = chdb::lnb::band_voltage_freq_for_mux(lnb, mux);
		assert(slave_voltage == master_voltage);
		return (slave_band == master_band) ? fe : nullptr;
	}

	assert(!is_slave);
	auto reserved_mux_ = std::get_if<chdb::dvbs_mux_t>(&this->reserved_mux);
	assert(reserved_mux_);
	if (!reserved_fe)
		reserved_mux_ = nullptr; // nothing reserved right now

	if (use_count_polband() > 0) {
		/*Some slave adapter requires that we do not change the polarisation and band, so check if
			the new reservation would respect this
		*/
		assert(reserved_mux_);
		auto [master_band, master_voltage, master_freq] =
			chdb::lnb::band_voltage_freq_for_mux(this->reserved_lnb, *reserved_mux_);
		auto [slave_band, slave_voltage, slave_freq] = chdb::lnb::band_voltage_freq_for_mux(lnb, mux);
		if (mux.k.sat_pos != reserved_mux_->k.sat_pos || master_band != slave_band || master_voltage != slave_voltage)
			return nullptr; // this cannot switch to proper band
	}

	if (lnb.rotor_control == chdb::rotor_control_t::FIXED_DISH)
		return fe;

	assert(use_count_mux() <= 1);
	auto& res = adaptermgr->sat_reservation(lnb.k.dish_id);
	auto dish_sat_pos = res.sat_pos - lnb.offset_pos; // position to which dish (as opposed to where offset lnb points)
	// if this is a master lnb on a rotor we can only move it if use c
	if (lnb.rotor_control == chdb::rotor_control_t::ROTOR_MASTER_USALS ||
			lnb.rotor_control == chdb::rotor_control_t::ROTOR_MASTER_DISEQC12) {
		if (dish_sat_pos == mux.k.sat_pos)
			return fe; // dish is pointing to the correct sat
		if (use_count_mux() > 0) {
			// the adapter is already in use, by the current subscription and will be released
			if (res.use_count() == 0)
				return fe; // dish will be able to move
			else if (res.use_count() == 1 && this->reserved_lnb.k.dish_id == lnb.k.dish_id)
				return fe; // sat has been reserved, but will be released by current subscription
			else
				return nullptr;
		} else {
			// the adapter is not yet in use
			if (res.use_count() == 0)
				return fe; // dish will be able to move
			else
				return nullptr;
		}
	} else if (lnb.rotor_control == chdb::rotor_control_t::ROTOR_SLAVE) {
		if (dish_sat_pos == mux.k.sat_pos)
			return fe; // dish is pointing to the correct sat
		else
			return nullptr;
	}

	return nullptr;
}

std::shared_ptr<dvb_frontend_t> dvbdev_monitor_t::find_fe_for_lnb(const chdb::lnb_t& lnb,
																																	const dvb_adapter_t* adapter_to_release,
																																	bool need_blindscan, bool need_spectrum) const {
	using namespace chdb;
	{
		auto& r = sat_reservation(lnb.k.dish_id);
		if (r.exclusive_fe) {
			dterror("LNB " << lnb << " connected to a dish which has been reserved exclusively");
		}
	}

	auto it_adapter = adapters.find(adapter_no_t(lnb.k.adapter_no));
	if (it_adapter == adapters.end()) {
		dtdebug("LNB " << lnb << " not connected to any adapter");
		return nullptr;
	}

	auto& [adapter_no, adapter] = *it_adapter;
	if (!adapter.can_be_used) {
		dtdebug("LNB " << lnb << " cannot be used because adapter cannot be used");
		return nullptr;
	}
	bool adapter_will_be_released = adapter_to_release == &adapter;
	if (!adapter_will_be_released && adapter.reservation.readAccess()->exclusive) {
		dtdebug("LNB " << lnb << " is reserved exclusively by some other user");
		return nullptr;
	}

	return adapter.fe_for_delsys(chdb::fe_delsys_t::SYS_DVBS2);
}

/*
	adapter_will_be_released indicates an the adapter is currently reserved but that
	reservation will be released by retuning, because it is no longer
	need by the subscription

	Return the frontend which can be used or a nullptr
*/
template <typename mux_t>
std::shared_ptr<dvb_frontend_t> adapter_reservation_t::can_tune_to(const mux_t& mux, bool adapter_will_be_released,
																																	 bool blindscan) const {
	if ((!adapter_will_be_released ? use_count_mux() : use_count_mux() - 1) > 0)
		return nullptr; // this adapter is reserved by another subscription and will remain reserved; we cannot use it
	auto fe = adapter->fe_for_delsys((chdb::fe_delsys_t)mux.delivery_system);
	if (!fe)
		return nullptr; // this adapter has no suitable frontend

	auto t = fe->ts.readAccess();
	if (blindscan && !t->dbfe.supports.blindscan)
		return nullptr;
	if (!t->can_be_used			// adapter is in use by an external program
			|| !t->dbfe.enabled // adapter is disabled by user configuration
			/*@todo: DVB-S2X: drivers support this if one passes DVB-S2 in stead
				but this approach makes it impossible to tell if the mux we are handling here
				requires DVB-S2X and if we should reject tuners which do not support DVB-S2X
			*/
			|| (mux.stream_id >= 0 && !t->dbfe.supports.multistream) // multistream is needed but not supported
		)
		return nullptr;

	assert(!is_tuned_to(mux, nullptr)); // caller should not call us in this case

	assert(use_count_mux() <= 1);
	return fe;
}

template std::shared_ptr<dvb_frontend_t>
adapter_reservation_t::can_tune_to(const chdb::dvbt_mux_t& mux, bool adapter_will_be_released, bool blindscan) const;

template std::shared_ptr<dvb_frontend_t>
adapter_reservation_t::can_tune_to(const chdb::dvbc_mux_t& mux, bool adapter_will_be_released, bool blindscan) const;

template <typename mux_t>
std::shared_ptr<dvb_frontend_t>
dvbdev_monitor_t::find_adapter_for_tuning_to_mux(db_txn& txn, const mux_t& mux, const dvb_adapter_t* adapter_to_release,
																								 bool blindscan) const {
	std::shared_ptr<dvb_frontend_t> best_fe;
	int best_fe_prio = -1;

	for (auto& [adapter_no, adapter] : adapters) {
		auto adapter_reservation = adapter.reservation.readAccess();
		bool adapter_will_be_released = adapter_to_release == &adapter;
		auto fe = adapter_reservation->can_tune_to(mux, adapter_will_be_released, blindscan);
		if (!fe.get() || fe->ts.readAccess()->dbfe.priority <= best_fe_prio)
			continue;
		auto fe_prio = fe->ts.readAccess()->dbfe.priority;
		if (fe_prio <= best_fe_prio)
			continue;
		best_fe = fe;
		best_fe_prio = fe_prio;
	}
	return best_fe;
}


std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t, int, int>
dvbdev_monitor_t::find_lnb_for_tuning_to_mux_(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
																						 const dvb_adapter_t* adapter_to_release, bool blindscan,
																						 int required_adapter_no) const {
	using namespace chdb;
	auto c = find_first<chdb::lnb_t>(txn);
	int best_lnb_prio = std::numeric_limits<int>::min();
	int best_fe_prio = std::numeric_limits<int>::min();
	// best lnb sofar, and the corresponding connected frontend
	chdb::lnb_t best_lnb;
	std::shared_ptr<dvb_frontend_t> best_fe;
	auto found = false;

	/*
		Loop over all lnbs to find a suitable one. If one is found, check if the connected adapter is suitable and
		not in use, or already tuned to the desired mux. Among all possibilities, pick the one with the best priority
	*/

	for (auto const& lnb : c.range()) {
		if (required_lnb && required_lnb->k != lnb.k)
			continue;
		auto* plnb = required_lnb ? required_lnb : &lnb;
		if (!plnb->enabled)
			continue;
		/*
			required_lnb may not have been saved in the database and may contain additional networks or
			edited settings when called from positioner_dialog
		*/
		auto [has_network, network_priority, usals_move_amount] = chdb::lnb::has_network(*plnb, mux.k.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		auto dish_needs_to_be_moved_ = usals_move_amount != 0;
		auto lnb_priority = network_priority >= 0 ? network_priority : plnb->priority;
		auto penalty = dish_needs_to_be_moved_ ? get_dish_move_penalty() : 0;
		if (!has_network ||
				(lnb_priority >= 0 && lnb_priority - penalty < best_lnb_prio) // priority of frontend is ignored in this case
			)
			continue;

		auto& r = sat_reservation(plnb->k.dish_id);
		if (r.exclusive_fe) {
			dtdebug("LNB " << *plnb << " connected to a dish which has been reserved exclusively");
			/*
				some user has reserved the dish to control the positioner. Unexpected consequences are possible,
				so we penalize this choice extra
			*/
			penalty += 1000;
		}

		auto it_adapter = adapters.find(adapter_no_t(required_adapter_no <0 ? plnb->k.adapter_no : required_adapter_no));
		if (it_adapter == adapters.end()) {
			dtdebug("LNB " << *plnb << " not connected to any adapter");
			continue;
		}

		auto& [adapter_no, adapter] = *it_adapter;
		if (!adapter.can_be_used) {
			dtdebug("LNB " << *plnb << " cannot be used because adapter cannot be used");
			continue;
		}


		bool adapter_will_be_released = adapter_to_release == &adapter;
		if (!adapter_will_be_released && adapter.reservation.readAccess()->exclusive) {
			dtdebug("LNB " << *plnb << " is reserved exclusively by some other user");
			continue;
		}

		auto adapter_reservation = adapter.reservation.readAccess();
		auto fe = adapter_reservation->can_tune_to(*plnb, mux, adapter_will_be_released, blindscan);
		if (!fe.get()) {
			dtdebug("LNB " << *plnb << " cannot be used");
			continue;
		}

		if (required_adapter_no > 0 && lnb.k.adapter_no != fe->ts.readAccess()->dbfe.master_adapter)
			continue;

		{
			auto fe_prio = fe->ts.readAccess()->dbfe.priority;

			if (lnb_priority < 0 || lnb_priority - penalty == best_lnb_prio)
				if (fe_prio - penalty <= best_fe_prio) // use fe_priority to break the tie
					continue;

			/*we cannot move the dish, but we can still use this lnb if the dish
				happens to be pointint to the correct sat
			*/

			best_fe_prio = fe_prio - penalty;
			best_lnb_prio = (lnb_priority < 0 ? fe_prio : lnb_priority) - penalty; //<0 means: use fe_priority
			best_lnb = *plnb;
			best_fe = fe;
		}
		found = true;
		if (required_lnb)
			break;
	}
	return std::make_tuple(best_fe, best_lnb, best_fe_prio, best_lnb_prio);
}

std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t>
dvbdev_monitor_t::find_slave_tuner_for_tuning_to_mux(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
																										 const dvb_adapter_t* adapter_to_release, bool blindscan) const {
	using namespace chdb;
	auto ca = find_first<chdb::fe_t>(txn);
	int best_lnb_prio = std::numeric_limits<int>::min();
	int best_fe_prio = std::numeric_limits<int>::min();
	// best lnb sofar, and the corresponding connected frontend
	chdb::lnb_t best_lnb;
	std::shared_ptr<dvb_frontend_t> best_fe;
	auto found = false;

	for (auto const& dbfe : ca.range()) {
		if(dbfe.master_adapter <0 || ! dbfe.can_be_used || ! dbfe.present)
			continue;
		int required_adapter_no = dbfe.master_adapter;
		auto [ fe, lnb, fe_prio, lnb_prio] =
			find_lnb_for_tuning_to_mux_(txn, mux, required_lnb, adapter_to_release, blindscan,
																										required_adapter_no);
		if (!fe)
			continue;
		if (lnb_prio < best_lnb_prio)
			continue;
		fe_prio = dbfe.priority;
		if (lnb_prio  == best_lnb_prio && fe_prio <= best_fe_prio)
			continue;

		best_fe_prio = fe_prio;
		best_lnb_prio = lnb_prio;
		best_lnb = lnb;
		best_fe = fe;
		found = true;
		if (required_lnb)
			break;
	}
	return std::make_tuple(best_fe, best_lnb);
}

std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t>
dvbdev_monitor_t::find_lnb_for_tuning_to_mux(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
																						 const dvb_adapter_t* adapter_to_release, bool blindscan) const {
	auto [fe, lnb] =
		find_slave_tuner_for_tuning_to_mux(txn, mux, required_lnb, adapter_to_release,blindscan);
	if (fe)
		return std::make_tuple(fe, lnb);
	{
		int required_adapter_no = -1;
		auto [fe, lnb, fe_prio, lnb_prio] =
			find_lnb_for_tuning_to_mux_(txn, mux, required_lnb, adapter_to_release, blindscan, required_adapter_no);
		return std::make_tuple(fe, lnb);
	}

}


template <typename mux_t>
std::shared_ptr<dvb_frontend_t> adaptermgr_t::find_adapter_for_tuning_to_mux(db_txn& txn, const mux_t& mux,
																																						 const dvb_adapter_t* adapter_to_release,
																																						 bool blindscan) const {
	return (static_cast<const dvbdev_monitor_t*>(this))
		->find_adapter_for_tuning_to_mux(txn, mux, adapter_to_release, blindscan);
}

template std::shared_ptr<dvb_frontend_t>
adaptermgr_t::find_adapter_for_tuning_to_mux(db_txn& txn, const chdb::dvbc_mux_t& mux,
																						 const dvb_adapter_t* adapter_to_release, bool blindscan) const;

template std::shared_ptr<dvb_frontend_t>
adaptermgr_t::find_adapter_for_tuning_to_mux(db_txn& txn, const chdb::dvbt_mux_t& mux,
																						 const dvb_adapter_t* adapter_to_release, bool blindscan) const;

std::tuple<std::shared_ptr<dvb_frontend_t>, chdb::lnb_t>
adaptermgr_t::find_lnb_for_tuning_to_mux(db_txn& txn, const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb,
																				 const dvb_adapter_t* adapter_to_release, bool blindscan) const {
	return (static_cast<const dvbdev_monitor_t*>(this))
		->find_lnb_for_tuning_to_mux(txn, mux, required_lnb, adapter_to_release, blindscan);
}

std::shared_ptr<dvb_frontend_t> adaptermgr_t::find_fe_for_lnb(const chdb::lnb_t& lnb,
																															const dvb_adapter_t* adapter_to_release,
																															bool need_blindscan, bool need_spectrum) const {
	return (static_cast<const dvbdev_monitor_t*>(this))
		->find_fe_for_lnb(lnb, adapter_to_release, need_blindscan, need_spectrum);
}

#ifdef TODO
struct new_reservation_t {
	int subscription_id{-1};
	int dish_id;

	new_reservation_t(

		int subscription_id, const lnb_t& lnb& lnb,
		int16_t sat_pos,								// if not "sat_pos_none" subscription promises to not move dish
		const* dvb_frontend_t frontend, // can be nullptr to pick any (in case of the tbs rf_mux
		chdb::fe_band_pol_t band_pol,		// if not "UNKNOWN", subscription promises to not change this later

		)
		: subscription_id(subscription_id), dish_id(dish_id) {}

	// subscribing a speciic mux means specificying a sat_pos and a band_pol

	new_reservation_t(int subscription_id, int16_t sat_pos, const dvb_frontend_t& frontend)
		:

		enum reservation_type_t {

	};

	int16_t sat_pos{sat_pos_none}
	:																// if different from sat_pos_none, subscription promises to not move dish
	chdb::fe_band_pol_t band_pol; // if not "UNKNOWN", subscription promises to not change this later

	// required capabilities
	bool specrum_scan;
	bool blindscan;
	bool multi_stream;
}
#endif
