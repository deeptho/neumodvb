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
#include "stackstring.h"
#include "receiver/subscriber.h"
#include "receiver/devmanager.h"
#include "neumoglcanvas.h"
#include "neumompv.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include <mpv/render_gl.h>

namespace py = pybind11;

inline void none() {}

class receiver_t;
class MpvPlayer_;
class playback_mpm_t;

class mpv_subscription_t {
	friend class MpvPlayer;
	friend class MpvPlayer_;
	bool pending_close = false; //used to speed up channel change
	std::mutex& m;
	std::condition_variable& cv;
	int pmt_change_count{0};
	std::shared_ptr<subscriber_t> subscriber;
public:
	std::atomic<bool> show_osd{false};
	std::atomic<bool> show_radiobg{false};
	int seqno =0;
	bool is_playing() const {
		return (int) subscriber->get_subscription_id() >=0;
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
	//subscription_id_t subscription_id =-1;
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

	mpv_subscription_t(receiver_t* receiver_, MpvPlayer_* mpv_player_);
	~mpv_subscription_t();

	void play_service(const chdb::service_t& service);
	template<typename _mux_t> int play_mux(const _mux_t& mux, bool blindscan);
	int play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time);
	int stop_play();
	int jump(int seconds);

	int set_audio_language(int idx);
	void on_audio_language_change(const chdb::language_code_t& lang, int id);

	int set_subtitle_language(int idx);
	void on_subtitle_language_change(const chdb::language_code_t& lang);

	void close(bool unsubscribe);
	int64_t wait_for_close();
};


class MpvPlayer_ : public MpvPlayer {
	friend class MpvGLCanvas;
	friend class mpv_subscription_t;

public:
	MpvGLCanvas* gl_canvas;
	mpv_handle* mpv = nullptr;
	mpv_subscription_t subscription;
	bool has_been_destroyed = false;
	inline void wait_for_destroy() {
		std::unique_lock<std::mutex> lk(m);
		cv.wait(lk, [this] { return has_been_destroyed; });
		assert(has_been_destroyed);
	}

	mpv_render_context* mpv_gl = nullptr;

	void on_mpv_wakeup_event();

	void handle_mpv_event(mpv_event& event);

	void mpv_draw(int w, int h);
	bool create();
	void destroy();
	int screenshot();
	void mpv_command(const char* cmd_, const char* arg2, const char* arg3);
	int play_recording(const recdb::rec_t& rec_, milliseconds_t start_play_time);
	int set_audio_language(int id);
	int set_subtitle_language(int id);
	int change_audio_volume(int step);

	int play_service(const chdb::service_t& service);

	template <typename _mux_t> int play_mux(const _mux_t& mux, bool blindscan);
	int jump(int seconds);
	int stop_play();
	int pause();
	int run();
#if 0
	void repaint();
#endif
	void make_canvas(py::object frame_);
	void notify(const signal_info_t& info);
	void update_playback_info();

	MpvPlayer_(receiver_t* receiver);
	virtual ~MpvPlayer_();
};
