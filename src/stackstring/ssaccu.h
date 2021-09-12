/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include "stackstring.h"
#include <ostream>


namespace ss {

	struct accu_t : std::basic_ostream<char>, std::basic_streambuf<char>
	{
		ss::string_& s;
		accu_t(ss::string_& s_) : std::basic_ostream<char>(this), s(s_) {
		}


		accu_t(ss::accu_t& other) : std::basic_ostream<char>(std::move(other)), s(other.s) {
		}


		int overflow(int c) {
			s.push_back(c);
			return 0;
		}
	};


	template<typename T>
	inline accu_t string_::operator<<(const T& x) {
		accu_t a(*this);
		a << x;
		return a;
	}



};
