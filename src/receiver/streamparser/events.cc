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

#include "events.h"
#include "neumotime.h"
#include "substream.h"

#define UNUSED __attribute__((unused))
using namespace dtdemux;

/*
	time computation principles:
	-last_time is always monotonic and is based on pcr
	-a time base discontinuity can be signalled in the stream by having one or more packets
	with discontinuity==1. In this case the new time base comes into effect when such a packet contains
	a pcr.
	-in case packets are lost in the transport stream, this is discovered by continuity
	errors and/or by pcr's which come too early (before the last one) or too late (more than 100 ms
	after the last one). In case the clock could be continuous, but it may also be docontinuous in
	case packets with discontinuity==1 were lost.
	The default is to assume that no discontinuity occured, but we perform some sanity checks:
	-in case of a live stream, the time of reception of the last packet before the loss and the first
	packet after, must match the PCR difference within a tolerance of 30 seconds
	-in case of a non-live stream, the PCR difference must be less than 5 minutes
	If we detect or suspect a discontinuity, we adjust the clock based based on the next received
	video frame (which should come first in normal cases) or audio frame (in case of stream loss),
	which ever comes first
*/

void event_handler_t::pcr_discontinuity_(int pid, const pcr_t& pcr) {
	const char* label = "PCR_DISC: ";
	dterrorf("PCR DISC: pcr={} ref_pcr={} last_pcr={}", pcr, ref_pcr, last_pcr);
	/*This value will be corrected later. Normally we will now decode a video frame.
		If the time is earlier than the expected next frame time, the value will be adjiusted accordingly
	*/
	if (ref_pcr_update_enabled) {
		auto now = time(NULL);
		auto t = milliseconds_t((now - start_time) * 1000); // in perfect conditions this should match the stream time
		auto t_old = pcr_play_time();												// play_time before the discont
		last_pcr = pcr;
		auto t_new = pcr_play_time_(); // uncorrected play_time after the discont
		// ensure that t_new + correction >= t_old + 5 (playtime must move forward)
		auto correction_lower_limit = t_old + milliseconds_t(5) - t_new;

		// we would like play time to differ as little as ossible from real time, so delta=0 is ideal
		auto correction = std::max(t - t_new, correction_lower_limit);

		// time_t is only accurate up to one second
		if (correction < milliseconds_t(1000)) // most likely: no discontinuities in stream, but packet loss
			correction = std::max(correction_lower_limit, milliseconds_t(0));
		dterrorf("PCR DISC: t_old={} t_new={}", t_old, t_new, t);
		ref_offset += correction;

		// now also update the reference itself, to support very long streams (overflow of pcr_t)
		auto delta = (last_pcr - ref_pcr).milliseconds();
		ref_offset += delta;
		ref_pcr = last_pcr;
		auto t_test = pcr_play_time();
		dterrorf("PCR DISC: t_old={} t_new={} t_test={}", t_old, t_new, t_test);
		assert(t_test > t_old);
		check_pcr_play_time();
	}
}

void event_handler_t::pcr_update(int pid, const pcr_t& pcr, bool discontinuity_pending) {
	/*p. 93 of iso13818-1.pdf: discontinuity_pending=1 may be set in multiple packets of a stream
		designated as "pcr-pid" the first such packets then do not contain a pcr). The next PCR after
		this bit (which is in the same packet as the discontinuity_pending=1 bit is in the new time base.
		The continuity counter may be discontinuous when discontinuity_pending=1, in two cases:
		1. non-pcr streams
		2. pcr stream, but only in the packet where pcr changes
	*/

	if (discontinuity_pending)
		return pcr_discontinuity_(pid, pcr);
	const char* label = "PCR_UPDATE: ";
	if (ref_pcr_update_enabled) {
		if (!ref_pcr_inited) {
			ref_pcr = pcr;
			ref_pcr_inited = true;
		} else {
			auto delta = (pcr - last_pcr); // note that pcr may overflow!; int64_t returns signed clock ticks
			if (delta > pcr_t::max_pcr_interval || pcr < last_pcr) {
				dterrorf("PCR jump detected: from={} to={}", last_pcr, pcr);
				pcr_discontinuity_(pid, pcr);
			}
		}
	}
	if (last_pcr.is_valid() && pcr < last_pcr) {
		dterrorf("PCR goes back in time: old={} new={}", pcr, last_pcr);
	} else
		last_pcr = pcr;
	check_pcr_play_time();
}

/*
	called when dts deviates too much from what is expected
	returns new clock period
*/
pts_dts_t event_handler_t::check_pes_discontinuity(pts_dts_t clock_period, const pts_dts_t& old_dts,
																									 const pts_dts_t& new_dts) {
	auto timedelta = new_dts - old_dts;
	const auto zero = pts_dts_t();
#if 0 /*Bad idea: pes could be corrupted due to decryption issues.	\
				This can make the clock jump																\
			*/
	if(timedelta != clock_period && clock_period != zero) {
		//auto p = clock_period;
		if(timedelta < clock_period) {
			if(timedelta < zero) {
				dtinfof("clock discontinuity from= {} to{}", old_dts, new_dts);
			} else {
				dtinfof("adjusting clock by {}", (clock_period -timedelta));
			}
			//move clock forward/backward to the desired time
			if(ref_pcr_update_enabled)
				ref_pcr += (clock_period - timedelta);
		} else  {
			//We seem to have a large jump forward in time.
			//We remove the gap caused by lost packets, but we must be careful to maintain monotonicity
			//However, other stream's pts will correct if needed.
			if(ref_pcr_update_enabled)
				ref_pcr += (clock_period - timedelta); // Can be a negative value!!
		}
	} else if(!clock_period.is_valid() || timedelta < clock_period  || clock_period == zero ) {
		if (clock_period.is_valid()) {
			dtdebugf("reducing clock period from {} to {}", clock_period, timedelta);
		} else {
			dtdebugf("setting clock period to {}", timedelta);
		}
		clock_period = timedelta;
	}
#else
	if (timedelta != clock_period && clock_period != zero) {
		if (timedelta < clock_period) {
			if (timedelta < zero) {
				dtinfof("clock discontinuity from={} to={}", old_dts, new_dts);
			} else {
				dtinfof("adjusting clock by {}", (clock_period - timedelta));
			}
		}
	} else if (!clock_period.is_valid() || timedelta < clock_period || clock_period == zero) {
		if (clock_period.is_valid()) {
			dtdebugf("reducing clock period from {} to {}", clock_period, timedelta);
		} else {
			dtdebugf("setting clock period to {}", timedelta);
		}
		clock_period = timedelta;
	}
#endif
	return clock_period;
}

void event_handler_t::index_event(stream_type::marker_t unit_type,
																	/*uint16_t pid, uint16_t stream_type,*/
																	const milliseconds_t& play_time_ms, pts_dts_t pts, pts_dts_t dts, uint64_t first_byte,
																	uint64_t last_byte, const char* name) {
	if (!idxdb)
		return;
	auto first_packetno = first_byte / ts_packet_t::size;
	auto last_packetno = last_byte / ts_packet_t::size;
	auto num_packets = last_packetno - first_packetno;
	if (ref_pcr_inited) {
		if (unit_type == stream_type::marker_t::illegal) {
			dtdebugf("IDX {:>8} ILLEGAL: [{:8d}, {:8d}] t={} pts={} dts={}", name, first_packetno, last_packetno,
							 play_time_ms, pts, dts);
		} else {
			dtdebugf("IDX {:>8} {:c}: [{:8d}, {:8d}] t={} pts={} dts={}", name, (char)unit_type,
							 first_packetno, last_packetno,
							 play_time_ms, pts, dts);
		}
		if (unit_type == stream_type::marker_t::i_frame || unit_type == stream_type::marker_t::pes_other) {
			auto start = std::min(last_pat_start_bytepos, last_pmt_start_bytepos) / ts_packet_t::size;
			auto end = std::max(last_pat_end_bytepos, last_pmt_end_bytepos) / ts_packet_t::size;
			start = std::min(start, first_packetno);
			end = std::max(end, last_packetno);
			// start and end point to a byte region containing a pat a pmt and an i-frame (and perhaps some other
			// data
			dtdebugf("WRITE pos=[{}, {}] time={}", start, end, play_time_ms);
			using namespace recdb;
			auto txn = idxdb->wtxn();
			{
				auto c = idxdb->tcursor<marker_t>(txn);
				last_saved_marker = marker_t(marker_key_t(play_time_ms), start, end);
				put_record(c, last_saved_marker);
			}
			txn.commit();
			///@todo create fewer transactions?
		}
	}
}
