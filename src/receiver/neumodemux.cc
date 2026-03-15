/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include <sys/ioctl.h>
#include "neumo-dmx.h"
#include "neumodemux.h"

int dmx_set_pes_filter(int demuxfd, int pid) {
	struct dmx_pes_filter_params pars;
	memset(&pars,0,sizeof(pars));
	pars.pid = pid;
	pars.input = DMX_IN_FRONTEND;
	pars.output = DMX_OUT_TSDEMUX_TAP;//DMX_OUT_TS_TAP;
	pars.pes_type = DMX_PES_OTHER;
	pars.flags = 0; //DMX_IMMEDIATE_START;
	dtdebugf("PES: fd={} Adding pid={}", demuxfd, pid);
	if(ioctl(demuxfd, DMX_SET_PES_FILTER, &pars) < 0) {
		dterrorf("DMX_SET_PES_FILTER  pid={} failed: {}", pid, strerror(errno));
		return -1;
	}
	return 0;
}

int dmx_set_stid_stream(int demuxfd, int stid_pid, int stid_isi) {
	struct dmx_stid_stream_params pars;
	memset(&pars,0,sizeof(pars));
	pars.embedding_pid = stid_pid;
	pars.isi = stid_isi;
	dtdebugf("STID: fd={} Adding pid={}", demuxfd, stid_pid);
	if (ioctl(demuxfd, DMX_SET_STID_STREAM, &pars) < 0) {
		dterrorf("DMX_SET_STID_STREAM  pid={} isi={} failed: {}", stid_pid, stid_isi,
						 strerror(errno));
		return -1;
	}
	return 0;
}

int dmx_set_t2mi_stream(int demuxfd, int t2mi_pid) {
	struct dmx_t2mi_stream_params pars;
	memset(&pars,0,sizeof(pars));
	pars.embedding_pid = t2mi_pid;
	pars.plp = T2MI_UNSPECIFIED_PLP; //not used
	dtdebugf("T2MI: fd={} Adding pid={}", demuxfd, t2mi_pid);
	if (ioctl(demuxfd, DMX_SET_T2MI_STREAM, &pars) < 0) {
		dterrorf("DMX_SET_T2MI_STREAM  pid={} failed: {}", t2mi_pid, strerror(errno));
		return -1;
	}
	return 0;
}

int dmx_set_mux(int demux_fd, chdb::mux_key_t mux_key, int initial_pid, bool bbframes_on) {
	auto stid_pid = (int16_t) 270;
	auto stream_id = mux_key.stream_id;
	auto t2mi_pid = mux_key.t2mi_pid;
	if (bbframes_on && stid_pid >= 0) {
		if(dmx_set_stid_stream(demux_fd, stid_pid, stream_id) <0)
			return -1;
	}
	if (t2mi_pid >= 0) {
		if(dmx_set_t2mi_stream(demux_fd, t2mi_pid)<0)
			return -1;
	}
	return dmx_set_pes_filter(demux_fd, initial_pid);
}
