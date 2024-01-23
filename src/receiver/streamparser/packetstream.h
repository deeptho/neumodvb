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
#include <string.h>
#include "streamparser.h"
#include "util/logger.h"
#include "streamtime.h"
#include "substream.h"

#include "psi.h"
#include "pes.h"
#include "events.h"
#include "util/dtassert.h"

namespace dtdemux {

	struct pat_entry_t;

	struct ts_stream_t : public stream_parser_base_t< ts_stream_t>
	{
		uint16_t pcr_pid = null_pid; //pid used for creating timestamps: video_pid for tv, audio
		stream_type::stream_type_t pcr_stream_type =
			stream_type::stream_type_t::RESERVED;
		LoggerPtr logger = Logger::getLogger("indexer");
		constexpr static int PAT_PID = 0x0;
		constexpr static int CAT_PID = 0x0001;
		constexpr static int TSDT_PID = 0x0002;
		constexpr static int NIT_PID = 0x0010;
		constexpr static int ST_PID = 0x0010;
		constexpr static int SDT_PID = 0x0011;
		constexpr static int EIT_PID = 0x0012;
		constexpr static int TDT_PID = 0x0014;
		constexpr static int MHW_PID1 = 0x00D2;
		constexpr static int MHW_PID2 = 0x00D3;
		constexpr static int FREESAT_EIT_PID = 3842;
		constexpr static int FREESAT_EIT_PF_PID = 3843;
		constexpr static int FREESAT_INFO_EIT_PID = 3003;
		constexpr static int FREESAT_INFO_EIT_PF_PID = 3004;
		constexpr static int PREMIERE_EPG_PID1 = 0x0b11;
		constexpr static int PREMIERE_EPG_PID2 = 0x0b12;
		constexpr static int EEPG_PID = 0x0300;
		constexpr static int PID_SKY_TITLE_LOW= 0x0030;
		constexpr static int PID_SKY_TITLE_HIGH= 0x0037;
		constexpr static int PID_SKY_SUMMARY_LOW= 0x0040;
		constexpr static int PID_SKY_SUMMARY_HIGH= 0x0047;


		bool eof=false;
		data_range_t current_range; //range in data input buffer
		event_handler_t event_handler;

		std::function<void(uint16_t, const ss::bytebuffer_&)>  psi_cb =
			[](uint16_t pid, const ss::bytebuffer_& payload) {};
		uint32_t num_encrypted_packets{0};

		void set_eof() {
			eof = true;
		}

		void clear_data() {
			current_range.reset();
		}

		bool need_data() const {
			return (current_range.available() < av_pkt_size);
		}


		ts_packet_t* read_packet() {
			for(;;) {
				if(need_data()) {
					assert(current_range.available()==0);
					//dtdebugf("READ: pid=NULL");
					return NULL;
				}

				auto range = current_range.sub_range(current_range.processed(), av_pkt_size);
				assert(range.available() <=188);
				current_range.skip(av_pkt_size);
				global_ts_packet = ts_packet_t(range);
				assert(global_ts_packet.range.available() == global_ts_packet.range.tst);

				//dtdebugf("READ: pid={:d} cc={:d}", global_ts_packet.get_pid(), global_ts_packet.get_continuity_counter());
				if(global_ts_packet.range.is_valid())
					return &global_ts_packet;
			}
		}

		/*
			Undo  the last read operation. Only possible directly after a read!
		 */
		void undo_read() {
			current_range.unread(av_pkt_size);
		}

		/*
			Set a new buffer from which to read data

		*/
		void set_buffer(uint8_t* buffer, int64_t len) {
			if(len <= 0) {
				set_eof();
				return;
			}

			if(!need_data()) {
				dterrorf("set_range can only be called after current range was fully processed");
				assert(0);
				return;
			}
			//we always process in units of av_pkt_size;
			assert(current_range.available()==0);
			auto new_start_bytepos = current_range.start_bytepos() + current_range.len();
			current_range = data_range_t(buffer, len);
			current_range.set_start_bytepos(new_start_bytepos);
		}
		inline void unregister_psi_pid(uint16_t pid) {
			unregister_parser(pid);
		}
		void register_psi_pid(uint16_t pid, const char*label="psi") {
			auto parser=std::make_shared<section_parser_t>(*this, pid, label);
			register_parser(pid, [parser](ts_packet_t* p){
				log4cxx::NDC::push(parser->name);
				parser->parse(p);}); //psi
			//return parser;
		}


		template<typename parser_t, typename... Args>
		inline auto register_pid(int pid, const ss::string_& ndc_prefix, Args... args) {
			auto parser=std::make_shared<parser_t>(*this, pid, args...);
			ss::string<128> ndc;
			ndc.format("{:s} PID(0x{:x})", ndc_prefix, pid);
			register_parser(pid, [parser, pid, ndc](ts_packet_t* p){
				log4cxx::NDC::clear();
				log4cxx::NDC::push(ndc.c_str());
				parser->parse(p);});
			return parser;
		}


		auto register_pat_pid() {
			auto parser=std::make_shared<pat_parser_t>(*this);
			register_parser(PAT_PID, [parser](ts_packet_t* p){
				log4cxx::NDC::push("PAT");
				parser->parse(p);}); //pat
			return parser;
		}

		auto register_pmt_pid(int pmt_pid, int service_id) {
			auto parser=std::make_shared<pmt_parser_t>(*this, pmt_pid, service_id);
			register_parser(pmt_pid, [parser](ts_packet_t* p){
				log4cxx::NDC::push("PMT");
				parser->parse(p);}); //pmt
			return parser;
		}

		void register_audio_pids(int service_id, int video_pid, int pcr_pid,
														 stream_type::stream_type_t stream_type);

		void register_video_pids(int service_id, int video_pid, int pcr_pid,
														 stream_type::stream_type_t stream_type);


		ts_stream_t(neumodb_t* idxdb =nullptr) : event_handler(idxdb) {
	}

	};


};
