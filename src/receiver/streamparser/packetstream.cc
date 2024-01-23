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

/*
	Used when reading data only to skip it
*/
#define UNUSED __attribute__((unused))

#include <iomanip>
#include <iostream>

#include "packetstream.h"
using namespace dtdemux;

using namespace boost;

/*
	compare dts of 2 succsive i-frames
	40 ms in case of failyre
	1024 * 90000 * fac (fac is related to aac sample rate)
*/

static bool get_pcr(data_range_t& range, pcr_t& pcr) {
	uint64_t v1 = range.get<uint32_t>();
	uint8_t v2 = range.get<uint8_t>();
	uint8_t v3 = range.get<uint8_t>();
	int64_t time = (v1 << 1) | (v2 >> 7);
	int32_t extension = ((v2 & 1) << 8) | v3;
	pcr = pcr_t(pts_ticks_t(time), extension);
	return true;
}

template <> uint8_t data_range_t::get_<uint8_t>(uint8_t* cursor) {
	return *(uint8_t*)cursor;
}

void ts_packet_t::parse_adaptation(data_range_t& range) {
	if (this->has_adaptation()) {
		adaptation_length = range.get<uint8_t>(); // adaptation field length
		if (adaptation_length > 0) {
			adaptation_field_flags = range.get<uint8_t>();
			if (this->has_pcr()) {
				get_pcr(range, this->pcr);
			}
			if (this->has_opcr())
				get_pcr(range, this->opcr);
		}
		/*set cursor to first entry after adapation field
			The offset is w.r.t. the start of the packet
		*/
		range.set_cursor(adaptation_length + 5);
		range.tst = range.available();
		assert(range.available() >= 0);
	}
}

int ts_substream_t::get_buffer(uint8_t* buffer, int64_t bytes) {
	uint8_t* p = buffer;
	auto toread = bytes;
	for (;;) {
		assert(current_ts_packet->range.available() >= 0);
		int l = std::min(toread, current_ts_packet->range.available());
		int ret = current_ts_packet->range.get_buffer(p, l);
		assert(current_ts_packet->range.available() >= 0);
		assert(ret == l);
		toread -= l;
		bytes_read += l;
		if (toread == 0) {
			return bytes;
		}
		p += l;
		if (get_next_packet() < 0)
			return -1;
	}
	return -1; // not reached
}

uint32_t ts_substream_t::get_bits(uint8_t& byte, int& startbit, int num_bits) {
	assert(num_bits <= 32);
	uint32_t res = 0;
	while (num_bits > 0) {
		if (startbit < 0) {
			byte = get<uint8_t>();
			startbit = 7;
		}
		res |= (((byte & (1 << (startbit--))) != 0) << (--num_bits));
	}
	return res;
}

pts_dts_t ts_substream_t::get_pts_or_dts() {
	uint64_t v = this->get<uint8_t>();
	uint32_t v2 = this->get<uint32_t>();
	uint64_t timestamp = 0;
	v = (v << 32) | v2; // this is a 64bit integer, lowest 36 bits contain a timestamp with markers
	timestamp = 0;
	timestamp |= (v >> 3) & (0x0007ll << 30); // top 3 bits, shifted left by 3, other bits zeroed out
	timestamp |= (v >> 2) & (0x7fff << 15);		// middle 15 bits
	timestamp |= (v >> 1) & (0x7fff << 0);		// bottom 15 bits
																						// pts now has correct timestamp without markers in lowest 33 bits
	return pts_dts_t(pts_ticks_t(timestamp));
}

/*
	byte is a byte which has already been read from the stream.
	The Golomb code starts at bit position "startbit" (7=left, 0 is right)
	more bytes will be read from the stream if needed.
	byte and startbit will be changed to point past the end of the code
*/
uint32_t ts_substream_t::get_Golomb_UE(uint8_t& byte, int& startbit) {
	int leading_zero_bits = -1;
	int b; // the bit
	auto byte_ = byte;
	for (b = 0; !b; leading_zero_bits++) {
		if (startbit < 0) {
			byte = get<uint8_t>();
			startbit = 7;
		}
		b = byte & (1 << (startbit--));
		if (leading_zero_bits > 32) {
			dtdebugf("bad golomn UE: {:x}", byte_);
			throw_bad_data();
		}
	}
	return (1 << leading_zero_bits) - 1 + get_bits(byte, startbit, leading_zero_bits);
}

int32_t ts_substream_t::get_Golomb_SE(uint8_t& byte, int& startbit) {
	auto v = get_Golomb_UE(byte, startbit);
	if (v == 0)
		return 0;

	int pos = (v & 1);
	v = (v + 1) >> 1;
	return pos ? v : -v;
}

uint8_t ts_substream_t::get_start_code() {
	bool start = current_ts_packet->get_payload_unit_start();
	assert(start);
	uint32_t code = get<uint32_t>();
	assert(current_ts_packet->valid); // should have been filtered elsewhere
	if (current_ts_packet->is_encrypted()) {
		// throw_encrypted_data(); already done in get_next_packet, but surpress error here
	} else if ((code & 0xffffff00) != 0x00000100) {
		dtdebugf("Bad start code 0x{:x}", code);
		throw_bad_data();
	}
	return code & 0xff;
}

/*
	Scan forward until start_code is reached
*/
uint8_t ts_substream_t::next_start_code() {
	uint32_t code = get<uint32_t>();
	for (;;) {
		if ((code & 0xffffff00) == 0x00000100) { /*00 00 01 XX*/
			return code & 0xff;
		} else if ((code & 0x00ffffff) == 0x00000001) { /*YY 00 00 01 */
			auto temp = get<uint8_t>();
			code = (code << 8) | temp;
		} else if ((code & 0xffff) == 0) { /*YY YY 00 00 */
			auto temp = get<uint16_t>();
			code = (code << 16) | temp;
		} else if ((code & 0xff) == 0) { /*YY YY YY 00 */
			auto temp = get<uint16_t>();
			code = (code << 16) | temp;
			auto temp1 = get<uint8_t>();
			code = (code << 8) | temp1;
		} else {
			code = get<uint32_t>();
		}
		if (has_error())
			return 0xff;
	}
	assert(0); // above loop never ends, except by throwing
	return 0xff;
}

/*
	Read a nalu_start_code if one is present at the  current
	Returns the first byte of the nalunit which is part of the nal_unit_bytestream


	See T-REC-H.264-201704-I!!PDF-E.pdf p. 329

	A nal equals one of the following (next_bits is a peak-ahead function)
	qq  00 00 01    <nal unit>   <trailing zeros>
	qq  00 00 00 01 <nal unit>  <trailing zeros>
	where qq is a number of zero bytes

	0x00 00 01
*/
uint8_t ts_substream_t::get_nalu_start_code() {
	uint32_t code = get<uint32_t>();
	if (code == 1) {								 // 00 00 00 01
		uint8_t code = get<uint8_t>(); // first byte of nal unit
		if (code & 0x80) {						 // test for forbidden_zero_bit
			dtdebugf("Not a nalu start code: 0x{:x}", code);
			throw_bad_data();
		}
		return code;
	}
	if ((code & 0xffffff00) != 0x00000100) {
		// first three bytes must be 0x 00 00 01
		// last byte is the code
		dtdebugf("Not a nalu start code 0x{:x}", code);
		throw_bad_data();
	}
	return code & 0xff;
}

int ts_substream_t::skip(int64_t toskip) {
	for (;;) {
		assert(current_ts_packet->range.available() >= 0);
		int l = std::min(toskip, current_ts_packet->range.available());
		if (current_ts_packet->range.skip(l) < 0)
			throw_bad_data();
		assert(current_ts_packet->range.available() >= 0);
		toskip -= l;
		bytes_read += l;
		if (toskip == 0) {
			//_bytesread += toskip;
			return 1;
		}
		if (get_next_packet() < 0)
			return -1;
	}
}

void ts_substream_t::skip_to_unit_start() {
	if (current_ts_packet->get_payload_unit_start()) {
		wait_for_unit_start = false;
		return;
	}

	wait_for_unit_start = true; // signal to get_next_packet ton only return at start of a pes/psi unit
	while (!current_ts_packet->get_payload_unit_start())
		get_next_packet();
	assert(current_ts_packet->get_payload_unit_start());
	wait_for_unit_start = false;
	bytes_read = 0;
}

/*
	provides the next packet for the CURRENT stream. If the next packet is for another
	stream, this function will yield (suspend it self).
	It will then be resumed later, i.e.,
	when the next packet of the current stream arrives.

	returns -1 on error 1 on success
*/

int ts_substream_t::get_next_packet(ts_packet_t* start) {
	for (int n = 0;; ++n) {
		ts_packet_t* p = (n == 0 && start) ? start : parent.get_packet_for_this_parser();
		if (!p->valid)
			throw_bad_data();
		auto is_duplicate = process_packet_header(p);
		if(is_duplicate)
			continue;
		if (!this->wait_for_unit_start)
			return 1;
	}
	return -1; // never reached
}

bool ts_substream_t::process_packet_header(ts_packet_t* p) {
	bool is_duplicate{false};
	if (!p->valid)
		throw_bad_data();
	if (p->get_payload_unit_start()) {
		if (has_encrypted()) {
			parent.num_encrypted_packets++;
			num_encrypted_packets++;
		} else
			unit_completed_cb();
	}
	current_unit_end_bytepos = current_ts_packet->range.start_bytepos(); // not an error

	current_ts_packet = p;
	if (this->continuity_counter != 0xff) { // if not first time ever
		uint8_t expected_cc =
				current_ts_packet->has_payload() ? (this->continuity_counter + 1) & 0x0f : this->continuity_counter;
		/*handle case where packet is repeat (same cont counter, identical content)
			See https://superuser.com/questions/777855/transport-stream-duplicate-packet:
			a packet can be repeated if it has a payload. It should be repeated exactly as is,
			except for the pcr if it contains one

			The code below does not handle this, but at least omits reporting a continuity error
		*/
		if (expected_cc != current_ts_packet->get_continuity_counter()) {
			is_duplicate = current_ts_packet->has_payload() && //duplicates only allowed with payload
				(current_ts_packet->get_continuity_counter() == this->continuity_counter);
			if (!current_ts_packet->get_is_discontinuity() &&!is_duplicate) {
				this->wait_for_unit_start = true; // p. 39 iso13818-1: pes packets cannot be interruped by discontinuity
				this->continuity_errors++;				// discontinuities are not errors if they are signalled
				dterrorf("[{:d}] stream error count={:d}: expected_cc={:d} cc={:d} payload={:d}", current_ts_packet->get_pid(),
								 this->continuity_errors, expected_cc, current_ts_packet->get_continuity_counter(),
								 current_ts_packet->has_payload());
				throw_bad_data();
			} else {
				cc_error_counter++;
			}
		}
	}
	if (current_ts_packet->get_payload_unit_start())
		this->wait_for_unit_start = false;

	this->continuity_counter = current_ts_packet->get_continuity_counter();
	if (current_ts_packet->has_pcr()) {

		parent.event_handler.pcr_update(current_ts_packet->get_pid(), current_ts_packet->pcr,
																		current_ts_packet->get_is_discontinuity());

	} else {
		if (current_ts_packet->get_is_discontinuity())
			LOG4CXX_ERROR(logger, "Discontinuity without PCR");
	}

	if (current_ts_packet->get_is_discontinuity()) {
		if (!current_ts_packet->has_pcr())
			LOG4CXX_ERROR(logger, "Discontinuity without PCR");
	}
	if (current_ts_packet->is_encrypted()) {
		throw_encrypted_data();
	}
	if (is_psi && current_ts_packet->get_payload_unit_start()) {
		pointer_field = current_ts_packet->range.get<uint8_t>();
		if (pointer_field < current_ts_packet->range.available()) {
			pointer_pos = current_ts_packet->range.processed() + pointer_field;
			assert(pointer_field >= 0);
			assert(pointer_pos >= 0);
		} else {
			pointer_pos = -1;
			pointer_field = 0;
			throw_bad_data();
		}
	} else
		pointer_field = 0;
	return is_duplicate;
}

void ts_stream_t::register_audio_pids(int service_id, int audio_pid, int pcr_pid,
																			stream_type::stream_type_t stream_type) {
	/*
		@todo: how do we end this parser?
	*/
	assert(pcr_pid == audio_pid);
	if (this->pcr_pid == pcr_pid && this->pcr_stream_type == stream_type) {
		dtdebugf("Parser for pid {:d} already registered (reusing)\n", pcr_pid);
		return;
	}

	// the code below probably fails is pcr_pid!=audio_pid
	if (this->pcr_pid != null_pid) {
		/*If there is already a video parser, then remove it
		 */

		fibers.erase(dvb_pid_t(this->pcr_pid));
		return;
	}

	this->pcr_pid = audio_pid;
	this->pcr_stream_type = stream_type;

	auto parser = std::make_shared<audio_parser_t>(*this, service_id, audio_pid);
	auto fn = [parser](ts_packet_t* p) {
		log4cxx::NDC::push("AUDIO");
		parser->parse(p);
	};
	register_parser(audio_pid, fn); // video
	// return std::static_pointer_cast<video_parser_t>(parser);
	return;
}

void ts_stream_t::register_video_pids(int service_id, int video_pid, int pcr_pid,
																			stream_type::stream_type_t stream_type) {
	/*
		@todo: how do we end this parser?
	*/
	if (this->pcr_pid == pcr_pid && this->pcr_stream_type == stream_type) {
		dtdebugf("Parser for pid {:d} already registered (reusing)\n", pcr_pid);
		return;
	}

	// the code below probably fails is pcr_pid!=video_pid
	if (this->pcr_pid != null_pid) {
		/*If there is already a video parser, then remove it
		 */

		fibers.erase(dvb_pid_t(this->pcr_pid));
		// return;
	}

	this->pcr_pid = video_pid;
	this->pcr_stream_type = stream_type;

	if (is_mpeg2(stream_type)) {
		auto parser = std::make_shared<mpeg2_parser_t>(*this, service_id, video_pid);
		auto fn = [parser](ts_packet_t* p) {
			log4cxx::NDC::push("MPEG2");
			parser->parse(p);
		};
		register_parser(video_pid, fn); // video
		// return std::static_pointer_cast<video_parser_t>(parser);
		return;
#ifdef NOTWORKING
		/*
			code interfderes with regular hdtv processing
		 */
	} else if(is_hevc(stream_type)) {
		auto parser = std::make_shared<hevc_parser_t>(*this, service_id, video_pid);
		auto fn = [parser](ts_packet_t* p) {
			log4cxx::NDC::push("HEVC");
			parser->parse(p);
		};
		register_parser(video_pid, fn); // video
		// return std::static_pointer_cast<video_parser_t>(parser);
		return;
#endif
	} else {
		///@todo Does H.265 work?
		auto parser = std::make_shared<h264_parser_t>(*this, service_id, video_pid);
		auto fn = [parser](ts_packet_t* p) {
			log4cxx::NDC::push("H264");
			parser->parse(p);
		};
		register_parser(video_pid, fn); // video
		// return std::static_pointer_cast<video_parser_t>(parser);
		return;
	}
}

void ts_substream_t::skip_to_pointer() {
	/*the number of bytes, immediately following the pointer_field
		until the first byte of the next section
	*/
	if (pointer_field) {
		if (pointer_pos < 0)
			throw_bad_data();
		current_ts_packet->range.set_cursor(pointer_pos);
		assert(current_ts_packet->range.available() >= 0);
		pointer_field = 0; // avoid second skip
		pointer_pos = -1;
	}
}

#include "substream_impl.h"
