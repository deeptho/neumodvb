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
#include <fmt/chrono.h>
#include "neumosvg.h"
#include "receiver/active_service.h"
#include "receiver/devmanager.h"
#include "receiver/receiver.h"
#include "stackstring.h"
#include "util/logger.h"
#include <cairo/cairo.h>

float get_width(wxSVGElement* el) {
	wxSVGTransformable* element = wxSVGTransformable::GetSVGTransformable(*el);
	auto w1 = element->GetResultBBox(wxSVG_COORDINATES_VIEWPORT).GetWidth();
	return w1;
}

float get_x(wxSVGElement* el) {
	wxSVGTransformable* element = wxSVGTransformable::GetSVGTransformable(*el);
	auto w0 = element->GetResultBBox().GetX();
	return w0;
}

double floatattr(wxSVGElement* elem, const char* key) {
	if (!elem->HasAttribute(key))
		return -1;
	wxString attr = elem->GetAttribute(key);
	double ret = -1;
	if (!attr.ToDouble(&ret))
		dterrorf("NOT A FLOAT");
	if (strcmp(key, "width") == 0) {
		dterrorf("????? key={:s} val={:f} {:f}\n", key, ret, get_width(elem));
	}
	if (strcmp(key, "x") == 0) {
		dterrorf("????? key={:s} val={:f} {:f}\n", key, ret, get_x(elem));
	}
	return ret;
}

void setfloatattr(wxSVGElement* elem, const char* key, float v) {
	if (!elem->HasAttribute(key))
		return;
	wxString val;
	val << v;
	auto k = wxString::FromUTF8(key);
	elem->SetAttribute(k, val);
}

#if 0 //not used
static wxSVGElement* find_child_of_type(wxSVGElement*parent, const char *tagname)
{
	wxSVGElement* elem = (wxSVGElement*) parent->GetChildren();
	wxSVGElement* ret=nullptr;
	while(elem) {
		auto * t = elem->GetName().ToStdString().c_str();
		if(strcmp(tagname, t)==0)
			ret=elem;
		std::string content=elem->GetContent().ToStdString();
		dtdebugf("Content=/{:s}/\n", content.c_str());
		elem = (wxSVGElement*) elem->GetNext();
	}
	return nullptr;
}
#endif

struct level_indicator {
	const char* scroller_id;
	const char* bar_id;
	double min_val{};	 // lower bound of the displayed value
	double max_val{};	 // upper bound of the displayed value
	double low_val{};	 // currently set value
	double high_val{}; // currently set value
	double min_x{};		 // leftmost allowed position on screen
	double max_x{};		 // rightmost allowed position on screen
	double width{};
	double scroller_width{};
	wxSVGElement* scroller{nullptr};
	wxSVGElement* bar{nullptr};
	wxSvgXmlNode* text{nullptr};

	level_indicator(const char* scroller_id, const char* bar_id, double min_val, double max_val)
		: scroller_id(scroller_id), bar_id(bar_id), min_val(min_val), max_val(max_val), low_val(min_val),
			high_val(min_val) {}
	void init(wxSVGDocument* doc);
	void set_values(double low, double high);
	void set_lower_value(double val) { set_values(val, high_val); }
	void set_upper_value(double val) { set_values(low_val, val); }
};

struct livebuffer_t : public level_indicator {
	wxSVGElement* indicator_ref{nullptr}; // element which points to the playback time
	wxSVGElement* indicator_box{nullptr}; // full box containing indicator
	int indicator_ref_x{0};
	livebuffer_t(const char* scroller_id, const char* bar_id, double min_val, double max_val)
		: level_indicator(scroller_id, bar_id, min_val, max_val) {}

	void init(wxSVGDocument* doc);

	void set_indicator_value(double val);
};

struct text_box {
	const char* id;
	wxSvgXmlNode* text{nullptr};

	text_box(const char* id) : id(id) {}
	void init(wxSVGDocument* doc);
	void set_value(const char* val);
	void set_value(const ss::string_& val);
	void set_value(int x, const char* fmt);
	void set_time_value(time_t t, const char* fmt = "{:%H:%M}");
};

void level_indicator::init(wxSVGDocument* doc) {
	ss::string<32> temp;
	temp.format("{:s}-scroller", scroller_id);
	scroller = doc->GetElementById(temp.c_str());
	if(!scroller) {
		dterrorf("Could not get scroller");
		assert(0);
		return;
	}
	temp.clear();

	temp.format("{:s}-bar", bar_id);
	bar = doc->GetElementById(temp.c_str());
	width = get_width(bar);

	scroller_width = get_width(scroller);

	min_x = get_x(bar);
	max_x = width + min_x;

	temp.clear();

	temp.format("{:s}-text", scroller_id);
	// magic: span elements seem to be hidden
	auto* p = doc->GetElementById(temp.c_str());
	if (p)
		p = (wxSVGElement*)p->GetChildren();
	if (p)
		text = p->GetChildren();
}

void livebuffer_t::init(wxSVGDocument* doc) {
	level_indicator::init(doc);

	ss::string<32> temp;
	temp.format("{:s}-indicator-ref", scroller_id);
	indicator_ref = doc->GetElementById(temp.c_str());
	temp.clear();
	temp.format("{:s}-indicator-box", scroller_id);
	indicator_box = doc->GetElementById(temp.c_str());

	wxSVGTransformable* box_element = wxSVGTransformable::GetSVGTransformable(*indicator_box);

	wxSVGTransformable* ref_element = wxSVGTransformable::GetSVGTransformable(*indicator_ref);
	indicator_ref_x = ref_element->GetResultBBox().GetX();

	wxSVGTransformList transforms = box_element->GetTransform().GetBaseVal();
	if (transforms.Count() == 0)
		box_element->Transform(wxSVGMatrix()); // create 1 transform
}

void level_indicator::set_values(double low, double high) {
	if (scroller && bar) {
		low_val = std::min(std::max(low, min_val), max_val);
		high_val = std::min(std::max(high, low_val), max_val);
		auto low_x = ((low_val - min_val) * width) / (max_val - min_val);
		auto high_x = ((high_val - min_val) * width) / (max_val - min_val);

    // works, but only for rectangle and not for scaled group
		auto* s = dynamic_cast<wxSVGRectElement*>(scroller);
		s->SetX(min_x + low_x);
		s->SetWidth(high_x - low_x);
	}
	if (text) {
		ss::string<16> str;
		str.format("{:3.1f}dB", high);
		auto s = wxString::FromUTF8(str.c_str());
		text->SetContent(s);
	}
}

void livebuffer_t::set_indicator_value(double val) {
	if (indicator_box) {
		val = std::min(std::max(val, min_val), max_val);
		auto x = ((val - min_val) * width) / (max_val - min_val);
		{
			wxSVGTransformable* element = wxSVGTransformable::GetSVGTransformable(*indicator_box);
			wxSVGTransformList transforms = element->GetTransform().GetBaseVal();
			auto matrix = wxSVGMatrix();
			// matrix=matrix.Translate(this->min_x-x2+x,0);
			matrix = matrix.Translate(this->min_x - indicator_ref_x + x, 0);
			transforms[transforms.Count() - 1].SetMatrix(matrix);
			element->SetTransform(transforms);
		}
	}
}

void text_box::init(wxSVGDocument* doc) {
	ss::string<32> temp;

	temp.format("{:s}-text", id);
	// magic: span elements seem to be hidden
	auto* p = doc->GetElementById(temp.c_str());
	if (p)
		p = (wxSVGElement*)p->GetChildren();
	if (p)
		text = p->GetChildren();
	if (!text)
		dterrorf("Could not find svg element {:s}", temp.c_str());
}

void text_box::set_value(const ss::string_& val) {
	if (text) {
		auto s = wxString::FromUTF8(val.c_str());
		text->SetContent(s);
	}
}

void text_box::set_value(const char* val) {
	if (text) {
		auto s = wxString::FromUTF8(val);
		text->SetContent(s);
	}
}

void text_box::set_value(int x, const char* fmt) {
	ss::string<32> val;
	val.format(fmt::runtime(fmt), x);
	if (text) {
		auto s = wxString::FromUTF8(val.c_str());
		text->SetContent(s);
	}
}

void text_box::set_time_value(time_t t, const char* fmt_) {
	ss::string<32> val;
	val.format(fmt::runtime(fmt_), fmt::localtime(t));
	if (text) {
		auto s = wxString::FromUTF8(val.c_str());
		text->SetContent(s);
	}
}

class svg_overlay_impl_t : public svg_overlay_t {
	friend class svg_overlay_t;
	wxSVGCtrl svgctrl;

public:
	bool uptodate{false};
	wxSVGElement* snr_panel{nullptr};
	bool snr_shown{true};
	level_indicator snr{"snr", "snr", 0.0, 20.0};
	level_indicator min_snr{"min-snr", "snr", 0.0, 20.0};
	level_indicator margin_snr{"margin-snr", "snr", 0.0, 20.0};
	level_indicator strength{"strength", "strength", -80.0, -20.0};
	livebuffer_t livebuffer{"livebuffer", "livebuffer", -7200, -20.0};
	text_box chno{"service-chno"};
	text_box service{"service"};
	text_box lang{"lang"};
	text_box epg{"epg-title"};
	text_box start_time{"start-time"};
	text_box end_time{"end-time"};
	text_box play_time{"play-time"};
	text_box rec{"recording"};
	wxSVGElement* scrollbar_scroller{nullptr};
	wxSVGElement* scrollbar_bar{nullptr};
	wxSVGDocument* doc{nullptr};
	wxSvgXmlNode* root{nullptr};
	svg_overlay_impl_t(const char* filename);
	~svg_overlay_impl_t();
	void traverse_xml(wxSVGElement* parent, int level = 0);
	int init();
	void show_snr(bool show);
};

/*
	show or hide the snr date (show if data is available, otherwise hide)
*/
void svg_overlay_impl_t::show_snr(bool show) {
	if (show == snr_shown || !snr_panel)
		return;
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	auto k = wxString::FromUTF8("visibility");
	wxString val { show ? "visibility" : "hidden"};
	snr_panel->SetAttribute(k, val);
	snr_shown = show;
	self->uptodate = false;
}

void svg_overlay_impl_t::traverse_xml(wxSVGElement* parent, int level) {
	wxSVGElement* elem = (wxSVGElement*)parent->GetChildren();
	while (elem) {
		std::string content = elem->GetContent().ToStdString();
		auto x = floatattr(elem, "x");
		auto y = floatattr(elem, "y");
		auto width = floatattr(elem, "width");
		auto height = floatattr(elem, "height");
		dtdebugf("{:<{}}"
						 "selement[{:d}] {:p} s={:s} x={:f} y={:f} w={:f} h={:f}",
						 "", level,
						 level, fmt::ptr(elem), content, x, y, width, height);
		traverse_xml(elem, level + 1);
		elem = (wxSVGElement*)elem->GetNext();
	}
}

svg_overlay_t::svg_overlay_t(const char* filename) : svg_filename{filename} {}

svg_overlay_impl_t::svg_overlay_impl_t(const char* filename) : svg_overlay_t(filename), svgctrl() { init(); }

svg_overlay_t::~svg_overlay_t() {}

svg_overlay_impl_t::~svg_overlay_impl_t() {}

int svg_overlay_impl_t::init() {
	bool ok = svgctrl.Load(svg_filename.c_str());
	if (!ok) {
		dterrorf("Could not open {:s}", svg_filename.c_str());
		return -1;
	}
	doc = svgctrl.GetSVG();
	root = doc->wxSvgXmlDocument::GetRoot();
	snr_panel = doc->GetElementById("snr-panel");
	if(!snr_panel) {
		dterrorf("Could not create snr_panel");
		assert(0);
		return -1;
	}
	show_snr(false);
	snr.init(doc);
	margin_snr.init(doc);
	min_snr.init(doc);
	strength.init(doc);
	chno.init(doc);
	service.init(doc);
	lang.init(doc);
	epg.init(doc);
	start_time.init(doc);
	end_time.init(doc);
	play_time.init(doc);
	livebuffer.init(doc);
	rec.init(doc);
	return 0;
}

uint8_t* svg_overlay_t::render(int window_width, int window_height) {
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	if (self->uptodate && surface && this->window_width == window_width && this->window_height == window_height) {
		return surface;
	}

	self->uptodate = true;
	surface = self->doc->RenderGetRef(window_width, window_height, NULL, true, true); //crash sometimes
	this->window_width = window_width;
	this->window_height = window_height;
	return surface;
}

std::unique_ptr<svg_overlay_t> svg_overlay_t::make(const char* filename) {
	return std::make_unique<svg_overlay_impl_t>(filename);
}

void svg_overlay_t::update_snr(double snr, double strength, double min_snr) {
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	auto margin_snr = min_snr + 2.0;
	snr /= 1000.;
	strength /= 1000.;
	self->snr.set_upper_value(snr);
	self->min_snr.set_upper_value(std::min(min_snr, snr));
	self->margin_snr.set_upper_value(std::min(margin_snr, snr));
	self->strength.set_upper_value(strength);
	if (self->scrollbar_scroller) {
	}

	self->uptodate = false;
}


static system_time_t livebuffer_horizon(const playback_info_t& playback_info) {
	if (playback_info.start_time > playback_info.end_time - 5min)
		return playback_info.end_time - 10min;
	if (playback_info.start_time > playback_info.end_time - 15min)
		return playback_info.end_time - 30min;
	if (playback_info.start_time > playback_info.end_time - 30min)
		return playback_info.end_time - 60min;
	if (playback_info.start_time > playback_info.end_time - 60min)
		return playback_info.end_time - 120min;
	return playback_info.start_time;
}

void svg_overlay_t::set_livebuffer_info(const playback_info_t& playback_info) {
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	auto& epg = playback_info.epg;
	if (epg.has_value()) {
		self->livebuffer.min_val = epg->k.start_time;
		self->livebuffer.max_val = epg->end_time;
	} else {
		self->livebuffer.min_val = system_clock_t::to_time_t(livebuffer_horizon(playback_info));
		self->livebuffer.max_val = system_clock_t::to_time_t(playback_info.end_time);
	}
	self->livebuffer.set_values(system_clock_t::to_time_t(playback_info.start_time),
															system_clock_t::to_time_t(playback_info.end_time));
	self->livebuffer.set_indicator_value(system_clock_t::to_time_t(playback_info.play_time));
}

static const char* recording_status_text(epgdb::rec_status_t status, bool is_timeshifted) {
	using namespace epgdb;
	switch (status) {
	case rec_status_t::SCHEDULED: // should not happen
		return "???";
	case rec_status_t::IN_PROGRESS:
		return "REC";
		break;
	case rec_status_t::FINISHING:
	case rec_status_t::FINISHED:
	default:
		break;
	};
	return is_timeshifted ? "TIMESHIFT" : "LIVE";
}

void svg_overlay_t::set_playback_info(const playback_info_t& playback_info) {
	set_livebuffer_info(playback_info);
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	self->uptodate = false;
	self->show_snr(!playback_info.is_recording);
	self->chno.set_value(playback_info.service.ch_order, "{:4d}");
	self->service.set_value(playback_info.service.name);
	self->lang.set_value(chdb::lang_name(playback_info.audio_language));
	if (playback_info.epg.has_value()) {
		auto& epg = *playback_info.epg;
		self->epg.set_value(epg.event_name.c_str());
		self->start_time.set_time_value(epg.k.start_time);
		self->end_time.set_time_value(epg.end_time);
		self->rec.set_value(recording_status_text(epg.rec_status, playback_info.is_timeshifted));
	} else {
		self->epg.set_value("");
		self->start_time.set_time_value(system_clock_t::to_time_t(livebuffer_horizon(playback_info)));
		self->end_time.set_time_value(system_clock_t::to_time_t(playback_info.end_time + 30s)); // add 30 seconds to round
																																														// up
		self->rec.set_value(recording_status_text(epgdb::rec_status_t::NONE, playback_info.is_timeshifted));
	}
	self->play_time.set_time_value(system_clock_t::to_time_t(playback_info.play_time), "{:%H:%M:%S}");
}

void svg_overlay_t::set_signal_info(const signal_info_t& signal_info, const playback_info_t& playback_info) {
	set_playback_info(playback_info);
	auto* self = dynamic_cast<svg_overlay_impl_t*>(this);
	float min_snr = chdb::min_snr(signal_info.driver_mux);
	self->update_snr(signal_info.last_stat().snr, signal_info.last_stat().signal_strength, min_snr);
}
