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
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "receiver/mpm.h"
#include "receiver/receiver.h"

#include "../stackstring/stackstring.h"
#include "../stackstring/neumotime.h"



class receiver_t;
class mpm_t;
class playback_mpm_t;

class MpvPlayer;
class MpvPlayer_;
class mpv_subscription_t;
class  MpvGLCanvas;

namespace chdb {
	struct signal_info_t;
};


namespace pybind11 {
	class handle;
	class object;
}



class MpvPlayer : public std::enable_shared_from_this<MpvPlayer>, public player_cb_t {
	friend class MpvGLCanvas;
	friend class mpv_subscription_t;
protected:
	bool mustexit = false;

	bool inited = true;
	MpvPlayer(receiver_t * receiver, MpvPlayer_* parent);
public:
	receiver_t* receiver = nullptr;
	virtual ~MpvPlayer() {}
	static std::shared_ptr<MpvPlayer> make(receiver_t * receiver, pybind11::object parent_window);
	std::string config_dir;
	std::condition_variable cv;
	std::mutex m;
	int frames_to_play =0;
	std::thread::id thread_id;
	std::thread::id run_id;
	std::thread thread_;

	pybind11::handle get_canvas() const;
	ss::vector_<chdb::language_code_t> audio_languages();
	chdb::language_code_t get_current_audio_language();

	ss::vector_<chdb::language_code_t> subtitle_languages();
	chdb::language_code_t get_current_subtitle_language();

	int play_service(const chdb::service_t& service);

	template<typename _mux_t> int play_mux(const _mux_t& mux, bool blindscan);
	int play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time);
	int screenshot();
	int stop_play();
	int stop_play_and_exit();
	int jump(int seconds);
	int set_audio_language(int idx);
	int set_subtitle_language(int id);
	int change_audio_volume(int step);

	void close();
	void signal();
	int pause();
	void mpv_command(const char* cmd, const char* arg2 = nullptr, const char* arg3 = nullptr);
	void notify(const signal_info_t& info);
	void update_playback_info();
	void toggle_overlay();
};
