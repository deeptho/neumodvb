/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

#include "identification.h"
#include "util/logger.h"
#include "util/version.h"

#define BUILD_DATA ""
std::string version_info() {
	std::stringstream ss;
	ss << "BUILD=" << GIT_REV << "BRANCH=" << GIT_BRANCH;
	if (strlen(GIT_TAG) > 0)
		ss << " TAG= " << GIT_TAG;
	return ss.str();
}

void identify() {
	LOG4CXX_INFO(logger, "BUILD=" << GIT_REV << " TAG=" << GIT_TAG << " BRANCH=" << GIT_BRANCH);
}
