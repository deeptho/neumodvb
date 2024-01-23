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

#include <wx/numformatter.h>
#include <wx/wx.h>
#include <wxSVG/svg.h>
#include <wxSVG/svgctrl.h>
//#include <wxSVG/SVGCanvasImageCairo.h>
//#include <wxSVG/SVGCanvasCairo.h>
#include "neumosvg.h"
#include "util/logger.h"


struct image_box {
	const char* id;
	wxSvgXmlNode* img{nullptr};

	image_box(const char* id) : id(id) {}
	void init(wxSVGDocument* doc);
};


void image_box::init(wxSVGDocument* doc) {
	ss::string<32> temp;

	temp.format("{:s}-picture", id);
	auto* p = doc->GetElementById(temp.c_str());
	if (!p)
		dterrorf("Could not find svg element {:s}", temp.c_str());
	img = p;
}



class svg_radiobg_impl_t : public svg_radiobg_t {
	friend class svg_radiobg_t;
	wxSVGCtrl svgctrl;

public:
	bool uptodate{false};
	bool radiobg_shown{true};
	image_box img{"radiobg"};
	wxSVGDocument* doc{nullptr};
	wxSvgXmlNode* root{nullptr};

	svg_radiobg_impl_t(const char* filename);
	virtual ~svg_radiobg_impl_t();
	int init();
};


svg_radiobg_t::svg_radiobg_t(const char* filename) : svg_filename(filename)
{}

svg_radiobg_impl_t::svg_radiobg_impl_t(const char* filename)
	: svg_radiobg_t(filename), svgctrl() {}


svg_radiobg_impl_t::~svg_radiobg_impl_t() {}

int svg_radiobg_impl_t::init() {
	bool ok = svgctrl.Load(svg_filename.c_str());
	if (!ok) {
		dterrorf("Could not open {:s}", svg_filename.c_str());
		return -1;
	}
	doc = svgctrl.GetSVG();
	root = doc->wxSvgXmlDocument::GetRoot();
	img.init(doc);
	//show_radiobg(false);
	return 0;
}

uint8_t* svg_radiobg_t::render(int window_width, int window_height) {
	auto* self = dynamic_cast<svg_radiobg_impl_t*>(this);
	if (self->uptodate && surface && this->window_width == window_width && this->window_height == window_height) {
		return surface;
	}

	self->uptodate = true;
	surface = self->doc->RenderGetRef(window_width, window_height, NULL, true, true); //crash sometimes
	this->window_width = window_width;
	this->window_height = window_height;
	return surface;
}






std::unique_ptr<svg_radiobg_t> svg_radiobg_t::make(const char* filename) {
	auto ret = std::make_unique<svg_radiobg_impl_t>(filename);
	if(ret->init() <0)
		return nullptr;
	return ret;
}

#if 0

void svg_overlay_impl_t::show_radiobg(bool show) {
	if (show == radiobg_shown)
		return;
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	auto k = wxString::FromUTF8("visibility");
	wxString val { show ? "visibility" : "hidden"};
	radiobg.img->SetAttribute(k, val);
	radiobg_shown = show;
	self->uptodate = false;
}

#endif
