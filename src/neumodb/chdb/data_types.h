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

#ifdef UNUSED
class freq_pol_t {
public:
	uint32_t freq : 29 ;
	chdb::fe_polarisation_t pol: 3;
	freq_pol_t(uint32_t freq_=0, chdb::fe_polarisation_t pol_= chdb::fe_polarisation_t::NONE)
		: freq(freq_), pol(pol_) {}
	inline bool operator==(const freq_pol_t &other) const
		{
		 return other.freq == freq && other.pol == pol;
		}
};
#endif
