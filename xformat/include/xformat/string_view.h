/*-
 * Copyright (c) 2013-2016 Zhihao Yuan.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _STDEX_STRING_VIEW_H
#define _STDEX_STRING_VIEW_H

#include <string>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
#include <functional>
#include <stdexcept>
#include <iosfwd>

#if defined(_MSC_VER)
#include <ciso646>
# if _MSC_VER < 1900
# define noexcept throw()
# define constexpr
# endif
#endif

namespace stdex {

namespace detail {

template <typename Container>
struct iter
{
#if defined(__GLIBCXX__)
	typedef __gnu_cxx::__normal_iterator
	    <typename Container::const_pointer, Container> type;
#else
	typedef typename Container::const_pointer type;
#endif
};

}

template <typename CharT, typename Traits = std::char_traits<CharT>>
struct basic_string_view
{
	typedef Traits				traits_type;
	typedef typename Traits::char_type	value_type;
	typedef std::size_t			size_type;
	typedef std::ptrdiff_t			difference_type;

	typedef value_type*			pointer;
	typedef value_type const*		const_pointer;
	typedef value_type&			reference;
	typedef value_type const&		const_reference;

	typedef typename detail::iter<basic_string_view>::type iterator;
	typedef iterator			const_iterator;
	typedef std::reverse_iterator<iterator>	reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

	static const auto npos = size_type(-1);

	constexpr basic_string_view() noexcept
		: it_(), sz_()
	{}

	template <typename Allocator>
	basic_string_view(std::basic_string
	    <CharT, Traits, Allocator> const& str) noexcept
		: it_(str.data()), sz_(str.size())
	{}

	basic_string_view(CharT const* str)
		: it_(str), sz_(traits_type::length(str))
	{}

	constexpr basic_string_view(CharT const* str, size_type len)
		: it_(str), sz_(len)
	{}

	constexpr iterator begin() const noexcept
	{
		return iterator(it_);
	}

	constexpr iterator end() const noexcept
	{
		return iterator(it_ + sz_);
	}

	constexpr const_iterator cbegin() const noexcept
	{
		return begin();
	}

	constexpr const_iterator cend() const noexcept
	{
		return end();
	}

	reverse_iterator rbegin() const noexcept
	{
		return reverse_iterator(end());
	}

	reverse_iterator rend() const noexcept
	{
		return reverse_iterator(begin());
	}

	const_reverse_iterator crbegin() const noexcept
	{
		return rbegin();
	}

	const_reverse_iterator crend() const noexcept
	{
		return rend();
	}

	constexpr size_type size() const noexcept
	{
		return sz_;
	}

	constexpr size_type length() const noexcept
	{
		return size();
	}

	constexpr bool empty() const noexcept
	{
		return size() == 0;
	}

	constexpr const_reference operator[](size_type pos) const
	{
		return it_[pos];
	}

	constexpr const_reference at(size_type pos) const
	{
		return pos < size() ? (*this)[pos] :
		    throw std::out_of_range("basic_string_view::at");
	}

	constexpr const_reference front() const
	{
		return (*this)[0];
	}

	constexpr const_reference back() const
	{
		return (*this)[size() - 1];
	}

	constexpr const_pointer data() const noexcept
	{
		return it_;
	}

	void clear() noexcept
	{
		sz_ = 0;
	}

	void remove_prefix(size_type n)
	{
		it_ += n;
		sz_ -= n;
	}

	void remove_suffix(size_type n)
	{
		sz_ -= n;
	}

	void swap(basic_string_view& sv) noexcept
	{
		std::swap(it_, sv.it_);
		std::swap(sz_, sv.sz_);
	}

#if !(defined(_MSC_VER) && _MSC_VER < 1800)

	template <typename Allocator>
	explicit operator std::basic_string<CharT, Traits, Allocator>() const
	{
		return std::basic_string<CharT, Traits, Allocator>(
		    data(), size());
	}

#endif

	std::basic_string<CharT, Traits> to_string() const
	{
		return std::basic_string<CharT, Traits>(data(), size());
	}

	template <typename Allocator>
	std::basic_string<CharT, Traits, Allocator> to_string(
	    Allocator const& a = Allocator()) const
	{
		return std::basic_string<CharT, Traits, Allocator>(
		    data(), size(), a);
	}

	size_type copy(CharT* s, size_type n, size_type pos = 0) const
	{
		if (pos > size())
			throw std::out_of_range("basic_string_view::copy");

		auto rlen = (std::min)(n, size() - pos);

#if !defined(_MSC_VER)
		std::copy_n(begin() + pos, rlen, s);
#else
		std::copy_n(begin() + pos, rlen,
		    stdext::make_unchecked_array_iterator(s));
#endif
		return rlen;
	}

	constexpr basic_string_view substr(size_type pos = 0,
	    size_type n = npos) const
	{
		return { pos <= size() ? data() + pos :
		    throw std::out_of_range("basic_string_view::substr"),
		    (std::min)(n, size() - pos) };
	}

	size_type find(basic_string_view s, size_type pos = 0) const noexcept
	{
		return find(s.data(), pos, s.size());
	}

	size_type find(CharT const* s, size_type pos, size_type n) const
	{
		// avoid overflow
		if (pos > size() || pos > size() - n)
			return npos;

		if (n == 0)
			return pos;

		auto it = std::search(begin() + pos, end(), s, s + n,
		    traits_eq());

		return offset_from_begin(it);
	}

	size_type find(CharT const* s, size_type pos = 0) const
	{
		return find(s, pos, traits_type::length(s));
	}

	size_type find(CharT ch, size_type pos = 0) const noexcept
	{
		if (pos >= size())
			return npos;

		auto p = traits_type::find(data() + pos, size() - pos, ch);

		if (p == nullptr)
			return npos;
		else
			return p - data();
	}

	size_type find_first_of(basic_string_view s,
	    size_type pos = 0) const noexcept
	{
		return find_first_of(s.data(), pos, s.size());
	}

	size_type find_first_of(CharT const* s, size_type pos,
	    size_type n) const
	{
		if (pos >= size())
			return npos;

		auto it = std::find_first_of(begin() + pos, end(), s, s + n,
		    traits_eq());

		return offset_from_begin(it);
	}

	size_type find_first_of(CharT const* s, size_type pos = 0) const
	{
		return find_first_of(s, pos, traits_type::length(s));
	}

	size_type find_first_of(CharT ch, size_type pos = 0) const
	{
		return find(ch, pos);
	}

	size_type find_first_not_of(basic_string_view s,
	    size_type pos = 0) const noexcept
	{
		return find_first_not_of(s.data(), pos, s.size());
	}

	size_type find_first_not_of(CharT const* s, size_type pos,
	    size_type n) const
	{
		if (pos >= size())
			return npos;

		auto it = std::find_if(begin() + pos, end(),
		    [=](CharT c)
		    {
			return std::none_of(s, s + n,
			    std::bind(traits_eq(), c, std::placeholders::_1));
		    });

		return offset_from_begin(it);
	}

	size_type find_first_not_of(CharT const* s, size_type pos = 0) const
	{
		return find_first_not_of(s, pos, traits_type::length(s));
	}

	size_type find_first_not_of(CharT ch, size_type pos = 0) const
	{
		if (pos >= size())
			return npos;

		auto it = std::find_if_not(begin() + pos, end(),
		    std::bind(traits_eq(), ch, std::placeholders::_1));

		return offset_from_begin(it);
	}

	friend
	bool operator==(basic_string_view a, basic_string_view b)
	{
		return a.size() == b.size() and traits_type::compare(a.data(),
		    b.data(), a.size()) == 0;
	}

	friend
	bool operator!=(basic_string_view a, basic_string_view b)
	{
		return !(a == b);
	}

private:
	struct traits_eq
	{
		constexpr bool operator()(CharT x, CharT y) const noexcept
		{
			return traits_type::eq(x, y);
		}
	};

	size_type offset_from_begin(iterator it) const
	{
		if (it == end())
			return npos;
		else
			return it - begin();
	}

	const_pointer it_;
	size_type sz_;
};

template <typename CharT, typename Traits>
inline
void swap(basic_string_view<CharT, Traits>& a,
    basic_string_view<CharT, Traits>& b) noexcept
{
	a.swap(b);
}

template <typename CharT, typename Traits>
inline
auto operator<<(std::basic_ostream<CharT, Traits>& out,
    basic_string_view<CharT, Traits> s) -> decltype(out)
{
#if defined(__GLIBCXX__)
	return __ostream_insert(out, s.data(), s.size());
#else
	typedef std::basic_ostream<CharT, Traits> ostream_type;

	typename ostream_type::sentry ok(out);

	if (not ok)
		return out;

	try
	{
		decltype(out.width()) w = 0;
		decltype(out.width()) n = s.size();

		if (out.width() > n)
		{
			w = out.width() - n;

			if ((out.flags() & ostream_type::adjustfield) !=
			    ostream_type::left)
				w = -w;
		}

		if (w == 0)
		{
			if (out.rdbuf()->sputn(s.data(), n) != n)
			{
				out.setstate(ostream_type::badbit);
				return out;
			}
		}
		else
		{
			auto c = out.fill();

			for (; w < 0; ++w)
			{
				if (Traits::eq_int_type(out.rdbuf()->sputc(c),
				    Traits::eof()))
				{
					out.setstate(ostream_type::badbit);
					return out;
				}
			}

			if (out.rdbuf()->sputn(s.data(), n) != n)
			{
				out.setstate(ostream_type::badbit);
				return out;
			}

			for (; w > 0; --w)
			{
				if (Traits::eq_int_type(out.rdbuf()->sputc(c),
				    Traits::eof()))
				{
					out.setstate(ostream_type::badbit);
					return out;
				}
			}
		}

		out.width(0);
		return out;
	}
	catch (...)
	{
		out.setstate(ostream_type::badbit);
		return out;
	}
#endif
}

typedef basic_string_view<char>		string_view;
typedef basic_string_view<wchar_t>	wstring_view;
typedef basic_string_view<char16_t>	u16string_view;
typedef basic_string_view<char32_t>	u32string_view;

}

#undef noexcept
#undef constexpr

#endif
