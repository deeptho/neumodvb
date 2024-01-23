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
#include "util/safe/safe.h"

/*
	Class which allows access only from one specific thread. Normally this is the thread
	which created the object
 */
template<typename data_t>
class thread_private_t  {
	data_t d;
	std::thread::id owner;
	const char* owner_name;
public:

	void set_owner() {
		owner=std::this_thread::get_id();
	}
	void set_owner(std::thread::id owner_) {
		owner=owner_;
	}

	template<typename ...Types>
		thread_private_t(const char* owner_name, Types... args) :
	d(args...), owner_name(owner_name) {
		set_owner(std::this_thread::get_id());
	}

	data_t* operator() () {
		//printf("GET ACCESS: {:d} {:d}\n", std::this_thread::get_id(), owner);
		if(true || std::this_thread::get_id() == owner) {
			//printf("returning %p\n", &d);
			return const_cast<data_t*>(&d);
		} else {
			dterrorf("Access from wrong thread: allowed={:s}", owner_name);
			assert(0);
			return nullptr;
		}
	}
};


template<typename map_t, typename element_t>
auto
find_in_safe_map(map_t& map, const element_t& element)
{
	auto y = map.readAccess();
	auto it = y->find(element);
	auto found = it != y->end();
	return std::make_tuple(it,found);
}

template<typename map_t, typename element_t>
auto
find_in_safe_map_with_owner_read_ref(map_t& map, const element_t& element)
{
	auto& y = map.owner_read_ref(); //important: must be reference (auto&)
	auto it = y.find(element);
	auto found = it != y.end();
	return std::make_tuple(it,found);
}

template<typename map_t, typename element_t>
auto
find_in_map(map_t& map, const element_t& element)
{
	auto it = map.find(element);
	auto found = it != map.end();
	return std::make_tuple(it,found);
}

template<typename map_t, typename fn_t>
auto
find_in_safe_map_if(map_t& map, fn_t fn)
{
	auto y = map.readAccess();
	auto it = std::find_if(y->begin(), y->end(), fn);
	auto found = it != y->end();
	return std::make_tuple(it,found);
}

template<typename map_t, typename fn_t>
auto
find_in_safe_map_if_with_owner_read_ref(map_t& map, fn_t fn)
{
	auto& y = map.owner_read_ref(); //important: must be reference (auto&)
	auto it = std::find_if(y.begin(), y.end(), fn);
	auto found = it != y.end();
	return std::make_tuple(it,found);
}



template<typename map_t, typename fn_t>
auto
find_in_map_if(map_t& map, fn_t fn)
{
	auto it = std::find_if(map.begin(), map.end(), fn);
	auto found = it != map.end();
	return std::make_tuple(it,found);
}
