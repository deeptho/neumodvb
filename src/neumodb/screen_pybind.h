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

#include <pybind11/pybind11.h>
#include <stdio.h>
#include "screen.h"

namespace py = pybind11;

template<typename record_t>
void export_screen_(py::module &m, const char* pytypename)
{
	static int called = false;
	if(called)
		return;
	called =true;
	py::class_<screen_t<record_t>>(m, pytypename)
						 .def("__repr__", [pytypename](const screen_t<record_t>& s) {
							 return std::string(fmt::format("screen_t<{}>", pytypename));
						 });
};

#undef xstr
#undef str
#define xstr(a) str(a)
#define str(a) #a

#define export_screen(m, pytypename) \
	export_screen_<pytypename>(m, str(pytypename) "_screen" )
