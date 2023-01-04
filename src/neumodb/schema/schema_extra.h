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
#include "stackstring/stackstring.h"
#include "neumodb/schema/schema_db.h"

namespace schema {

	template<typename T>
	auto to_str(T& x)
	{
		ss::string<16> s;
		s.sprintf("%p", &x);
		return s;
	}

	//default for missing implementation
	template<typename T>
	inline std::ostream& operator<<(std::ostream& os, const T& t) {
		os << &t;
		return os;
	}

};
