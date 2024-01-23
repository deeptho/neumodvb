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

class wxSVGCtrl;
class wxImage;

namespace epgdb {
struct epg_record_t;
};


struct signal_info_t;
struct playback_info_t;

class svg_t {
protected:
	svg_t() {};
	uint8_t* surface{nullptr};
	int window_width{-1};
	int window_height{-1};

public:
	inline int get_width() { return window_width;}
	inline int get_height() { return window_height;}
	virtual ~svg_t() {};
	virtual uint8_t* render(int window_width, int window_height) = 0;
};

class svg_overlay_t : public svg_t {
protected:
	ss::string<128> svg_filename;
	svg_overlay_t(const char* filename);
	double max_snr {18.0};
	double min_snr {2.0};
	void update_snr(double snr, double strength, double min_snr);

public:
	virtual ~svg_overlay_t() = 0;
	void set_signal_info(const signal_info_t& signal_info,
											 const playback_info_t& playback_info);
	void set_playback_info(const playback_info_t& playback_info);
	void set_livebuffer_info(const playback_info_t& playback_info);
	virtual uint8_t* render(int window_width, int window_height);
	static std::unique_ptr<svg_overlay_t> make(const char* filename);
};


class svg_radiobg_t : public svg_t {
protected:
	ss::string<128> svg_filename;
	svg_radiobg_t(const char* filename);
	bool updatedate{false};

public:
	virtual ~svg_radiobg_t() {};
	virtual uint8_t* render(int window_width, int window_height);
	static std::unique_ptr<svg_radiobg_t> make(const char* filename);
};
