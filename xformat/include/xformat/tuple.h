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

#include <tuple>
#include <array>
#include <functional>
#include <stdexcept>

namespace stdex
{

template <typename R, int Low, int High, int Mid = (Low + High) / 2,
          typename = void>
struct _visit_at;

template <typename R, int Low, int High, int Mid>
struct _visit_at<R, Low, High, Mid, std::enable_if_t<(Low > High)>>
{
	template <typename... T>
	[[noreturn]] static R apply(int, T&&...)
	{
		throw std::out_of_range{ "no such element" };
	}
};

template <typename R, int Mid>
struct _visit_at<R, Mid, Mid, Mid>
{
	template <typename Tuple, typename F>
	static decltype(auto) apply(int n, F&& f, Tuple&& tp)
	{
		return std::forward<F>(f)(
		    std::get<Mid>(std::forward<Tuple>(tp)));
	}
};

template <typename R, int Low, int High, int Mid>
struct _visit_at<R, Low, High, Mid, std::enable_if_t<(Low < High)>>
{
	template <typename... T>
	static decltype(auto) apply(int n, T&&... t)
	{
		if (n < Mid)
			return _visit_at<R, Low, Mid - 1>::apply(
			    n, std::forward<T>(t)...);
		else if (n == Mid)
			return _visit_at<R, Mid, Mid>::apply(
			    n, std::forward<T>(t)...);
		else
			return _visit_at<R, Mid + 1, High>::apply(
			    n, std::forward<T>(t)...);
	}
};

template <typename R = void, typename Tuple, typename F>
inline
decltype(auto) visit_at(int n, F&& f, Tuple&& tp)
{
	constexpr int m = std::tuple_size<std::decay_t<Tuple>>::value;
	return _visit_at<R, 0, m - 1>::apply(n, std::forward<F>(f),
	                                     std::forward<Tuple>(tp));
}

template <typename R = void, typename T, size_t N, typename F>
inline
decltype(auto) visit_at(int n, F&& f, std::array<T, N>& v)
{
	return std::forward<F>(f)(v.at(size_t(n)));
}

template <typename T, typename = void>
struct _param_type
{
	using type = T const&;
};

template <typename T>
struct _param_type<T, std::enable_if_t<std::is_scalar<T>::value>>
{
	using type = T;
};

template <typename T>
using _param_type_t = typename _param_type<T>::type;

template <typename T>
struct _param_type<std::reference_wrapper<T>>
{
	using type = _param_type_t<T>;
};

template <typename T>
using param_type_t = _param_type_t<std::decay_t<T>>;

template <typename... T>
struct _type_list;

template <typename T, typename...>
struct _head
{
	using type = T;
};

template <typename... T>
using _head_t = typename _head<T...>::type;

template <typename T>
struct _ident
{
};

template <typename T, typename... Ts>
struct all_same
    : std::is_same<
          _type_list<_ident<T>, _ident<Ts>...>,
          _type_list<_ident<T>, decltype((void)_ident<Ts>(), _ident<T>())...>>
{
};

template <typename... T>
auto _as_array_of_cref(std::false_type, T&&... v)
{
	using t = param_type_t<_head_t<T...>>;
	return std::array<t, sizeof...(T)>{ { v... } };
}

template <typename... T>
auto _as_array_of_cref(std::true_type, T&&... v)
{
	using t = std::reference_wrapper<
	    std::remove_reference_t<param_type_t<_head_t<T...>>>>;
	return std::array<t, sizeof...(T)>{ { v... } };
}

template <typename... T>
auto _as_tuple_of_cref(std::true_type, T&&... v)
{
	return _as_array_of_cref(
	    std::is_reference<param_type_t<_head_t<T...>>>(),
	    std::forward<T>(v)...);
}

template <typename... T>
auto _as_tuple_of_cref(std::false_type, T&&... v)
{
	return std::tuple<param_type_t<T>...>{ v... };
}

template <typename... T>
auto as_tuple_of_cref(T&&... v)
{
	return _as_tuple_of_cref(all_same<param_type_t<T>...>(),
	                         std::forward<T>(v)...);
}

template <>
inline
auto as_tuple_of_cref<>()
{
	return std::make_tuple();
}

}
