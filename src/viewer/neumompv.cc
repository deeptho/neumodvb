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
#include "util/logger.h"
#include <clocale>
#include <pybind11/pybind11.h>
#include "neumompv_private.h"
#include "wx/dcsvg.h"
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glut.h>
#include <string>
#include <sys/timeb.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/window.h>
#include <fmt/chrono.h>
#include "neumotime.h"
#include "receiver/active_service.h"
#include "neumoglcanvas.h"
#include "neumompv.h"

#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include <mpv/render_gl.h>

namespace py = pybind11;


/*
	Comments in  makeContextCurrent suggest that a (new?) requirement is that glcontexts cannot be used
	from a thread other than the one they are created in. This poses problems as more than one mpv thread
	accesses the same wxGLCanvas widgets.

	The solution we adopt is to create one  wxGLContext per thread, disregarding the possibility that one
	thread might accress wmultiple wxGLCanvas widgets. The documentation of wxGLContext suggests that it is safe
	to use the same wxGLContext on multiple wxGLCanvas widgets, as long as they share the same "attributes".

	From wxGLContext docs: "one rendering context is usually used with or bound to multiple output windows in
	turn, so that the application has access to the complete and identical state while rendering into each window.
	Binding (making current) a rendering context with another instance of a wxGLCanvas however works only if the
	other wxGLCanvas was created with the same attributes as the wxGLCanvas from which the wxGLContext was
	initialized. (This applies to sharing display lists among contexts analogously."



 */
class dt_context_t : public wxGLContext {
	static thread_local std::unique_ptr<dt_context_t> c_;
public:
	dt_context_t(wxGLCanvas *canvas) : wxGLContext(canvas)
		{}
	static wxGLContext* get(wxGLCanvas *canvas) {
		if(!c_)
			c_ = std::make_unique<dt_context_t>(canvas);
		return c_.get();
	}
};

thread_local std::unique_ptr<dt_context_t> dt_context_t::c_;

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

static void InitializeTexture(GLuint& g_texture) {
	GLenum err;
	glGenTextures(1, &g_texture); // generate 1 texture name
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	glBindTexture(GL_TEXTURE_2D, g_texture); // make the texture 2d

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	dtdebugf("g_texture={}", g_texture);
}

wxBEGIN_EVENT_TABLE(MpvGLCanvas, wxGLCanvas)
//EVT_SIZE(MpvGLCanvas::OnSize)
EVT_WINDOW_CREATE(MpvGLCanvas::OnWindowCreate)
EVT_PAINT(MpvGLCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MpvGLCanvas::OnErase)
wxEND_EVENT_TABLE()

MpvGLCanvas::MpvGLCanvas(wxWindow *parent, std::shared_ptr<MpvPlayer_> player)
: wxGLCanvas(parent, wxID_ANY,  NULL, wxDefaultPosition, wxDefaultSize
						 , 0, wxString("GLCanvas"), wxNullPalette)
	, to_prevent_destruction(player), mpv_player(player.get())
	, overlay(player.get(), this)
{
	InitializeTexture(g_texture);
	SetBackgroundStyle(wxBG_STYLE_CUSTOM);
	SetClientSize(parent->GetSize());
}


MpvGLCanvas::~MpvGLCanvas() {
	this->MpvDestroy();
	if (mpv_player) {
		mpv_player->destroy();
		mpv_player->wait_for_destroy();
		mpv_player = nullptr;
	}

	OnRender = nullptr;
	OnSwapBuffers = nullptr;
}

bool MpvGLCanvas::SetCurrent() // MPV_CALLBACK
{
	static std::mutex m;
	std::scoped_lock<std::mutex> lck(m);
	auto* glContext = dt_context_t::get(this);
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

void MpvGLCanvas::Render() {// MPV_CALLBACK and timer callback
	DoRender();
}

wxDEFINE_EVENT(WX_MPV_WAKEUP, wxThreadEvent);
wxDEFINE_EVENT(WX_MPV_REDRAW, wxThreadEvent);

void MpvGLCanvas::OnWindowCreate(wxWindowCreateEvent& evt) {
	dtdebugf("CREATE {:p}", fmt::ptr(this));
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



void MpvGLCanvas::OnMpvWakeupEvent(wxThreadEvent&) {
	std::lock_guard<std::mutex> lk(mpv_player->m);
	if (mpv_player)
		mpv_player->on_mpv_wakeup_event();
}

void MpvGLCanvas::DoRender() // MPV_CALLBACK and timer
{
	SetCurrent();
	auto s = GetSize();
	GLint dims[4];
	glGetIntegerv(GL_VIEWPORT, &dims[0]);
	int w = dims[2];
	int h = dims[3];
	if (OnRender) {
		prepare_buffer(s.x, s.y);
		OnRender(this, w, h);
	} else {
		dterrorf("ONRENDER NOT READY");
	}
	// glClearColor(0.0, 0.0, 0.0, 0.0);
	// glClear(GL_COLOR_BUFFER_BIT);
	SetCurrent();

	if(mpv_player->valid_frames < 2)
		this->clear_window();
	int width = s.x;
	int height = s.y;
	if (mpv_player->subscription.show_radiobg) {
		overlay.render_radiobg(width, height);
	}
	overlay.render_osd(width, height, mpv_player->subscription.show_osd);
	SwapBuffers();
}

void* MpvGLCanvas::GetProcAddress(const char* name) {
	SetCurrent();
	return (void*)::glXGetProcAddressARB((const GLubyte*)name);
}

void MpvGLCanvas::MpvDestroy() {
	Unbind(WX_MPV_WAKEUP, &MpvGLCanvas::OnMpvWakeupEvent, this);
	dtdebugf("MpvDestroy {:p} mpv_player set to null\n", fmt::ptr(this));
}

struct file_t;

static int64_t size_fn(void* cookie) {
	return MPV_ERROR_UNSUPPORTED;
}

static int64_t seek_fn(void* cookie, int64_t bytepos) {
	auto* player = (MpvPlayer_*)cookie;
	if(bytepos <0)
		bytepos =0;
	auto ret = player->subscription.move_to_bytepos(bytepos);
	dtdebugf("seek_fn: bytepos={} ret={}", bytepos, ret);
	{
		return ret < 0 ? MPV_ERROR_GENERIC : bytepos;
	}
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
	auto* player = (MpvPlayer_*)cookie;
	{
		auto ret = player->subscription.read_data(buf, nbytes);
#if 0
		if(ret>0)
		{
			static FILE* fp=fopen("/tmp/ttt.ts", "w");
			fwrite(buf, ret, 1, fp);
			fflush(fp);
		}
#endif
		return ret < 0 ? 0 : ret;
	}
}

void mpv_subscription_t::close_fn() {
	auto get = [this] {
		std::scoped_lock lck(m);
		auto ret = next_op;
		next_op = none;
		return ret;
	};
	auto op = get();
	op();
	mpv_player->reset_valid_frames();
}

static void close_fn(void* cookie) {
	auto* player = (MpvPlayer_*)cookie;
	dtdebugf("MPV fake close: player={:p}", fmt::ptr(player));
	player->subscription.close_fn();
	//deliberately don't do anything
	//player->subscription.close();
}

int mpv_subscription_t::open() {
	if(!mpm) {
		dterrorf("mpm=nulptr");
	}
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
	int subscription_id = (int) this->subscriber->get_subscription_id();
	dtdebug_nicef("Open subscription_id={} mpm={}", subscription_id, !!mpm);
	if(mpm) {
		auto playback_info = mpm->get_current_program_info();
		dtdebug_nicef("QQQ start_time={}", playback_info.start_time);
		auto w = mpv_player->trick_play.writeAccess();
		w->start_time = playback_info.start_time;
	}
	return mpm ? 0 : -1;
}

/*
	called from python gui
	returns -1 on error
*/
int mpv_subscription_t::set_audio_language(int idx) {
	if (!mpm) {
		dtdebugf("No active playback");
		return -1;
	}
	auto ret = mpm->set_audio_language(idx);

#ifndef PMTREWRITE
	ss::string<16> arg;
	arg.format("{:d}", idx + 1);
	if (idx < 0) {
		dterrorf("setting BAD audio language (from GUI) to {:d}", idx);
	}
	dtdebugf("setting audio language (from GUI) to {:d}", idx);
#endif
	return ret;
}

/*
	called from python gui
	returns -1 on error
*/
int mpv_subscription_t::set_subtitle_language(int idx) {
	if (!mpm) {
		dtdebugf("No active playback");
		return -1;
	}
	idx = mpm->set_subtitle_language(idx);
	int ret = 0;
#ifndef PMTREWRITE
	ss::string<16> arg;
	if (idx < 0) {
		dterrorf("Disabling subtitle language (from GUI)");
		arg.format("no");
	} else {
		arg.format("{:d}", idx + 1);
		dtdebugf("setting subtitle language (from GUI) to {:d}", idx);
	}
	if (mpv_set_property_string(mpv_player->mpv, "sid", arg.c_str()) < 0) {
		ret = -1;
		dterrorf("Failed setting subtitle language {:d}", idx);
	}
#endif
	return ret;
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
void mpv_subscription_t::on_language_change(const chdb::language_code_t& lang, int idx, bool for_subtitles) {
	ss::string<16> arg;
	// id=2;
	if(idx <0) {
		if (for_subtitles) {
			dterrorf("Disabling subtitle language (MPV only)");
			arg.format("no");
		} else {
			dterrorf("setting BAD audio language (MPV ONLY) to {:d}", idx);
		}
	} else {
		arg.format("{:d}", idx + 1);
		dtdebugf("setting {} language (MPV ONLY) to {:d} {:s}",
						 for_subtitles ? "subtitle" : "audio",
						 idx, chdb::lang_name(lang));
	}
	if (mpv_set_property_string(mpv_player->mpv, for_subtitles ? "sid" : "aid", arg.c_str()) < 0)
		dterrorf("Failed setting {} language {:d}",
						 for_subtitles ? "subtitle" : "audio", idx);
}

static int open_fn(void* user_data, char* uri, mpv_stream_cb_info* info) {
	auto* player = (MpvPlayer_*)user_data;
	int seqno;
	log4cxx_store_threadname();
	dtdebugf("OPEN_FN");
	player->subscription.set_pending_close(false);
	player->reset_valid_frames();
	sscanf(uri, "neumo://%p/%d", &player, &seqno);
	dtdebugf("MPV open: player={:p}", fmt::ptr(player));
	dttime_init();
	info->cookie = player;
	info->seek_fn = ::seek_fn;
	info->read_fn = ::read_fn;
	info->size_fn = ::size_fn;
	info->close_fn = ::close_fn;
	auto ret = player->subscription.open();
	dttime(100);

	return (info->cookie && ret>=0) ? 0 : MPV_ERROR_LOADING_FAILED;
}

MpvPlayer::MpvPlayer(receiver_t * receiver, MpvPlayer_* mpv)
	: receiver(receiver)
	, config_dir(receiver->options.readAccess()->mpvconfig.c_str()) {
}


template <typename T> T* wxLoad(py::object src, const wxString& inTypeName) {
	/* Extract PyObject from handle */
	PyObject* source = src.ptr();

	T* obj = nullptr;

	bool success = wxPyConvertWrappedPtr(source, (void**)&obj, inTypeName);
	wxASSERT_MSG(success, _T("Returned object was not a ") + inTypeName);

	return obj;
}

std::shared_ptr<MpvPlayer> MpvPlayer::make(receiver_t* receiver, pybind11::object parent_window, int idx) {
	// make_shared does not work with private constructor

	auto ret = std::shared_ptr<MpvPlayer_>(new MpvPlayer_(receiver));
	ret->make_canvas(parent_window);
	//auto* w = wxLoad<wxWindow>(parent_window, "wxWindow");
	auto* w = ret->gl_canvas;
	ret->subscription.subscriber = subscriber_t::make(receiver, w);
	ret->subscription.subscriber->event_flag = int(subscriber_t::event_type_t::ERROR_MSG);
	ret->subscription.subscriber->set_mpv(ret);
	return ret;
}

void MpvPlayer_::get_audio_volume()
{
	auto user_options = receiver->get_options();
	auto & volumes = user_options.audio_volumes;
	auto s = volumes.size();
	if (idx >= s)
		volume = 100;
	else
		volume = volumes[idx];
}


void MpvPlayer_::save_audio_volume_async()
{
	if(volume_expiration.has_expired_now()) {
		auto devdb_wtxn = receiver->devdb.wtxn();
		receiver->update_options(devdb_wtxn, [this](neumo_options_t& options) {
			auto& volumes = options.audio_volumes;
			for (int i=volumes.size() ; i <=idx; ++i)
				volumes.push_back(100);
			volumes[idx] =volume;
		}, true /*save*/);
		devdb_wtxn.commit();
		dtdebugf("Saved audio volume [{}] = {}", idx, volume);
	}
}

MpvPlayer_::MpvPlayer_(receiver_t* receiver)
	: MpvPlayer(receiver, dynamic_cast<MpvPlayer_*>(this))
	, subscription(receiver, this)
{
	get_audio_volume();
}

MpvPlayer_::~MpvPlayer_() {
	if (!has_been_destroyed) {
		dterrorf("MpvPlayer destroyed without calling close");
	}
}

void MpvGLCanvas::clear_window() {
	glClearColor(0.0, 0., 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

bool MpvPlayer_::create() {
	if (mpv)
		return false;
	dtdebugf("Creating mpv {:p}", fmt::ptr(this));
	setlocale(LC_NUMERIC, "C");
	// MpvDestroy();
	mpv = mpv_create();
	if (!mpv) {
		dterrorf("failed to create mpv instance");
		assert(0);
	}

	if (mpv_set_property_string(mpv, "config-dir", config_dir.c_str()) < 0) {
		dterrorf("failed to register config-dir");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "config", "yes") < 0) {
		dterrorf("failed to activate config reading");
		assert(0);
	}
	if (mpv_initialize(mpv) < 0) {
		dterrorf("failed to initialize mpv");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "profile", "neumo") < 0) {
		dterrorf("failed to register mpv neumo profile");
		assert(0);
	}
	if (mpv_stream_cb_add_ro(mpv, "neumo", (void*)this, open_fn) < 0) {
		dterrorf("failed to register mpv protocol");
		assert(0);
	}
	if (mpv_set_property_string(mpv, "vo", "opengl-cb") < 0) {
		dterrorf("failed to set mpv VO");
		assert(0);
	}
	if(mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE) <0) {
		dtdebugf("failed to observe");
	}
#ifdef STREAM_POS
	if(mpv_observe_property(mpv, 0, "stream-pos", MPV_FORMAT_INT64) <0) {
		dtdebugf("failed to observe");
	}
#endif

#ifdef BUG
	if (mpv_set_property_string(mpv, "hwdec", "auto") < 0) {
		dterrorf("failed to set mpv VO");
		assert(0);
	}
#endif

	auto get_proc_address = [](void* ctx, const char* name) {
		auto glCanvas = reinterpret_cast<MpvGLCanvas*>(ctx);
		return glCanvas ? glCanvas->GetProcAddress(name) : nullptr;
	};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
	mpv_opengl_init_params gl_init_params{get_proc_address, gl_canvas};
#pragma clang diagnostic pop
	mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
														{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
														{MPV_RENDER_PARAM_INVALID, nullptr}};
	if (mpv_render_context_create(&mpv_gl, mpv, params) < 0) {
		dterrorf("failed to initialize mpv GL context");
		assert(0);
	}
	if (!mpv_gl) {
		dterrorf("failed to create mpv render context");
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
		} else if(strcmp(prop->name, "time-pos") == 0) {
			if(prop->format ==  MPV_FORMAT_DOUBLE) {
				bool must_slow_down{false};
				bool must_play_forward{false};
				{
					auto w = this->trick_play.writeAccess();
					w->time_pos = *(double*)prop->data;
					//return to normal in case reverse play is active
					must_play_forward = w->time_pos <=2 && w->reverse_playing;
					//return to normal in case fast forward is active
					must_slow_down = w->fast_forwarding && w->time_pos >= w->fast_forwarding_time_pos_limit;
#if 0
					if(w->fast_forwarding)
						dtdebug_nicef("time_pos={} limit={}",  w->time_pos, w->fast_forwarding_time_pos_limit);
#endif
				}
				if(must_slow_down)
					this->set_playback_speed(1.0);

				if (must_play_forward)
					this->set_play_direction(true/*forward*/);
				this->update_playback_info();
			}
		}
#ifdef STREAM_POS
		else if(strcmp(prop->name, "stream-pos") == 0) {
			if(prop->format ==  MPV_FORMAT_INT64) {
				auto w = this->trick_play.writeAccess();
				w->stream_pos = *(int64_t*)prop->data;
			}
		}
#endif
		break;
	}
	case MPV_EVENT_SHUTDOWN:
		gl_canvas->MpvDestroy();
		break;
	case MPV_EVENT_FILE_LOADED: {
#if 0
		int play_offset_s = this->subscription.mpm->move_to_live();
		printf("delay=%d\n", play_offset_s);
		ss::string<16> arg;
		arg.format("{:d}", play_offset_s);

		const char* cmd1[] = {"seek", arg.c_str(), nullptr};
		::mpv_command(mpv, cmd1);
#endif
	}
		break;
	case MPV_EVENT_END_FILE:
		dtdebugf("End of file event");
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
		if(valid_frames < 2) {
			mpv_render_frame_info info ;
			mpv_render_param param{MPV_RENDER_PARAM_NEXT_FRAME_INFO, &info};
			mpv_render_context_get_info(mpv_gl, param);
			bool present= info.flags &  MPV_RENDER_FRAME_INFO_PRESENT;
			valid_frames+= present;
		}

		int fbo_;
		GLint dims[4];
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbo_);
		glGetIntegerv(GL_VIEWPORT, &dims[0]);
#if 1
		static int lastw=0;
		static int lasth=0;
		if( w!= dims[2] || h!= dims[3] || dims[0]!=0 ||dims[1]!=0 || dims[2]!=lastw || dims[3]!=lasth)
			dtdebugf("TEST: fbo={} w={}/{} h={}/{} x={} y={}", fbo_, w, dims[2], h, dims[3], dims[0], dims[1]);
		lastw = dims[2];
		lasth = dims[3];
#endif
		mpv_opengl_fbo mpfbo{fbo_, dims[2], dims[3], 0};
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
void MpvPlayer_::make_canvas(py::object frame_) {
	thread_id = std::this_thread::get_id();
	auto* frame = wxLoad<wxWindow>(frame_, "wxWindow");
	auto ptr = std::static_pointer_cast<MpvPlayer_>(shared_from_this());
	this->gl_canvas = new MpvGLCanvas(frame, ptr);
}


py::handle MpvPlayer::get_canvas() const {
	auto* self = dynamic_cast<const MpvPlayer_*>(this);
	return wxCast(self->gl_canvas);
}


int MpvPlayer_::screenshot() {
	if (!mpv) {
		dterrorf("mpv not ready");
		assert(0);
		return -1;
	}
	const char* cmd[] = {"screenshot", nullptr};
	::mpv_command(mpv, cmd);
	return 0;
}

int MpvPlayer::screenshot() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->screenshot();
}

void MpvPlayer_::mpv_command(const char* cmd_, const char* arg2, const char* arg3) {
	if (!mpv) {
		dterrorf("mpv not ready");
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
	int v = volume + 5*step;
	v = std::min(std::max(v, 0), 100);
	arg.format("{:d}", v);
	dtdebugf("setting audio volume to {:d}", v);
	const char* cmd[] = {"set", "volume", arg.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	volume = v;
	volume_expiration.start(1s);
	this->gl_canvas->overlay.set_volume(v);
	if(step !=0 ) //only show when volume changes (step==0 when called at service start)
		this->gl_canvas->overlay.show_volume();
	return 0;
}

int MpvPlayer::change_audio_volume(int step) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->change_audio_volume(step);
}

int MpvPlayer_::set_audio_language(int id) {
	if (subscription.set_audio_language(id) < 0) {
		dterrorf("Failed setting audio language {:d}", id);
	}
	return 1;
}

int MpvPlayer::set_audio_language(int id) {
	dtdebugf("setting audio language to {:d}", id);
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->set_audio_language(id);
}

int MpvPlayer_::set_subtitle_language(int id) {
	dtdebugf("setting subtitle language to {:d}", id);
	if (subscription.set_subtitle_language(id) < 0) {
		dterrorf("Failed setting subtitle language {:d}", id);
	}
	return 1;
}

int MpvPlayer::set_subtitle_language(int id) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->set_subtitle_language(id);
}

/*
	returns number of seconds to delay playback
 */
void mpv_subscription_t::play_service(const chdb::service_t& service) {
	log4cxx_store_threadname();
	dtdebugf("PLAY SUBSCRIPTION (service)");
	if (is_playing()) {
		dtdebugf("PLAY SUBSCRIPTION (service) close mpm");
		this->close(false /*unsubscribe*/);
	}
	subscription_id_t subscription_id{-1};
	mpm = subscriber->subscribe_service_for_viewing(service);
	subscription_id = mpm.get() ? mpm->subscription_id : subscription_id_t{-1};
	if ((int) subscription_id >= 0) {
		dtdebugf("PLAY SUBSCRIPTION (service): subscribed={:d}", (int) subscription_id);
	} else {
		dtdebugf("PLAY SUBSCRIPTION (service): subscription failed");
	}

	if ((int) subscription_id >= 0) {
		mpm->register_language_changed_callback(subscription_id,
																				 [this](auto lang, auto pos, bool for_subtitles)
																					 { this->on_language_change(lang, pos, for_subtitles); });

		dtdebugf("PLAY SUBSCRIPTION (service): mpm init done");
		if (mpm->move_to_live() < 0) {
			dtdebugf("PLAY SUBSCRIPTION (service): aborting");
			this->close(false /*unsubscribe*/);
			subscriber->unsubscribe(true /*wait*/);
			return;
		}
		dtdebugf("PLAY SUBSCRIPTION (service): mpm move_to_live done");
	}
	return;
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
		dtdebugf("FORCE ABORT before tuning to {:s}", service.name.c_str());
		if (subscription.mpm)
			subscription.mpm->force_abort();
		dttime(100);
		subscription.next_op = op;
		dttime(100);
		// will be run by the first open_fn or close_fn call
	}

	subscription.filepath.clear();

	// we need to fake a different file each time, hence the seqno
	subscription.filepath.format("neumo://{:p}/{:d}", fmt::ptr(this), subscription.seqno++);
	if (!mpv) {
		dterrorf("mpv not ready");
		//assert(0);
		return -1;
	}
	this->subscription.set_pending_close(true);

	const char* cmd[] = {"loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	this->change_audio_volume(0);
	dtdebugf("PLAY SUBSCRIPTION {:p} STARTED", fmt::ptr(this));
	return 0;
}


int MpvPlayer::play_service(const chdb::service_t& service) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_service(service);
}

int mpv_subscription_t::play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time) {
	log4cxx_store_threadname();
	dtdebugf("PLAY RECORDING {:s}", rec.epg.event_name.c_str());
	if (is_playing()) {
		this->close(false /*unsubscribe*/);
	}

	subscription_id_t subscription_id{-1};
	mpm = subscriber->subscribe_recording(rec);
	subscription_id = mpm.get() ? mpm->subscription_id : subscription_id_t{-1};
	assert(subscription_id == subscriber->get_subscription_id());
	if ((int) subscription_id >= 0) {
		dtdebugf("PLAY SUBSCRIPTION (rec): subscribed subscription_id={:d}", (int) subscription_id);
	} else {
		dtdebugf("PLAY SUBSCRIPTION (rec): subscription failed");
	}
	if ((int) subscription_id >= 0) {
		mpm->register_language_changed_callback(subscription_id,
																						[this](auto x, auto id, bool for_subtitles)
																							{ this->on_language_change(x, id, for_subtitles); });

		dtdebugf("PLAY SUBSCRIPTION (rec): mpm init done");
		if (mpm->move_to_time(start_play_time) < 0) {
			dtdebugf("PLAY SUBSCRIPTION (rec): aborting");
			this->close(false /*unsubscribe*/);
			if ((int) subscription_id >= 0) {
				subscriber->unsubscribe(true/*wait*/);
				assert((int) subscriber->get_subscription_id() < 0);
			}
			return -1;
		}
		dtdebugf("PLAY SUBSCRIPTION (rec): mpm move to start_play_time done: {}", start_play_time);
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
	subscription.filepath.format("neumo://{:p}/{:d}", fmt::ptr(this), subscription.seqno++);
	if (!mpv) {
		dterrorf("mpv not ready");
		assert(0);
		return -1;
	}

	const char* cmd[] = { "loadfile", subscription.filepath.c_str(), nullptr};
	::mpv_command(mpv, cmd);
	this->change_audio_volume(0);

	dtdebugf("PLAY RECORDING {:p} END", fmt::ptr(this));
	return 0;
}

int MpvPlayer::play_recording(const recdb::rec_t& rec, milliseconds_t start_play_time) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->play_recording(rec, start_play_time);
}

int mpv_subscription_t::jump(int seconds, system_time_t play_pos) {
	auto playback_info = mpm->get_current_program_info();
	auto end_pos = playback_info.end_time;
	auto start_pos = playback_info.start_time;
	play_pos += std::chrono::seconds(seconds);
	if (play_pos < start_pos)
		play_pos = start_pos;
	if (play_pos > end_pos)
		play_pos = end_pos;
	auto ret = std::chrono::duration_cast<std::chrono::seconds>(play_pos-start_pos).count();
	dtdebugf("JUMP seconds={} play_pos={}", seconds, ret);
	return ret;
}

int MpvPlayer_::jump(int seconds) {
	if (!mpv || !subscription.mpm) {
		dterrorf("mpv not ready");
		return -1;
	}
	auto absolute_seconds= this->subscription.jump(seconds, this->get_mpv_play_real_time());
	ss::string<16> arg;
	arg.format("{:d}", absolute_seconds);

	const char* cmd1[] = {"seek", arg.c_str(), "absolute", nullptr};
	::mpv_command(mpv, cmd1);
	return 0;
}

int MpvPlayer::jump(int seconds) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->jump(seconds);
}

int mpv_subscription_t::smartjump(bool forward)
{
	auto interval = jump_state.jump(forward);
	dtdebugf("jump_interval={}", interval);
	return interval;
}

int MpvPlayer::smartjump(bool forward) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	// retune request
	auto seconds = self->subscription.smartjump(forward);
	return self->jump(seconds);
}

void mpv_subscription_t::close(bool unsubscribe) {
	pmt_change_count = 0;
	if (!mpm)
		return;
	auto subscription_id = subscriber->get_subscription_id();
	if ((int) subscription_id >= 0)
		mpm->unregister_language_changed_callback(subscription_id);
	std::scoped_lock lck(m);
	mpm->close();
	mpm.reset();
	if(unsubscribe)
		subscriber->unsubscribe(true/*wait*/);
}

int mpv_subscription_t::stop_play() {
	auto subscription_id = subscriber->get_subscription_id();
	dtdebugf("STOP SUBSCRIPTION {:d}", (int) subscription_id);
	subscriber->unsubscribe(true /*wait*/);
	std::scoped_lock lck(m);
	if (mpm) {
		mpm->close();
		if ((int) subscription_id >= 0) {
			mpm->unregister_language_changed_callback(subscription_id);
		}
	}

	if (mpm)
		mpm.reset();
	return 0;
}

int MpvPlayer_::stop_play() {
	if (!mpv) {
		dterrorf("mpv not ready");
		assert(0);
		return -1;
	}
	dtdebugf("PLAY SUBSCRIPTION {:p} END", fmt::ptr(this));

	auto op = [this]() { subscription.stop_play(); };
	{
		// lock must be placed after above lambda
		std::scoped_lock lck(subscription.m);
		subscription.next_op = op;
	}
	subscription.set_pending_close(true);
	const char* cmd[] = {"stop", nullptr};
	::mpv_command(mpv, cmd);
	dtdebugf("PLAY SUBSCRIPTION {:p} END - DONE", fmt::ptr(this));
	return 0;
}

int MpvPlayer::stop_play() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->stop_play();
}

int MpvPlayer::stop_play_and_exit() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->stop_play(/*true unsubscribe*/);
}

int MpvPlayer_::pause() {
	if (!mpv || !subscription.mpm) {
		dterrorf("mpv not ready");
		return -1;
	}
	int paused;
	{ auto w = this->trick_play.writeAccess();
		paused = !w->paused;
		w->paused = paused;
	}
	mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
	dtdebugf("PLAY SUBSCRIPTION paused={}", paused);
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
	subscription.subscriber->remove_mpv();
	thread_.join();
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
		save_audio_volume_async();
		on_mpv_wakeup_event();
	}
	if (mpv_gl) {
		mpv_render_context_set_update_callback(mpv_gl, nullptr, nullptr);
		mpv_render_context_free(mpv_gl);
	}
	mpv_terminate_destroy(mpv);
	return 0;
}

void MpvPlayer::signal() {
	{
		std::lock_guard<std::mutex> lk(m);
		frames_to_play++;
	}
	cv.notify_one();
}

mpv_subscription_t::mpv_subscription_t(receiver_t* receiver_, MpvPlayer_* mpv_player_)
	: m(mpv_player_->m)
	, cv(mpv_player_->cv)
	, receiver(receiver_)
	, mpv_player(mpv_player_) {
}

mpv_subscription_t::~mpv_subscription_t() {}

int64_t mpv_subscription_t::read_data(char* buffer, uint64_t nbytes) {
	static thread_local bool thread_name_set{false};
	if (!thread_name_set) {
		log4cxx_store_threadname();
		thread_name_set = true;
	}
	auto subscription_id = subscriber->get_subscription_id();
#if 0
	dtdebug_nicef("read_fn: subscription_id={:d} mpm={}", (int) subscription_id, (void*)mpm.get());
#endif
	if ((int) subscription_id < 0)
		return 0;
	if (mpm) { // regular service
		auto [ret, have_pmt] =	mpm->read_data(buffer, nbytes);
		return ret;
	}
	else
		return wait_for_close();
}

int64_t mpv_subscription_t::wait_for_close() {
	std::unique_lock<std::mutex> lk(m);
	cv.wait(lk, [this] { return pending_close; });
	return 0;
}

ss::vector_<chdb::language_code_t> MpvPlayer::audio_languages() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->subscription.mpm ? self->subscription.mpm->audio_languages() : ss::vector_<chdb::language_code_t>();
}

chdb::language_code_t MpvPlayer::get_current_audio_language() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->subscription.mpm ? self->subscription.mpm->get_current_audio_language() : chdb::language_code_t();
}

ss::vector_<chdb::language_code_t> MpvPlayer::subtitle_languages() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->subscription.mpm ? self->subscription.mpm->subtitle_languages() : ss::vector_<chdb::language_code_t>();
}

chdb::language_code_t MpvPlayer::get_current_subtitle_language() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->subscription.mpm ? self->subscription.mpm->get_current_subtitle_language() : chdb::language_code_t();
}

void MpvPlayer::close() {
	{
		std::lock_guard<std::mutex> lk(m);
		mustexit = true;
	}
	cv.notify_one();
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	if (!self->subscription.mpm)
		return;
	stop_play();
	self->subscription.set_pending_close(true);
}

// returns true if this was the right mpv
void MpvPlayer_::notify_signal_info(const signal_info_t& signal_info) {
	std::scoped_lock lck(m);
	if (!subscription.mpm)
		return;
	auto* as = subscription.mpm->active_service();
	if (!as)
		return;
	playback_info_t playback_info = subscription.mpm->get_current_program_info();
#ifdef STREAM_POS
	auto t = this->get_play_real_time();
#else
	auto t = this->get_mpv_play_real_time();
#endif
	playback_info.play_time = t;
	gl_canvas->overlay.set_signal_info(signal_info, playback_info);
	subscription.show_radiobg = (playback_info.service.media_mode == chdb::media_mode_t::RADIO);
	return;
}

void MpvPlayer::notify_signal_info(const signal_info_t& info) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->notify_signal_info(info);
}

//! returns true if this was the right mpv
void MpvPlayer_::notify_message(const ss::string_& msg) {
	std::scoped_lock lck(m);
#if 0
	if (!subscription.mpm)
		return;
	auto* as = subscription.mpm->active_service();
	if (!as)
		return;
#endif
	gl_canvas->overlay.set_message(msg);
	return;
}

void MpvPlayer::notify_message(const ss::string_& msg) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->notify_message(msg);
}

void MpvPlayer_::update_playback_info() {
	std::scoped_lock lck(m);
	if (!subscription.mpm)
		return;
	playback_info_t playback_info = subscription.mpm->get_current_program_info();
#ifdef STREAM_POS
	auto t = this->get_play_real_time();
#else
	auto t = this->get_mpv_play_real_time();
#endif
	playback_info.play_time = t;

	// std::lock_guard<std::mutex> lk(m);
	gl_canvas->overlay.set_playback_info(playback_info);
	return;
}

void MpvPlayer::update_playback_info() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->update_playback_info();
}

void mpv_overlay_t::render(svg_t* svg, int window_width, int window_height) {
	GLenum err;
	if(!svg)
		return;
	uint8_t* data = svg->render(window_width, window_height);

	assert(data);
	glBindTexture(GL_TEXTURE_2D, g_texture); // make the texture 2d
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	glEnable(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT,
								1); // set texture parameter
										// The following copies the data
										// https://gamedev.stackexchange.com/questions/168045/avoid-useless-copies-of-buffers
	auto width = svg->get_width();
	auto height = svg->get_height();
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glEnable(GL_BLEND);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glViewport(0,0, window_width, window_height);
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

void MpvGLCanvas::prepare_buffer(int window_width, int window_height) {
	GLenum err;
	static thread_local int last_w = -1;
	static thread_local int last_h = -1;
	//static thread_local GLuint g_texture;

	if(last_w == window_width && last_h == window_height)
		return;
	if(last_h == -1) {
		glGenTextures(1, &g_texture);
		glBindTexture(GL_TEXTURE_2D, g_texture);
	}
	last_h = window_height;
	last_w = window_width;

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, window_width, window_height, 0, GL_RGB,
							 GL_UNSIGNED_BYTE, nullptr);

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	glEnable(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}

	while ((err = glGetError()) != GL_NO_ERROR) {
		dterrorf("OPENGL error {:d}\n", err);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT,
								1); // set texture parameter
										// The following copies the data
										// https://gamedev.stackexchange.com/questions/168045/avoid-useless-copies-of-buffers
	glViewport(0,0, window_width, window_height);
}

void mpv_overlay_t::set_signal_info(const signal_info_t& signal_info, const playback_info_t& playback_info) {
	if (svg_overlay.get()) {
		svg_overlay->set_signal_info(signal_info, playback_info);
	}
}

void mpv_overlay_t::set_playback_info(const playback_info_t& playback_info) {
	if (svg_overlay.get()) {
		svg_overlay->set_playback_info(playback_info);
	}
}

void mpv_overlay_t::set_volume(int volume) {
	if (svg_overlay.get()) {
		svg_overlay->set_volume(volume);
	}
}

void mpv_overlay_t::show_volume() {
	if (svg_overlay.get()) {
		svg_overlay->show_volume();
	}
}

mpv_overlay_t::mpv_overlay_t(MpvPlayer_* player, MpvGLCanvas* canvas)
	: g_texture(canvas->g_texture)
{
	auto o = player->receiver->options.readAccess();
	auto osd_path = config_path / o->osd_svg;
	svg_overlay = svg_overlay_t::make(osd_path.c_str());
	auto radiobg_path = config_path / o->radiobg_svg;
	svg_radiobg = svg_radiobg_t::make(radiobg_path.c_str());
}


void MpvPlayer::toggle_overlay(){
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	self->gl_canvas->toggle_overlay();
}

playback_info_t MpvPlayer::get_current_program_info() {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->subscription.mpm ?
		self->subscription.mpm->get_current_program_info() : playback_info_t();
}


int MpvPlayer_::set_play_direction(bool forward) {
	if (!mpv || !subscription.mpm) {
		dterrorf("mpv not ready");
		return -1;
	}
	if (mpv_set_property_string(this->mpv, "play-direction",
															forward ? "forward" : "backward") < 0)
		dterrorf("Failed setting play_direction {:d}", forward);
	auto w = this->trick_play.writeAccess();
	w->reverse_playing = !forward;
	ss::string<64> msg;
	msg.format("Play {}", forward ? "forward" : "backward");
	this->notify_message(msg);
	return 0;
}

int MpvPlayer::set_play_direction(bool forward) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->set_play_direction(forward);
}

int MpvPlayer_::set_playback_speed(double speed) {
	if (!mpv || !subscription.mpm) {
		dterrorf("mpv not ready");
		return -1;
	}
	auto playback_info = this->subscription.mpm->get_current_program_info();
	auto end_pos = playback_info.end_time;
	auto play_pos = this->get_mpv_play_real_time();
	auto to_play = std::chrono::duration_cast<std::chrono::seconds>(end_pos - play_pos).count();
	if (mpv_set_property(this->mpv, "speed", MPV_FORMAT_DOUBLE, &speed) < 0)
		dterrorf("Failed setting speed {}", speed);

	ss::string<64> msg;
	msg.format("{:.2f}x speed", speed);
	this->notify_message(msg);
	auto w = this->trick_play.writeAccess();
	auto limit= w->time_pos + to_play; ///speed;
	w->fast_forwarding_time_pos_limit = limit;
	w->fast_forwarding = speed != 1.0;
	dtdebugf("set_playback_speed: to_play={} limit={}", to_play, limit);
	return 0;
}

int MpvPlayer_::change_playback_speed(bool faster) {
	if (!mpv || !subscription.mpm) {
		dterrorf("mpv not ready");
		return -1;
	}

	static std::array<double,8> fast_speeds {1.,  1.5,   2,    4,    8,    16,    32};
	static std::array<double,8> slow_speeds {1., 0.75, 0.5, 1./4, 1./8, 1./16, 1./32};
	int idx;
	{ auto w = this->trick_play.writeAccess();
		idx = w->playback_speed_index + (faster ? 1 : -1);
		idx = std::min((int)fast_speeds.size()-1, idx);
		idx = std::max(-(int)(slow_speeds.size()-1), idx);
		w->playback_speed_index = idx;
	}
	double& speed = idx>=0 ? fast_speeds[idx] : slow_speeds[-idx];
	return set_playback_speed(speed);
}

int MpvPlayer::change_playback_speed(bool faster) {
	auto* self = dynamic_cast<MpvPlayer_*>(this);
	return self->change_playback_speed(faster);
}

int jump_state_t::jump(bool forward) {
	auto now = system_clock_t::now();
	auto delta =std::chrono::duration_cast<std::chrono::seconds>(now - last_jump_time).count();
	bool timedout = delta > timeout;
	if(timedout) {
		fast_jump_idx = 0;
		jump_type = jump_type_t::INCREASING_JUMPS;
		last_was_forward = forward;
		jump_interval = forward_jumps[0];
	}
	switch(jump_type) {
	case jump_type_t::INCREASING_JUMPS:
		if(forward == last_was_forward) {
			jump_interval= forward_jumps[fast_jump_idx++];
			fast_jump_idx=std::min(fast_jump_idx, (int)forward_jumps.size()-1);
		} else {
			jump_type = jump_type_t::DECREASING_JUMPS;
			jump_interval= std::max((jump_interval+1)/2, 5);
		}
		break;

	case jump_type_t::DECREASING_JUMPS:
		if(forward != last_was_forward) {
			jump_interval= std::max((jump_interval+1)/2, 5);
		}
		break;
	}
	return forward ? jump_interval : -jump_interval;
}
