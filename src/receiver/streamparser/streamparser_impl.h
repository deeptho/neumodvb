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
#include <util/logger.h>
//#include <boost/context/continuation_fcontext.hpp>
#include "streamparser.h"

typedef boost::context::continuation continuation_t;
namespace dtdemux {
	struct ts_packet_t;
};

__attribute__((optnone)) //Without this  the code will crash!
inline dtdemux::ts_packet_t* do_transfer(continuation_t& from, continuation_t& to,
																				 dtdemux::ts_packet_t*  parameter)
{
	/*The address of from needs to be stored in a global variable,
		because the stack will be replaced*/
	static thread_local continuation_t * store_at_{};
	static thread_local dtdemux::ts_packet_t* parameter_{};
	store_at_ = &from;
	parameter_ = parameter;
	assert (to);
	*store_at_ = to.resume();
	//at this point, store_at is no longer needed!
	return parameter_;
}

namespace dtdemux {
	inline data_range_t::data_range_t(uint8_t* _start, int64_t len):
		start(_start), curpos(_start), end(_start + len) {
		assert(available()>=0);
		assert(len>0);
	}

	inline data_range_t data_range_t::sub_range(int64_t offset_, int64_t len_) const {
		assert( offset_ <= end -start);
		assert( offset_+len_ <= end -start);
		assert(len_ >= 0);
		auto ret = *this;
		if(len_>=0 &&  offset_+len_ <= end - start) {
			ret._start_bytepos += offset_;
			ret.start += offset_;
			ret.curpos = ret.start;
			ret.end = ret.start + len_;
			assert(available()>=0);
		}
		return ret;
	}


	inline data_range_t data_range_t::remainder_range() const {
		auto ret = *this;
		ret._start_bytepos += processed();
		ret.start += processed();
		ret.curpos = ret.start;
		assert(available()>=0);
		return ret;
	}

	inline int data_range_t::get_buffer(uint8_t* buffer, int64_t len) {
		assert(!is_writer);
		int l = std::min(available(), len);
		memcpy(buffer, curpos, l);
		curpos += l;
		assert(available()>=0);
		return l;
	}

	inline int data_range_t::put_buffer(const uint8_t* buffer, int64_t len) {
		assert(is_writer);
		int l = std::min(available(), len);
		memcpy(curpos, buffer, l);
		curpos += l;
		return l;
	}

	template<typename T> inline T data_range_t::get_(uint8_t* cursor) {
		assert(!is_writer);
		return net_to_native((T&)*cursor);
	}

	template<typename T> inline void data_range_t::put_(uint8_t* cursor, const T& val) {
		assert(is_writer);
		*(T*) cursor = native_to_net(val);
	}


	template<typename T> inline T data_range_t::get() {
		assert(curpos+sizeof(T) <= end);
		if(curpos+sizeof(T) < end) {
			T ret= get_<T>(curpos);
			curpos += sizeof(T);
			assert(available()>=0);
			return ret;
		} else
			return {};
	}





	template<typename T> inline void data_range_t::put(const T& val) {
		assert(curpos+sizeof(T) <= end);
		put_<T>(curpos, val);
		curpos += sizeof(T);
		assert(available()>=0);
	}


	inline void data_range_t::put_start_code(uint8_t _code) {
		uint32_t code = 0x00000100 | _code;
		put<uint32_t>(code);
	}


	inline int data_range_t::skip(int toskip) {
		if(curpos+toskip > end)
			return -1;
		curpos += toskip;
		assert(available()>=0);
		return toskip;
	}


	inline bool data_range_t::set_cursor(uint64_t s) {
		if(start+s >= end)
			return false;
		curpos= start + s;
		assert(available()>=0);
		return true;
	}

	//abort parsing early because we intend to switch to the next file
	template<typename implementation_t>
	inline ts_packet_t* stream_parser_base_t<implementation_t>::return_early()
	{
		ts_packet_t *p = NULL;
		p = call_fiber(root, p);
		return p;
	}
/*
	current_pid is the current pid being parsed; a parser may parse multiple pids
*/
	template<typename implementation_t>
	inline ts_packet_t* stream_parser_base_t<implementation_t>::get_packet_for_this_parser()
	{
		ts_packet_t* p = NULL;
		int parser_pid = current_pid;
		/*
			p may be NULL below when the parser runs out of input data temporarily.
			In this case read_packet must be retried.
		*/
		for(;;) {
			p= static_cast<implementation_t*>(this)->read_packet();
			assert(!p || p->range.available() == p->range.tst);
			if (p==NULL) {
				//printf("parser[{:d}] end of stream\n", parser_pid);
				assert(self);
				p = call_fiber(root, p);
				assert(!p || p->range.available() == p->range.tst);
				if(p)
					break;
			} else
				break;
		}
		assert(!p || p->range.available() == p->range.tst);

		//printf("parser[{:d}] reading packet pid={:d} seqno={:d}\n", pid, p[0], p[1]);
		int packet_pid = p->get_pid();
		if (parser_pid == packet_pid) {
			//printf("returning directly\n");
			//dtdebugf("READx: pid={:d} cc={:d}", global_ts_packet.get_pid(), global_ts_packet.get_continuity_counter());
			return p;
		}
		assert(!p || p->range.available() == p->range.tst);

		/*the current packet has a different pid than the current parser.
			Usually this means that parsing must be transfered to a different parser.
			However, some parsers may process packets of more than one pid (e.g., parsers which skip data)
		*/
		auto it = fibers.find(dvb_pid_t(packet_pid));
		auto& fn = (it!=fibers.end()) ? it->second : root;

		/*transfer control to the new parser. This parser will save our own stack frame
			and will then start or continue processing. In turn the new parser can transfer
			control to a second, third... parser.

			Eventually control will be transfered to the current parser again.
			std::get<0>(result) will be a stack frame of the parser Q which transfered control
			to the current parser.

			The current parser is responsible for saving Q's stack frame. The place to save it
			is indicated by std::get<2>(result)

		*/
		current_pid = packet_pid;
		assert(!p || p->range.available() == p->range.tst);

		p = call_fiber(fn, p);

		return p;
	}

	template <class implementation_t>
	inline void stream_parser_base_t<implementation_t>::unregister_parser(int parser_pid) {
		fibers.erase(dvb_pid_t(parser_pid));
	}

	template <class implementation_t>
	template<typename fn_t>
	inline void stream_parser_base_t<implementation_t>::register_parser(int parser_pid,   fn_t&& fn) {
#ifndef NDEBUG
		auto it = fibers.find(dvb_pid_t(parser_pid));
		if (it != fibers.end()) {
			dterrorf("Cannot add multiple parsers for pid {:d}", parser_pid);
			assert(0);
			return;
		}
#endif
		auto &self = fibers[dvb_pid_t(parser_pid)];
		auto f = [this, &self, fn, parser_pid](continuation_t&& invoker) -> continuation_t {
			//return to root at startup and do nothing
			dtdemux::ts_packet_t* in = do_transfer(self, invoker, nullptr);
			//start processing many packets; fn will only return when fuly done
			assert(in);
			fn(in);
			fibers.erase(dvb_pid_t(parser_pid));
			//printf("returning root pid={:d}\n", in[0]);
			/*
				If we ever end, we transfer control to the very first caller.
				*/
			return std::move(root);
		};
		self = boost::context::callcc(f);
		assert(self);
	}

	template<typename implementation_t>
	ts_packet_t* stream_parser_base_t<implementation_t>::call_fiber(continuation_t& fiber, ts_packet_t* p) {
		auto* caller = self;
		assert(caller);
		self = &fiber; //needed in case self has not yet been initialized
		auto saved = log4cxx::NDC::pop();
		assert(!p || p->range.available() == p->range.tst);
		p = do_transfer(*caller, fiber, p);
		log4cxx::NDC::push(saved);
		assert(self == caller); //should have been set by some other fiber
		return p;
	}

	template<typename implementation_t>
	void stream_parser_base_t<implementation_t>::parse()
	{
		for(;;) {
			auto* p= static_cast<implementation_t*>(this)->read_packet();
			assert(!p || p->range.available() ==  p->range.tst);
			if(!p)
				break; //temporary end of stream
			assert(!p || p->range.available() == p->range.tst);
			int packet_pid = p->get_pid();
			assert(!p || p->range.available() == p->range.tst);
			auto it = fibers.find(dvb_pid_t(packet_pid));
			if (it != fibers.end()) {
				auto& fn = it->second;
				current_pid = packet_pid;
				//dump_fibers("before", packet_pid);
				p = call_fiber(fn, p);
				//dump_fibers("after", packet_pid);
				if(!p)
					break;
				else {
				}
			} else {
			}
		}
	}

	template<typename implementation_t>
	int stream_parser_base_t<implementation_t>::exit()
	{
		fibers.clear();
		return 0;
	}






} //namespace dtdemux
