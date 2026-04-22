/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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
int dmx_set_pes_filter(int demuxfd, int pid);
int dmx_set_stid_stream(int demuxfd, int stid_pid, int stid_isi);
int dmx_set_t2mi_stream(int demuxfd, int t2mi_pid);
namespace chdb {
	struct mux_key_t;
}
int dmx_set_mux(int demux_fd, chdb::mux_key_t mux_key, int initial_pid, bool bbframes_on);
