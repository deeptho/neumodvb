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

#include<chrono>
using namespace std::chrono_literals;



using std::chrono::system_clock;
using system_clock_t = std::chrono::system_clock;
using steady_clock_t = std::chrono::steady_clock;
using system_time_t = std::chrono::time_point<std::chrono::system_clock> ;
using steady_time_t = std::chrono::time_point<std::chrono::steady_clock> ;
extern __thread system_time_t now;
