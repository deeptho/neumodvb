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
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "receiver/mpm.h"

#include "../stackstring/stackstring.h"
#include "../stackstring/neumotime.h"



class receiver_t;
class mpm_t;
class playback_mpm_t;

class MpvPlayer;
class MpvPlayer_;
class subscription_t;
class  MpvGLCanvas;

namespace chdb {
	struct signal_info_t;
};


namespace pybind11 {
	class handle;
	class object;
}

inline void none() {}

class subscription_t {
	friend class MpvPlayer;
	friend class MpvPlayer_;
	bool pending_close = false; //used to speed up channel change
	std::mutex& m;
	std::condition_variable& cv;
	int pmt_change_count{0};
public:
	std::atomic<bool> show_overlay{false};
	int seqno =0;
	bool is_playing() const {
		return subscription_id >=0;
	}
	std::function<void()> next_op = none; //callback run on close
private:
	receiver_t* receiver = nullptr;
	MpvPlayer_* mpv_player = nullptr;
	std::unique_ptr<playback_mpm_t> mpm;
	//@todo: merge the following
	//active_service_t* active_service = nullptr;
	//active_playback_t* active_playback = nullptr;
	ss::string<128> filepath;
	int subscription_id =-1;
public:
	int64_t read_data(char*buffer, uint64_t nbytes);
	void close_fn(); /*called when mpv player thinks it closes the file, but this "fake" close
										 is also used to jump back/forward in stream*/
	void set_pending_close(bool on) {
		{
			std::scoped_lock lck(m);
			pending_close = on;
			if(on && mpm.get())
				mpm->force_abort();
		}
		cv.notify_all();
	}
	void open();

	subscription_t(receiver_t* receiver_, MpvPlayer_* mpv_player_);
	~subscription_t();

	int play_service(const chdb::service_t& service);

	template<typename _mux_t> int play_mux(const _mux_t& mux, bool blindscan);
	int play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time);
	int stop_play();
	int jump(int seconds);

	int set_audio_language(int idx);
	void on_audio_language_change(const chdb::language_code_t& lang, int id);

	int set_subtitle_language(int idx);
	void on_subtitle_language_change(const chdb::language_code_t& lang);

	void close();
	int64_t wait_for_close();
};

class MpvPlayer : public std::enable_shared_from_this<MpvPlayer>, public player_cb_t {
	friend class MpvGLCanvas;
	friend class subscription_t;
protected:
	receiver_t* receiver = nullptr;
	bool mustexit = false;

	bool inited = true;
	MpvPlayer(receiver_t * receiver, MpvPlayer_* parent);
public:
	virtual ~MpvPlayer() {}
	static std::shared_ptr<MpvPlayer> make(receiver_t * receiver);

	subscription_t subscription;
	std::string config_dir;
	std::condition_variable cv;
	std::mutex m;
	int frames_to_play =0;
	std::thread::id thread_id;
	std::thread::id run_id;
	std::thread thread_;


	pybind11::handle make_canvas(pybind11::object);

	ss::vector_<chdb::language_code_t> audio_languages();
	chdb::language_code_t get_current_audio_language();

	ss::vector_<chdb::language_code_t> subtitle_languages();
	chdb::language_code_t get_current_subtitle_language();

	int play_service(const chdb::service_t& service);
	template<typename _mux_t> int play_mux(const _mux_t& mux, bool blindscan);
	int play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time);
	int play_file(const char* name);
	int stop_play();
	int jump(int seconds);
	int set_audio_language(int idx);
	int set_subtitle_language(int id);
	int change_audio_volume(int step);

	void close();
	void signal();
	int pause();
	void mpv_command(const char* cmd, const char* arg2 = nullptr, const char* arg3 = nullptr);
	void notify(const chdb::signal_info_t& info);
	void update_playback_info();
	void toggle_overlay() {
		subscription.show_overlay = ! 	subscription.show_overlay;
	}
};
