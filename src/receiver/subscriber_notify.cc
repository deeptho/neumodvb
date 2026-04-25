/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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
#include "neumodb/statdb/statdb_extra.h"
#include "receiver/receiver.h"
#include "receiver/scan.h"
#include "receiver/subscriber.h"
#include "util/neumovariant.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include "viewer/wxpy_api.h"
#include <wx/window.h>

namespace py = pybind11;

#ifdef UNSAFE
/*
	Problem: creating a python object in a multithreaded environment
	seems to create possible crashes
	https://github.com/pybind/pybind11/issues/2765
	The code does crash sometimes in pyalloc.

	A less risky but more complex approach is to pass a pointer to a c++ object
	and have the python call back do the conversion

*/

template <typename T> inline static intptr_t make_py_object(T& obj) {
	auto x = py::cast(obj);
	static_assert(sizeof(x) <= sizeof(int64_t));
	x.inc_ref();
	return (intptr_t)x.ptr();
}

template <typename T> void subscriber_t::notify(const T& data) {
	wxCommandEvent event(wxEVT_COMMAND_ENTER);
	auto p = make_py_object(data);
	static_assert(sizeof(p) <= sizeof(long));
	event.SetExtraLong(p);
	wxQueueEvent(window, event.Clone());
}

#else
typedef std::unique_ptr<std::string> string_ptr_t;
typedef std::unique_ptr<signal_info_t> signal_info_ptr_t;
typedef std::unique_ptr<sdt_data_t> sdt_data_ptr_t;
typedef std::unique_ptr<devdb::scan_stats_t> scan_stats_ptr_t;
typedef std::unique_ptr<scan_mux_end_report_t> scan_mux_end_report_ptr_t;
typedef std::unique_ptr<positioner_motion_report_t> positioner_motion_report_ptr_t;
typedef std::unique_ptr<statdb::spectrum_t> spectrum_ptr_t;
typedef std::variant<signal_info_ptr_t, sdt_data_ptr_t, scan_stats_ptr_t, scan_mux_end_report_ptr_t,
										 positioner_motion_report_ptr_t, spectrum_ptr_t, string_ptr_t> notification_ptr_t;

py::object subscriber_t::handle_to_py_object(int64_t handle) {
	auto& ptr = *(notification_ptr_t*)handle;
	auto x = std::visit([](auto& ptr) { return py::cast(std::move(*ptr)); }, ptr);
	delete &ptr;
	return x;
}

template <typename T> inline static intptr_t make_object(const T& obj) {
	auto* x = new notification_ptr_t(std::move(std::make_unique<T>(obj)));
	return (intptr_t)x;
}

//called from receiver thread
template <typename T> void subscriber_t::notify(const T& data) const {
	wxCommandEvent event(wxEVT_COMMAND_ENTER);
	auto p = make_object(data);
	static_assert(sizeof(p) <= sizeof(long));
	event.SetExtraLong(p);
	auto* newevent = event.Clone();
	if(window)
		wxQueueEvent(window, newevent);
}

#endif

template void subscriber_t::notify<std::string>(const std::string&) const;
template void subscriber_t::notify<signal_info_t>(const signal_info_t&) const;
template void subscriber_t::notify<sdt_data_t>(const sdt_data_t&) const;
template void subscriber_t::notify<statdb::spectrum_t>(const statdb::spectrum_t&) const;
template void subscriber_t::notify<scan_mux_end_report_t>(const scan_mux_end_report_t&) const;
template void subscriber_t::notify<positioner_motion_report_t>(const positioner_motion_report_t&) const;
template void subscriber_t::notify<devdb::scan_stats_t>(const devdb::scan_stats_t&) const;

void subscriber_t::notify_signal_info(const signal_info_t& signal_info) const {
	if (this->mpv)
		this->mpv->notify_signal_info(signal_info);
	else if (!(event_flag & int(subscriber_t::event_type_t::SIGNAL_INFO)))
		return;
	this->notify(signal_info);
}

void subscriber_t::notify_scan_progress(const devdb::scan_stats_t& scan_stats) {
	auto match = subscriber_t::event_type_t::SCAN_PROGRESS;
	if(scan_stats_done(scan_stats))
		set_scanning(false);
	if (!(event_flag & int(match)))
		return;
	notify(scan_stats);
}

void subscriber_t::notify_scan_mux_end(const scan_mux_end_report_t& report) {
	if (!(event_flag & int(subscriber_t::event_type_t::SCAN_MUX_END)))
		return;
	notify(report);
}


void subscriber_t::notify_positioner_motion(const positioner_motion_report_t& motion_report) const
{
	if (!(event_flag & int(subscriber_t::event_type_t::POSITIONER_MOTION)))
		return;
	notify(motion_report);
}

void subscriber_t::notify_sdt_actual(const sdt_data_t& sdt_data) const
{
	if (!(event_flag & int(subscriber_t::event_type_t::SDT_ACTUAL)))
		return;
	notify(sdt_data);
}

void subscriber_t::notify_message(const ss::string_& errmsg) {
	if (!(event_flag & int(subscriber_t::event_type_t::ERROR_MSG)))
		return;
	if (this->mpv)
		this->mpv->notify_message(errmsg);
	else {
		auto temp = std::string(errmsg);
		notify(temp);
	}
}

void subscriber_t::notify_spectrum_scan_band_end(const statdb::spectrum_t& spectrum) {
	if (!(event_flag & int(subscriber_t::event_type_t::SPECTRUM_SCAN)))
		return;
	notify(spectrum);
}
