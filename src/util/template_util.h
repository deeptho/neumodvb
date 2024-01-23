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
#include<type_traits>


#define has_member(v, m)												\
	constexpr(requires(decltype(v) a){a. m;})

#define get_member(v, m, def)																			\
	[](auto& x) {																										\
	if has_member(x, m)  {																					\
		return x.m; } else  {																					\
		return def;																										\
}																																	\
	}(v)																														\

#define set_member(v, m, val)																					\
	[&](auto& x)  {																											\
		if has_member(x, m)  {																						\
				x.m =val;																											\
			}																																\
	}(v)																																\



template <typename T1, typename T2>
inline constexpr bool is_same_type_v =  std::is_same_v<typename std::remove_cvref<T1>::type, typename std::remove_cvref<T2>::type>;
