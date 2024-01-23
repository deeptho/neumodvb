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

#include "neumodb/cursors.h"

/*
	singleton; same for all threads
 */
template<typename db_t>
class wtxn_reservation_t {
	db_t& db;
	int last_issued_ticket{0}; //last ticket issued by to any threads
	int owning_ticket{-1}; //ticket in use by thread currently having the lock
	std::thread::id last_owner; //last ticket obtained by this thread
	std::condition_variable cv;
	std::mutex mutex;
	std::optional<db_txn> wtxn_;
	steady_time_t last_wtxn_start_time;
	static constexpr std::chrono::milliseconds max_wtxn_lifetime{200ms};

	wtxn_reservation_t(db_t& db)
		: db(db)
		{}

	/*
		commit and release wtxn
	 */
	void commit_master_wtxn() {
		assert(wtxn_);
		assert(owning_ticket>=0);
		owning_ticket = -1; //allow other threads to use the mutex
		assert(wtxn_->can_commit());
		wtxn_->commit();
		wtxn_.reset();
	}

	bool thread_waiting_for_wtxn() const {
		return last_issued_ticket != owning_ticket;
	}

	bool release_should_commit() const {
		if(!thread_waiting_for_wtxn())
			return true;  /*we must commit as no thread will continue to use the wtxn
											and therefore no future commit action might happen soon*/
		/*
			Some other threads is going to use the transaction and will be responsible
			for comitting, but if the last commit was to long ago, we should commit
			and the next user should create a new wtxn
		 */
		auto now = 	steady_clock_t::now();
		auto lifetime =  std::chrono::duration_cast<std::chrono::milliseconds>(now - last_wtxn_start_time);
		return lifetime >= max_wtxn_lifetime;
	}

public:

	//Meyers singleton: should be tread safe, but destruction order is not specified
	static wtxn_reservation_t& get_instance(db_t& db) {
		static wtxn_reservation_t<db_t> ret(db);
		return ret;
	}

	db_t& get_db() {
		return db;
	}

	db_txn& acquire_master_wtxn() {
		std::unique_lock<std::mutex> lk(mutex);

		//wait for access to the wtxn
		int my_ticket = ++last_issued_ticket;
		cv.wait(lk, [this] {
			return (owning_ticket == -1) ;
		});

		last_owner = std::this_thread::get_id();
		owning_ticket = my_ticket;
		if(wtxn_) {
			return *wtxn_;
		}
		wtxn_.emplace(db.wtxn());
		last_wtxn_start_time = 	steady_clock_t::now();
		return *wtxn_;
	}

	/*
		release wtxn without committing yet, except in the following cases:
		-no other thread is waiting to acquire the wtxn, and therefore there is no guarantee
		of a future commit in the near term
		-the transaction has been open for too long

		If the txn is not committed now, then  the next thread which
		will acquire the wtxn will later try to commit
	*/
	void release_master_wtxn(/*bool force_commit=false*/) {
		const bool force_commit=true;
		std::unique_lock<std::mutex> lk(mutex);
		if(!wtxn_) {
			return;
		}
		if(force_commit || release_should_commit()) {
			commit_master_wtxn();
			owning_ticket = -1; //allow other threads to use the wtxn now
			cv.notify_one();
		}
		else if(thread_waiting_for_wtxn()) {
			owning_ticket = -1; //allow other threads to use the wtxn now
			cv.notify_one();
		} else {
			owning_ticket = -1; //allow other threads to use the wtxn in the future
		}
	}

	/*
		If other threads are waiting, then commit the transaction and
		release the wtxn so that other threads can use it.

		Otherwise do not commit the txn, but keep it open so that we can quickly
		reuse it later. This counts on our thread later calling release_wtxn
	 */
	inline bool maybe_release_master_wtxn() {
		std::unique_lock<std::mutex> lk(mutex);
		if(wtxn_ && thread_waiting_for_wtxn()) {
			//other thread wants to being a wtxn
			commit_master_wtxn();
			owning_ticket = -1; //allow other threads to use the wtxn now
			cv.notify_one();
			return true;
		}
		return false;
	}
};


template<typename db_t> class txnmgr_t;

template<typename db_t>
class txn_proxy_t {
	txnmgr_t<db_t>* txnmgr;
	bool readonly{false};

public:

	txn_proxy_t(txnmgr_t<db_t>* txnmgr, bool readonly)
		: txnmgr(txnmgr)
		, readonly(readonly) {}

	~txn_proxy_t();

	inline operator db_txn& ();

	inline void commit();
	inline void abort();
};

/*
	factory object for creating proxy objects to
	a singleton lmdb wtxn transaction shared by all threads
	and/or for creating a resetable rtxn transaction

	multiple factories may exist per thread. They use the same wtxn
	but separate rtxn

	each factory object should only be accessed from a single thread

 */
template<typename db_t>
class txnmgr_t {
	db_t& db;
	std::optional<db_txn> readonly_txn;
	bool has_been_reset{false};
	wtxn_reservation_t<db_t>& reservation;
	int last_wtxn_id{-1};

	int rtxn_open_count{0};
	db_txn* owned_wtxn{nullptr};
	std::optional<db_txn>  child_txn; //to store the last used child_txn

	inline db_txn* acquire_wtxn() {
		auto& wtxn = reservation.acquire_master_wtxn();
		owned_wtxn = & wtxn;
		last_wtxn_id = wtxn.txn_id();
		return owned_wtxn;
	}

	inline void release_master_wtxn(/*bool force_commit=false*/) {
		reservation.release_master_wtxn(/*force_commit*/);
		owned_wtxn = nullptr;
	}

	inline void maybe_release_master_wtxn() {
		bool released = reservation.maybe_release_master_wtxn();
		if(released) {
			owned_wtxn = nullptr;
		}
	}

public:

	/*
		returns a reference to a global write tranaction,
		after sleeping until it can be acquired
	 */
	inline txn_proxy_t<db_t> wtxn() {
		//cannot start a new wtxn before the old one has been committed or aborted
		assert(!owned_wtxn);
		assert(!child_txn);
		return txn_proxy_t(this, false /*readonly*/);
	}

	inline db_txn& begin_wtxn() {
		//cannot start a new wtxn before the old one has been committed or aborted
		if(!owned_wtxn) {
			owned_wtxn = acquire_wtxn();
			assert(!child_txn);
			child_txn.emplace(owned_wtxn->child_txn());
		}
		return *child_txn;
	}

	inline void commit_child_wtxn() {
		//cannot start a new wtxn before the old one has been committed or aborted
		if(!child_txn)
			return;
		assert(owned_wtxn);
		child_txn->commit();
		child_txn.reset();
		this->maybe_release_master_wtxn();
	}

	inline void abort_child_wtxn() {
		//cannot start a new wtxn before the old one has been committed or aborted
		if(!child_txn)
			return;
		assert(owned_wtxn);
		child_txn->abort();
		child_txn.reset();
		this->maybe_release_master_wtxn();
	}


	inline bool can_commit() const {
		return owned_wtxn;
	}

	inline void flush_wtxn() {
		if(!owned_wtxn)
			return;
		assert(!child_txn);
		this->release_master_wtxn(/*true*/ /*force_commit*/);
	}

	/*
		release our wtxn, make it available for other threads to use,
		but without commiting or aborting.

		If another thread acquires the wtxn, it will first commit our data
	 */
	inline void release_wtxn() {
		assert(!child_txn);
		if(owned_wtxn) {
			if(child_txn) {
				child_txn->abort();
				child_txn.reset();
			}
			assert(!child_txn);
			this->release_master_wtxn(/*false*/ /*force_commit*/);
		} else {
			assert(!child_txn);
		}
	}



	/*
		returns a reference to a global readonl tranaction,
		which may be in the reset state.
	 */
	inline txn_proxy_t<db_t> rtxn() {
		//cannot start a new rtxn before the old one has been committed or aborted
		return txn_proxy_t(this, true /*readonly*/);
	}

	inline db_txn& begin_rtxn() {
		//cannot start a new rtxn before the old one has been committed or aborted
		if(!readonly_txn) {
			assert(!has_been_reset);
			readonly_txn.emplace(db.rtxn());
		} else {
			if(has_been_reset) {
				assert(rtxn_open_count == 0);
				readonly_txn->renew();
				has_been_reset = false;
			}
		}
		rtxn_open_count++;
		return *readonly_txn;
	}

	inline void commit_rtxn() {
		assert(rtxn_open_count>0);
		if(--rtxn_open_count >0)
			return;
		if(!readonly_txn)
			return;
		assert(!has_been_reset);
		readonly_txn->reset(); //calls mdb_txn_reset
		has_been_reset = true;
	}

	inline void abort_rtxn() {
		assert(rtxn_open_count>0);
		if(--rtxn_open_count >0)
			return;
		if(!readonly_txn)
			return;
		assert(!has_been_reset);
		readonly_txn->reset(); //calls mdb_txn_reset
		has_been_reset = true;
	}

	txnmgr_t(db_t& db)
		: db(db)
		, reservation(wtxn_reservation_t<db_t>::get_instance(db))
		{}

	~txnmgr_t() {
		if(owned_wtxn)
			release_wtxn();
	}
};


template<typename db_t>
txn_proxy_t<db_t>::~txn_proxy_t() {
	if(!readonly)
		txnmgr->release_wtxn();
}


template<typename db_t>
txn_proxy_t<db_t>::operator db_txn& () {
	return unlikely(readonly) ? txnmgr->begin_rtxn() : txnmgr->begin_wtxn();
}

template<typename db_t>
void txn_proxy_t<db_t>::commit() {
	return unlikely(readonly) ? txnmgr->commit_rtxn() : txnmgr->commit_child_wtxn();
}

template<typename db_t>
void txn_proxy_t<db_t>::abort() {
	return unlikely(readonly) ? 	txnmgr->abort_rtxn() :txnmgr->abort_child_wtxn();
}
