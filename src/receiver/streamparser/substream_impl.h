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

namespace dtdemux {
void ts_substream_t::parse(ts_packet_t* p) {
	int pid = p->get_pid();
	current_ts_packet = p;
	process_packet_header(current_ts_packet);
	dtdebugf("parser[{:d}] start", pid);
	for(;;) {
		if(!p) //end of file before parser can even start
			break;

		try {
			/*
				If a continuity counter error is detected, get_next_packet will throw a bad_data_exception
				All operations which discover bad data will also throw such an exception
				In this case, the current parser completely aborts and will try to resynchronize using skip to unit_start

				This exception throwing could be ineffficient on bad streams. An alternative is therefore to
				simply set an "error has occurred" flag and test this after parsing a payload. In this case
				the parser must carefully handle all bad data and continue processing as well as it can, but without
				causing side effects such as allocating unreasonable amounts of memory
				The has_error_test() below then takes the required action (skipping to the next payload unit start)
				possibly after needlessly  parsing bad data.

			 */
			skip_to_unit_start();
			if(has_error() || has_encrypted())
				goto next_;
			if(is_psi)
				skip_to_pointer(); //move to the start byte of the table header
			while(!has_error() &&! has_encrypted()) {
				if(is_psi) {
					//pointer_field = current_ts_packet->range.get<uint8_t>();
					/*See EN 300 468 v1.15.1, pp. 21
						pointer field points to start of first section in packet (one packet can contain multiple sections)
						from then on, sections succeed each other
						The last section in a packet can consist of stuffing (0xff). This can be found by checking
						for table_id==0xff, which indicates that all the other bytes are stuffing

					*/
					bytes_read = 0;
					parse_payload_unit();
				} else { // ! is_psi
					bytes_read = 0;
					parse_payload_unit();
					wait_for_unit_start = true;
					get_next_packet();
					skip_to_unit_start(); //will reset wait_for_unit_start but not do anything else
				}
			}
		next_:
			while(has_error()||has_encrypted()) {
				clear_error();
				wait_for_unit_start = true;
				get_next_packet();
				clear_encrypted();
			}
		} catch(bad_data_exception()) {
			dterrorf("parser[{:d}]: bad_data_exception", pid);
		};
		//current_ts_packet = p;
	}
	//printf("parser[{:d}]: exiting\n", pid);
}

} //namespace dtdemux
