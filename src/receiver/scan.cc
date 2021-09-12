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

#include "scan.h"
#include "active_adapter.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/db_keys_helper.h"
#include "receiver.h"

/*
	Processes events and continues scanning.
	Return 0 when scanning is done; otherwise number of muxes left

	finished_subscription_id, if >=0 is a subscription which has completed scanning a mux
*/
int scanner_t::housekeeping(const active_adapter_t* active_adapter_p, const chdb::any_mux_t* finished_mux) {
	if (must_end)
		return 0;

	int remaining = scan_loop(active_adapter_p, finished_mux);

	dterrorx("%d muxes left to scan", remaining);
	return must_end ? 0 : remaining + subscriptions.size();
}

static void report(const char* msg, int finished_subscription_id, int subscription_id, const chdb::dvbs_mux_t& mux,
				 const ss::vector_<int>& subscription_ids) {
	if (finished_subscription_id < 0 && subscription_id < 0)
		return;
	ss::string<128> s;
	to_str(s, mux);
	s.sprintf("Scan  %s %d,%d: [", msg, s.c_str(), finished_subscription_id, subscription_id);
	for (auto& id : subscription_ids)
		s.sprintf("%2d ", id);
	s.sprintf("] \n");
	dtdebug(s.c_str());
}

/*
	Returns number of muxes left to scan
*/
int scanner_t::scan_loop(const active_adapter_t* active_adapter_p, const chdb::any_mux_t* finished_mux) {
	std::vector<task_queue_t::future_t> futures;
	int error{};
	int remaining = 0;
	auto& rm = receiver.reserved_muxes.owner_read_ref();
	try {
		auto todo_txn = done_db.wtxn();
		add_new_muxes(todo_txn);
		int finished_subscription_id{-1};
		if (finished_mux) {
			auto [it, found] = find_in_map_if(rm, [active_adapter_p](const auto& x) {
				auto [fd_, ptr] = x;
				return ptr.get() == active_adapter_p;
			});
			if (!found) {
				dterror("Cannot find subscription for active_adapter");
			}
			finished_subscription_id = it->first;
			bool as_completed = true;
			if (finished_mux) {
				auto dvbs_mux = std::get_if<chdb::dvbs_mux_t>(finished_mux);
				if (dvbs_mux) {
					auto tmp = *dvbs_mux;
					add_mux(todo_txn, tmp, as_completed);
				}
			}
		}
		int subscription_id = -1;
		auto todo_c = chdb::find_first<chdb::dvbs_mux_t>(todo_txn);
		int count = 0;
		// start as many subscriptions as possible
		auto txn = receiver.chdb.rtxn();
		for (auto mux_to_scan : todo_c.range()) {
			count++;
			if (mux_to_scan.c.scan_status != chdb::scan_status_t::PENDING)
				continue;
			remaining++;
			/*@todo: add newly found muxes*/
			if (subscriptions.size() >= (finished_subscription_id < 0 ? max_num_subscriptions : max_num_subscriptions + 1))
				break;
			tune_options_t tune_options(scan_target_t::SCAN_MINIMAL); // prevent epg scan
			int subscription_id =
				receiver_thread.subscribe_mux(futures, txn, mux_to_scan, finished_subscription_id, tune_options, nullptr);
			report("SUBSCRIBED", finished_subscription_id, subscription_id, mux_to_scan, subscriptions);
			error = wait_for_all(futures); // must be done before continuing this loop

			if (subscription_id < 0) {
				// we cannot subscribe the mux right now
				continue;
			}
			mux_to_scan.c.scan_status = chdb::scan_status_t::ACTIVE;
			put_record(todo_txn, mux_to_scan);

			if (finished_subscription_id >= 0) {
				/*all other available subscriptions are still in use, and there is no point
					in continuing to check
				*/
				assert(subscription_id == finished_subscription_id);
				finished_subscription_id = -1; // we have reused finished_subscription_id, but can only do that once
				continue;
			} else {
				// this is a new subscription
				assert(finished_subscription_id == -1);
				subscriptions.push_back(subscription_id);
			}
		}
		todo_txn.commit();
		txn.abort();
		if (finished_subscription_id >= 0 && subscription_id < 0) {
			/*
				We arrive here only when finished_subscription_id was not reused at all
			*/
			// This subscription is currently not of any use
			auto it = std::find(subscriptions.begin(), subscriptions.end(), finished_subscription_id);
			if (it != subscriptions.end()) {
				dtdebugx("Abandoning subscription %d", finished_subscription_id);
				cb(receiver_thread).unsubscribe(finished_subscription_id);
				auto idx = &*it - &subscriptions[0];
				assert(idx >= 0);
				assert(idx < subscriptions.size());
				subscriptions.erase(idx);
				report("ERASED", finished_subscription_id, subscription_id, chdb::dvbs_mux_t(), subscriptions);
			} else {
				dtdebugx("Unexpected: %d", finished_subscription_id);
			}
		}

		// error = wait_for_all(futures);

		if (error) {
			dterror("Error encountered during scan");
		}
	} catch (std::runtime_error) {
		dterror("run time exception encoutered");
		assert(0);
	}
	return remaining;
}

int receiver_thread_t::cb_t::subscribe_scan(ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<chdb::lnb_t>* lnbs,
																						bool scan_found_muxes, int max_num_subscriptions, int subscription_id) {
	ss::string<32> s;
	s << "SUB[" << subscription_id << "] Request scan";
	log4cxx::NDC ndc(s);

	std::vector<task_queue_t::future_t> futures;
	bool service_only = false;
	if (subscription_id >= 0)
		unsubscribe_(futures, subscription_id, service_only);

	bool error = wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe_scan");
	}
	subscription_id = this->receiver_thread_t::subscribe_scan(futures, muxes, lnbs, scan_found_muxes,
																														max_num_subscriptions, subscription_id);
	error |= wait_for_all(futures);
	if (error) {
		dterror("Unhandled error in subscribe_scan");
	}
	return subscription_id;
}

scanner_t::scanner_t(receiver_thread_t& receiver_thread_,
										 //ss::vector_<chdb::dvbs_mux_t>& muxes, ss::vector_<chdb::lnb_t>* lnbs_,
										 bool scan_found_muxes_, int max_num_subscriptions_,
										 int subscription_id_)
	: receiver_thread(receiver_thread_)
	, receiver(receiver_thread_.receiver)
	,	subscription_id(subscription_id_)
	,	max_num_subscriptions(max_num_subscriptions_)
	,	scan_found_muxes(scan_found_muxes_)
	,	done_db(/*readonly*/ false, /*is_temp*/ true)
{
	done_db.open_temp("/tmp/neumoscan");
	// if(lnbs_)
	//	allowed_lnbs = *lnbs_;
	auto txn = receiver.chdb.rtxn();
	last_seen_txn_id = txn.txn_id();
	txn.abort();
	//	add_initial_muxes(muxes);
}

static bool check_rescan(const chdb::dvbs_mux_t& db_mux, const chdb::dvbs_mux_t& mux) {
	bool rescan = db_mux.c.scan_result == chdb::scan_result_t::FAILED &&
		(chdb::is_template(db_mux) || db_mux.delivery_system != mux.delivery_system ||
		 db_mux.frequency != mux.frequency || db_mux.symbol_rate != mux.symbol_rate ||
		 db_mux.modulation != mux.modulation);
	return rescan;
}

static bool check_rescan(const chdb::dvbc_mux_t& db_mux, const chdb::dvbc_mux_t& mux) {
	bool rescan = db_mux.c.scan_result == chdb::scan_result_t::FAILED &&
		(chdb::is_template(db_mux) || db_mux.delivery_system != mux.delivery_system ||
		 db_mux.frequency != mux.frequency || db_mux.symbol_rate != mux.symbol_rate ||
		 db_mux.modulation != mux.modulation);
	return rescan;
}

static bool check_rescan(const chdb::dvbt_mux_t& db_mux, const chdb::dvbt_mux_t& mux) {
	bool rescan = db_mux.c.scan_result == chdb::scan_result_t::FAILED &&
		(chdb::is_template(db_mux) || db_mux.delivery_system != mux.delivery_system ||
		 db_mux.frequency != mux.frequency || db_mux.modulation != mux.modulation);
	return rescan;
}

/*
	Returns true if a the  mux was new, or false if it was already planned
*/
template <typename mux_t> bool scanner_t::add_mux(db_txn& done_txn, mux_t& mux, bool as_completed) {
	/*
		The following will check if a mux with similar frequency of coordinates already exists.
		If not, the mux will be inserted in done_db; otherwise done_db will remain unchanged
	*/
	bool updated = false;
	auto c = chdb::find_mux_by_key_or_frequency(done_txn, mux);
	if (!as_completed && c.is_valid()) {
		// if(db_mux.c.scan_status != chdb::scan_status_t::PENDING)
		//	return false;

		auto db_mux = c.current();
		bool rescan = check_rescan(db_mux, mux);
		if (!rescan)
			return false;
		updated = true;
		if (db_mux.k == mux.k && db_mux.c.scan_status != mux.c.scan_status &&
				mux.c.scan_status != chdb::scan_status_t::IDLE) {
			ss::string<32> s;
			to_str(s, mux);
			dtdebugx("Overwrite %s: %d => %d\n", s.c_str(), (int)db_mux.c.scan_status, (int)mux.c.scan_status);
		}
	}
	bool failed = false;
	if (as_completed) {
		failed = mux.c.scan_result == chdb::scan_result_t::FAILED;
		mux.c.scan_result = chdb::scan_result_t::OK; //@todo: add a failed/ok status
	}
	mux.c.scan_status = as_completed ? chdb::scan_status_t::IDLE : chdb::scan_status_t::PENDING;

	namespace m = chdb::update_mux_preserve_t;
	auto r = chdb::update_mux(done_txn, mux, mux.c.mtime, m::flags{m::ALL & ~m::SCAN_DATA});

	bool is_new = (r == chdb::update_mux_ret_t::NEW);
	{
		auto w = scan_stats.writeAccess();
		if (updated) {
			w->updated_muxes++;
			assert(!is_new);
		} else {
			w->new_muxes += !!is_new;
			w->updated_muxes += !is_new;
		}
		if (as_completed) {
			w->finished_muxes++;
			w->failed_muxes += failed;
		} else if (!updated)
			w->scheduled_muxes++;
	}
	return is_new;
}

int scanner_t::add_initial_muxes(ss::vector_<chdb::dvbs_mux_t>& muxes) {
	scan_dvbs = true;
	auto txn = done_db.wtxn();
	for (auto& mux : muxes) {
		mux.c.scan_status = chdb::scan_status_t::PENDING;
		put_record(txn, mux);
	}
	{
		auto w = scan_stats.writeAccess();
		w->scheduled_muxes = muxes.size();
	}
	txn.commit();
	return 0;
}

int scanner_t::add_initial_muxes(ss::vector_<chdb::dvbc_mux_t>& muxes) {
	scan_dvbc = true;
	auto txn = done_db.wtxn();
	for (auto& mux : muxes) {
		mux.c.scan_status = chdb::scan_status_t::PENDING;
		put_record(txn, mux);
	}
	txn.commit();
	return 0;
}

int scanner_t::add_initial_muxes(ss::vector_<chdb::dvbt_mux_t>& muxes) {
	scan_dvbt = true;
	auto txn = done_db.wtxn();
	for (auto& mux : muxes) {
		mux.c.scan_status = chdb::scan_status_t::PENDING;
		put_record(txn, mux);
	}
	txn.commit();
	return 0;
}

template <typename mux_t> int scanner_t::add_new_muxes_(db_txn& done_txn) {
	if (!scan_found_muxes)
		return 0;

	auto txn = receiver.chdb.rtxn();
	auto& from_db = receiver.chdb;

	int count = 0;

	// make a key containing (type_id, to_txn_id) as its value; this is a key in the log table
	auto start_logkey = mux_t::make_log_key(last_seen_txn_id);

	// make a prefix, which will restrict the type of the records we will actually handle
	ss::bytebuffer<32> key_prefix;
	encode_ascending(key_prefix, data_types::data_type<mux_t>());

	// initialize an index cursor. It will run over all log records with the correct type_id
	auto c = from_db.template tcursor_log<mux_t>(txn, key_prefix);

	// position the index cursor at the first log record of interest
	find_by_serialized_secondary_key(c, start_logkey, key_prefix, find_type_t::find_geq);

	/*we cannot use c.range() because some secondary keys in
		log may point to deleted records and my not have a primary record
	*/
	auto done = !c.is_valid();
	for (; !done; done = !c.next()) {
		assert(c.is_valid());
		// k points to serialized (type_id, txn_id)
		bool has_been_deleted = !c.maincursor.is_valid(); //@todo: maybe too much of a hack
		ss::bytebuffer<32> old_secondary_key;
		count++;
		if (!has_been_deleted) {
			mux_t mux = c.current();
			bool as_completed = false;
			add_mux(done_txn, mux, as_completed);
		} else {
		}
	}
	last_seen_txn_id = txn.txn_id();
	txn.abort();
	// from_txn.abort();
	return count > 0;
}

int scanner_t::add_new_muxes(db_txn& done_txn) {
	bool ret = false;
	if (scan_dvbs)
		ret |= add_new_muxes_<chdb::dvbs_mux_t>(done_txn);
	if (scan_dvbc)
		ret |= add_new_muxes_<chdb::dvbc_mux_t>(done_txn);
	if (scan_dvbt)
		ret |= add_new_muxes_<chdb::dvbt_mux_t>(done_txn);
	return ret;
}

void scanner_t::set_allowed_lnbs(const ss::vector_<chdb::lnb_t>& lnbs) { allowed_lnbs = lnbs; }

void scanner_t::set_allowed_lnbs() { allowed_lnbs.clear(); }

scanner_t::~scanner_t() {
	for (auto subscription_id : subscriptions) {
		cb(receiver_thread).unsubscribe(subscription_id);
	}
}
