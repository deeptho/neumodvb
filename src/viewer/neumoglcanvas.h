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
#include "stackstring.h"

//#include <pybind11/pybind11.h>
#include <sip.h>

#include "wxpy_api.h"
#include <wx/wxprec.h>
#include <wx/glcanvas.h>
#include "neumosvg.h"

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/glcanvas.h>

void InitializeTexture(GLuint& g_texture);

//namespace py = pybind11;
class MpvApp : public wxApp
{
public:
    bool OnInit() override;
};


class MpvPlayer_;
struct playback_info_t;


class mpv_overlay_t {
	GLuint g_texture{0};
	ss::string<64> signal_info;
	ss::string<64> service_info;
	std::unique_ptr<svg_overlay_t> svg_overlay;
	std::unique_ptr<svg_radiobg_t> svg_radiobg;
	void render(svg_t* svgptr, int window_width, int window_height);
public:
	void set_signal_info(const signal_info_t& signal_info, const playback_info_t& info);
	void set_playback_info(const playback_info_t& info);

	void render_osd(int window_width, int window_height) {
		return render(svg_overlay.get(), window_width, window_height);
	}

	void render_radiobg(int window_width, int window_height) {
		return render(svg_radiobg.get(), window_width, window_height);
	}

	mpv_overlay_t(MpvPlayer_* player);
};

class MpvGLCanvas : public wxGLCanvas
{
	friend class MpvPlayer_;
#ifdef DTSUBCLASS
//when creating a sub class we need sip to export to python
	wxDECLARE_DYNAMIC_CLASS 	(MpvGLCanvas );
#endif
	std::shared_ptr<MpvPlayer_> to_prevent_destruction;
	MpvPlayer_ * mpv_player;
	mpv_overlay_t overlay;
	int inited = 0;
public:

	std::atomic<bool> playing_ok = false;
#ifndef TEST
	void MpvCreate();
	void MpvDestroy();
	void OnMpvRedrawEvent(wxThreadEvent &event);


#endif
	void clear_window();
	MpvGLCanvas(wxWindow *parent, std::shared_ptr<MpvPlayer_> player);
	~MpvGLCanvas();

	void Render();
	bool SetCurrent();
	bool SwapBuffers() /*override*/;
	void *GetProcAddress(const char *name);

	std::function<void (wxGLCanvas *, int w, int h)> OnRender = nullptr;
	std::function<void (wxGLCanvas *)> OnSwapBuffers = nullptr;

 	//int play_file(const char* name);
	//int jump_to(uint64_t milliseconds);

private:
	wxSize current_size;
	std::unique_ptr<wxTimer> tm;
	void OnSize(wxSizeEvent &event);
	void OnWindowCreate(wxWindowCreateEvent &);
	void OnPaint(wxPaintEvent &event);
	void OnTimer(wxTimerEvent &event);
	void OnErase(wxEraseEvent &event);
	void OnMpvWakeupEvent(wxThreadEvent &event);

	void DoRender();

	wxDECLARE_EVENT_TABLE();
};
