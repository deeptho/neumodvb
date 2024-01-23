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
#include <map>

/*
	do_transfer fails for some reason when NDEBUG is defined
 */
#ifdef NDEBUG
#undef NDEBUG
#define NDEBUG_SAVED
#endif
#include <boost/context/continuation_fcontext.hpp>
#ifdef NDEBUG_SAVED
#define NDEBUG
#undef NDEBUG_SAVED
#endif

#include <iostream>
#include "util/util.h"
#include "streamtime.h"

unconvertable_int(uint16_t, dvb_pid_t);

	/*mpeg:   The byte order of multi-byte words is most significant byte first.
		x86_64: little endian
	*/
	template <typename T> inline const T net_to_native(const T& x) {
		return x;
	}


#define native_to_net net_to_native

	inline uint32_t net_to_native(const uint32_t& x)
	{

		uint8_t *y = (uint8_t*) &x;
		return (y[0]<<24) | (y[1] << 16) | (y[2] << 8) | y[3];
	}


	inline uint16_t net_to_native(const uint16_t& x)
	{
		uint8_t* y = (uint8_t*) &x;
		return (y[0]<<8) | y[1];
	}

	inline uint8_t net_to_native(const uint8_t& x)
	{
		return x;
	}



/*!

	base class for the generic parts os a transport stream parser. The base class demuxes the dvb input stream into
	elementary streams which are dispatched to fibers. A fiber processeses packtets for one or more
	specific pids in the stream.

	The user creates an implementation implementation_t, by deriving from the base class stream_parser_base_t using
	the curiously recurring template pattern.

	implementation_t needs to provide
	char* read_packet()
	for reading input data. This function should return a pointer to the next dvb packet to process in
	the input stream, or NULL if no such packet is currently available.

	implementation_t calls register_parser(pid, fn) to register a lambda function fn which will parse
	all packets for pid pid in one fiber. fn calls get_packet_for_this_parser() to obtain the next packet for the
	pid being parsed.

	If implementation_t registers the same lambda for multiple pids, then  get_packet_for_this_parser() will return
	packets for any of these pids. Two lambdas are the "same" if they have the same address. So in case A below,
	two DIFFERENT fibers running identical code, but each with their own state will be registered for pids 1 and 2.
	In case B ONE fiber handling both pid 1 and 2 will be created:
	A) 	register_parser(1, [this](char*p){ pat.parse(p);});
	register_parser(2, [this](char*p){ pat.parse(p);});
	B) auto fn = [this](char*p){ pat.parse(p);};
	register_parser(1, fn);
	register_parser(2, fn);

	When called by fn, get_packet_for_this_parser() will return NULL when the end of stream is reached.
	At this point, fn should return, possibly after cleaning up its internal data structures.
	fn can also return earlier. In this case, the parser for the pids processed by fn will be unregistered.

	packets for which no parser is registered will be skipped. New parsers may be registered at any time.

	}

*/

namespace dtdemux {

	class data_range_t {
		int64_t _start_bytepos = NULL;
		uint8_t* start = NULL;   //start of the range
		uint8_t* curpos = NULL; //current location in the range
		uint8_t* end = NULL;
	public:
		uint8_t tst{0};
		bool is_writer = false;
		data_range_t() = default;

		inline bool is_valid() const {
			return start != nullptr;
		}
		inline void reset() {
			*this = data_range_t();
		}
		data_range_t(uint8_t* _start, int64_t len);

		inline int64_t available() const {
			return start ? (end-curpos): 0;
		}

		inline int64_t processed() const {
			return start ? (curpos-start): 0;
		}

		inline int64_t start_bytepos() const {
			return _start_bytepos;
		}

		inline void set_start_bytepos(int64_t pos) {
			_start_bytepos = pos;
		}

		inline int64_t end_bytepos() const {
			return _start_bytepos+ (end-start);
		}

		inline int64_t len() const {
			return start ? (end-start) :0;
		}

		data_range_t sub_range(int64_t _offset, int64_t _len) const;

		data_range_t remainder_range() const;

		int get_buffer(uint8_t* buffer, int64_t len);

		inline uint8_t* get_buffer_ptr() const {
			return start;
		}

		inline uint8_t* current_pointer(int len) {
			auto ret = curpos;
			curpos += len;
			assert(curpos <= end);
			return ret;
		}

		int put_buffer(const uint8_t* buffer, int64_t len);

		template<typename T> T get_(uint8_t* cursor);

		template<typename T> void put_(uint8_t* cursor, const T& val);

		template<typename T> T get();

		template<typename T> void put(const T& val);

		void put_start_code(uint8_t _code);

		int skip(int toskip);

		inline void unread(int num) {
			assert (curpos >= start + num);
			if(curpos>= start +num)
				curpos -= num;
		}

		bool set_cursor(uint64_t s);

	};

#ifndef DTPACKED
#define DTPACKED __attribute__((packed))
#endif

	struct ts_packet_t {
		constexpr static const int size = 188;
		pcr_t pcr;
		pcr_t opcr;
		uint16_t header = 0;
		uint8_t flags = 0;
		uint8_t adaptation_field_flags = 0;
		data_range_t range;
		uint8_t adaptation_length = 0 ;
		uint8_t adaptation_extenstion_length = 0;
		bool valid{true};
		uint16_t get_pid () const {
			return this->header & 0x1fff;
		}

		bool get_transport_error() const {
			return (this->header & 0x8000) != 0;
		}

		bool is_encrypted() const {
			return (this->flags & 0xc0) != 0x0;
		}

		bool get_payload_unit_start() const {
			return (this->header & 0x4000) != 0;
		}


		bool has_payload() const {
			return (this->flags & 0x10) != 0;
		}

		uint8_t get_continuity_counter() const {
			return this->flags & 0x0f;
		}

		inline bool has_adaptation() const {
			return  (this->flags & 0x20) != 0;
		}

		inline bool get_is_discontinuity() const  {
			return (adaptation_field_flags & 0x80) != 0;
		}
		inline bool get_random_access_indicator() const {
			return (adaptation_field_flags & 0x40) != 0;
		}

		inline bool elementary_stream_priority_indicator() const {
			return (adaptation_field_flags & 0x20) != 0;
		}

		inline bool has_pcr() const  {
			return (adaptation_field_flags & 0x10) != 0;
		}
		inline bool has_opcr() const {
			return (adaptation_field_flags & 0x08) != 0;
		}

		inline bool has_splicing_point() const {
			return (adaptation_field_flags & 0x04);
		}

		inline bool has_private_data() const {
			return (adaptation_field_flags & 0x2) !=0;
		}

		inline bool has_adaptation_field_extension() const {
			return (adaptation_field_flags & 0x1) !=0;
		}

#if 0
		inline bool get_has_private_data() const {
			return (adaptation_field_flags & 0x2) !=0;
		}
#endif



		void parse_adaptation(data_range_t& range);

		ts_packet_t() = default;

		ts_packet_t(data_range_t& range)
		{
			if(range.get<uint8_t>() != 0x47) {
				valid = false;
				return;

			}
			header = range.get<uint16_t>();
			flags = range.get<uint8_t>();

			if (this->get_transport_error()) {
				valid = false; //cannot decrypt or packet is invalid
				return;
			}
			// Null packet
			if (this->get_pid() == 0x1fff)
				return;

			parse_adaptation(range);
			range.tst = range.available();
			this->range=range;
		}
	};

	struct descriptor_t {
		uint8_t tag = 0; //pp.81
		int len =0;

	};

	template <class implementation_t>
	class stream_parser_base_t  {
		typedef boost::context::continuation continuation_t;
		typedef continuation_t fn(continuation_t& sink, ts_packet_t*,  void*);
		continuation_t root;
		continuation_t * volatile  self{&root};
		volatile int current_pid = -1;
	protected:
		std::map<dvb_pid_t, continuation_t> fibers;
		void dump_fibers(const char * caller="", int i=0) {
			printf("%s %d:++++++++++++++++++++++++++\n", caller,i);
			for(auto& [pid, f] : fibers) {
				printf("%s %d: pid=%d addr=%p valid=%d\n", caller, i, (uint16_t)pid, &f, bool(f));
			}
			printf("%s %d:--------------------------\n",caller,i);
		}
		ts_packet_t global_ts_packet;

	private:
		ts_packet_t* call_fiber(continuation_t& fn, ts_packet_t*p);

	protected:


		ts_packet_t* read_packet() {
			assert(0);
			throw std::runtime_error("This function should be implemented by implementation_t");
		}

	public:

		/*!abort parsing early because we intend to switch to the next file
		*/
		ts_packet_t* return_early();

		/*!
			Read a packet and return the packet to the proper sub-parser
		*/
		ts_packet_t* get_packet_for_this_parser();



		/*!
			read packets, which will call implementation_t::read_packet(), and pass them to the
			right parser.
			This function will return when read_packet() return NULL, which means that the parser has
			parsed all currently available data. When new data is available, the parser can be restarterd
			by calling parse() again
		*/
		void parse();

		/*!
			unregister parser function for a specific pid
		*/
		void unregister_parser(int pid);

		/*!
			register parser function for a specific pid
		*/
		template<typename fn_t>
		void register_parser(int pid,   fn_t&& fn);

		/*!
			close all parsers
		*/
		int exit();

		stream_parser_base_t() {

		}
#if 0
	  ~stream_parser_base_t() {
		}
#endif
	};

} //namespace dtdemux

//implementation details
#include "streamparser_impl.h"


//https://trenki2.github.io/blog/2019/03/21/c++-coroutines/
//clang++ --std=c++14 -o /tmp/x ~/c/boost/context8.cc -lboost_context
