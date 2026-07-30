/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include "receiver/receiver.h"
#include "util/logger.h"
#include <clocale>
#include "wx/dcsvg.h"
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glut.h>
#include <string>
#include <sys/timeb.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>

#include "neumoglcanvas.h"
#include "neumompv.h"

#include "stackstring/stackstring_pybind.h"

namespace nb = nanobind;

void init_threads() {
	static bool called = false;
	if (!called) {
		XInitThreads();
		int argc = 1;
		char* argv[1] = {(char*)"mpv"};
		glutInit(&argc, argv);
		called = true;
	}
}

NB_MODULE(pyneumompv, m) {
	export_ss(m);
	//export_ss_vector(m, chdb::language_code_t);
	m.def("init_threads", &init_threads);
	nb::class_<MpvPlayer>(m, "MpvPlayer")
		.def(nb::new_(&MpvPlayer::make)
			, nb::arg("receiver"), nb::arg("parent_window"), nb::arg("index"))
		.def_prop_ro("glcanvas", &MpvPlayer::get_canvas)
		.def("toggle_overlay", &MpvPlayer::toggle_overlay)
		.def("screenshot", &MpvPlayer::screenshot)
		.def("play_service", &MpvPlayer::play_service, nb::arg("service"))
		.def("play_recording", &MpvPlayer::play_recording, nb::arg("recording"),
				 nb::arg("start_play_time") = milliseconds_t(0))
		.def("jump", &MpvPlayer::jump, nb::arg("seconds"))
		.def("smartjump", &MpvPlayer::smartjump, nb::arg("forward"))
		.def("set_play_direction", &MpvPlayer::set_play_direction, nb::arg("forward"))
		.def("change_playback_speed", &MpvPlayer::change_playback_speed, nb::arg("faster"))
		.def("mpv_command", &MpvPlayer::mpv_command, nb::arg("command"), nb::arg("arg1") = nullptr,
				 nb::arg("arg2") = nullptr)
		.def("stop_play", &MpvPlayer::stop_play)
		.def("stop_play_and_exit", &MpvPlayer::stop_play_and_exit)
		.def("close", &MpvPlayer::close)
		.def("pause", &MpvPlayer::pause)
		.def("audio_languages", &MpvPlayer::audio_languages)
		.def("set_audio_language", &MpvPlayer::set_audio_language)
		.def("subtitle_languages", &MpvPlayer::subtitle_languages)
		.def("change_audio_volume", &MpvPlayer::change_audio_volume)
		.def("set_subtitle_language", &MpvPlayer::set_subtitle_language)
		.def("get_current_audio_language", &MpvPlayer::get_current_audio_language)
		.def("get_current_subtitle_language", &MpvPlayer::get_current_subtitle_language)
		.def("get_current_program_info", &MpvPlayer::get_current_program_info)
		;
};
