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
#include <boost/context/continuation_fcontext.hpp>
#include "neumodb/chdb/chdb_extra.h"

class receiver_thread_t;
class tuner_thread_t;
class receiver_t;
class active_adapter_t;


class spectrum_t {
	friend class receiver_thread_t;
	receiver_thread_t& receiver_thread;
	receiver_t& receiver;
	subscription_id_t subscription_id{-1};
	bool must_end = false;

public:
	spectrum_t(receiver_thread_t& receiver_thread_,
						subscription_id_t subscription_id);
	~spectrum_t();
};
