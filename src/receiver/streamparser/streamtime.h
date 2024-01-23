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

#define DTPACKED __attribute__((packed))
#include <iostream>
#include <iomanip>
#include "stackstring.h"
#include "neumotime.h"
#include "neumodb/serialize.h"
#include "neumodb/deserialize.h"
#include "fmt/core.h"

namespace dtdemux {




	/*! needed to avoid confusion between timestamps in milliseconds
		and pes_pts ticks according to a 90kHz clock
	*/

	struct pts_ticks_t {
		int64_t ticks = 0;

		pts_ticks_t() = default;

		explicit pts_ticks_t(uint64_t ticks_) : ticks(ticks_) {}

		explicit operator int64_t() const {
			return ticks;
		}
		pts_ticks_t operator-() const {
			pts_ticks_t ret;
			ret.ticks = - ticks;
			return ret;
		}
	};

	struct pcr_ticks_t {
		int64_t ticks = 0;

		pcr_ticks_t() = default;
		explicit pcr_ticks_t(int64_t ticks_) : ticks(ticks_) {}

		explicit operator int64_t() const {
			return ticks;
		}

		pcr_ticks_t operator-() const {
			pcr_ticks_t ret;
			ret.ticks = - ticks;
			return ret;
		}
	};


	/*
		The constraint specified by MPEG is
		that a time stamp should occur at least every 0.7 seconds
		in a video or audio packetised elementary stream
	*/
	/*
		mpeg time stamp in units of 90kHz clock.
		33 useful bits, stored starting at the MSB
		value 1 means: invalid
		value 1<<30000 means 1/90 of 1 millisecond
	*/

	struct pts_dts_t {
		uint64_t time = 1; //33 useful bits, stored starting at the MSB

		bool is_valid() const {
			return time != 1;
		}

		pts_dts_t() = default;

		pts_dts_t(const pts_dts_t&) = default;

		/*
			initializer is in ticks of 1/90 ms.
		*/
		pts_dts_t(const pts_ticks_t ticks) : time(((int64_t)ticks)<<31)
		{}

		pts_dts_t& operator=(const pts_dts_t& other) = default;

		pts_dts_t& operator=(const pts_ticks_t ticks) {
			time = ((int64_t)ticks)<<31;
			return *this;
		}

		pts_dts_t& operator+=(const pts_dts_t& other);

		pts_dts_t scale(int v) const {
			pts_dts_t out;
			if(v>0) {
				out.time = (time>>v)& 0xffffffff80000000;
			} else if (v<0) {
				v = -v;
				out.time = time<<v;
			}	 else
				return  *this;
			return out;
		}


		pts_dts_t operator+(const pts_ticks_t delta_ticks) const  {
			pts_dts_t ret;
			assert(is_valid());
			//ret.time = (time+delta) & ((((uint64_t)1)<<33)-1);
			ret.time = time+ (((int64_t)delta_ticks)<<31);
			return ret;
		}

		pts_dts_t operator-(const pts_ticks_t delta_ticks) const  {
			assert(is_valid());
			return *this + (-delta_ticks);
		}

		/*
			in units of 90 kHz clock
			This returns negative values as well as positive ones and so can express also
			negative differences between two pts_dts_t values
		*/

		explicit operator pts_ticks_t () const  {
			assert(is_valid());
			return pts_ticks_t(((int64_t)time)>>31);
		}

		/*
			raw timestamp is an encoded version of ticks, i.e., ticks<<31
		*/
		static pts_dts_t from_raw_timestamp(uint64_t  timestamp) {
			pts_dts_t ret; ret.time = timestamp;
			return ret;
		}


		milliseconds_t milliseconds() const {
			uint64_t ull = time>>31;
			return milliseconds_t(ull/90L);
		}


	};


	/*
		Subtract two timestamps taking into account overflow.
		Among all modulo versions, always returns the difference smallest in abs val
	*/
	inline pts_dts_t operator-(const pts_dts_t& a, const pts_dts_t&b) {
		//int64_t x = (int64_t)(a.time<<31) - (int64_t)(b.time<<31);
		assert(a.is_valid());
		assert(b.is_valid());
		int64_t x = (int64_t)(a.time) - (int64_t)(b.time);
		return pts_dts_t(pts_ticks_t(x >> 31));
	}

	inline pts_dts_t operator+(const pts_dts_t&a,  const pts_dts_t&b) {
		assert(a.is_valid());
		assert(b.is_valid());
		return a + (const pts_ticks_t)b;
	};

	inline pts_dts_t& pts_dts_t::operator+=(const pts_dts_t& other)
	{
		assert(is_valid());
		assert(other.is_valid());
		*this = *this + other;
		return *this;
	}

	inline bool operator<(const pts_dts_t& a, const pts_dts_t&b) {
		//note that a-b handles wrap around; This is not the same as a.time<b.time
		return ((int64_t)(a.time - b.time))<0;
	}

	inline  bool operator<=(const pts_dts_t& a, const pts_dts_t&b) {
		//note that a-b handles wrap around; This is not the same as a.time<=b.time
		return ((int64_t)(a.time - b.time))<=0;
	}

	inline bool operator>=(const pts_dts_t& a, const pts_dts_t&b) {
		return !operator<(a,b);
	}

	inline bool operator>(const pts_dts_t& a, const pts_dts_t&b) {
		return !operator<=(a,b);
	}

	inline bool operator!=(const pts_dts_t& a, const pts_dts_t&b) {
		return a.time != b.time;
	}

	inline bool operator==(const pts_dts_t& a, const pts_dts_t&b) {
		return a.time == b.time;
	}

}

	/*
		A Programme Clock Reference
		for each Programme Clock must appear in the
		transport stream at least every 0.1 seconds

	*/
	/*@todo: time and extenstion can be stored in the same 64 bit integer
		time is already left shifted by 31 bits. extension is only 9 bit.
		After each + operation, extension needs to be normalized
		FOr a - operation, things are less clear.
	*/
namespace dtdemux {
	struct pcr_t {
		uint64_t time = 1; /*
													time stores the value of pcr_base (a 33 bit number) but left shifted
													by 31 bits. This means that we can apply uint64_t computations to
													achieve a proper overflow. It does mean that delta's added to the clock
													must also be left shifted by 31 bits

													extension is stored as is, but in subtractions it is always normalized
													to be positive

													Two pcr_t values can be subtracted, according to the rules of modulo arithmetic.

													A pcr_t can be converted to an int64_t. In this case, the returned value
													is a signed integer, which is handy when the pcr_t is the subtraction of
													two other similar pcr_t's

													value of 1 means invalid
											 */

		uint16_t extension = 0;

		bool is_valid() const {
			return time != 1;
		}

		pcr_t(const pcr_t & other) = default;
		pcr_t() = default;

		//pts_ticks_t is correct!
		pcr_t(const pts_ticks_t val) : time(((int64_t)val)<<31), extension(0)
		{}

		pcr_t(const pts_ticks_t val, int32_t _extension) : time(((int64_t)val)<<31), extension(_extension)
		{}

	pcr_t(const pts_dts_t& other) : time(other.time), extension(0)
		{}

		pcr_t& operator=(const pcr_t& other) = default;
		pcr_t& operator=(const pcr_ticks_t other) {
			time = ((int64_t)other)<<31;
			extension = 0;
			return *this;
		}

		pcr_t& operator+=(const pcr_t& other);

		//pts_ticks_t is correct!
		pcr_t operator+(const pts_ticks_t delta) const  {
			pcr_t ret;
			ret.time = time + (((int64_t)delta)<<31);
			ret.extension = extension;
			return ret;
		}

		pcr_t operator-(const pts_ticks_t delta) const  {
			return *this + (-delta);
		}

		pcr_t scale(int v) const {
			pcr_t out;
			if(v>0) {
				out.time = (this->time>>v)& 0xffffffff80000000;
				out.extension = this->extension >> v;
			} else if (v<0) {
				v = -v;
				out.time = this->time<<v;
				out.extension = this->extension << v;
				if(out.extension>300) {
					int64_t ticks = out.extension/300;

					out.time+= (ticks<<31);
					out.extension -= ticks*300;
				}

			} else
				return *this;
			return out;
		}

		explicit operator pts_dts_t() const {
			pts_dts_t p;
			p.time = time; //this part is equivalent to a pts
			return p;
		}

		/*
			in units of 27 Mhz clock
			This returns negative values as well as positive ones and so can express also
			negative differences between two pcr_t values

		*/
		explicit operator pcr_ticks_t() const {
			return pcr_ticks_t((((int64_t)time)>>31)*300 + extension);
		}

		milliseconds_t milliseconds() const {
			auto ull = ((int64_t&)time)>>31;
			return milliseconds_t(ull/90L);
		}


		static const pcr_t max_pcr_interval;

	};

	/*
		Subtract two timestamps taking into account overflow.
		Among all modulo versions, always returns the difference smallest in abs val
	*/
	inline pcr_t operator-(const pcr_t& a, const pcr_t&b) {
		pcr_t ret;
		//handle overflow
		ret.time = a.time - b.time;
		int delta = a.extension - b.extension;
		if(delta>=300) {
			while(delta >300) {
				delta -= 300;
				ret.time+= (((uint64_t)1)<<31);
			}
		} else if (delta <0) {
			while(delta<0) {
				delta += 300;
				ret.time -= (((uint64_t)1)<<31);
			}
		}
		ret.extension = delta;
		return ret;
	}

	inline pcr_t operator+(const pcr_t&a,  const pcr_t&b) {
		pcr_t ret;
		//handle overflow
		ret.time = a.time + b.time;
		ret.extension = a.extension + b.extension;
		while (ret.extension>300) {
			ret.time+= (((uint64_t)1)<<31);
			ret.extension -=300;
		}
		return ret;
	};

	inline pcr_t& pcr_t::operator+=(const pcr_t& other)
	{
		*this = *this + other;
		return *this;
	}


	inline bool operator<(const pcr_t& a, const pcr_t&b) {
		//note that a-b handles wrap around; This is not the same as a.time<b.time
		if (a.time== b.time)
			return a.extension < b.extension;
		return ((int64_t)(a.time - b.time))<0;
	}

	inline bool operator<=(const pcr_t& a, const pcr_t&b) {
		//note that a-b handles wrap around; This is not the same as a.time<=b.time
		if (a.time== b.time)
			return a.extension <= b.extension;
		return ((int64_t)(a.time-b.time))<=0;
	}

	inline bool operator>=(const pcr_t& a, const pcr_t&b) {
		return !operator<(a,b);
	}

	inline bool operator>(const pcr_t& a, const pcr_t&b) {
		return !operator<=(a,b);
	}

	inline bool operator==(const pcr_t& a, const pcr_t&b) {
		return a.time == b.time && a.extension == b.extension;
	}

	inline bool operator!=(const pcr_t& a, const pcr_t&b) {
		return ! operator==(a,b);
	}

	inline auto format_as(const pcr_t& a) {
		return pts_dts_t(a);
	}
}

template <> struct fmt::formatter<dtdemux::pts_dts_t> {
		inline constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator {
			return ctx.begin();
		}

	auto format(const dtdemux::pts_dts_t& a, format_context& ctx) const -> format_context::iterator;
};
