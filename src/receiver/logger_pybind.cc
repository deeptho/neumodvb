/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
 *
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
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/receiver.h"
#include "receiver/scan.h"
#include "subscriber_pybind.h"
#include "viewer/wxpy_api.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include <sip.h>
#include <stdio.h>
#include <wx/window.h>

namespace py = pybind11;

static void logger_log(bool is_error, const char* file, const char* func, int line, const char* message) {
	if (LOG4CXX_UNLIKELY(logger->isDebugEnabled())) {
		::log4cxx::helpers::MessageBuffer oss_;
		logger->forcedLog(
			is_error
			? ::log4cxx::Level::getError()
			: ::log4cxx::Level::getDebug(),
			oss_.str(oss_ << message),
#ifdef LOG4CXX_VERSION_MAJOR
			::log4cxx::spi::LocationInfo(file, file, func, 	line));
#else
		::log4cxx::spi::LocationInfo(file, func, 	line));
#endif
	}
}

void export_logger(py::module& m) {
	m.def("log", &logger_log);
}
