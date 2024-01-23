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

#define LMDBXX_TXN_ID
#include "lmdb++.h"
#include "stackstring.h"
#include "serialize.h"
#include "deserialize.h"
#include "util/logger.h"
#include "util/util.h"

#include<optional>

/*
	class to easily start a read transaction and perform a search, returning both a transaction and a cursor
	This approach is more convenient than return a tuple.
	ret=db_rcursor(env)
	ret.get() : call any cursor function
	ret.cursor::get() : it is still possible to call functions in a specific namespace

 */


enum find_type_t {
	find_eq,
	find_geq,
	find_leq
};


/*
	Class to define a database range. It is initialized with a cursor pointing
	to a "current" record, and a serialized "prefix" key (primary or secondary). The next() method will
	continue to return records as long as the first part of the current key complies with the prefix.
	With n the length of prefix, the test performed is truncate(key,n) <= prefix

 */
template <typename data_t, template<typename T> class cursor_t>
class PrimitiveCursorRange
{
	bool done_ = false;
	cursor_t<data_t>& cursor;
	MDB_cursor_op op_next = MDB_NEXT;

	void check_done() {
		if(done_)
			return;
		done_ = ! cursor.is_valid();
		if(done_)
			return;
	}

public:
	PrimitiveCursorRange(cursor_t<data_t>& c, 	MDB_cursor_op op_next_ = MDB_NEXT)
		: cursor{c}
		, op_next(op_next_) {
		done_ = (c.handle() == nullptr || !c.is_valid());
	}

	inline auto current() {
		data_t ret;
		auto rc = cursor.get_value(ret);
#pragma unused (rc)
		assert(rc); //caller should always test for valid cursor before calling
		return ret;
	}

	inline auto current_key() {
		data_t ret;
		auto rc = cursor.get_key(ret);
		if(!rc) {
			dterrorf("Invalid access");
			assert(0);
		}
		return ret;
	}

	inline auto current_serialized_primary_key() {
		/*be careful: meaning can be different for regular cursor (primary key) and index cursor
			(secondary key)
		 */
		data_t ret;
		auto rc = cursor.current_serialized_primary_key(ret);
		if(!rc) {
			dterrorf("Invalid access");
			assert(0);
		}
		return ret;
	}

	inline auto done() const {
		return done_;
	}
	inline void next() {
#if 0
		done_ = !cursor.is_valid() || !cursor.next(op_next);
#else
		if(!done_)
			done_ = !cursor.next(op_next);
#endif
		check_done();
	}
};


// adapt any primitive range with current/done/next to Iterator/Sentinel pair with begin/end
template <class Derived>
struct RangeAdaptor : private Derived
{
    using Derived::Derived;

    struct Sentinel {};

    struct Iterator
    {
        Derived*  rng;

        friend auto operator==(Iterator it, Sentinel) { return it.rng->done(); }
        friend auto operator==(Sentinel, Iterator it) { return it.rng->done(); }

        friend auto operator!=(Iterator lhs, Sentinel rhs) { return !(lhs == rhs); }
        friend auto operator!=(Sentinel lhs, Iterator rhs) { return !(lhs == rhs); }

        inline auto operator*()  {
					return rng->current();
				}

       inline  auto current_key()  {
					return rng->current_key();
				}

			inline auto current_serialized_key()  {
				/*be careful: meaning can be different for regular cursor (primary key) and index cursor
					(secondary key)
				*/
				return rng->current_serialized_primary_key();
			}


			inline auto& operator++() { rng->next(); return *this;
			}
    };

    inline auto begin() { return Iterator{this}; }
    inline auto end()   { return Sentinel{};     }
};

class neumodb_t;

template <typename record_t> inline ss::bytebuffer_ lowest_primary_key() {
		static_assert("NOT IMPLEMENTED");
		return ss::bytebuffer_();
}


struct db_cursor;
class neumodb_t;


struct db_txn : public lmdb::txn {
	enum update_type_t {
		added,
		updated,
		deleted
	};
	neumodb_t* pdb{nullptr};
	bool readonly{false};
	int num_cursors = 0;
	bool use_log{false};
	::lmdb::dbi dbi_log;
	const char* lmdb_file{nullptr};
	int lmdb_line{-1};

	db_txn(db_txn&& other)
		: lmdb::txn(std::move(other))
		, pdb(other.pdb)
		, readonly(other.readonly)
		, num_cursors(other.num_cursors)
		, use_log(other.use_log)
		, dbi_log(other.dbi_log.handle())
		, lmdb_file(other.lmdb_file)
		, lmdb_line(other.lmdb_line)
		{
			other._handle = nullptr;
		}

	db_txn() = default;

	db_txn& operator=(db_txn&& other)
		{
			*(lmdb::txn*)this =std::move((lmdb::txn&) other);
			pdb = other.pdb;
			other.pdb = nullptr;
			other._handle = nullptr;
			readonly = other.readonly;
			num_cursors = other.num_cursors;
			other.num_cursors = 0;
			use_log = other.use_log;
			dbi_log = std::move(other.dbi_log);
			lmdb_file = other.lmdb_file;
			lmdb_line = other.lmdb_line;

			return *this;
		}



	db_txn(neumodb_t& db_, bool readonly, unsigned int flags);

	//db_txn() : db_txn(*(neumodb_t*) nullptr, 0) {}

	db_txn(db_txn& parent, neumodb_t& parent_db, bool readonly, unsigned int flags);
	db_txn child_txn();
	db_txn child_txn(neumodb_t& db);

	int txn_id() {
		return lmdb::txn_id(this->handle());
	}

	void clean_log(int num_keep);

	bool can_commit() const {
		return this->_handle;
	}
	void commit() {
		assert(this->_handle);
		if(readonly)
			this->lmdb::txn::reset();
		else {
			this->lmdb::txn::commit();
			this->_handle = nullptr;
		}
	}

	void abort() noexcept {
		assert(this->_handle);
		if(readonly)
			this->lmdb::txn::reset();
		else {
			this->lmdb::txn::abort();
			this->_handle = nullptr;
		}
	}

	void renew() {
		assert(this->_handle);
		assert(readonly);
		this->lmdb::txn::renew();
	}

	~db_txn() {
		assert(!this->_handle||readonly);
		if (num_cursors!=0)
			dterrorf("Implementation error: Transaction ends while cursors are still active: {}", num_cursors);
		assert(num_cursors==0);
		if(!readonly)
			this->_handle = nullptr;
	}
};


/*generic cursor for iterating over a part of the data base;
	agnostic about data type
 */
struct db_cursor : private lmdb::cursor {
	struct clone_t {
	};

	db_txn& txn;
	bool is_index_cursor = false;
	bool valid_ = false; //true if a cursor was positioned to some entry
	bool dead_ = false; //true if a cursor has been destroyed explicitly
	ss::bytebuffer<16> key_prefix; //cursor is valid if it points to a record with a matching key
#if 0
	inline void log(ss::bytebuffer_& primary_key, db_txn::update_type_t update_type) {
		txn.log(primary_key, update_type);
	}
#endif
  bool get(MDB_val* const key,
           MDB_val* const val,
           const MDB_cursor_op op) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		valid_ = lmdb::cursor::get(key, val, op);
		return valid_;
	}

	bool find(const ss::bytebuffer_& serialized_key,
						lmdb::val & v_ret,
            const MDB_cursor_op op = MDB_SET) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		valid_ =  lmdb::cursor::find(serialized_key, v_ret, op);
		return valid_;
	}


	bool find_both(const ss::bytebuffer_& serialized_secondary_key,
								 const ss::bytebuffer_& serialized_primary_key,
            const MDB_cursor_op op = MDB_SET) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		valid_ =  lmdb::cursor::find_both(serialized_secondary_key, serialized_primary_key, op);
		return valid_;
	}


	void drop(const bool del=false) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		::mdb_drop(txn.handle(), dbi(), del ? 1 : 0);
	}


	bool find(const ss::bytebuffer_& serialized_key,
            const MDB_cursor_op op = MDB_SET) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		valid_ =  lmdb::cursor::find(serialized_key, op);
		return valid_;
	}

	bool find(const ss::string_& serialized_key,
            const MDB_cursor_op op = MDB_SET) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		valid_ =  lmdb::cursor::find(serialized_key, op);
		return valid_;
	}

	void close() noexcept {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
			lmdb::cursor::close();
	}

	MDB_cursor* handle() const noexcept {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return lmdb::cursor::handle();
	}

	inline void set_key_prefix( const ss::bytebuffer_& key_prefix_) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		bool  was_valid = is_valid();
		auto old_size = key_prefix.size();
		key_prefix = key_prefix_;
		if(was_valid && old_size>0 && !is_valid()) {
			dterrorf("Implementation error: changed key_prefix invalidates current record");
			assert(0);
		}
	}
	db_cursor(db_txn& txn_, lmdb::dbi&dbi, uint32_t key_type_, bool is_index_cursor_=false) :
		lmdb::cursor(lmdb::cursor::open(txn_, dbi))
		, txn(txn_)
		, is_index_cursor(is_index_cursor_)
		{
			txn.num_cursors++;
			if(key_type_  != (uint32_t) -1)
				key_prefix.copy_raw((uint8_t*)&key_type_, sizeof(key_type_));
			else {
				assert(key_prefix.size()==0);
				assert(0); //test to see if the code is used like this
			}
		assert(txn.handle() != nullptr);
		}

	db_cursor(db_txn& txn_, lmdb::dbi&dbi, const ss::bytebuffer_& key_prefix_,
						bool is_index_cursor_=false) :
		lmdb::cursor(lmdb::cursor::open(txn_, dbi))
		, txn(txn_)
		, is_index_cursor(is_index_cursor_)
		, key_prefix(key_prefix_) {
			assert(txn.handle() != nullptr);
			txn.num_cursors++;
		}

	db_cursor(db_txn& txn_, lmdb::dbi&dbi, bool is_index_cursor_=false) :
		lmdb::cursor(lmdb::cursor::open(txn_, dbi))
		, txn(txn_)
		, is_index_cursor(is_index_cursor_)
		{
			assert(txn.handle() != nullptr);
			assert(key_prefix.size()==0);
			txn.num_cursors++;
		}

	db_cursor(db_cursor&& other) :
		lmdb::cursor(std::move((lmdb::cursor&)other))
		, txn(other.txn)
		, is_index_cursor(other.is_index_cursor)
		, valid_(other.valid_)
		, key_prefix(other.key_prefix) {
			assert(txn.handle() != nullptr);
			txn.num_cursors++;
		}

	db_cursor(const db_cursor& other, clone_t*)
		: lmdb::cursor(other.handle()
								 ? lmdb::cursor::open(other.txn, other.dbi())
								 : nullptr)
		, txn(other.txn)
		, is_index_cursor(other.is_index_cursor)
		, valid_(other.valid_&& other.handle())
		, key_prefix (other.key_prefix)
		{
			assert(txn.handle() != nullptr);
			lmdb::val k{}, v{};
			txn.num_cursors++;
			if(!valid_)
				return;
			bool found = ((lmdb::cursor&)other).get(k, v, (MDB_cursor_op) MDB_GET_CURRENT);
			if(found) {
				found = get(k,v, is_index_cursor ? MDB_GET_BOTH : MDB_SET);
				//if(found)
				//	printf("duplicated cursor\n");
			}
		}

	inline void destroy() {
#ifdef ASSERT_DEAD
		assert(!dead_); //may only be destroyed once
#endif
		if(!txn.is_valid()) {
			_handle = nullptr;
		}
		if (txn.is_valid() && !!handle()) {
			close(); /* !txn.is_valid() means that transaction was aborted/committed in which case low level
									lmdb cursors have been freed*/
		}
		assert (txn.num_cursors >= 1);
		txn.num_cursors--;
		_handle = nullptr;
		dead_ = true;
	}

	~db_cursor() {
		if(!dead_)
			destroy();
	}

	db_cursor clone() const {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return db_cursor(*this, (db_cursor::clone_t*) NULL);
	}

	db_cursor& operator=(const db_cursor& other) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		if(this != &other)
			*this = other.clone();
		return *this;
		}

	db_cursor& operator=(db_cursor&& other) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		if(this != &other) {
			lmdb::cursor::operator=(std::move((lmdb::cursor&)other));
			assert( &txn == &other.txn); //cursors can be moved only if they relate to the same transaction
			is_index_cursor = other.is_index_cursor;
			valid_ = other.valid_;
			other.valid_ = false;
			key_prefix = other.key_prefix;
			other.key_prefix.clear();
		}
			return *this;
	}

	template<class key_t>
	bool get_key(key_t& key_out, const MDB_cursor_op op=MDB_GET_CURRENT) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{}, v{};
		const bool found = get(k, v, op);
		if(!found)
			return found;
		auto serialized = ss::bytebuffer_::view((uint8_t*)k.data(), k.size(), k.size());
		if(deserialize(serialized, key_out)<0)
			return false;
		return found;
	}

	bool get_serialized_key(ss::bytebuffer_& key_out, const MDB_cursor_op op=MDB_GET_CURRENT) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{}, v{};
		const bool found = get(k, v, op);
		if(!found)
			return found;
		key_out = ss::bytebuffer_::view((uint8_t*)k.data(), k.size(), k.size());
		return found;
	}

	bool get_serialized_value(ss::bytebuffer_& val_out, const MDB_cursor_op op=MDB_GET_CURRENT) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{}, v{};
		const bool found = get(k, v, op);
		if(!found)
			return found;
		val_out = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
		return found;
	}

	template<typename data_t>
	inline bool get_value(data_t& out, const MDB_cursor_op op=MDB_GET_CURRENT);



	bool is_valid() {
		if(!valid_ || dead_ || handle()==nullptr)
			return false;
		lmdb::val k{}, v{};
		bool ret = get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
		if(!ret) //we reached the end of the index table
			return ret;
		if(this->key_prefix.size() == 0)
			return true;
		if((int)k.size() < (int)key_prefix.size())
			return false;
		if(memcmp((void*) key_prefix.buffer(), (void*)k.data(), key_prefix.size())) {
			//we have reached another key type, but iteration must stop when the last key of the current type was found
			return false;
		}
		return true;
	}

	bool has_key_prefix(const ss::bytebuffer_& key) {
		if(!is_valid())
			return false;
		lmdb::val k{}, v{};
		bool ret = get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
		if(!ret) //we reached the end of the index table
			return ret;
		if((int) k.size() >= key.size() &&
			 0 ==memcmp(k.data(), key.buffer(), key.size())) {
			//the current record has the same primary key as the desired key
			return true;
		}
		return false;
	}


	bool has_key(const ss::bytebuffer_& key) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		if(handle()==nullptr)
			return false;
		lmdb::val k{}, v{};
		bool ret = get(k, v, (const MDB_cursor_op) MDB_GET_CURRENT);
		if(!ret) //we reached the end of the index table
			return ret;
		if((int) k.size() != key.size())
			return false;
		return this->has_key_prefix(key);
	}



	inline bool next(MDB_cursor_op op=MDB_NEXT) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{}, v{};
		bool ret = get(k, v, op);
		if(!ret) //we reached the end of the index table
			return ret;
		if(this->key_prefix.size() == 0)
			return true;
		if((int)k.size()< (int)key_prefix.size())
			return false;
		if(memcmp((void*) key_prefix.buffer(), (void*)k.data(), std::min((int)k.size(), key_prefix.size()))) {
			//we have reached another key type, but iteration must stop when the last key of the current type was found
			return false;
		}
		return true;
	}

	inline bool prev() {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{}, v{};
		bool ret = this->get(k, v, (const MDB_cursor_op) MDB_PREV);
		if(!ret) //we reached the start of the index table
			return ret;
		if(this->key_prefix.size() == 0)
			return true;
		if((int)k.size() < (int)key_prefix.size())
			return false;
		if(memcmp((void*) key_prefix.buffer(), (void*)k.data(), key_prefix.size())) {
			//we have reached another key type, but iteration must stop when the last key of the current type was found
			return false;
		}
		return true;
	}


	/*!
		move the cursor backward by offset,
		and position the cursor at this position. If the position cannot be reached,
		leave the cursor at the closest possible position.
		Returns the actual offset
	 */
	inline int move_backward(int offset) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
			int i=0;
			bool ret=true;
			for(i=0; i <offset &&ret; ++i)
				ret = this->prev();
			if(!ret)
				ret = this->next();
			if(!ret)
				close();
			return i;
		}


	/*!
		move the cursor forward by offset,
		and position the cursor at this position. If the position cannot be reached,
		leave the cursor at the closest possible position.
		Returns the actual offset
	 */
	inline int move_forward(int offset) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
			int i=0;
			bool ret=true;
			for(i=0; i <offset &&ret; ++i)
				ret = this->next();
			if(!ret)
				ret=this->prev();
			if(!ret)
				close();
			return i;
		}


	//delete record at an arbitrary key position
	//returns false if key was not found
	auto del(lmdb::val& key, lmdb::val& val) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return lmdb::dbi(this->dbi()).del(this->txn, key, val);
	}

	//delete record at an arbitrary key position
	//returns false if key was not found
	auto del(lmdb::val& key) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return lmdb::dbi(this->dbi()).del(this->txn, key);
	}


	//put a result at an arbitrary key position
	//returns false if key already existed
	auto put(const lmdb::val& key, lmdb::val& val, unsigned int flags=0) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return lmdb::dbi(this->dbi()).put(this->txn, key, val, flags);
	}

	//put a result at the current cursor position
	void cursor_put(const lmdb::val& key, lmdb::val& val, unsigned int flags=0) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::cursor_put(this->cursor::handle(), (MDB_val*) &key, (MDB_val*) &val, MDB_RESERVE|MDB_CURRENT);
	}

	//put a key and reserve space for the result
	auto reserve(const lmdb::val& key, lmdb::val& val, unsigned int flags=0) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		return lmdb::dbi(this->dbi()).put(this->txn, key, val, flags);
	}

	void cursor_del(unsigned int flags=0) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val key{nullptr,0};
		lmdb::cursor_del(this->cursor::handle(), flags);
		valid_ = false;
	}

	//delete at a specific key
	//returns false if key was not found
	bool del_k(const ss::bytebuffer_& serialized_key) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{serialized_key.buffer(), (size_t)serialized_key.size()};

		return this->del(k);
	}

	//delete at a specific key and value; used for MDB_DUP index
	//returns false if key was not found
	bool del_kv(const ss::bytebuffer_& serialized_key, const ss::bytebuffer_& serialized_val) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{serialized_key.buffer(), (size_t) serialized_key.size()};
		lmdb::val v{serialized_val.buffer(), (size_t) serialized_val.size()};

		return this->del(k, v);
	}


	//save data at a specific key, ignores any cursor which would be present
	//returns false if key already existed
	bool put_kv(const ss::bytebuffer_& serialized_key, const ss::bytebuffer_& serialized_val,
							unsigned int put_flags=0) {
#ifdef ASSERT_DEAD
		assert(!dead_);
#endif
		lmdb::val k{serialized_key.buffer(), (size_t)serialized_key.size()};
		lmdb::val v{serialized_val.buffer(), (size_t)serialized_val.size()};
		//reserve enough space

		/*
			This will also not work for a secondary key, because this needlessly encodes size twice.
																			So we need a second version that does NOT serialize in case it is passed
																			a bytebuffer
		*/
		return this->put(k, v, (this->is_index_cursor? MDB_NODUPDATA : 0)|put_flags);
	}
};




/*
	typed cursor for iterating over range of records of a specific data type

	Both db_tcursor_index and  db_tcursor inherit from 	db_tcursor_
	but db_tcursor_index  must never be downconverted
	to its base class, because that  introduces false behaviour (using wrong dbi);
	For this reason, db_tcursor must be different from db_tcursor_ even though it has not
	a single different method


 */
template<typename data_t>
struct db_tcursor_ : public db_cursor {


	/*
		A cursor only iterates over records of the same type, as identified by the key_type prefix
		However, if no iteration is performed, key_type can be left unspecified.
	 */
	db_tcursor_(db_txn& txn, lmdb::dbi&dbi, const ss::bytebuffer_& key_prefix, bool is_index_cursor=false) :
		db_cursor(txn, dbi, key_prefix, is_index_cursor)
		{}

	db_tcursor_(db_txn& txn, lmdb::dbi&dbi, bool is_index_cursor=false) :
		db_cursor(txn, dbi, is_index_cursor)
		{}

	db_tcursor_(const db_tcursor_& other, clone_t*x) :
		db_cursor(other, x)
		{}

	db_tcursor_(const db_tcursor_& other) :
		db_tcursor_(other, (clone_t*)nullptr)
		{}

	db_tcursor_ clone() const {
		return db_tcursor_(*this, (db_cursor::clone_t*) NULL);
	}

	db_tcursor_& operator=(db_tcursor_& other) {
		if( this != &other )
			db_cursor::operator=((db_cursor&) other);
		return *this;
	}

	db_tcursor_& operator=(db_tcursor_&& other) {
		if( this != &other )
			db_cursor::operator=(std::move((db_cursor&) other));
		return *this;
	}

	inline data_t current() {
		if(!is_valid()) {
			dterrorf("Invalid access");
			assert(0);
		}
		data_t out;
		get_value(out);
		return out;
	}

protected:
	auto current_key() {
		data_t ret;
		auto rc = get_key(ret);
		assert(rc);
		return ret;
	}

	auto current_serialized_key() {
		ss::bytebuffer<16> ret;
		auto rc = get_serialized_key(ret);
#pragma unused (rc)
		assert(rc);
		return ret;
	}
public:
	using db_cursor::put_kv;

	//save data at a specific key, ignores any cursor which would be present
	//returns false if key already existed
	bool put_kv(const ss::bytebuffer_& serialized_key, const data_t& val, unsigned int put_flags=0) {
		assert(!this->is_index_cursor);
		auto val_size = serialized_size(val);
		lmdb::val k{serialized_key.buffer(), (size_t)serialized_key.size()};
		lmdb::val v{nullptr, (size_t)val_size};
		//reserve enough space

		/*
			This will also not work for a secondary key, because this needlessly encodes size twice.
																			So we need a second version that does NOT serialize in case it is passed
																			a bytebuffer
		*/
		bool ret=this->put(k, v, MDB_RESERVE | put_flags);
		auto  serialized_val = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), 0);
		serialize(serialized_val, val);
		return ret;
	}

	//update data at the current cursor
	bool put_kv_at_cursor(const data_t& val) {
		auto serialized_key = current_serialized_key();
		auto val_size = serialized_size(val);
		lmdb::val k{serialized_key.buffer(), (size_t)serialized_key.size()};
		lmdb::val v{nullptr, (size_t)val_size};
		//reserve enough space

		/*
			This will also not work for a secondary key, because this needlessly encodes size twice.
																			So we need a second version that does NOT serialize in case it is passed
																			a bytebuffer
		*/
		this->cursor_put(k, v, MDB_RESERVE|MDB_CURRENT);
		auto  serialized_val = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), 0);
		serialize(serialized_val, val);
		return true;
	}


	auto range() {
		return RangeAdaptor<PrimitiveCursorRange<data_t, db_tcursor_>>(*this, MDB_NEXT);
	}

	auto range(const ss::bytebuffer_& upper_bound) {
		assert(upper_bound.size() == this->key_prefix.size());
		assert(memcmp(upper_bound.buffer(), this->key_prefix.buffer(), upper_bound.size())==0);
		return RangeAdaptor<PrimitiveCursorRange<data_t, db_tcursor_>>(*this, upper_bound, MDB_NEXT);
	}

};


/*
	Both db_tcursor_index and  db_tcursor inherit from 	db_tcursor_
	but db_tcursor_index  must never be downconverted
	to its base class, because that  introduces false behaviour (using wrong dbi);
	For this reason, db_tcursor must be different from db_tcursor_ even though it has not
	a single different method
*/
template<typename data_t>
struct db_tcursor : public db_tcursor_<data_t> {

	typedef db_tcursor_<data_t> base_t;
	/*
		inherit constructors
	*/
	using base_t::base_t;

	inline 	auto current_serialized_key() {
		return this->db_tcursor_<data_t>::current_serialized_key();
	}

	auto current_serialized_primary_key() {
		return this->db_tcursor_<data_t>::current_serialized_key();
	}

	db_tcursor clone() const {
		return db_tcursor(*this, (db_cursor::clone_t*) NULL);
	}

};


/*
	Both db_tcursor_index and  db_tcursor inherit from 	db_tcursor_
	but db_tcursor_index  must never be downconverted
	to its base class, because that  introduces false behaviour (using wrong dbi);
	For this reason, db_tcursor must be different from db_tcursor_ even though it has not
	a single different method
*/
template<typename data_t>
struct db_tcursor_index : public db_tcursor_<data_t> {

	db_tcursor<data_t> maincursor;

		inline void set_key_prefix( const ss::bytebuffer_& key_prefix_) {
			db_tcursor_<data_t>::set_key_prefix(key_prefix_);
			auto main_key_prefix = data_t::make_key(data_t::keys_t::key, data_t::partial_keys_t::none,  nullptr);
			maincursor.set_key_prefix(main_key_prefix);
		}

	auto current_serialized_secondary_key() {
		return this->db_tcursor_<data_t>::current_serialized_key();
	}

	auto current_serialized_primary_key() {
		lmdb::val k{}, v{};
		bool found = this->get(k, v, MDB_GET_CURRENT);
#pragma unused (found)
		assert(found);
		auto primary_key = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
		return ss::bytebuffer<16>(v.data(), v.size());
	}

	auto current_serialized_key() {
		return current_serialized_secondary_key();
	}



	db_tcursor_index(db_txn& txn_, lmdb::dbi&dbi, lmdb::dbi&dbi_index, const ss::bytebuffer_& key_prefix)
		: db_tcursor_<data_t>(txn_, dbi_index, key_prefix, true)
		, maincursor(txn_, dbi)
		{
			assert(txn_.handle() != nullptr);
		}


	db_tcursor_index(db_txn& txn_, lmdb::dbi&dbi, lmdb::dbi&dbi_index) :
		db_tcursor_<data_t>(txn_, dbi_index, ss::bytebuffer_(), true)
		, maincursor(txn_, dbi) {
		assert(txn_.handle() != nullptr);
	}

	db_tcursor_index(const db_tcursor_index& other, db_cursor::clone_t* x) :
		db_tcursor_<data_t>(other, x)
		, maincursor(other.maincursor, x)
		{
		}

	void close() noexcept {
		maincursor.close();
		db_tcursor_<data_t> ::close();
	}

	db_tcursor_index clone() const {
		return db_tcursor_index(*this, (db_cursor::clone_t*) NULL);
	}

	inline bool get_value(data_t& out, const MDB_cursor_op op=MDB_GET_CURRENT) {
		assert (maincursor.is_valid());
		if(!this->handle())
			return false;
		auto found2 = maincursor.get_value(out, (const MDB_cursor_op) MDB_GET_CURRENT);
		return found2;
	}

	using db_tcursor_<data_t>::is_valid;

	inline data_t current() {
		if(!is_valid()) {
			dterrorf("Invalid access");
			assert(0);
		}
		data_t out;
		get_value(out);
		return out;
	}


/*

*/
	inline bool next(MDB_cursor_op op = MDB_NEXT) {
		lmdb::val k{}, v{};
		bool ret = this->get(k, v, op);
		if(!ret) //we reached the end of the index table
			return ret;

		if((int) k.size() < (int) this->key_prefix.size())
			return false;

		if(memcmp((void*) this->key_prefix.buffer(), (void*)k.data(),
							this->key_prefix.size())) {
			//we have reached another key type, but iteration must stop when the last key of the current type was found
			return false;
		}
		auto primary_key = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
		/*
			The following call sets the valid_ flag on maincursor
			Normally maincursor.valid_ should always be true after this call,  except
			when reading from the log table, where maincursor.valid_==false indicates
			the record was deleted
		*/
		bool found = maincursor.find(primary_key, v);
#pragma unused (found)

		return true;
	}

	inline bool prev() {
		lmdb::val k{}, v{};
		bool ret = this->get(k, v, (const MDB_cursor_op) MDB_PREV);
		if(!ret) //we reached the start of the index table
			return ret;

		assert((int)k.size() >= this->key_prefix.size());
		if((int)k.size() < (int)this->key_prefix.size())
			return false;

		if(memcmp((void*) this->key_prefix.buffer(), (void*)k.data(),
							std::min((int)k.size(), this->key_prefix.size()))) {
			//we have reached another key type, but iteration must stop when the last key of the current type was found
			return false;
		}
		auto primary_key = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
		bool found = maincursor.find(primary_key, v);
		return found;
	}



	auto range(MDB_cursor_op op = MDB_NEXT) {
		return RangeAdaptor<PrimitiveCursorRange<data_t, db_tcursor_index>>(*this, op);
	}

	auto range(const ss::bytebuffer_& upper_bound) {
		assert(upper_bound.size() == this->key_prefix.size());
		assert(memcmp(upper_bound.buffer(), this->key_prefix.buffer(), upper_bound.size())==0);
		return RangeAdaptor<PrimitiveCursorRange<data_t, db_tcursor_index>>(*this, upper_bound);
	}
	void put_kv_at_cursor(const data_t& val) =delete; //not possible for index cursor
};

#include "neumodb.h"




inline	db_txn::db_txn(neumodb_t& db_, bool readonly, unsigned int flags) :
	lmdb::txn(lmdb::txn::begin(*db_.envp, nullptr /*parent*/, flags))
	, pdb(&db_)
	, readonly(readonly)
	, use_log (pdb->use_log)
	, dbi_log(pdb->dbi_log.handle())
	, lmdb_file(::lmdb_file)
	, lmdb_line(::lmdb_line)
{
}

inline	db_txn::db_txn(db_txn& parent_txn, neumodb_t& db_, bool readonly, unsigned int flags) :
	lmdb::txn(lmdb::txn::begin(*db_.envp, parent_txn._handle /*parent*/, flags))
	, pdb(&db_)
	, readonly(readonly)
	, use_log(db_.use_log)
	, dbi_log(db_.dbi_log.handle())
	, lmdb_file(::lmdb_file)
	, lmdb_line(::lmdb_line)
{
	assert(!parent_txn.readonly);
}

inline	db_txn db_txn::child_txn(neumodb_t& db) {
	assert(!readonly);
	return db_txn(*this, db, false /*readonly*/, 0);
}

inline	db_txn db_txn::child_txn() {
	assert(!readonly);
	return db_txn(*this, *this->pdb, false/*readonly*/, 0);
}

template<typename data_t>
inline bool db_cursor::get_value(data_t& out, const MDB_cursor_op op) {
	lmdb::val k{}, v{};
	if(!valid_ || !handle())
		return false;
	const bool found = get(k, v, op);
	if(!found)
		return found;
	auto serialized = ss::bytebuffer_::view((uint8_t*)v.data(), v.size(), v.size());
	if(this->txn.pdb->schema_is_current) {
		//Note: this could be replaced with out = this->txn.db.dbdesc.get_value_safe(out, serialized)
		if(deserialize(serialized, out)<0)  {
			return false;
		}
	} else  {
		if(deserialize_safe(serialized, out, *this->txn.pdb->dbdesc)<0) {
			return false;
		}
	}
	return found;
}


template <typename record_t> inline bool put_record(db_txn& txn, const record_t& record,
																										unsigned int put_flags=0) {
	auto c = txn.pdb->template tcursor<record_t>(txn);
	return put_record(c, record, put_flags);
}

template <typename record_t> inline void delete_record(db_txn& txn, const record_t& record) {
	auto c = txn.pdb->template tcursor<record_t>(txn);
	delete_record(c, record);
}

/*TODO: make an iterator that  can safely iterate between two bounds. See egdb.cc save_epg_record_if_better
	although that function is special in the start condition.
	Example would be: iterate over all epg records on the same service between two start times.
	This would require a start and an end primary or secondary key value and a greater/greater than
	equal, and small/smaller or equal test

	Idea 1: concept of a "prefix" match. For instance, epg_key_t starts with service. A prefix is the
	part of the serialized key which only encodes the "service" part or more generally the first n
	parts of a key.

	The range iterator still uses a find_... function to position the cursor at the first matching
	primary/secondary key, but iteration is ended as soon as a key is encoded which is > than the prefix.
	This would be enough to find all epg data for a specific service.

	One tricky bit is that we must be able to generate the prefix. This can be automated if the prefix
	parts are encoded in separate structures, e.g. key ={key1, ...} where key1={key2, ...}.
	ALternatively, code could generate: size_service, size_service_event_id, .... numbers to help
	in the process. The prefix could then be derived from an actual key (same_prefix_as)

	The above is a little more compicated to find all epg records with start_time between start and end; however
	in this case as there is a unique record for a given start_time, a prefix much is the same as an == or <= match


	The main reason this can cause problems is that loops may exist of the types
	for i=istart1 .... iend1:
	...
	istart2=idend1; //we want record istart2 to only be processed in the next loop!
	for i=istart2 .... iend2:


*/

template <typename record_t, typename cursor_t> inline std::optional<record_t>
record_at_cursor(cursor_t& cursor) {
	if (cursor.is_valid())
		return cursor.current();
	return {};
}

#define lmdb_hint() \
	lmdb_file = __FILE__; lmdb_line=__LINE__
