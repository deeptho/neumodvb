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
#include "../util/template_util.h"
#include "../util/dtassert.h"
using namespace ss;

inline int next_power_of_two(int n) {
    n--;
    n |= n >> 1u;
    n |= n >> 2u;
    n |= n >> 4u;
    n |= n >> 8u;
    n |= n >> 16u;
    if constexpr (sizeof(int) > 32) {
      n |= n >> 32u;
    }
    n++;
    return n;
  }


	inline int strcmp_safe(const char*a, const char*b) {
		return a? (b ? strcmp(a,b) : 1)
			: b ? -1 : 0;
	}

	inline const char *str_safe(const char *str) {
		return str?str:"";
	}




	template<typename data_t>
	INLINE data_t& databuffer_<data_t>::operator[] (int pos)
	{
		auto s = size();
		if(pos<0)
			pos = s +pos;
		if(pos < 0)
			throw std::runtime_error("Index out of range");
		reserve(pos + 1 );

		auto* b =buffer();
		if(pos >= s) {
			if constexpr (std::is_trivial<data_t>::value) {
				memset(b+size(), 0, pos - s +1);
			} else {
				for(int i=size(); i<= pos; ++i)
					new(b+i) data_t();
			}
			set_size(pos + 1);
		}
#ifdef SS_ASSERT
		assert(pos < capacity());
#endif
		return b[pos];
	}

	template<typename data_t>
	INLINE const data_t& databuffer_<data_t>::operator[] (int pos) const
	{
		if(pos<0)
			pos = size() +pos;
		if(pos<0)
			throw std::runtime_error("Index out of range");
		if(pos  >= capacity() || pos >=size())
			throw std::out_of_range("index out of range");
		auto* b =buffer();
		return b[pos];
	}


	template<typename data_t>
	inline void databuffer_<data_t>::reserve(int newsize)
	{
		auto newcap = newsize;
		if (newcap <= capacity())
			return;

		if(is_view())
			return;

		auto old_length = size();
		newcap = next_power_of_two(newcap);
		if(newcap - old_length <= 32)
			newcap *= 2;
		if(newcap==0)
			newcap++;
		assert(size() <= newcap);
		//allocate data, but make sure that there is room for one additional
		using D1 = typename header_t<data_t>::template data_with_capacity<1>;
		using D2 = typename header_t<data_t>::template data_with_capacity<2>;
		auto s= sizeof(D1) + (sizeof(D2)-sizeof(D1))*(newcap-1);
		auto* p  = (D1*) operator new (s);
		auto* old_data = buffer();
		p->capacity_ = newcap;
		if constexpr (std::is_trivial<data_t>::value) {
			memcpy(p->data(), old_data, old_length*sizeof(data_t));
		} else {
			for(int i=0; i< old_length; ++i) {
				new(p->data()+i) data_t(old_data[i]);
			}
			for(int i=0; i< old_length; ++i)
				old_data[i].~data_t();
		}
		if(is_allocated()) {
			auto* src = header.allocated_buffer();
			operator delete (src);
		}

		set_external_buffer(p);
#ifndef NDEBUG
		auto* x = buffer();
		assert(x == &p->data()[0]);
#endif
		set_size(old_length);
	}

template<typename data_t>
	databuffer_<data_t>::~databuffer_() {
		if(is_allocated()) {
			assert(!is_view());
			auto *p = buffer();
			if(!std::is_trivial<data_t>::value) {
				for(int i=0; i< size(); ++i)
					p[i].~data_t();
			}
			operator delete (header.allocated_buffer());
		}
	}

	template<typename data_t>
	void databuffer_<data_t>::copy_raw(const data_t* v, int v_len)
	{
		assert(header.h.inited);
		reserve(v_len);

		auto* dest = buffer();
		if constexpr (std::is_trivial<data_t>::value) {
			memcpy(dest, v, v_len*sizeof(data_t));
		} else {
		for(int i=0; i< v_len; ++i)
			dest[i] = (const data_t&) v[i];
		}
		set_size(v_len);
	};

	template<typename data_t>
	void databuffer_<data_t>::copy(const databuffer_&x)
	{
#ifdef SS_ASSERT
		assert(header.h.inited);
#endif
		if(this== &x)
			return;
		if constexpr (std::is_trivial<data_t>::value) {
			copy_raw(x.buffer(), x.size());
		} else {
			clear();
		for (auto&xx: x)
			push_back(xx);
		}
	};


/*@brief: take over the memory of the string, resulting in an empty string
 */
	template<typename data_t>
	void databuffer_<data_t>::move(databuffer_& other) {
		assert(!is_view());
		assert(!other.is_view());
		if(this == &other)
			return;
		if(!other.is_allocated()) {
			//no resources available for take over
			copy(other);
			other.clear(true);
			return;
		}
		if(other.size() < capacity() && !is_allocated()) {
			//copying is better as no reallocation is needed
			copy(other);
			other.clear(true);
			return;
		}
		clear(true);
		assert(other.is_allocated());
		auto other_size = other.size();
		set_external_buffer(other.header.steal_allocated_buffer());
		set_size(other_size);
	}

	template<typename data_t>
	databuffer_<data_t>::databuffer_(const data_t*v, int v_len)
		: header(0, false)
	{
		reserve(v_len);
		assert(v_len <=capacity());
		int size_ = 0;
		auto b = buffer();
		for(size_ = 0; size_< v_len; size_++)
			b[size_] = v[size_];
		set_size(size_);
	};


	template<typename data_t>
	void databuffer_<data_t>::truncate(int n) {
		if(n < size()) {
			set_size(n);
		}
	}

	template<typename data_t>
	void databuffer_<data_t>::erase(int idx) {
		if(idx<0 || idx>= size())
			throw std::out_of_range("index out of range");
		auto* b =buffer();
		for(auto i= idx, j=idx+1; j < size(); ++i, ++j) {
			b[i]=b[j];
		}
		truncate(size()-1);
	}

	template<typename data_t>
	databuffer_<data_t>::databuffer_(const databuffer_&x)
		: header(0, false)
	{
		copy(x);
	}

	template<typename data_t>
	databuffer_<data_t>::databuffer_(databuffer_&&x)
		: header(0, false) {
		move(x);
	}



	inline string_& string_::operator=(const char* x)
	{
		auto size_ = strlen(x);
		reserve(size_+1);
		set_size(size_+1);
		memcpy(buffer(), x, size_ +  1);
		return *this;
	}

namespace ss {
	template<typename data_t>
	void rotate(ss::vector_<data_t>&  v, int r)
	{
		int n=v.size();
		if(r==0)
			return;
		r= (r+n)%n; //this makes it also work for negative and out of range r
		int steps = ::gcd(r, n);
		for(int i=0; i<steps ; ++i) {
			//auto t = v[i];
			int j = i;
			for(;;) {
				int nextj = (j+r) % n;
				if (nextj==i)
					break;
				v[j] = v[nextj];
				j=nextj;
			}
		}
	}





}; //end namespace ss
