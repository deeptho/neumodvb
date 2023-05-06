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

#include "neumo.h"
#include "task.h"
//#include "simgr/simgr.h"
#include "util/access.h"
#include "devmanager.h"
#include "options.h"
#include "recmgr.h"
#include "mpm.h"
#include "signal_info.h"
#include "streamparser/packetstream.h"
#include "streamparser/psi.h"
#include "util/safe/safe.h"

/*@brief: all reservation data for a service. reservations are modified atomically while holding
	a lock. Afterwards the tune is requested to change state to the new reserved state, but this
	may take some time
*/
class service_reservation_t {
	friend class receiver_thread_t;

	active_adapter_t* active_adapter {nullptr};
	chdb::service_t reserved_service;   //
	use_count_t use_count; /* if >0 it is not allowed to stop the service*/

	inline int check_consistency(int x) {

		return 0;
	}

public:

	bool is_same_service(const chdb::service_t& service) const {
		return service.k == reserved_service.k;
	}

	/*!
		returns the old service use count
	*/
	int reserve_service(active_adapter_t & active_adapter_, const chdb::service_t& service) {
		if (is_same_service(service)) {
		} else {
			assert(use_count() ==0);
			reserved_service = service;
			active_adapter = &active_adapter_;
		}
		return check_consistency(use_count.register_subscription());
	}

	int reserve_current_service(const chdb::service_t& service) {
		assert(is_same_service(service));
		return check_consistency(use_count.register_subscription());
	}

	/*!
		returns the new service use count
	*/
	int release_service() {
		return use_count.unregister_subscription();
	}


};
