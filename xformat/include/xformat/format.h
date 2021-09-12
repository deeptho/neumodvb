/*-
 * Copyright (c) 2016 Zhihao Yuan.  All rights reserved.
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

#pragma once

#include <assert.h>
#include <limits>

#include "tuple.h"
#include "gliteral.h"
#include "qoi.h"
#include <stdexcept>
namespace stdex
{

using std::enable_if_t;

template <bool V>
using bool_constant = std::integral_constant<bool, V>;

enum class fmtoptions
{
	none,
	left = 0x01,
	right = 0x02,
	sign = 0x04,
	aligned_sign = 0x08,
	alt = 0x10,
	zero = 0x20,
};

constexpr
fmtoptions operator&(fmtoptions __x, fmtoptions __y)
{
	return fmtoptions(int(__x) & int(__y));
}

constexpr
fmtoptions operator|(fmtoptions __x, fmtoptions __y)
{
	return fmtoptions(int(__x) | int(__y));
}

constexpr
fmtoptions operator^(fmtoptions __x, fmtoptions __y)
{
	return fmtoptions(int(__x) ^ int(__y));
}

constexpr
fmtoptions
operator~(fmtoptions __x)
{
	return fmtoptions(~int(__x) & 0x3f);
}

constexpr
fmtoptions& operator&=(fmtoptions& __x, fmtoptions __y)
{
	return __x = __x & __y;
}

constexpr
fmtoptions& operator|=(fmtoptions& __x, fmtoptions __y)
{
	return __x = __x | __y;
}

constexpr
fmtoptions& operator^=(fmtoptions& __x, fmtoptions __y)
{
	return __x = __x ^ __y;
}

struct fmtshape
{
	constexpr auto facade() const
	{
		return ch_;
	}

	constexpr auto options() const
	{
		return fmtoptions(opts_);
	}

	_STDEX_constexpr void operator=(char ch)
	{
		ch_ = ch;
	}

	_STDEX_constexpr void operator=(fmtoptions opts)
	{
		opts_ = (unsigned char)opts;
	}

	_STDEX_constexpr void operator|=(fmtoptions opts)
	{
		*this = options() | opts;
	}

private:
	char ch_ = '\0';
	unsigned char opts_{};
};

namespace detail
{

enum op_type
{
	OP_RAW_S = 1,
	OP_RAW_C,
	OP_FMT,
};

enum op_attr
{
	REG_ARG1 = 0b0100,
	REG_ARG2 = 0b1000,
};

struct entry
{
	constexpr auto opcode() const
	{
		return op_type(op_ & 0b11);
	}

	constexpr bool has(op_attr attr) const
	{
		return (op_ & attr) != 0;
	}

	unsigned short op_;
	fmtshape shape;
	int arg;
	int arg1;
	int arg2;
};

}

template <typename charT>
struct fmtstack
{
	using iterator = detail::entry const*;

	constexpr explicit fmtstack(charT const* s) : start(s)
	{
	}

	constexpr iterator begin() const
	{
		return line;
	}

	constexpr iterator end() const
	{
		return line + size;
	}

	static constexpr size_t max_size()
	{
		return sizeof(line) / sizeof(detail::entry);
	}

	_STDEX_constexpr void push(detail::entry x)
	{
		if (size == max_size())
			throw std::length_error{ "format string too complex" };

		line[size++] = std::move(x);
	}

	_STDEX_constexpr auto raw_string(detail::entry const& x) const
	    -> basic_string_view<charT>
	{
		auto hi = x.arg1;
		auto lo = x.arg2;
		return { start + hi, size_t(lo - hi) };
	}

	static _STDEX_constexpr charT raw_char(detail::entry const& x)
	{
		assert(x.opcode() == detail::OP_RAW_C);
		return charT(x.arg);
	}

private:
	charT const* start;
	size_t size = 0;
	// maximum 9 arguments, 10 raw inputs
	// for each extra escape character, sacrifice 1 argument.
	detail::entry line[19] = {};
};

static_assert(sizeof(fmtstack<char>) <= 80 * sizeof(int), "");

namespace detail
{

template <typename Iter>
_STDEX_constexpr
auto instruction(Iter from, Iter first, Iter last)
{
	assert(from <= first and first <= last);
	if (last - from > (std::numeric_limits<int>::max)())
		throw std::length_error{ "raw string is too long" };

	return entry{ OP_RAW_S, {}, {}, int(first - from), int(last - from) };
}

template <typename charT>
constexpr
auto instruction(charT ch)
{
	return entry{ OP_RAW_C, {}, int(ch), {} ,{} };
}

template <typename Iter, typename charT>
_STDEX_constexpr
auto findc(Iter first, Iter last, charT c)
{
	for (; first != last; ++first)
	{
		if (*first == c)
			return first;
	}

	return last;
}

template <typename charT>
struct bounded_reader
{
	using pointer = charT const*;

	constexpr bounded_reader(pointer p, size_t sz) : cur_(p), end_(p + sz)
	{
	}

	constexpr explicit operator bool() const
	{
		return not empty();
	}

	constexpr bool empty() const
	{
		return cur_ == end_;
	}

	_STDEX_constexpr void incr()
	{
		++cur_;
	}

	_STDEX_constexpr auto read()
	{
		return *cur_++;
	}

	_STDEX_constexpr bool look_ahead(charT c) const
	{
		auto p = ptr();
		return ++p < eptr() and *p == c;
	}

	_STDEX_constexpr void seek_to(charT c)
	{
		cur_ = findc(cur_, end_, c);
	}

	constexpr auto operator*() const
	{
		return *ptr();
	}

	constexpr auto ptr() const
	{
		return cur_;
	}

	constexpr auto eptr() const
	{
		return end_;
	}

private:
	pointer cur_;
	pointer end_;
};

template <typename charT>
constexpr
bool is_1to9(charT c)
{
	return STDEX_G(charT, '1') <= c and c <= STDEX_G(charT, '9');
}

template <typename charT>
constexpr
bool is_0to9(charT c)
{
	return STDEX_G(charT, '0') <= c and c <= STDEX_G(charT, '9');
}

template <typename charT>
constexpr
int to_int(charT c)
{
	return int(c - STDEX_G(charT, '0'));
}

template <typename charT>
_STDEX_constexpr
int parse_int(bounded_reader<charT>& r)
{
	int n = 0;
	for (; r and is_0to9(*r); r.incr())
	{
		n *= 10;
		n += to_int(*r);
	}
	return n;
}

template <typename charT>
_STDEX_constexpr
int parse_position(bounded_reader<charT>& r)
{
	int n = to_int(r.read());
	if (n < 1 or n > 9 or r.empty() or r.read() != STDEX_G(charT, '$'))
		throw std::invalid_argument{ "position is not 1-9$" };
	return n - 1;
}

template <typename charT>
_STDEX_constexpr
auto parse_flags_c(bounded_reader<charT>& r)
{
	fmtshape sp;
	sp = fmtoptions::right;

	for (; r; r.incr())
	{
		// not clearing conflicting flags; the formatters have rights
		// to interpret them.
		switch (*r)
		{
		case STDEX_G(charT, '-'):
			sp |= fmtoptions::left;
			break;
		case STDEX_G(charT, '+'):
			sp |= fmtoptions::sign;
			break;
		case STDEX_G(charT, ' '):
			sp |= fmtoptions::aligned_sign;
			break;
		case STDEX_G(charT, '#'):
			sp |= fmtoptions::alt;
			break;
		case STDEX_G(charT, '0'):
			sp |= fmtoptions::zero;
			break;
		default:
			return sp;
		}
	}

	return sp;
}

template <typename charT>
constexpr
char charcvt(charT c, std::true_type)
{
	return char(c);
}

template <typename charT>
_STDEX_constexpr
char charcvt(charT c, std::false_type)
{
	static_assert(STDEX_G(charT, 'A') == 65,
	              "non-execution character set is not Unicode");
	return "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz"
	    [c - 65];
}

template <typename charT>
constexpr
char charcvt(charT c)
{
	return charcvt(c, bool_constant<STDEX_G(charT, 'A') == 'A'>());
}

template <typename charT>
_STDEX_constexpr
fmtstack<charT> compile_c(charT const* s, size_t sz)
{
	using std::invalid_argument;

	fmtstack<charT> fstk{ s };
	bounded_reader<charT> r(s, sz);
	int ac = -1;
	bool sequential{};  // expects: ac != -1

	while (r)
	{
		auto p = r.ptr();
		r.seek_to(STDEX_G(charT, '%'));

		// process escaping first
		if (r)
		{
			if (r.ptr() + 1 == r.eptr())
				throw invalid_argument{ "single '%'" };

			if (r.look_ahead(STDEX_G(charT, '%')))
			{
				bool single = p == r.ptr();
				r.incr();
				if (single)
					fstk.push(instruction(*r));
				else
					fstk.push(instruction(s, p, r.ptr()));

				r.incr();
				continue;
			}
		}

		if (auto d = r.ptr() - p)
		{
			if (d == 1)
				fstk.push(instruction(*p));
			else
				fstk.push(instruction(s, p, r.ptr()));
		}

		if (r.empty())
			break;

		r.incr();

		// assert: !r.empty()
		if (ac == -1)
			sequential = !(is_1to9(*r) and
			               r.look_ahead(STDEX_G(charT, '$')));

		if (sequential)
			++ac;
		else
			ac = parse_position(r);

		auto sp = parse_flags_c(r);

		unsigned short op = OP_FMT;
		int width = -1;
		int precision = -1;

		// field width
		if (r)
		{
			if (is_1to9(*r))
				width = parse_int(r);
			else if (*r == STDEX_G(charT, '*'))
			{
				r.incr();
				op |= REG_ARG1;
				width = sequential or r.empty()
				            ? ac++
				            : parse_position(r);
			}
		}

		// precision
		if (r and *r == STDEX_G(charT, '.'))
		{
			r.incr();
			if (r and *r == STDEX_G(charT, '*'))
			{
				r.incr();
				op |= REG_ARG2;
				precision = sequential or r.empty()
				                ? ac++
				                : parse_position(r);
			}
			else
				precision = parse_int(r);
		}

		// ignore all length modifiers
		if (r)
		{
			switch (auto c = *r)
			{
			case STDEX_G(charT, 'h'):
			case STDEX_G(charT, 'l'):
				r.incr();
				if (r and *r == c)
					r.incr();
				break;
			case STDEX_G(charT, 'j'):
			case STDEX_G(charT, 'z'):
			case STDEX_G(charT, 't'):
			case STDEX_G(charT, 'L'):
				r.incr();
				break;
			}
		}

		if (r.empty())
			throw invalid_argument{ "incomplete specification" };

		switch (auto c = r.read())
		{
		case STDEX_G(charT, 'd'):
		case STDEX_G(charT, 'i'):
		case STDEX_G(charT, 'o'):
		case STDEX_G(charT, 'u'):
		case STDEX_G(charT, 'x'):
		case STDEX_G(charT, 'X'):
		case STDEX_G(charT, 'f'):
		case STDEX_G(charT, 'F'):
		case STDEX_G(charT, 'e'):
		case STDEX_G(charT, 'E'):
		case STDEX_G(charT, 'g'):
		case STDEX_G(charT, 'G'):
		case STDEX_G(charT, 'a'):
		case STDEX_G(charT, 'A'):
		case STDEX_G(charT, 'c'):
		case STDEX_G(charT, 's'):
		case STDEX_G(charT, 'p'):
			sp = charcvt(c);
			break;
		default:
			throw invalid_argument{ "unknown format specifier" };
		}

		fstk.push({ op, sp, ac, width, precision });
	}

	return fstk;
}

template <typename charT>
_STDEX_constexpr
int parse_index(bounded_reader<charT>& r)
{
	int n = to_int(r.read());
	if (n < 0 or n > 9 or r.empty())
		throw std::invalid_argument{ "index is not 0-9" };
	return n;
}

template <typename charT>
_STDEX_constexpr
fmtstack<charT> compile(charT const* s, size_t sz)
{
	using std::invalid_argument;

	fmtstack<charT> fstk{ s };
	bounded_reader<charT> r(s, sz);
	int ac = -1;
	bool sequential{};  // expects: ac != -1

	while (r)
	{
		auto p = r.ptr();
		charT c{};
		for (; r; r.incr())
		{
			c = *r;
			if (c == STDEX_G(charT, '{') or
			    c == STDEX_G(charT, '}'))
				break;
		}

		if (r)
		{
			if (r.ptr() + 1 == r.eptr())
				throw invalid_argument{ "single brace" };

			// handle escaping
			if (r.look_ahead(c))
			{
				bool single = p == r.ptr();
				r.incr();
				if (single)
					fstk.push(instruction(*r));
				else
					fstk.push(instruction(s, p, r.ptr()));

				r.incr();
				continue;
			}
		}

		if (auto d = r.ptr() - p)
		{
			if (d == 1)
				fstk.push(instruction(*p));
			else
				fstk.push(instruction(s, p, r.ptr()));
		}

		if (r.empty())
			break;

		r.incr();

		// assert: !r.empty()
		if (ac == -1)
			sequential = *r == STDEX_G(charT, ':') or
			             *r == STDEX_G(charT, '}');

		if (sequential)
			++ac;
		else
			ac = parse_index(r);

		entry e{ OP_FMT, {}, ac, -1, -1 };

		if (*r == STDEX_G(charT, '}'))
		{
			r.incr();
			fstk.push(e);
			continue;
		}

		if (r.read() != STDEX_G(charT, ':'))
			throw invalid_argument{ "missing ':' before spec" };

		if (r)
		{
			switch (*r)
			{
			case STDEX_G(charT, '<'):
				e.shape = fmtoptions::left;
				r.incr();
				break;
			case STDEX_G(charT, '>'):
				e.shape = fmtoptions::right;
				r.incr();
				break;
			}
		}

		if (r)
		{
			switch (*r)
			{
			case STDEX_G(charT, '+'):
				e.shape |= fmtoptions::sign;
			case STDEX_G(charT, '-'):
				r.incr();
				break;
			case STDEX_G(charT, ' '):
				e.shape |= fmtoptions::aligned_sign;
				r.incr();
				break;
			}
		}

		if (r and *r == STDEX_G(charT, '#'))
		{
			e.shape |= fmtoptions::alt;
			r.incr();
		}

		if (r and *r == STDEX_G(charT, '0'))
		{
			e.shape |= fmtoptions::zero;
			r.incr();
		}

		// field width
		if (r)
		{
			if (is_0to9(*r))
				e.arg1 = parse_int(r);
			else if (*r == STDEX_G(charT, '{'))
			{
				r.incr();
				if (r and is_0to9(*r))
					e.arg1 = to_int(r.read());
				else
					e.arg1 = ++ac;
				e.op_ |= REG_ARG1;
				if (!(r and r.read() == STDEX_G(charT, '}')))
					throw invalid_argument
					{
					    "width not ended with '}'"
					};
			}
		}

		// precision
		if (r and *r == STDEX_G(charT, '.'))
		{
			r.incr();
			if (is_0to9(*r))
				e.arg2 = parse_int(r);
			else if (*r == STDEX_G(charT, '{'))
			{
				r.incr();
				if (r and is_0to9(*r))
					e.arg2 = to_int(r.read());
				else
					e.arg2 = ++ac;
				e.op_ |= REG_ARG2;
				if (!(r and r.read() == STDEX_G(charT, '}')))
					throw invalid_argument
					{
					    "precision not ended with '}'"
					};
			}
			else
				throw invalid_argument{ "missing precision" };
		}

		if (r)
		{
			switch (*r)
			{
			case STDEX_G(charT, 'd'):
			case STDEX_G(charT, 'o'):
			case STDEX_G(charT, 'u'):
			case STDEX_G(charT, 'x'):
			case STDEX_G(charT, 'X'):
			case STDEX_G(charT, 'f'):
			case STDEX_G(charT, 'F'):
			case STDEX_G(charT, 'e'):
			case STDEX_G(charT, 'E'):
			case STDEX_G(charT, 'g'):
			case STDEX_G(charT, 'G'):
			case STDEX_G(charT, 'a'):
			case STDEX_G(charT, 'A'):
			case STDEX_G(charT, 'c'):
			case STDEX_G(charT, 's'):
			case STDEX_G(charT, 'p'):
				e.shape = charcvt(*r);
				r.incr();
				break;
			}
		}

		if (r.empty())
			throw invalid_argument{ "incomplete specification" };

		if (r.read() != STDEX_G(charT, '}'))
			throw invalid_argument{ "unknown format specifier" };

		fstk.push(e);
	}

	return fstk;
}

#ifndef XFORMAT_HEADER_ONLY

extern template fmtstack<char> compile_c(char const*, size_t);
extern template fmtstack<wchar_t> compile_c(wchar_t const*, size_t);
extern template fmtstack<char16_t> compile_c(char16_t const*, size_t);
extern template fmtstack<char32_t> compile_c(char32_t const*, size_t);

extern template fmtstack<char> compile(char const*, size_t);
extern template fmtstack<wchar_t> compile(wchar_t const*, size_t);
extern template fmtstack<char16_t> compile(char16_t const*, size_t);
extern template fmtstack<char32_t> compile(char32_t const*, size_t);

#endif

template <typename T, typename = void>
struct do_int_cast
{
	[[noreturn]] static int fn(T const&)
	{
		throw std::invalid_argument{ "not an integer" };
	}
};

template <typename T>
struct do_int_cast<T, enable_if_t<std::is_convertible<T, int>::value and
                                  not std::is_floating_point<T>::value>>
{
	constexpr static int fn(_param_type_t<T> v)
	{
		return static_cast<int>(v);
	}
};

struct int_cast
{
	template <typename T>
	_STDEX_always_inline
	constexpr int operator()(T const& v) const
	{
		return do_int_cast<T>::fn(v);
	}
};

template <typename charT, typename Formatter, typename Tuple>
inline
decltype(auto) vformat(Formatter fter, fmtstack<charT> const& fstk, Tuple tp)
{
	for (auto&& et : fstk)
	{
		switch (et.opcode())
		{
		case OP_RAW_S:
			fter.send(fstk.raw_string(et));
			break;
		case OP_RAW_C:
			fter.send(fstk.raw_char(et));
			break;
		case OP_FMT:
			int w = et.arg1;
			int p = et.arg2;

			if (et.has(REG_ARG1))
				w = visit_at<int>(et.arg1, int_cast(), tp);
			if (et.has(REG_ARG2))
				p = visit_at<int>(et.arg2, int_cast(), tp);

			visit_at(et.arg,
			         [=, &fter](auto&& x)
			         {
				         fter.format(et.shape, w, p, x);
				 },
			         tp);
			break;
		}
	}

	return fter.state();
}

}

template <typename charT, typename Formatter, typename... Args>
inline
decltype(auto) format(Formatter fter, fmtstack<charT> const& fstk,
                      Args&&... args)
{
	return detail::vformat(fter, fstk, as_tuple_of_cref(args...));
}

inline namespace literals
{
_STDEX_constexpr
auto operator"" _cfmt(char const* s, size_t sz)
{
	return detail::compile_c(s, sz);
}

_STDEX_constexpr
auto operator"" _cfmt(wchar_t const* s, size_t sz)
{
	return detail::compile_c(s, sz);
}

_STDEX_constexpr
auto operator"" _cfmt(char16_t const* s, size_t sz)
{
	return detail::compile_c(s, sz);
}

_STDEX_constexpr
auto operator"" _cfmt(char32_t const* s, size_t sz)
{
	return detail::compile_c(s, sz);
}

_STDEX_constexpr
auto operator"" _fmt(char const* s, size_t sz)
{
	return detail::compile(s, sz);
}

_STDEX_constexpr
auto operator"" _fmt(wchar_t const* s, size_t sz)
{
	return detail::compile(s, sz);
}

_STDEX_constexpr
auto operator"" _fmt(char16_t const* s, size_t sz)
{
	return detail::compile(s, sz);
}

_STDEX_constexpr
auto operator"" _fmt(char32_t const* s, size_t sz)
{
	return detail::compile(s, sz);
}
}

}
