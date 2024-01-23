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
#include "util/dtutil.h"
#include <functional>
#include "mpeg.h"
#include "streamtime.h"
#include "streamparser.h"
#include "neumodb/recdb/recdb_extra.h"

namespace dtdemux {
	class ts_substream_t;
	struct pes_parser_t;

	struct event_handler_t {
		LoggerPtr logger = Logger::getLogger("indexer");
		neumodb_t* idxdb = nullptr;
	private:
		pcr_t  last_pcr;///@todo change this to pts_dts_t so that timeoffset can be reconstructed from db records
		pcr_t  ref_pcr; /* with t= time since start of playback,
													 t  = pcr - ref_pcr + ref_offset*/
		milliseconds_t ref_offset{0};
		milliseconds_t last_pcr_play_time{}; //defaults to invalid - only for debugging
		time_t start_time; //time at which stream starts

	public:
		uint64_t last_pat_start_bytepos = 0;
		uint64_t last_pmt_start_bytepos = 0;
		uint64_t last_pat_end_bytepos = 0;
		uint64_t last_pmt_end_bytepos = 0;
		recdb::marker_t last_saved_marker;

		bool ref_pcr_inited = false;
		bool ref_pcr_update_enabled = true; //set to false, when timing is read from database instead of from stream

		/*
			@todo: last_pcr - ref_pcr will overflow after 26 hours
			This is not handled yet and could cause problems in case a live buffer remains active for more than
			24 hours. //ref_pcr:
		*/

		milliseconds_t pcr_play_time_() {
			auto temp =  last_pcr - ref_pcr;
			auto ret=  temp.milliseconds();
			return ret +ref_offset;
		}

		milliseconds_t pcr_play_time() {
			auto temp =  last_pcr - ref_pcr;
			auto ret=  temp.milliseconds();
			assert (ret + ref_offset >= last_pcr_play_time);
			last_pcr_play_time = ret +ref_offset;
			return last_pcr_play_time;
		}

		void check_pcr_play_time() {
			auto ret= (last_pcr -ref_pcr).milliseconds() + ref_offset;
			if(!(ret>= last_pcr_play_time)) {
				dterrorf("ASSERT ret={} last_pcr_play_time={}", ret, last_pcr_play_time);
			}
			assert (ret>= last_pcr_play_time);
		}

		int64_t last_pos = -1;
		//end data for writer

		void set_range(const pts_dts_t& start_display_time,
									 const pts_dts_t& end_display_time,
									 const pts_dts_t& start_scan_completed) {
		}

		event_handler_t(neumodb_t* idxdb_ =nullptr)
			: idxdb(idxdb_)
			, start_time(time(NULL))
			{
			}


		void pcr_discontinuity_(int pid, const pcr_t& pcr);

		void pcr_update(int pid, const pcr_t& pcr, bool discontinuity_pending);

		/*
			called when dts deviates too much from what is expected
			returns new clock period
		*/
		pts_dts_t check_pes_discontinuity(pts_dts_t clock_period,
																			const pts_dts_t& old_dts,
																			const pts_dts_t& new_dts);


		std::tuple<int, int> get_packet_range_(const data_range_t& range) {
			/*
				In the case of an "end" event, range.end points to the first packet after the
				section or pes packet.
				Therefore the last packet number is (range.end-1)/188

			*/
			int first_packet = range.start_bytepos()/188;
			int end_packet = range.end_bytepos()/188; //one past the last packet
			int num_packets = end_packet - first_packet;

			assert(first_packet*(int64_t)188 == range.start_bytepos());
			assert(end_packet*(int64_t)188 == range.end_bytepos());
			return std::make_tuple(first_packet, num_packets);

		}

		void index_event(stream_type::marker_t unit_type,
										 /*uint16_t pid, uint16_t stream_type,*/
										 const milliseconds_t& play_time_ms, pts_dts_t pts,
										 pts_dts_t dts, uint64_t first_byte,
										 uint64_t last_byte, const char* name);


		void pts_update(bool isvideo, int pos, const pts_dts_t& pts) {

		}

		void au_start_cb();

		void packet_processed_cb();

		void pes_pts_cb(const pts_dts_t& pts);

		void frame_start_cb(
			stream_type::marker_t pic_type);

	};


} //namespace dtdemux
