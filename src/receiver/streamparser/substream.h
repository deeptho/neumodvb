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
#include "mpeg.h"
#include <sys/mman.h>
#include "util/dtassert.h"

#define RETURN_ON_ERROR  if (has_error()) return
#define RETURN_ON_ENCRYPTED  if (has_encrypted()) return
#define THROW_BAD_DATA { throw_bad_data(); return;}

#ifndef null_pid
#define null_pid 8191
#endif

namespace dtdemux {

	struct bad_data_exception {
	};


}

#include "streamtime_impl.h"

namespace dtdemux {

		/*Describes a possibly incomplete audio frame */
	struct pes_packet_desc_t {
		bool valid{false};
		uint64_t range_start = 0;
		uint64_t range_end = 0;
		bool is_valid() const {
			return valid;
		}

		pes_packet_desc_t() = default;

	pes_packet_desc_t(const ts_packet_t& packet) :
		valid(true), range_start(packet.range.start_bytepos())
		{
		}

		void set_end(uint64_t _end) {
			range_end = _end;
		}


		pes_packet_desc_t(const pes_packet_desc_t& other) = default;
		pes_packet_desc_t& operator=(const pes_packet_desc_t&other) = default;
	};

	/*
		Provides a reader for a packet stream containing a mixture of multiple pids.
		The stream will start outputting packets at byte position start and end at byte position
		end, at which point soft_end_of_stream will become true.

		@todo: handle the case of infinite streams

	*/


	constexpr int av_pkt_size =  dtdemux::ts_packet_t::size;


	struct ts_stream_t;

	class ts_substream_t   {
	protected:
		//LoggerPtr logger = Logger::getLogger("indexer");
		bool error = false; //a data error has occurred
		bool encrypted = false; //an encrypted packet was encountered
	public:
		int cc_error_counter{0}; //counts the number of cc errors since start
		bool throw_on_error = false;
		int64_t continuity_errors = 0;
		bool wait_for_unit_start = true; //if true, we discard all data until payload unit start
		//int _bytesread =0; //number of bytes read so far
		uint8_t continuity_counter = 0xff;
		uint8_t pointer_field = 0;
		int pointer_pos {-1}; //-1 means not set
		int num_encrypted_packets{0};
		bool is_psi = false;

		bool eof=false; //physical end packet, typically end of the file. Also, eof is een upper limit for end

		stream_type::marker_t current_unit_type = stream_type::marker_t::illegal;
		uint64_t current_unit_start_bytepos = 0; /*byte position (in the global stream) of the first byte  of the
																							 current pes_packet or current section*/
		uint64_t current_unit_end_bytepos = 0;  /*byte position (in the global stream) of the last byte  of the
																							 current pes_packet or current section*/
		ts_packet_t* current_ts_packet = NULL; //last read packet
		int bytes_read = 0;
	protected:
		ts_stream_t& parent;
	public:
		const char * name = "";
	protected:
		milliseconds_t first_play_time; //in milliseconds
		milliseconds_t last_play_time; //in milliseconds
	private:
		bool process_packet_header(ts_packet_t* p);
	public:
		virtual void unit_completed_cb() = 0;

		virtual void parse_payload_unit() {
		}

		void skip_to_unit_start();
		void skip_to_pointer();
		void throw_bad_data() {
			error = true;
			if (throw_on_error)
				throw bad_data_exception();
		}

		void throw_encrypted_data() {
			encrypted = true;
#if 0
			if (throw_on_error)
				throw bad_data_exception();
#endif
		}

		bool has_error () const {
			return error;
		}
		bool has_encrypted () const {
			return encrypted;
		}
		void clear_error() {
			error =false;
		}

		void clear_encrypted() {
			encrypted = false;
		}

		int get_next_packet(ts_packet_t* start=nullptr);

		int get_buffer(uint8_t* buffer, int64_t bytes);

		void put_buffer(uint8_t* buffer, int64_t bytes);

		uint32_t get_bits(uint8_t& byte, int& startbit, int num_bits);

		/*
			byte is a byte which has already been read from the stream.
			The Golomb code starts at bit position "startbit" (7=left, 0 is right)
			more bytes will be read from the stream if needed.
			byte and startbit will be changed to point past the end of the code
		*/

		pts_dts_t get_pts_or_dts();

		uint32_t get_Golomb_UE(uint8_t& byte, int& startbit);

		int32_t get_Golomb_SE(uint8_t& byte, int& startbit);

		template<typename T> T get() {
			typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
			get_buffer((uint8_t*)::std::addressof(storage), sizeof(T));
			return net_to_native((const T&) storage);
		}

		template<typename T> T get(const descriptor_t& desc, uint16_t stream_pid=null_pid);

		template<typename T> void put(const T& val) {
			//typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
			auto storage = native_to_net(&val);
			put_buffer((uint8_t*)::std::addressof(storage), sizeof(T));
		}


		uint8_t get_start_code();

		/*
			Scan forward until start_code is reached
		*/
		uint8_t next_start_code();

		/*
			Read a nalu_start_code, but only
			at the current position
			See T-REC-H.264-201704... p. 329
		*/
		uint8_t get_nalu_start_code();

		int skip(int64_t toskip);

		ts_substream_t(ts_stream_t& parent, bool is_psi, const char*name_) : is_psi(is_psi),
																																				 parent(parent), name(name_) {
		}

		ts_substream_t(const ts_substream_t& other) = delete;
		ts_substream_t& operator= (ts_substream_t& other) = delete;

		~ts_substream_t() {
		}

		void parse(ts_packet_t* p);
	};



} //namespace dtdemux
