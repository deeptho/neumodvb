/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "util/dtassert.h"
#include "stackstring/stackstring.h"
#include "util/safe/safe.h"

class monitor_t {
public:
	typedef ss::bytebuffer<32> key_t;
	enum update_type_t {
		added,
		updated,
		deleted
	};

	enum index_type_t {
		primary,
		secondary,
		temp
	};


	struct state_t {
	bool screen_content_changed = false; //has something changed to the records on screen?
	bool content_moved = false; /*records have been added or deleted before or AFTER the current screen
																such that scrollbar will move or list size will change
															*/
	int pos_top=0; //positional index of top record on the screen
	int list_size =0 ; //number of entries in the complete list
	key_t key_top{}; //key of op top record (if screen not empty
	key_t key_bottom{}; //key > bottom record
	};

	struct reference_t {
		int row_number{-1};
		ss::bytebuffer<32> primary_key;
		ss::bytebuffer<32> secondary_key;
		inline void update(const ss::bytebuffer_& secondary_key,
											 const ss::bytebuffer_& primary_key,
											 bool is_removal) {
			auto inc = is_removal ? -1 :1;
			auto ret = cmp(secondary_key, this->secondary_key);
			if(ret<0)
				row_number += inc;
			else if(ret==0) {
				ret = cmp(primary_key, this->primary_key);
				/*
					The following assertion tests if the reference itself is being removed
					from the database. Then it would become invalid. The caller must prevent this
					situation by either invalidating the reference or moving it to another record
				 */
				assert(ret !=0 || ! is_removal);
				if(ret<0)
					row_number += inc;
			}
		}
		inline void reset() {
			row_number = -1;
			primary_key.clear();
			secondary_key.clear();
		}
	};

	reference_t reference;
	reference_t auxiliary_reference;


	state_t state;
	int txn_id{-1};
protected:


	inline static int key_cmp(const ss::bytebuffer_& a, const ss::bytebuffer_& b)
	{
		auto x = memcmp((void*) a.buffer(), b.buffer(), (int)std::min(a.size(), b.size()));
		if(x!=0 || a.size() == b.size())
			return x;
		return (a.size() < b.size()) ? -1 : 1;
	}

public:


	bool filter() const {
		//@todo: implement filter. Note that this is record type specific
		return true;
	}

	void notify(const ss::bytebuffer_& key, update_type_t type);

};
