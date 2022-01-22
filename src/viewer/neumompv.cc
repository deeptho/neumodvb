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

#include "receiver/adapter.h"
#include "receiver/receiver.h"
#include "receiver/reservation.h"
#include "util/logger.h"
#include <clocale>
#include <pybind11/pybind11.h>
#include "wx/dcsvg.h"
#include <GL/glut.h>
#include <string>
#include <sys/timeb.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>

#include "neumotime.h"

#include "receiver/active_playback.h"
#include "receiver/active_service.h"

#include "neumoglcanvas.h"
#include "neumompv.h"

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <mpv/render_gl.h>

namespace py = pybind11;

class MpvPlayer_ : public MpvPlayer {
	friend class MpvGLCanvas;
	friend class subscription_t;

public:
	MpvGLCanvas* gl_canvas;
	mpv_handle* mpv = nullptr;

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
	int play_file(const char* name);
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
	void repaint();

	py::handle make_canvas(py::object frame_);
	void notify(const chdb::signal_info_t& info);
	void update_playback_info();

	MpvPlayer_(receiver_t* receiver);
	virtual ~MpvPlayer_();
};

extern void test_paint(cairo_t* cr);

void save_debug(uint8_t* buffer, off_t size) {
	if (size > 0) {
		static FILE* fpdebug = nullptr;
		if (!fpdebug) {
			fpdebug = fopen("/tmp/y.ts", "w");
		}
		fwrite(buffer, size, 1, fpdebug);
		// fflush(fpdebug);
	}
}

wxBEGIN_EVENT_TABLE(MpvGLCanvas, wxGLCanvas)
//EVT_SIZE(MpvGLCanvas::OnSize)
EVT_WINDOW_CREATE(MpvGLCanvas::OnWindowCreate)
EVT_PAINT(MpvGLCanvas::OnPaint)
EVT_TIMER(wxID_ANY, MpvGLCanvas::OnTimer)
EVT_ERASE_BACKGROUND(MpvGLCanvas::OnErase)
wxEND_EVENT_TABLE()

MpvGLCanvas::MpvGLCanvas(wxWindow *parent, std::shared_ptr<MpvPlayer_> player)
: wxGLCanvas(parent, wxID_ANY,  NULL, wxDefaultPosition, wxDefaultSize
						 , 0, wxString("GLCanvas"), wxNullPalette)
	, to_prevent_destruction(player), mpv_player(player.get())
	, overlay(config_path / player->receiver->options.readAccess()->gui)
{
	SetBackgroundStyle(wxBG_STYLE_CUSTOM);
	glContext = new wxGLContext(this);
	SetClientSize(parent->GetSize());
	tm = std::make_unique<wxTimer>(this);
}

void InitializeTexture(GLuint& g_texture) {
	GLenum err;
	glGenTextures(1, &g_texture); // generate 1 texture name
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}
	glBindTexture(GL_TEXTURE_2D, g_texture); // make the texture 2d
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}
}

MpvGLCanvas::~MpvGLCanvas() {
	if (mpv_player) {
		mpv_player->destroy();
		mpv_player->wait_for_destroy();
		mpv_player = nullptr;
	}

	OnRender = nullptr;
	OnSwapBuffers = nullptr;
	if (glContext)
		delete glContext;
	glContext = nullptr;
}

bool MpvGLCanvas::SetCurrent() const // MPV_CALLBACK
{
	static std::mutex m;
	std::scoped_lock<std::mutex> lck(m);
	if (!glContext)
		return false;
	return wxGLCanvas::SetCurrent(*glContext);
}

bool MpvGLCanvas::SwapBuffers() {
	bool result = wxGLCanvas::SwapBuffers();
	if (OnSwapBuffers)
		OnSwapBuffers(this);
	return result;
}


void MpvGLCanvas::OnSize(wxSizeEvent& evt) {
	std::lock_guard<std::mutex> lk(mpv_player->m);
	Update();
}

void MpvGLCanvas::OnErase(wxEraseEvent& event) {
	// do nothing to skip erase
}

void MpvGLCanvas::Render() {// MPV_CALLBACK
	DoRender();
}

wxDEFINE_EVENT(WX_MPV_WAKEUP, wxThreadEvent);
wxDEFINE_EVENT(WX_MPV_REDRAW, wxThreadEvent);

void MpvGLCanvas::OnWindowCreate(wxWindowCreateEvent& evt) {
	dtdebugx("CREATE %p", this);
	if (mpv_player->create()) {
		Bind(WX_MPV_WAKEUP, &MpvGLCanvas::OnMpvWakeupEvent, this);
	}
	evt.Skip();
}

void MpvGLCanvas::OnPaint(wxPaintEvent& evt) {
	if (!inited) {
		this->OnRender = std::bind(&MpvPlayer_::mpv_draw, mpv_player,
                                  std::placeholders::_2, std::placeholders::_3);
		inited = true;
	}
	mpv_player->signal();
}

void MpvGLCanvas::OnTimer(wxTimerEvent& evt) {
	std::lock_guard<std::mutex> lk(mpv_player->m);
	evt.Skip();
}

void MpvGLCanvas::OnMpvWakeupEvent(wxThreadEvent&) {
	std::lock_guard<std::mutex> lk(mpv_player->m);
	if (mpv_player)
		mpv_player->on_mpv_wakeup_event();
}

void MpvGLCanvas::OnMpvRedrawEvent(wxThreadEvent&) // MPV_CALLBACK
{
	assert(0);
	this->Render();
}

void MpvGLCanvas::DoRender() // MPV_CALLBACK
{
	SetCurrent();
	if (!playing_ok)
		SwapBuffers();
	auto s = GetSize();
	if (OnRender) {

		static auto olds = s;
		if (s.x != olds.x || s.y != olds.y) {
			olds = s;
		}

		OnRender(this, s.x, s.y);
		playing_ok = true;
	} else {
		dterror("ONRENDER NOT READY");
	}

	static int called = 0;
	if (called++ == 100)
		called = 0;
	// glClearColor(0.0, 0.0, 0.0, 0.0);
	// glClear(GL_COLOR_BUFFER_BIT);
	SetCurrent();
	// clear_window();

	int width = s.x;
	int height = s.y;
	if (mpv_player->subscription.show_overlay) {
		overlay.render(width, height);
	}

	SwapBuffers();
}

void* MpvGLCanvas::GetProcAddress(const char* name) {
	SetCurrent();
	return (void*)::glXGetProcAddressARB((const GLubyte*)name);
}

void MpvGLCanvas::MpvDestroy() {
	Unbind(WX_MPV_WAKEUP, &MpvGLCanvas::OnMpvWakeupEvent, this);
	// Unbind(WX_MPV_REDRAW, &MpvGLCanvas::OnMpvRedrawEvent, this);
	dtdebugx("MpvDestroy %p mpv_player set to null\n", this);
	if (mpv_player)
		mpv_player->destroy();
	mpv_player = nullptr;
}

struct file_t;

static int64_t size_fn(void* cookie) {
	return MPV_ERROR_UNSUPPORTED;
}
/**
 * Read callback used to implement a custom stream. The semantics of the
 * callback match read(2) in blocking mode. Short reads are allowed (you can
 * return less bytes than requested, and libmpv will retry reading the rest
 * with a nother call). If no data can be immediately read, the callback must
 * block until there is new data. A return of 0 will be interpreted as final
 * EOF, although libmpv might retry the read, or seek to a different position.
 */
static int64_t read_fn(void* cookie, char* buf, uint64_t nbytes) {
	auto* player = (MpvPlayer*)cookie;
	{
		auto ret = player->subscription.read_data(buf, nbytes);
		return ret < 0 ? 0 : ret;
	}
}

void subscription_t::close_fn() {
	auto get = [this] {
		std::scoped_lock lck(m);
		auto ret = next_op;
		next_op = none;
		return ret;
	};
	auto op = get();
	op();
}

static void close_fn(void* cookie) {
	auto* player = (MpvPlayer*)cookie;
	dtdebugx("MPV fake close: player=%p", player);
	player->subscription.close_fn();
	//deliberately don't do anything
	//player->subscription.close();
}

void subscription_t::open() {

	auto get = [this] {
		std::scoped_lock lck(m);
		auto ret = next_op;
		next_op = none;
		return ret;
	};
	dttime_init();
	auto op = get();
	dttime(100);
	op();
	dttime(1000);
}

/*
	called from python gui
	returns -1 on error
*/
int subscription_t::set_audio_language(int idx) {
	if (!mpm) {
		dtdebug("No active playvack");
		return -1;
	}
	return mpm->set_audio_language(idx);
}

/*
	called from python gui
	returns -1 on error
*/
int subscription_t::set_subtitle_language(int idx) {
	if (!mpm) {
		dtdebug("No active playvack");
		return -1;
	}
	return mpm->set_subtitle_language(idx);
}

/*
	Note that the event loop is detached from the actual player. Not calling
	* mpv_wait_event() will not stop playback. It will eventually congest the
	* event queue of your API handle, though.
	*
	The client API is generally fully thread-safe, unless otherwise noted.
	* Currently, there is no real advantage in using more than 1 thread to access
	* the client API, since everything is serialized through a single lock in the
	* playback core.
	*
	*/

/*
	Called when pmt changes and the index of the current audio stream should be changed to
	match what the user wants
	This function is thread safe
*/
void subscription_t::on_audio_language_change(const chdb::language_code_t& lang, int id) {
	ss::string<16> arg;
	// id=2;
	arg.sprintf("%d", id + 1);
	if (id < 0) {
		dterrorx("setting BAD audio language (MPV ONLY) to %d", id);
		return;
	}
	dtdebugx("setting audio language (MPV ONLY) to %d %s", id, chdb::lang_name(lang));

	if (pmt_change_count++) {
		if (mpv_set_property_string(mpv_player->mpv, "demuxer-lavf-o", "merge_pmt_versions=1") < 0)
			dterrorx("Failed  language XXX");
	}
	if (mpv_set_property_string(mpv_player->mpv, "aid", arg.c_str()) < 0)
		dterrorx("Failed setting audio language %d", id);
}

static int open_fn(void* user_data, char* uri, mpv_stream_cb_info* info) {
	auto* player = (MpvPlayer*)user_data;
	int seqno;
	log4cxx_store_threadname();
	dtdebugx("OPEN_FN");
	player->subscription.set_pending_close(false);
	sscanf(uri, "neumo://%p/%d", &player, &seqno);
	dtdebugx("MPV open: player=%p", player);
	dttime_init();
	info->cookie = player;
	info->read_fn = ::read_fn;
	info->size_fn = ::size_fn;
	info->close_fn = ::close_fn;
	player->subscription.open();
	dttime(100);

	return info->cookie ? 0 : MPV_ERROR_LOADING_FAILED;
}

MpvPlayer::MpvPlayer(receiver_t * receiver, MpvPlayer_* mpv)
	: receiver(receiver)
	, subscription(receiver, mpv)
	, config_dir(receiver->options.readAccess()->mpvconfig.c_str()) {
}

std::shared_ptr<MpvPlayer> MpvPlayer::make(receiver_t* receiver) {
	// make_shared does not work with private constructor

	auto ret = std::shared_ptr<MpvPlayer_>(new MpvPlayer_(receiver));

	receiver->active_mpvs.writeAccess()->insert({ret.get(), ret});

	return ret;
}

MpvPlayer_::MpvPlayer_(receiver_t* receiver) : MpvPlayer(receiver, dynamic_cast<MpvPlayer_*>(this)) {}

MpvPlayer_::~MpvPlayer_() {
	if (!has_been_destroyed) {
		dterror("MpvPlayer destroyed without calling close");
	}
}

void MpvGLCanvas::clear_window() {
	glClearColor(1.0, 0., 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

bool MpvPlayer_::create() {
	if (mpv)
		return false;
	dtdebugx("Creating mpv %p", this);
	setlocale(LC_NUMERIC, "C");
	// MpvDestroy();
	mpv = mpv_create();
	if (!mpv) {
		dterror("failed to create mpv instance");
		assert(0);
	}

	if (mpv_set_property_string(mpv, "config-dir", config_dir.c_str()) < 0) {
		dterror("failed to register config-dir");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "config", "yes") < 0) {
		dterror("failed to activate config reading");
		assert(0);
	}
	if (mpv_initialize(mpv) < 0) {
		dterror("failed to initialize mpv");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "profile", "neumo") < 0) {
		dterror("failed to register mpv neumo profile");
		assert(0);
	}
	if (mpv_stream_cb_add_ro(mpv, "neumo", (void*)this, open_fn) < 0) {
		dterror("failed to register mpv protocol");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "vo", "opengl-cb") < 0) {
		dterror("failed to set mpv VO");
		assert(0);
	}
#ifdef BUG
	if (mpv_set_property_string(mpv, "hwdec", "auto") < 0) {
		dterror("failed to set mpv VO");
		assert(0);
	}
#endif

	auto get_proc_address = [](void* ctx, const char* name) {
		auto glCanvas = reinterpret_cast<MpvGLCanvas*>(ctx);
		return glCanvas ? glCanvas->GetProcAddress(name) : nullptr;
	};
	mpv_opengl_init_params gl_init_params{get_proc_address, gl_canvas, nullptr};
	mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
		{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
		{MPV_RENDER_PARAM_INVALID, nullptr}};
	if (mpv_render_context_create(&mpv_gl, mpv, params) < 0) {
		dterror("failed to initialize mpv GL context");
		assert(0);
	}
	if (!mpv_gl) {
		dterror("failed to create mpv render context");
		assert(0);
	}

	mpv_render_context_set_update_callback(
		mpv_gl,

		[](void* data) {
			auto* canvas = reinterpret_cast<MpvGLCanvas*>(data);
			if (canvas) {
				canvas->mpv_player->signal();
			}
		},
		reinterpret_cast<void*>(gl_canvas));

	auto task = std::packaged_task<int(void)>(std::bind(&MpvPlayer_::run, this));
	thread_ = std::thread(std::move(task));
	return true;
}

void MpvPlayer_::handle_mpv_event(mpv_event& event) {

	switch (event.event_id) {
	case MPV_EVENT_VIDEO_RECONFIG:
		// something like --autofit-larger=95%
#ifdef NOTNEEDED
		Autofit(95, true, false);
#endif
		break;
	case MPV_EVENT_PROPERTY_CHANGE: {
		mpv_event_property* prop = (mpv_event_property*)event.data;
		if (strcmp(prop->name, "media-title") == 0) {
			char* data = nullptr;
			if (mpv_get_property(mpv, prop->name, MPV_FORMAT_OSD_STRING, &data) < 0) {
				// SetTitle("mpv");
			} else {
				mpv_free(data);
			}
		}
		break;
	}
	case MPV_EVENT_SHUTDOWN:
		gl_canvas->MpvDestroy();
		break;
	default:
		break;
	}
}

void MpvPlayer_::on_mpv_wakeup_event() {
	while (mpv) {
		mpv_event* e = mpv_wait_event(mpv, 0);
		if (e->event_id == MPV_EVENT_NONE)
			break;
		handle_mpv_event(*e);
	}
}

void MpvPlayer_::mpv_draw(int w, int h) {
	if (mpv_gl) {
		mpv_opengl_fbo mpfbo{0, w, h, 0};
		int flip_y{1};

		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo}, {MPV_RENDER_PARAM_FLIP_Y, &flip_y}, {MPV_RENDER_PARAM_INVALID, nullptr}};
		// See render_gl.h on what OpenGL environment mpv expects, and
		// other API details.
		mpv_render_context_render(mpv_gl, params);
	}
}

template <typename T> py::handle wxCast(T* src) {
	wxASSERT(src);

	// As always, first grab the GIL
	wxPyBlock_t blocked = wxPyBeginBlockThreads();
	wxString typeName(src->GetClassInfo()->GetClassName());
	PyObject* obj = wxPyConstructObject(src, typeName, false);
	// Finally, after all Python stuff is done, release the GIL
	wxPyEndBlockThreads(blocked);

	wxASSERT(obj != nullptr);
	return obj;
}

template <typename T> T* wxLoad(py::handle src, const wxString& inTypeName) {
	/* Extract PyObject from handle */
	PyObject* source = src.ptr();

	T* obj = nullptr;

	bool success = wxPyConvertWrappedPtr(source, (void**)&obj, inTypeName);
	wxASSERT_MSG(success, _T("Returned object was not a ") + inTypeName);

	return obj;
}

/*
	create an mpvglcanvas and return t as a glcanvas;
	this requires "import wx.glcanvas" in the python code
*/
py::handle MpvPlayer_::make_canvas(py::object frame_) {
	thread_id = std::this_thread::get_id();
	auto* frame = wxLoad<wxWindow>(frame_, "wxWindow");
	auto ptr = std::static_pointer_cast<MpvPlayer_>(shared_from_this());
	this->gl_canvas = new MpvGLCanvas(frame, ptr);
	return wxCast(this->gl_canvas);
}

py::handle MpvPlayer::make_canvas(py::object frame_) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->make_canvas(frame_);
}

int MpvPlayer_::play_file(const char* name) {
	subscription.filepath = name;
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}
	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	return 0;
}

int MpvPlayer::play_file(const char* name) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_file(name);
}

void MpvPlayer_::mpv_command(const char* cmd_, const char* arg2, const char* arg3) {
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return;
	}
	const char* cmd[] = {cmd_, arg2, arg3, nullptr};
	::mpv_command(mpv, cmd);
	return;
}
void MpvPlayer::mpv_command(const char* cmd_, const char* arg2, const char* arg3) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->mpv_command(cmd_, arg2, arg3);
}

int MpvPlayer_::change_audio_volume(int step) {
	ss::string<16> arg;
	arg.sprintf("%d", step);
	dtdebugx("adjusting audio volume by %d", step);
	const char* cmd[] = {"add", "volume", arg.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	return 0;
}

int MpvPlayer_::set_audio_language(int id) {
	ss::string<16> arg;
	arg.sprintf("%d", id + 1);
	dtdebugx("setting audio language to %d", id);
	if (subscription.set_audio_language(id) >= 0) {
		if (mpv_set_property_string(mpv, "aid", arg.c_str()) < 0)
			dterrorx("Failed setting audio language %d", id);
	} else {
		dterrorx("Failed setting audio language %d", id);
	}
	return 1;
}

int MpvPlayer::set_audio_language(int id) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->set_audio_language(id);
}

int MpvPlayer::change_audio_volume(int step) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->change_audio_volume(step);
}

int MpvPlayer_::set_subtitle_language(int id) {
	ss::string<16> arg;
	arg.sprintf("%d", id + 1);
	dtdebugx("setting subtitle language to %d", id);
	if (subscription.set_subtitle_language(id) >= 0) {
		if (mpv_set_property_string(mpv, "sid", arg.c_str()) < 0)
			dterrorx("Failed setting subtitle language %d", id);
	} else {
		dterrorx("Failed setting subtitle language %d", id);
	}
	return 1;
}

int MpvPlayer::set_subtitle_language(int id) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->set_subtitle_language(id);
}

int subscription_t::play_service(const chdb::service_t& service) {
	log4cxx_store_threadname();
	dtdebugx("PLAY SUBSCRIPTION (service)");
	if (is_playing()) {
		dtdebugx("PLAY SUBSCRIPTION (service) close mpm");
		this->close();
	}
	int ret;
	mpm = receiver->subscribe_service(service, subscription_id);
	ret = mpm.get() ? mpm->subscription_id : -1;
	if (ret >= 0) {
		dtdebugx("PLAY SUBSCRIPTION (service): subscribed=%d", subscription_id);
	} else {
		dtdebugx("PLAY SUBSCRIPTION (service): subscription failed");
	}

	if (ret >= 0) {
		assert(ret == subscription_id || subscription_id < 0);
		subscription_id = ret;
		mpm->register_audio_changed_callback(subscription_id,
																				 [this](auto lang, auto pos) { this->on_audio_language_change(lang, pos); });
		// mpm.init(active_service->mpm);
		dtdebugx("PLAY SUBSCRIPTION (service): mpm init done");
		milliseconds_t start_play_time{0};
		if (mpm->move_to_time(start_play_time) < 0) {
			dtdebug("PLAY SUBSCRIPTION (service): aborting");
			this->close();
			if (subscription_id >= 0) {
				receiver->unsubscribe(subscription_id);
				subscription_id = -1;
			}
			return 0;
		}
		dtdebug("PLAY SUBSCRIPTION (service): mpm move to start_play_time done: " << start_play_time);
	} else {
		if (subscription_id >= 0) {
			// receiver->unsubscribe(subscription_id); already done
			subscription_id = -1;
		}
	}
	return 0;
}

template <typename _mux_t> int subscription_t::play_mux(const _mux_t& mux, bool blindscan) {
	dtdebugx("PLAY SUBSCRIPTION (mux)");
	if (is_playing()) {
		this->close();
	}
	auto ret = receiver->subscribe_mux(mux, blindscan, subscription_id);
	if (ret >= 0) {
		assert(ret == subscription_id || subscription_id < 0);
		subscription_id = ret;
	} else {
		if (subscription_id >= 0) {
			// receiver->unsubscribe(subscription_id); //already don
			//@todo: send a message asynchronously to the gui that subscription has failed
			subscription_id = -1;
		}
	}
	return subscription_id;
}

int MpvPlayer_::play_service(const chdb::service_t& service) {
	// retune request
	log4cxx_store_threadname();
	auto op = [this, service]() {
		// service is captured by copy
		subscription.play_service(service);
	};
	{
		// lock must be placed after lambda
		dttime_init();
		std::scoped_lock lck(subscription.m);
		dttime(100);
		dtdebugx("FORCE ABORT before tuning to %s", service.name.c_str());
		if (subscription.mpm)
			subscription.mpm->force_abort();
		dttime(100);
		subscription.next_op = op;
		dttime(100);
		// will be run by the first open_fn or close_fn call
	}

	subscription.filepath.clear();

	// we need to fake a different file each time, hence the seqno
	subscription.filepath.sprintf("neumo://%p/%d", this, subscription.seqno++);
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}
	this->subscription.set_pending_close(true);
	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	dtdebugx("PLAY SUBSCRIPTION %p STARTED", this);
	return 0;
}

int MpvPlayer::play_service(const chdb::service_t& service) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_service(service);
}

template <typename _mux_t> int MpvPlayer_::play_mux(const _mux_t& mux, bool blindscan) {
	log4cxx_store_threadname();
	// retune request
	auto op = [this, blindscan, mux]() {
		// mux is captured by copy
		subscription.play_mux(mux, blindscan);
	};
	{
		// lock must be placed after lambda
		std::scoped_lock lck(subscription.m);
		subscription.next_op = op;
	}
	// will be run by the first open_fn or close_fn call

	subscription.filepath.clear();

	// we need to fake a different file each time, hence the seqno
	subscription.filepath.sprintf("neumo://%p/%d", this, subscription.seqno++);
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}
	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	dtdebugx("PLAY SUBSCRIPTION %p STARTED", this);
	return 0;
}

template <typename _mux_t> int MpvPlayer::play_mux(const _mux_t& mux, bool blindscan) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_mux(mux, blindscan);
}

template int MpvPlayer::play_mux<chdb::dvbs_mux_t>(const chdb::dvbs_mux_t& mux, bool blindscan);

template int MpvPlayer::play_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, bool blindscan);

template int MpvPlayer::play_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, bool blindscan);

int subscription_t::play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time) {
	log4cxx_store_threadname();
	dtdebugx("PLAY RECORDING %s", rec.epg.event_name.c_str());
	if (is_playing()) {
		this->close();
	}

	int ret;
	mpm = receiver->subscribe_recording(rec, subscription_id);
	ret = mpm.get() ? mpm->subscription_id : -1;

	if (ret >= 0) {
		dtdebugx("PLAY SUBSCRIPTION (rec): subscribed subscription_id=%d", ret);
	} else {
		dtdebugx("PLAY SUBSCRIPTION (rec): subscription failed");
	}
	if (ret >= 0) {

		assert(ret == subscription_id || subscription_id < 0);
		subscription_id = ret;
		mpm->register_audio_changed_callback(subscription_id,
																				 [this](auto x, auto id) { this->on_audio_language_change(x, id); });

		dtdebugx("PLAY SUBSCRIPTION (rec): mpm init done");
		if (mpm->move_to_time(start_play_time) < 0) {
			dtdebug("PLAY SUBSCRIPTION (rec): aborting");
			this->close();
			if (subscription_id >= 0) {
				receiver->unsubscribe(subscription_id);
				subscription_id = -1;
			}
			return 0;
		}
		dtdebug("PLAY SUBSCRIPTION (rec): mpm move to start_play_time done: " << start_play_time);
	} else {
		if (subscription_id >= 0) {
			// receiver->unsubscribe(subscription_id); already done
			subscription_id = -1;
		}
	}
	return 0;
}

int MpvPlayer_::play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time) {
	// retune request
	auto op = [this, rec, start_play_time]() { subscription.play_recording(rec, start_play_time); };
	{
		// lock must be placed after lambda
		std::scoped_lock lck(subscription.m);
		subscription.next_op = op;
	}
	// will be run by the first opn_fn or close_fn call

	subscription.filepath.clear();

	// we need to fake a different file each time, hence the seqno
	subscription.filepath.sprintf("neumo://%p/%d", this, subscription.seqno++);
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}
	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	dtdebugx("PLAY RECORDING %p END", this);
	return 0;
}

int MpvPlayer::play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_recording(rec, start_play_time);
}

int subscription_t::jump(int seconds) {
	auto play_pos = mpm->get_current_play_time();
	play_pos += milliseconds_t(1000 * seconds);
	if (play_pos < milliseconds_t(0))
		play_pos = milliseconds_t(0);
	dtdebug("JUMP seconds=" << seconds << " play_pos=" << play_pos);
	mpm->move_to_time(play_pos); // open the first file
	// sleep(5);  //for testing
	return 0;
}

int MpvPlayer_::jump(int seconds) {
	if (!mpv || !subscription.mpm) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}

	// retune request
	auto op = [this, seconds]() {
		// service is captured by copy
		subscription.jump(seconds);
	};
	{
		// lock must be placed after lambda
		std::scoped_lock lck(subscription.m);
		subscription.next_op = op;
	}
	// will be run by the first opn_fn or close_fn call

	subscription.filepath.clear();
	// int64_t start = 0;
	const char* cmd1[] = {"loadfile", nullptr};
	::mpv_command(mpv, cmd1);
	subscription.filepath.sprintf("neumo://%p/%d", this, subscription.seqno++);
	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	dtdebugx("JUMP SUBSCRIPTION %p", this);
	return 0;
}

int MpvPlayer::jump(int seconds) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->jump(seconds);
}

void subscription_t::close() {
	pmt_change_count = 0;
	if (!mpm)
		return;
	if (subscription_id >= 0)
		mpm->unregister_audio_changed_callback(subscription_id);
	std::scoped_lock lck(m);
	mpm->close();
	mpm.reset();
}

int subscription_t::stop_play() {
	dtdebugx("STOP SUBSCRIPTION %d", subscription_id);
	std::scoped_lock lck(m);
	if (mpm) {
		mpm->close();
		if (subscription_id >= 0) {
			mpm->unregister_audio_changed_callback(subscription_id);
		}
	}
	if (receiver)
		receiver->unsubscribe(subscription_id);

	subscription_id = -1;
	if (mpm)
		mpm.reset();
	return 0;
}

int MpvPlayer_::stop_play() {
	if (!mpv) {
		dterror("mpv not ready");
		assert(0);
		return -1;
	}
	dtdebugx("PLAY SUBSCRIPTION %p END", this);

	auto op = [this]() { subscription.stop_play(); };
	{
		// lock must be placed after lambda
		std::scoped_lock lck(subscription.m);
		subscription.next_op = op;
	}
	subscription.set_pending_close(true);
	const char* cmd[] = {"stop", nullptr};
	::mpv_command(mpv, cmd);
	dtdebugx("PLAY SUBSCRIPTION %p END - DONE", this);
	return 0;
}

int MpvPlayer::stop_play() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->stop_play();
}

int MpvPlayer_::pause() {
	if (!mpv || !subscription.mpm) {
		dterror("mpv not ready");
		return -1;
	}
	subscription.mpm->set_live(false);
	static bool onoff = 1;
	mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &onoff);
	onoff = !onoff;
	dtdebugx("PLAY SUBSCRIPTION %p pause", this);
	return 0;
}

int MpvPlayer::pause() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->pause();
}

void MpvPlayer_::destroy() {
	{
		std::lock_guard<std::mutex> lk(m);
		mustexit = true;
	}
	cv.notify_one();

	receiver->active_mpvs.writeAccess()->erase(this);
	thread_.join();
	if (mpv_gl) {
		mpv_render_context_set_update_callback(mpv_gl, nullptr, nullptr);
	}
	mpv_render_context_free(mpv_gl);
	mpv_terminate_destroy(mpv);
	mpv_gl = nullptr;
	mpv = nullptr;
	gl_canvas = nullptr;
	{
		std::lock_guard<std::mutex> lk(m);
		has_been_destroyed = true;
	}
	cv.notify_one();
}

int MpvPlayer_::run() {
	// keep the shared ptr alive until we exit
	auto saved = shared_from_this();

	run_id = std::this_thread::get_id();
	for (;;) {
		bool timedout;
		{
			std::unique_lock<std::mutex> lk(m);
			timedout = !cv.wait_for(lk, 500ms,
									[this] { return mustexit || (frames_to_play > (inited ? 0 : 1)); });
			if (mustexit)
				break;
		}
		if (gl_canvas->inited) { // inited is set in OnPaint
			wxMutexGuiEnter();
			gl_canvas->Render();
			wxMutexGuiLeave();
		}
		if(! timedout)
			frames_to_play--;
	}
	return 0;
}

void MpvPlayer::signal() {
	{
		std::lock_guard<std::mutex> lk(m);
		frames_to_play++;
	}
	cv.notify_one();
}

subscription_t::subscription_t(receiver_t* receiver_, MpvPlayer_* mpv_player_)
	: m(mpv_player_->m), cv(mpv_player_->cv), receiver(receiver_), mpv_player(mpv_player_) {
}

subscription_t::~subscription_t() {}

int64_t subscription_t::read_data(char* buffer, uint64_t nbytes) {
	static thread_local bool thread_name_set{false};
	if (!thread_name_set) {
		log4cxx_store_threadname();
		thread_name_set = true;
	}

	if (subscription_id < 0)
		return 0;
	if (mpm) // regular service
		return mpm->read_data(buffer, nbytes);
	else
		return wait_for_close();
}

int64_t subscription_t::wait_for_close() {
	std::unique_lock<std::mutex> lk(m);
	cv.wait(lk, [this] { return pending_close; });
	return 0;
}

ss::vector_<chdb::language_code_t> MpvPlayer::audio_languages() {
	return subscription.mpm ? subscription.mpm->audio_languages() : ss::vector_<chdb::language_code_t>();
}

chdb::language_code_t MpvPlayer::get_current_audio_language() {
	return subscription.mpm ? subscription.mpm->get_current_audio_language() : chdb::language_code_t();
}

ss::vector_<chdb::language_code_t> MpvPlayer::subtitle_languages() {
	return subscription.mpm ? subscription.mpm->subtitle_languages() : ss::vector_<chdb::language_code_t>();
}

chdb::language_code_t MpvPlayer::get_current_subtitle_language() {
	return subscription.mpm ? subscription.mpm->get_current_subtitle_language() : chdb::language_code_t();
}

void MpvPlayer::close() {
	{
		std::lock_guard<std::mutex> lk(m);
		mustexit = true;
	}
	cv.notify_one();
	if (!subscription.mpm)
		return;
	stop_play();
	this->subscription.set_pending_close(true);
}

void MpvPlayer_::repaint() {
	wxPaintEvent evt(wxID_ANY);
	gl_canvas->AddPendingEvent(evt);
}

//! returns true if this was the right mpv
void MpvPlayer_::notify(const chdb::signal_info_t& signal_info) {
	std::scoped_lock lck(m);
	if (!subscription.mpm)
		return;
	auto* as = subscription.mpm->active_service();
	if (!as)
		return;
	if (as->get_adapter_no() == signal_info.stat.k.lnb.adapter_no) {
		playback_info_t playback_info = subscription.mpm->get_current_program_info();
		gl_canvas->overlay.set_signal_info(signal_info, playback_info);
		return;
	}
	return;
}

void MpvPlayer::notify(const chdb::signal_info_t& info) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->notify(info);
}

void MpvPlayer_::update_playback_info() {
	std::scoped_lock lck(m);
	if (!subscription.mpm)
		return;
	playback_info_t playback_info = subscription.mpm->get_current_program_info();
	playback_info.is_recording = true;
	// std::lock_guard<std::mutex> lk(m);
	gl_canvas->overlay.set_playback_info(playback_info);
	return;
}

void MpvPlayer::update_playback_info() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->update_playback_info();
}

void mpv_overlay_t::render(int window_width, int window_height) {
	GLenum err;
	static int called = 0;
	if (!called) {
		called = 1;
	}

	uint8_t* data = svg_overlay->render(window_width, window_height);

	assert(data);
	glBindTexture(GL_TEXTURE_2D, g_texture); // make the texture 2d
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	glEnable(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT,
								1); // set texture parameter
										// The following copies the data
										// https://gamedev.stackexchange.com/questions/168045/avoid-useless-copies-of-buffers
	auto width = svg_overlay->get_width();
	auto height = svg_overlay->get_height();
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	glEnable(GL_BLEND);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorx("OPENGL error %d\n", err);
	}

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(-1.0, 1.0);

	glTexCoord2f(0, 1);
	glVertex2f(-1.0, -1.0);

	glTexCoord2f(1, 1);
	glVertex2f(1.0, -1.0);

	glTexCoord2f(1, 0);
	glVertex2f(1.0, 1.0);
	glEnd();
}

void mpv_overlay_t::set_signal_info(const chdb::signal_info_t& signal_info, const playback_info_t& playback_info) {
	if (svg_overlay.get()) {
		svg_overlay->set_signal_info(signal_info, playback_info);
	}
}

void mpv_overlay_t::set_playback_info(const playback_info_t& playback_info) {
	if (svg_overlay.get()) {
		svg_overlay->set_playback_info(playback_info);
	}
}

mpv_overlay_t::mpv_overlay_t(std::string filename) {

	svg_overlay = svg_overlay_t::make(filename.c_str());

	InitializeTexture(g_texture);
}

/*
	mpv_terminate_destroy(mpv_handle *ctx); Similar to mpv_destroy(), but brings the player and all clients down
	* as well, and waits until all of them are destroyed. This function blocks. T

	he first call on any mpv_handle will block until the core is destroyed.
	*  This means it will wait until other mpv_handle have been destroyed. If you
	*  want asynchronous destruction, just run the "quit" command, and then react
	*  to the MPV_EVENT_SHUTDOWN event


	*/
