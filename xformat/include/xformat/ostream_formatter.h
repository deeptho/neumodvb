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

#include <iosfwd>
#include <sstream>
#include <cmath>
#include <algorithm>

#include <locale>
#include <new>
#include <iterator>
#include <stdio.h>
#include <locale.h>

#include "format.h"
#include "gliteral.h"
#include "qoi.h"

namespace stdex
{

using std::enable_if_t;

template <typename charT, typename traits = std::char_traits<charT>>
struct ostream_outputter
{
	using ostream_type = std::basic_ostream<charT, traits>;

	explicit ostream_outputter(ostream_type& out) : out_(out)
	{
	}

	void send(charT ch)
	{
		state().put(ch);
	}

	void send(basic_string_view<charT, traits> sv)
	{
		state().write(sv.data(), std::streamsize(sv.size()));
	}

	ostream_type& state()
	{
		return out_;
	}

private:
	ostream_type& out_;
};

template <typename T>
struct superficial
{
	using type = T;
};

template <typename T>
struct superficial<T const*> : superficial<T*>
{
	using type = T*;
};

template <typename T>
using superficial_t = typename superficial<T>::type;

template <typename charT, typename traits = std::char_traits<charT>>
struct ostream_formatter : ostream_outputter<charT, traits>
{
	using outputter_type = ostream_outputter<charT, traits>;
	using outputter_type::ostream_outputter;
	using outputter_type::state;

	template <typename T>
	auto format(fmtshape shape, int width, int precision, T const& v)
	{
		print(shape, width, precision, v, superficial<T>());
	}

	void format(fmtshape sp, int w, int p,
	            basic_string_view<charT, traits> sv)
	{
		print_string_ref(sp, w, sv);
	}

	template <typename Allocator>
	void format(fmtshape sp, int w, int p,
	            std::basic_string<charT, traits, Allocator> const& s)
	{
		print_string_ref(sp, w, s);
	}

	void format(fmtshape sp, int w, int p, charT c)
	{
		if (sp.facade() != 'i')
			return print_string_ref(sp, w, c);
		else
			return potentially_formattable(sp, w, p,
			                               traits::to_int_type(c));
	}

	void format(fmtshape sp, int w, int p, bool v)
	{
		switch (sp.facade())
		{
		case '\0':
		case 's':
			return print_string_ref(sp, w, v, os::boolalpha);
		}

		potentially_formattable(sp, w, p, v);
	}

	template <typename T>
	void format(fmtshape sp, int w, int p, std::reference_wrapper<T> r)
	{
		format(sp, w, p, r.get());
	}

private:
	using os = typename outputter_type::ostream_type;
	using fmtflags = typename os::fmtflags;
	using view_type = basic_string_view<charT, traits>;

	template <typename T>
	void print(fmtshape sp, int w, int p, T const& v, ...)
	{
		potentially_formattable(sp, w, p, v);
	}

	template <typename T>
	auto print(fmtshape sp, int w, int p, T v, superficial<T>)
	    -> enable_if_t<std::is_signed<T>::value and
	                   std::is_integral<T>::value>
	{
		switch (sp.facade())
		{
		case 'u':
		case 'o':
		case 'x':
		case 'X':
			return format(sp, w, p, std::make_unsigned_t<T>(v));
		}

		print_signed(sp, w, p, v);
	}

	template <typename T>
	auto print(fmtshape sp, int w, int p, T v, superficial<T>)
	    -> enable_if_t<std::is_unsigned<T>::value>
	{
		print_basic_arithmetic(sp, w, p, v);
	}

	template <typename T>
	auto print(fmtshape sp, int w, int p, T v, superficial<T>)
	    -> enable_if_t<std::is_floating_point<T>::value>
	{
		switch (sp.facade())
		{
		case 'a':
		case 'A':
			return print_hexfloat(sp, w, p, v);
		}

		print_signed(sp, w, p, v);
	}

	void print(fmtshape sp, int w, int p, char c, superficial<char>)
	{
		if (sp.facade() != 'i')
			return print_string_ref(sp, w, c);
		else
			return potentially_formattable(sp, w, p, int(c));
	}

	template <typename T>
	void print(fmtshape sp, int w, int p, T s, superficial<charT*>)
	{
		if (p == -1 or sp.facade() != 's')
			print_string_ref(sp, w, s);
		else
			print_string_ref(sp, w, view_type(s, size_t(p)));
	}

	template <typename T>
	auto print(fmtshape sp, int w, int p, T s, superficial<char*>)
	    -> enable_if_t<not std::is_same<superficial_t<T>, charT*>::value>
	{
		if (p == -1 or sp.facade() != 's')
			print_string_ref(sp, w, s);
		else
			print_string_ref(sp, w,
			                 std::string(s, size_t(p)).data());
	}

	template <typename T>
	void potentially_formattable(fmtshape sp, int w, int p, T const& v,
	                             fmtflags fl = {}, bool intp = false)
	{
		fl |= base_flags();
		if (w != -1)
			state().width(w);
		state().precision(p);

		if (has(sp, fmtoptions::sign))
			fl |= os::showpos;
		if (has(sp, fmtoptions::alt))
			fl |= os::showbase | os::showpoint;
		if (has(sp, fmtoptions::left))
			fl |= os::left;
		else if (has(sp, fmtoptions::zero) and not intp and
		         not_infinity_or_NaN(v))
		{
			auto fc = state().fill(STDEX_G(charT, '0'));
			state().flags(fl | os::internal | to_flags(sp));
			state() << v;
			state().fill(fc);
			return;
		}
		else if (has(sp, fmtoptions::right))
			fl |= os::right;

		state().flags(fl | to_flags(sp));
		state() << v;
	}

	template <typename T>
	static bool not_infinity_or_NaN(T const& v)
	{
		return not_infinity_or_NaN(v, std::is_floating_point<T>());
	}

	template <typename T>
	static bool not_infinity_or_NaN(T const& v, std::true_type)
	{
		return std::isfinite(v);
	}

	template <typename T>
	static bool not_infinity_or_NaN(T const& v, std::false_type)
	{
		return true;
	}

	template <typename T>
	_STDEX_always_inline
	void print_string_ref(fmtshape sp, int w, T const& v, fmtflags fl = {})
	{
		fl |= base_flags();
		if (has(sp, fmtoptions::left))
			fl |= os::left;
		state().flags(fl);
		if (w != -1)
			state().width(w);
		state() << v;
	}

	template <typename T>
	auto print_basic_arithmetic(fmtshape sp, int w, int p, T v,
	                            fmtflags fl = {})
	    -> enable_if_t<std::is_floating_point<T>::value>
	{
		potentially_formattable(sp, w, p, v, fl);
	}

	template <typename T>
	auto print_basic_arithmetic(fmtshape sp, int w, int p, T v,
	                            fmtflags fl = {})
	    -> enable_if_t<std::is_integral<T>::value>
	{
		int d;
		charT sign{};
		switch (sp.facade())
		{
		case '\0':
		case 'd':
		case 'i':
			if (std::is_signed<T>() and has(sp, fmtoptions::sign))
				sign = STDEX_G(charT, '+');
			/* fallthrough */
		case 'u':
			d = lexical_width<10>(v);
			break;
		case 'o':
			d = lexical_width<8>(v);
			if (has(sp, fmtoptions::alt))
			{
				d += 1;
				sign = STDEX_G(charT, '0');
			}
			break;
		case 'x':
		case 'X':
			d = lexical_width<16>(v);
			break;
		default:
			return potentially_formattable(sp, w, p, v, fl);
		}

		if (p == 0 and v == 0)
		{
			if (sign)
				return print_string_ref(sp, w, sign);
			else
				return print_string_ref(sp, w, view_type());
		}

		if (p <= d)
			return potentially_formattable(sp, w, p, v, fl,
			                               p != -1);

		std::basic_stringstream<charT, traits> dout;
		state().copyfmt(dout);
		ostream_formatter(dout)
		    .potentially_formattable(sp, 0, p, v, fl, true);
		auto s = dout.str();
		s.insert(s.size() - size_t(d), size_t(p - d),
		         STDEX_G(charT, '0'));
		print_string_ref(sp, w, s);
	}

	template <int Base, typename Int>
	static int lexical_width(Int i)
	{
		if (i == 0)
			return 1;

		int n = 0;
		while (i != 0)
		{
			i /= Base;
			++n;
		}
		return n;
	}

	template <typename T>
	void print_signed(fmtshape sp, int w, int p, T v)
	{
		if (not wants_aligned_sign(sp))
			return print_basic_arithmetic(sp, w, p, v);

		std::basic_stringstream<charT, traits> dout;
		state().copyfmt(dout);
		ostream_formatter(dout)
		    .print_basic_arithmetic(sp, w, p, v, os::showpos);
		auto s = dout.str();
		auto it = std::find(s.begin(), s.end(), STDEX_G(charT, '+'));
		if (it != s.end())
			*it = dout.fill();
		state().write(s.data(), std::streamsize(s.size()));
	}

	template <typename T>
	void print_hexfloat(fmtshape sp, int w, int p, T v)
	{
		char buf[128];
		int n = [=, &buf]
		{
			char fmt[8] = { '%' };
			auto bp = fmt;
			if (has(sp,
			        fmtoptions::sign | fmtoptions::aligned_sign))
				*++bp = '+';
			if (has(sp, fmtoptions::alt))
				*++bp = '#';
			if (p != -1)
			{
				*++bp = '.';
				*++bp = '*';
			}
			if (std::is_same<T, long double>())
				*++bp = 'L';
			*++bp = sp.facade();

			if (p == -1)
				return ::snprintf(buf, sizeof(buf), fmt, v);
			else
				return ::snprintf(buf, sizeof(buf), fmt, p, v);
		}();

		if (!(0 < n and n < int(sizeof(buf))))
			throw std::bad_alloc();

		auto&& punct =
		    std::use_facet<std::numpunct<charT>>(state().getloc());
		auto&& ctype =
		    std::use_facet<std::ctype<charT>>(state().getloc());

		auto off = buf[0] == '+';
		auto dp =
		    std::find(buf, buf + n, *::localeconv()->decimal_point);

		if (has(sp, fmtoptions::zero) and w > n and
		    not_infinity_or_NaN(v))
		{
			std::basic_string<charT, traits> s;
			s.reserve(size_t(w));
			if (off)
				s.push_back(wants_aligned_sign(sp)
				                ? state().fill()
				                : STDEX_G(charT, '+'));
			s.append(size_t(w - n), STDEX_G(charT, '0'));
			s.resize(size_t(w));
			auto wp = s.begin() + w - n + off;
			ctype.widen(buf + off, buf + n, &*wp);
			if (dp != buf + n)
				*(wp + std::distance(buf, dp) - off) =
				    punct.decimal_point();
			this->send(s);
		}
		else
		{
			charT wp[sizeof(buf)];
			ctype.widen(buf, buf + n, wp);
			if (wants_aligned_sign(sp) and off)
				wp[0] = state().fill();
			if (dp != buf + n)
				*(wp + std::distance(buf, dp)) =
				    punct.decimal_point();
			print_string_ref(sp, w, view_type(wp, size_t(n)));
		}
	}

	void print_hexfloat(fmtshape sp, int w, int p, float v)
	{
		print_hexfloat(sp, w, p, double(v));
	}

	fmtflags base_flags()
	{
		return state().flags() & os::unitbuf;
	}

	_STDEX_constexpr static fmtflags to_flags(fmtshape sp)
	{
		switch (sp.facade())
		{
		case 'd':
		case 'u':
			return os::dec;
		case 'o':
			return os::oct;
		case 'x':
			return os::hex;
		case 'X':
			return os::hex | os::uppercase;
		case 'f':
			return os::fixed;
		case 'F':
			return os::fixed | os::uppercase;
		case 'e':
			return os::scientific;
		case 'E':
			return os::scientific | os::uppercase;
		case 'G':
			return os::uppercase;
		case 'a':
			return os::fixed | os::scientific;
		case 'A':
			return os::fixed | os::scientific | os::uppercase;
		case 's':
			return os::boolalpha;
		default:
			return {};
		}
	}

	constexpr static bool wants_aligned_sign(fmtshape sp)
	{
		return has(sp, fmtoptions::aligned_sign) and
		       not has(sp, fmtoptions::sign);
	}

	constexpr static bool has(fmtshape sp, fmtoptions opt)
	{
		return (sp.options() & opt) != fmtoptions::none;
	}
};

#ifndef XFORMAT_HEADER_ONLY

extern template void
ostream_formatter<char>::print_hexfloat(fmtshape, int, int, double);
extern template void
ostream_formatter<char>::print_hexfloat(fmtshape, int, int, long double);
extern template void
ostream_formatter<wchar_t>::print_hexfloat(fmtshape, int, int, double);
extern template void
ostream_formatter<wchar_t>::print_hexfloat(fmtshape, int, int, long double);
extern template void
ostream_formatter<char16_t>::print_hexfloat(fmtshape, int, int, double);
extern template void
ostream_formatter<char16_t>::print_hexfloat(fmtshape, int, int, long double);
extern template void
ostream_formatter<char32_t>::print_hexfloat(fmtshape, int, int, double);
extern template void
ostream_formatter<char32_t>::print_hexfloat(fmtshape, int, int, long double);

#endif
}
