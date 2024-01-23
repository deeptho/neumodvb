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
#include <cstdlib>
#include "mpeg.h"
#include "substream.h"

namespace dtdemux {


struct pes_parser_t : public ts_substream_t {
	uint16_t service_id = 0x00;
	uint8_t pes_stream_id = 0x00;
	int pes_packet_len = 0;
	int pes_header_len = 0; //total bytes of pes header, including mandatory and optional fields
	uint8_t flags1 = 0;
	uint8_t flags2 = 0;
	uint8_t pes_header_data_len = 0;

	pts_dts_t clock_period;

	pts_dts_t pts;
	pts_dts_t dts;

	pts_dts_t last_pts;
	pts_dts_t last_dts;

	pes_packet_desc_t current_frame; /*start is the first byte of the first pes packet of this frame;
																		 end is the first byte past the end of the pes packet*/

	int stream_type  = -1;

#if 0
	virtual bool end_of_pes_or_psi_stream() const {
			auto& limit = data_source->ts_packet_stream.end_display_time;
			return
				(limit.is_valid() && last_play_time >= limit) ||
				data_source->end_of_stream(); //all streams end for sure when there are no more bytes

		}
#endif

		bool has_pts() const {
			return pts.is_valid();
		}

		bool has_dts() const {
			return dts.is_valid();
		}

		const pts_dts_t& dts_or_pts() const {
			return dts.is_valid() ? dts : pts;
		}


		bool parse_pes_header();

		virtual void parse_payload_unit() override = 0;

 pes_parser_t(ts_stream_t& parent, int service_id, int pid, /*int stream_type,*/  const char* name) :
	 ts_substream_t(parent, false, name), service_id(service_id)
			{}

		pes_parser_t(const pes_parser_t& other) = delete;

		virtual ~pes_parser_t() {}
		virtual void unit_completed_cb() override;
	};


	struct video_parser_t : public pes_parser_t {
		typedef  stream_type::marker_t marker_t;
		//marker_t pic_type = marker_t::illegal;
		/*
			Report the smallest datarange completely enclusing the frame.
			Note that this range may contain packets of other PIDs. The
			reported length includes those bytes.

			http://www.tiliam.com/Blog/2015/07/06/effective-use-long-gop-video-codecs
		*/


		virtual void parse_payload_unit()  override = 0;
		void on_pes_start();
		video_parser_t(ts_stream_t& parent, int service_id, int pid, const char* _name) :
			pes_parser_t(parent, service_id, pid, /*stream_type,*/ _name)
			{
			}

		video_parser_t(const video_parser_t& other) = delete;
		virtual ~video_parser_t() {}
	};


	struct h264_parser_t : public video_parser_t {
		virtual void parse_payload_unit()  override;
		void parse_slice_header(int nal_unit_type);
		void parse_sps(int nal_unit_type);
		void parse_sei(int nal_unit_type);


	h264_parser_t(ts_stream_t& parent, int service_id, int pid) :
		video_parser_t(parent, service_id, pid, /*stream_type,*/ "h264")
			{}

		h264_parser_t(const h264_parser_t& other) = delete;

		virtual ~h264_parser_t() {}
	};

#ifdef NOTWORKING
	struct hevc_parser_t : public video_parser_t {
		virtual void parse_payload_unit()  override;

		hevc_parser_t(ts_stream_t& parent, int service_id, int pid) :
			video_parser_t(parent, service_id, pid, /*stream_type,*/ "hevc")
			{}

		hevc_parser_t(const hevc_parser_t& other) = delete;

		virtual ~hevc_parser_t() {}
	};
#endif

	struct mpeg2_parser_t : public video_parser_t {
		virtual void parse_payload_unit() override;

	mpeg2_parser_t(ts_stream_t& parent, int service_id, int pid) :
		video_parser_t(parent, service_id, pid, /*stream_type,*/ "mpeg2")
			{}

		mpeg2_parser_t(const mpeg2_parser_t& other) = delete;
		virtual ~mpeg2_parser_t() {}

	};


	struct audio_parser_t : public pes_parser_t {
		typedef  stream_type::marker_t marker_t;
		virtual void parse_payload_unit() override;
		void on_pes_start();
		audio_parser_t(ts_stream_t& parent, int service_id, int pid) :
			pes_parser_t(parent, service_id, pid, "AUDIO")
			{
			}

		audio_parser_t(const video_parser_t& other) = delete;
		virtual ~audio_parser_t() {}
	};



} //namespace dtdemux
