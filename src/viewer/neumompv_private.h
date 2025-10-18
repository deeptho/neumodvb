/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "util/expiration.h"
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

/*
	state for implementing "smart" jumping  forward and backward during playback.
	Jumping is controlled by the left (junp backward in time) and right arrows (jump forward in time).
	It proceeds in two phases
	1. increasing-duration jumps: multiple presses of one type only (all left or all right)
	   jump by increasingly large amounts
	2. decreasing-duration jumps: this phase is entered when the uer first switches direction (pressing left after
	   a series of right-only presses) or vice versa. The jumps are initially of size jump_interval seconds, where
		 jump_interval is half the duration of the last jump in phase 1.
		 Each time the user reveres direction, jump_interval is approximately halved, allowing the user to
		 zoom in onto the desired position.
	3. After a timeout (no key presses for a few seconds), phase 1 is re-entered and jumps will again be large
 */
class jump_state_t {
	int timeout{2}; //seconds
	std::array<int, 6> forward_jumps{30, 60, 120, 300, 600, 1800};

	system_time_t last_jump_time{};

	enum jump_type_t {
		INCREASING_JUMPS,
		DECREASING_JUMPS,
	};

	jump_type_t jump_type{INCREASING_JUMPS};
	bool last_was_forward{false};
	int fast_jump_idx{0};
	int jump_interval{0};

public:
	jump_state_t() = default;

	int jump (bool forward);
};

class mpv_subscription_t {
	friend class MpvPlayer;
	friend class MpvPlayer_;
	std::mutex& m;
	std::condition_variable& cv;
	int pmt_change_count{0};
	std::shared_ptr<subscriber_t> subscriber;
	bool pending_close = false; //used to speed up channel change
	jump_state_t jump_state;
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
			if(pending_close && on) {
				dtdebugf("Ignoring multiple on calls");
			} else {
				pending_close = on;
				if(on && mpm.get())
					mpm->force_abort();
			}
		}
		cv.notify_all();
	}
	void open();

	mpv_subscription_t(receiver_t* receiver_, MpvPlayer_* mpv_player_);
	~mpv_subscription_t();

	void play_service(const chdb::service_t& service);
	int play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time);
	int stop_play();
	int jump(int seconds, system_time_t play_time);
	int smartjump(bool forward);

	int set_audio_language(int idx);
	void on_language_change(const chdb::language_code_t& lang, int idx, bool for_subtitles);

	int set_subtitle_language(int idx);
	void on_subtitle_language_change(const chdb::language_code_t& lang);

	void close(bool unsubscribe);
	int64_t wait_for_close();
	inline int64_t move_to_bytepos(int64_t bytepos) {
		return this->mpm->move_to_bytepos(bytepos);
	}
};

struct trick_play_t {
	system_time_t start_time;
	double time_pos;
	bool reverse_playing {false};
	bool fast_forwarding {false};
	double fast_forwarding_time_pos_limit{0.0};
	int playback_speed_index{0};
	bool paused{false};
	inline system_time_t get_play_time() const {
		return start_time + std::chrono::duration<int64_t>((int64_t)time_pos);
	}
};

class MpvPlayer_ : public MpvPlayer {
	friend class MpvGLCanvas;
	friend class mpv_subscription_t;
public:
	using tp_t = safe::Safe<trick_play_t>;
	tp_t trick_play;

	expiration_t volume_expiration;
	int volume{100}; //current audio volume
	int idx{0}; //to indec audio_volumes
	int valid_frames{0};
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
	inline void reset_valid_frames() {
		std::scoped_lock lck(subscription.m);
		valid_frames =0;
	}
	void handle_mpv_event(mpv_event& event);

	void mpv_draw(int w, int h);
	bool create();
	void destroy();
	int screenshot();
	int set_play_direction(bool forward);
	int set_playback_speed(double speed);
	int change_playback_speed(bool faster);
	void mpv_command(const char* cmd_, const char* arg2, const char* arg3);
	int play_recording(const recdb::rec_t& rec_, milliseconds_t start_play_time);
	int set_audio_language(int id);
	int set_subtitle_language(int id);
	int change_audio_volume(int step);

	int play_service(const chdb::service_t& service);
	int jump(int seconds);
	int stop_play();
	int pause();
	int run();
	void get_audio_volume();
	void save_audio_volume_async();
#if 0
	void repaint();
#endif
	void make_canvas(py::object frame_);
	void notify_signal_info(const signal_info_t& info);
	void notify_message(const ss::string_& msg);
	void update_playback_info();

	inline system_time_t get_play_time() const {
		return trick_play.readAccess()->get_play_time();
	}

	MpvPlayer_(receiver_t* receiver);
	virtual ~MpvPlayer_();
};
