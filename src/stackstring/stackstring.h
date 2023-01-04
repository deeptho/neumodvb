/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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

#include "../util/dtassert.h"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string.h>

#include "stackstring_header.h"

extern int gcd(int a, int b);

namespace ss {
	using offset_t = int64_t;

	template <typename data_t> int array_item_size() {
		auto s = sizeof(data_t[2]) - sizeof(data_t[1]);
		return s;
	}

/*forward declarations*/
	template <int buffer_size, typename data_t> class databuffer;
	class string_;

	struct fmt_t;

	struct accu_t;

	struct dateTime {
		time_t t;
		const char* format;
		explicit dateTime(time_t _t, const char* _tfmt = "%H:%M")
			: t(_t)
			, format(_tfmt) {
		}
		explicit operator ::time_t() const {
			return t;
		}
	};

	template <typename T, typename parent, int buffer_size> void clear_something(T& s, bool release) {
		// assert (buffer_size>0);
		static_cast<parent&>(s).clear(release);
		if (release) {
			s.set_inline_buffer();
		}
	}

	template <typename T, bool zero_terminate> void shrink_to_fit_something(T& s) {
		if (s.is_view())
			return;
		if (s.capacity() == zero_terminate + s.size() || !s.is_allocated())
			return;

		auto newcap = zero_terminate + s.size();
		assert(newcap <= s.capacity());

		if (newcap >= s.capacity())
			return;

		// we have a non-inline and allocated string

		assert(s.is_allocated());

		auto old_length = s.size();
		auto old = s.steal_allocated_buffer();
		s.clear(true);
		s.reserve(old_length);
		memcpy(s.header.allocated_buffer(), old, (old_length + zero_terminate) * s.item_size);
		s.set_size(old_length);
		assert(!s.is_view());
		operator delete(old);
	}

/*
	Class for storing a buffer of bytes.
*/
	template <typename data_t> class databuffer_ {
	public:
		header_t<data_t> header;

	protected:
		void set_inline_buffer() {
			assert(header.h.inited);
			header.set_inline_buffer();
		}

		void set_external_buffer(typename header_t<data_t>::template data_with_capacity<1>* ext_buffer) {
			assert(header.h.inited);
			header.set_external_buffer(ext_buffer);
		}

		void copy(const databuffer_<data_t>& x);

		void move(databuffer_<data_t>& x);
		template <int buffer_size> friend class string;

		void set_size(int size) {
			assert(header.h.inited);
			header.set_size(size);
		}

	public:
		void copy_raw(const data_t* v, int v_len);

		constexpr static int item_size = sizeof(data_t[2]) - sizeof(data_t[1]);

		bool is_allocated() const {
			return header.is_allocated();
		}

		bool is_view() const {
			return header.is_view();
		}

		inline int size() const {
			return header.size();
		}

		inline int capacity() const {
			return header.capacity();
		}

		inline const data_t* buffer() const {
			return header.buffer();
		}

		inline data_t* buffer() {
			return header.buffer();
		}

		typename header_t<data_t>::template data_with_capacity<1>* steal_allocated_buffer() {
			return header.steal_allocated_buffer();
		}

		template <typename T> offset_t write(offset_t const pos, T const& val) {
			assert(header.h.inited);
			reserve(size() + sizeof(val));
			buffer()[size()] = val;
			set_size(size() + sizeof(val));
			return size();
		}

		/*change the capacity
			if shrink_if_possible, then capacity will be reduced to size
			otherwise, capacity will be max(old capacity, size)
		*/
		void reserve(int size);

		/*
			num_elements= number of inline elements after header
		*/
		void clear_helper(bool release, bool destroy, int num_inline_elements) {
			assert(header.h.inited);
			assert(size() == 0 || buffer());
			if (release && is_allocated()) {
				auto* p = buffer();
				if (destroy) {
					for (int i = 0; i < size(); ++i)
						p[i].~data_t();
				}
				auto old = steal_allocated_buffer();
				operator delete(old);
			}
			if (release)
				set_inline_buffer();
			else
				set_size(0);
		}

		void clear(bool release = false) {
			clear_helper(release, true, 0);
		}

		void shrink_to_fit();
		void grow(int extra);

		void resize_no_init(int new_size) {
			reserve(new_size);
			set_size(new_size);
		}

		void resize(int new_size) {
			int old_size = size();
			if (old_size == (int)new_size)
				return;
			if ((int)new_size < old_size) {
				set_size(new_size);
				return;
			}
			reserve(new_size);
			for (int i = old_size; i < (int)new_size; ++i) {
				data_t x{};
				operator[](i) = x;
			}
		}

		static constexpr struct owning_t {
		} owning{};
		static constexpr struct non_owning_t {
		} non_owning{};

	protected:
		// for use by derived classes
		databuffer_(const header_t<data_t>& header)
			: header(header) {
		}

	public:
		databuffer_()
			: header(0, false) {
		}

		databuffer_(const databuffer_& x);

		databuffer_(databuffer_&& x);

		databuffer_(const data_t* v, // initial value for databuffer
								int v_len				 //(upper bound for ) length of string at v;
								// if v_len==-1, then use strlen(v) instead
			);

		databuffer_(std::initializer_list<data_t> v)
			: header(0, false) {

			assert(header.h.inited);
			for (const auto& x : v)
				this->push_back(x);
		}

		static databuffer_ view(data_t* v, int v_cap, int v_len) {
			databuffer_ ret(header_t<data_t>(v_cap, true));
			ret.header.set_view_buffer(v, v_len);
			return ret;
		}

		inline databuffer_& operator=(const databuffer_& x) {
			assert(header.h.inited);
			copy(x);
			return *this;
		}

		inline databuffer_& operator=(databuffer_&& other) {
			assert(header.h.inited);
			if (other.is_view())
				copy(other);
			else
				move(other);
			return *this;
		}

		// bool operator==(const data_t* other);
		inline bool operator==(const databuffer_& other) const {
			if (other.size() != size())
				return false;
			if (std::is_trivial<data_t>::value) {
				return memcmp(buffer(), other.buffer(), size()) == 0;
			} else {
				for (int i = 0; i < other.size(); ++i) {
					if (other[i] != operator[](i))
						return false;
				}
			}
			return true;
		}

		bool operator!=(const databuffer_& other) const {
			return !operator==(other);
		}

		~databuffer_();

		data_t& operator[](int pos);
		const data_t& operator[](int pos) const;

		void truncate(int _n);
		void erase(int n);

		void push_back(data_t&& val) {
			assert(header.h.inited);
			reserve(size() + 1);
			operator[](size()) = val;
		}
		void push_back(const data_t& val) {
			assert(header.h.inited);
			reserve(size() + 1);
			operator[](size()) = val;
		}
		///@todo the following does not make much sense for data_t!=char
		template <typename entry_t> void append_raw(const entry_t* data, int num_data) {
			assert(header.h.inited);
			int data_len = (array_item_size<entry_t>() * num_data + item_size - 1) / item_size;
			int r = size() + data_len;
			if (r >= capacity())
				grow(r);
			auto* b = buffer();
			memcpy(b + size(), data, data_len * item_size);
			set_size(r);
		}

		///@todo the following does not make much sense for data_t!=char
		template <typename entry_t> static int append_raw_size(const entry_t* data, int num_data) {
			const int extra = 0; // no alignment, no padding => sinmpler
			int data_len = (array_item_size<entry_t>() * num_data + item_size - 1) / item_size;
			int r = data_len + extra;
			return r;
		}

		template <typename entry_t> void append_raw(const entry_t& val) {
			append_raw(&val, 1);
		}

		template <typename entry_t> static int append_raw_size(const entry_t& val) {
			return append_raw_size(&val, 1);
		}

		template <typename entry_t> void push_back(const entry_t& val) {
			operator[](size()) = val;
		}

		void check() const {
			assert(header.h.inited);
		}
		// code for iterations
		data_t const* begin() const {
			assert(header.h.inited);
			return buffer();
		}
		data_t const* end() const {
			assert(header.h.inited);
			return buffer() + size();
		}
		data_t* begin() {
			assert(header.h.inited);
			return buffer();
		}
		data_t* end() {
			assert(header.h.inited);
			return buffer() + size();
		}
#if 0
		//TODO
		std::reverse_iterator<data_t const*> rbegin() const {
			return std::reverse_iterator<data_t*>(el_ + size());
		}
		std::reverse_iterator<data_t const*> rend() const {
			return std::reverse_iterator<data_t*>(el_);
		}
		std::reverse_iterator<data_t*> rbegin() {
			return std::reverse_iterator<data_t*>(el_ + size());
		}
		std::reverse_iterator<data_t*> rend() { return std::reverse_iterator<data_t*>(el_); }
#endif
		friend data_t const* begin(databuffer_ const& a) {
			return a.begin();
		}
		friend data_t const* end(databuffer_ const& a) {
			return a.end();
		}

		friend data_t* begin(databuffer_& a) {
			return a.begin();
		}
		friend data_t* end(databuffer_& a) {
			return a.end();
		}

#if 0
		data_t const& back() const {
			return el_[used_size_ - 1]; }
		data_t& back() {
			return el_[used_size_ - 1]; }

		data_t& front() {
			return el_[0]; }
		data_t const& front() const {
			return el_[0]; }
#endif
	};

// return <0 if a<b, >0 if a>b and 0 otherwise
	template <typename data_t> inline int cmp(const databuffer_<data_t>& a, const databuffer_<data_t>& b) {
		auto n = std::min(a.size(), b.size());
		auto ret = memcmp(a.buffer(), b.buffer(), n);
		return (ret != 0) ? ret : a.size() < b.size();
	}

/*
	buffer clase which includes space for internally allocating buffer_size bytes
	zero_terminiate=true stored an additional zero at the end of the data
	This zero is NOT counted in size() but capacity is allocated for it.
	With zero_terminate==true, the class has string semantics
*/

	template <int num_elements, typename data_t> class databuffer : public databuffer_<data_t> {
		using parent = databuffer_<data_t>;
		constexpr static int buffer_size_ = header_t<data_t>::template extended_inline_to_reserve<num_elements>();
		char _extra[buffer_size_];

	public:
		using parent::operator=;

		using parent::buffer;
		using parent::capacity;
		using parent::copy_raw;
		using parent::length;

		using parent::clear_helper;
		using parent::is_allocated;
		using parent::item_size;
		using parent::set_external_buffer;
		using parent::set_inline_buffer;
		using parent::set_size;

		void clear(bool release) {
			clear_helper(release, true, num_elements);
		}

		databuffer()
			: parent(header_t<data_t>(num_elements)) {
		}

		databuffer(const databuffer_<data_t>& x)
			: databuffer() {
			this->copy(x);
		}

		databuffer(const databuffer& x)
			: databuffer() { // NEEDED?
			this->copy(x);
		}

		databuffer(databuffer_<data_t>&& x)
			: parent(header_t<data_t>(num_elements)) {
			this->move(x);
		}

		template <int othernum_elements>
		databuffer(const databuffer<othernum_elements, data_t>& x)
			: databuffer() {
			assert(this->header.inited);
			this->copy(x);
		}

		databuffer(const data_t* v, // initial value for databuffer
							 int v_len				//(upper bound for ) length of string at v;
							 // if v_len==-1, then use strlen(v) instead
			)
			: databuffer() {
			assert(this->header.h.inited);
			for (const auto& x : v)
				this->push_back(x);
		}

		databuffer(std::initializer_list<data_t> v)
			: databuffer() {
			assert(this->header.inited);
			for (const auto& x : v)
				this->push_back(x);
		}

		template <typename T, typename parent_, int num_elements_> friend void clear_something(T& s, bool release);
		template <typename T, bool zero_terminate_> friend void shrink_to_fit_something(T& s);

		void shrink_to_fit() {
			return shrink_to_fit_something<decltype(*this), false>(*this);
		}

#if 0
		databuffer& operator=(const databuffer_<data_t>&x) {
			assert(this->header.inited);
			this->copy(x);
			return *this;
		}



		databuffer& operator=(const databuffer&x) {
			assert(this->header.inited);
			this->copy(x);
			return *this;
		}
#endif
	};

	typedef databuffer_<uint8_t> bytebuffer_;

	template <int buffer_size> class bytebuffer : public bytebuffer_ {
		using parent = bytebuffer_;
		char buffer_[header_t<char>::extended_inline_to_reserve<buffer_size>()];

	public:
		using parent::buffer;
		using parent::capacity;
		using parent::copy_raw;
		using parent::size;
		using parent::is_allocated;
		using parent::item_size;
		using parent::set_external_buffer;
		using parent::set_inline_buffer;

		void clear(bool release = false) {
			clear_helper(release, true, buffer_size);
		}

		bytebuffer()
			: bytebuffer_(header_t<uint8_t>(buffer_size, false)) {
			clear(true); // essential for initialization
		}

		bytebuffer(const bytebuffer_& x)
			: bytebuffer() {
			assert(header.h.inited);
			clear(false);
			copy_raw((uint8_t*)x.buffer(), x.size());
		}

		bytebuffer(const bytebuffer& x)
			: bytebuffer((const bytebuffer_&)x) // NEEDED?
			{
			}

		bytebuffer(const bytebuffer_&& x)
			: bytebuffer() {
			assert(header.h.inited);
			move(x);
		}

		bytebuffer(const uint8_t* v, int v_len)
			: bytebuffer() {
			clear(false);
			copy_raw((uint8_t*)v, v_len);
		}

		bytebuffer(const char* v, int v_len)
			: bytebuffer((uint8_t*)v, v_len) {
		}

		bytebuffer(const char* v)
			: bytebuffer((uint8_t*)v, strlen(v)) {
			assert(header.h.inited);
		}

		bytebuffer(std::initializer_list<char> v)
			: bytebuffer() {
			assert(header.h.inited);
			for (const auto& x : v)
				this->push_back(x);
		}

		template <int otherbuffer_size>
		bytebuffer(const bytebuffer<otherbuffer_size>& x)
			: bytebuffer(x.buffer(), x.size()) {
			assert(header.h.inited);
		}

		template <typename T, typename parent_, int buffer_size_> friend void clear_something(T& s, bool release);

		template <typename T, bool zero_terminate_> friend void shrink_to_fit_something(T& s);

		void shrink_to_fit() {
			return shrink_to_fit_something<decltype(*this)>(*this);
		}

		bytebuffer& operator=(const bytebuffer_& x) {
			assert(header.h.inited);
			copy(x);
			return *this;
		}

		bytebuffer& operator=(const bytebuffer& x) {
			assert(header.h.inited);
			this->copy(x);
			return *this;
		}

	};

	template <typename data_t> using vector_ = databuffer_<data_t>;

	template <typename data_t, int vector_size = 0> class vector : public vector_<data_t> {
		using parent = vector_<data_t>;
		constexpr static int buffer_size = header_t<data_t>::template extended_inline_to_reserve<vector_size>();
		char buffer_[buffer_size];
		using parent::clear_helper;

	public:
		using parent::copy;
		using parent::move;
		using parent::set_size;
		using parent::set_inline_buffer;

		void clear(bool release = false) {
			clear_helper(release, true, vector_size);
		}

		vector()
			: parent(header_t<data_t>(vector_size, false)) {
			// clear(true);
		}

		vector(const vector& x)
			: vector() {
			copy(x);
		}

		vector(const vector_<data_t>& x)
			: vector() {
			copy(x);
		}

		template <int othervector_size>
		vector(const vector<data_t, othervector_size>& x)
			: vector() {
			copy(x);
		}

		vector(vector_<data_t>&& x)
			: vector() {
			move(x);
		}

		vector(const data_t* v, int v_len)
			: vector() {
			for (int i = 0; i < v_len; ++i)
				this->push_back(v[i]);
		}

		vector(std::initializer_list<data_t> v)
			: vector() {
			for (const auto& x : v)
				this->push_back(x);
		}

		vector& operator=(const vector&) = default;

		inline int index_of(const data_t& data) const {
			int i=0;
			for(const auto &el: *this) {
				if (el == data)
					return i;
				++i;
			}
			return -1;
		}

		inline bool contains(const data_t& data) const {
			auto i = index_of(data);
			return i>=0;
		}
	};

	class string_ : public databuffer_<char> {
		int _append_as_utf8(const char* input, int input_len, const char* enc);
		int translate_dvb_control_characters(int oldn, bool clean);

		using parent = databuffer_<char>;

	public:
		inline void reserve(int size) {
			parent::reserve(size + 1);
		}

		inline void resize_no_init(int new_size) {
			if(new_size < size())
				this->buffer()[new_size] =0;
			databuffer_<char>::resize_no_init(new_size+1);
		}
		void clear(bool release = false) {
			clear_helper(release, false, 0);
			set_size(1);
			buffer()[0] = 0;
		}

		// v_len does not include terminating 0 byte
		void copy_raw(const char* v, int v_len);

		// size does not include traling 0 byte, but capacity does
		inline int size() const {
			int s = parent::size();
			return s > 1 ? s - 1 : 0;
		}

		//append zero terminated string of length len (not including zero terminator)
		inline void append(const char* data, int len) {
			int s = parent::size();
			if(s > 0)
				set_size(s-1); //remove training 0x0
			append_raw(data, len);
		}
		void append_tolower(const string_& in);

		static string_ view(char* v, int v_cap, int v_len) {
			auto x = parent::view(v, v_cap, v_len);
			return static_cast<string_&>(x);
		}

		using parent::copy;
		using parent::item_size;

	protected:
		string_(const header_t<char>& header)
			: parent(header) {
		}

	public:
		string_()
			: parent() {
			assert(header.h.inited);
			clear();
		}

		string_(const string_& x)
			: parent() {
			copy_raw(x.buffer(), x.size());
		}

		string_(string_&& x)
			: parent() {
			copy_raw(x.buffer(), x.size());
		}

		string_(const char* v, // initial value for databuffer
						int v_len = -1 //(upper bound for ) length of string at v;
						// if v_len==-1, then use strlen(v) instead
			)
			: parent(v, (v_len == (int)-1) ? strlen(v) : v_len) {
			auto len = (v_len == (int)-1) ? strlen(v) : v_len;
			copy_raw(v, len);
		}

		inline string_& operator=(const string_& x) {
			assert(header.h.inited);
			copy_raw(x.buffer(), x.size());
			return *this;
		}

		inline bool operator==(const string_& other) const {
			if (size() != other.size())
				return false;
			return memcmp(buffer(), other.buffer(), size()) == 0;
		}

		inline bool operator!=(const string_& other) const {
			return !(*this == other);
		}

		const char* c_str() const {
			return buffer();
		}

		const char* c_str() {
			return buffer();
		}

		operator std::string() const {
			assert(header.h.inited);
			return buffer();
		}

		inline string_& operator=(const char* other);

		template <typename T> inline accu_t operator<<(const T& x);

		template <typename T> string_& snprintf(const char* fmt, T&& x) {
			int r = ::snprintf(buffer() + size(), capacity() - size(), fmt, x);
			auto size_ = size();
			if (r + 1 >= (signed)(capacity() - size())) {
				grow(r + 1 - capacity() + size_);
				size_ += ::snprintf(buffer() + size_, capacity() - size_, fmt, x);
			} else
				size_ += r;
			set_size(size_ + 1);
			assert(size() == size_);
			assert(parent::size() == size_ + 1);
			assert(1 + size() <= capacity());
			buffer()[size_] = 0;
			return *this;
		}

		string_& sprintf(const dateTime& x);
		string_& sprintf(const char* fmt, ss::string_& x);
		string_& sprintf(ss::string_& x) {
			return sprintf("%s", x);
		}

		int snprintf(int s, const char* fmt, ...);

		int sprintf(const char* fmt, ...);

		std::string str(void) const {
			return std::string(buffer());
		}

		int append_as_utf8(const char* input, int input_len, const char* enc, bool clean = true);

		int strftime(const char* fmt, const struct tm* tm);
		void trim(int start = 0);

		const char& operator[](int pos) const {
			assert(header.h.inited);
			assert(pos < size());
			return buffer()[pos];
		}

		char& operator[](int pos) {
			assert(header.h.inited);
			if (pos >= size()) {
				auto size_ = pos + 1;
				reserve(size_);
				set_size(size_ + 1);
				assert(parent::size() == size_ + 1);
				assert(size() == size_);
				assert(size_ + 1 <= capacity());
				(char&)(buffer()[size_]) = 0;
			}
			return buffer()[pos];
		}

		void push_back(char val) {
			operator[](size()) = val;
		}
	};

#if 0
	inline bool operator==(const string_&a, const string_& b) {
		if (a.size()!=b.size())
			return false;
		return memcmp(a.buffer(), b.buffer(), a.size())==0;
	}
#endif

/*
	buffer_size>0: there is room after the header to store string data and the room is buffer_size
	buffer_size=0: there is no room
*/
	template <int buffer_size = 0> class string : public string_ {
		typedef char data_t;

		char buffer_[header_t<char>::extended_inline_to_reserve<buffer_size>()];

	public:
		using parent = string_;
		using parent::item_size;
#if 0
		using string_::copy;
		using string_::operator==;
		using string_::operator!=;
#endif
		void clear(bool release = false) {
			clear_helper(release, false, buffer_size);
			set_size(1);
			buffer()[0] = 0;
		}

		static constexpr bool zero_terminate = true;

		string()
			: string_(header_t<char>(buffer_size, false)) {
			clear(true);
		}

		string(const string_& x)
			: string() {
			clear(false);
			copy_raw(x.buffer(), x.size());
		}

		string(string_&& x)
			: string() {
			clear(false);
			move(x);
		}

		string(const char* v, int v_len)
			: string() {
			clear(false);
			copy_raw(v, v_len);
		}

		string(std::initializer_list<char> v)
			: string() {
			clear(false);
			for (const auto& x : v)
				this->push_back(x);
		}

		string(const char* v)
			: string(v, strlen(v)) {
		}

		string& operator=(const string&) = default;

		string(const string& x)
			: string(x.buffer(), x.size()) {
		}

		template <int otherbuffer_size>
		string(const string<otherbuffer_size>& x)
			: string(x.buffer(), x.size()) {
		}

		const char* c_str() const {
			return buffer();
		}

		char* c_str() {
			return buffer();
		}

		template <typename T, typename parent_, int buffer_size_> friend void clear_something(T& s, bool release);
#if 0
		void clear (bool release=false) {
			return clear_something<decltype(*this), parent, buffer_size>(*this, release);
		}
#endif
		string<buffer_size> tolower() const {
			string<buffer_size> ret;
			ret.append_tolower(*this);
			return ret;
		}

		template <typename T, bool zero_terminate_> friend void shrink_to_fit_something(T& s);

		void shrink_to_fit() {
			return shrink_to_fit_something<decltype(*this), zero_terminate>(*this);
		}

		using string_::operator=;

		string& operator=(const string_& x) {
			assert(header.h.inited);
			copy(x);
			return *this;
		}
#if 0
		string& operator=(string&x) {
			copy(x);
			return *this;
		}
#endif

#if 0
		string& operator=(const string&x) {
			copy(x);
			return *this;
		}
#endif
	};

	template <int buffer_size> string<buffer_size> tolower(const string<buffer_size>& in) {
		return in.tolower();
	}

	struct fmt_t {
		const char* _fmt = NULL;

		fmt_t(const char* _fmt)
			: _fmt(_fmt) {
		}
		void reset() {
			_fmt = NULL;
		}
		operator bool() {
			return _fmt;
		}
	};

	inline fmt_t operator"" _f(const char* x, unsigned long) {
		return fmt_t(x);
	}

	template <typename T, int buffer_size>
	ss::vector<T, buffer_size> compute_running_sum(const ss::vector<T, buffer_size>& in) {
		ss::vector<T, buffer_size> out;
		int offset = 0;
		for (auto& i : in) {
			out.push_back(offset);
			if (i < 0)
				offset = -1;
			else
				offset += i;
		}
		return out;
	}

/*! rotate a vector by r: newv[0]= oldv[r], newv[1]= oldv[r+1], ...
	indices are module size
*/
	template <typename data_t> inline void rotate(ss::vector_<data_t>& v, int r);

}; // namespace ss

namespace std { // needed ot log4cxx creates errors
	std::ostream& operator<<(std::ostream& os, const ss::string_& a);
	std::ostream& operator<<(std::ostream& os, const ss::bytebuffer_& a);
#if 0
	inline std::ostream& operator<<(std::ostream& os, const ss::string_& a) {
		return os << a.c_str();
	}
#endif

	template <int buffer_size> inline std::ostream& operator<<(std::ostream& os, const ss::string<buffer_size>& a) {
		return os << a.c_str();
	}

}; // namespace std

#include "stackstring_impl.h"
