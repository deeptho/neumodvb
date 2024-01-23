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
#include "devmanager.h"
#include "scam.h"
#include "stackstring.h"
#include "streamparser/si.h"
#include <stdint.h>
#include <unistd.h>

/** @brief sending to oscam data to begin geting cw's for channel
		Returns 0 on success.

		See EN50221.pdf
*/

enum class ca_pmt_cmd_id_t : uint8_t
{
	// requests to oscam; see en50211.pdf  p. 31
	ok_descrambling = 1, // host does not expect answer message, but rather a start of descrambling
	ok_mmi = 2,					 // some sort of reservation>
	query = 3,					 // request ca_pmt reply
	not_selected = 4
};

using namespace dtdemux;

void ca_pmt_t::add_adapter_device_descriptor(int adapter_no) {
	assert(adapter_no >= 0 && adapter_no <= 255);
	uint8_t data[] = {0x83, 0x1, (uint8_t)adapter_no};
	add_raw_ca_descriptor(data, sizeof(data));
}

void ca_pmt_t::add_pmt_pid_descriptor(uint16_t pmt_pid) {
	uint8_t data[] = {0x84, 0x2, (uint8_t)(pmt_pid >> 8), (uint8_t)(pmt_pid & 0xff)};
	add_raw_ca_descriptor(data, sizeof(data));
}

void ca_pmt_t::add_demux_device_descriptor(uint8_t demux_device) {
	uint8_t data[] = {0x86, 0x1, demux_device};
	add_raw_ca_descriptor(data, sizeof(data));
}

void ca_pmt_t::add_ca_device_descriptor(uint8_t ca_device) {
	uint8_t data[] = {0x87, 0x1, ca_device};
	add_raw_ca_descriptor(data, sizeof(data));
}

void ca_pmt_t::init(capmt_list_management_t lm, const pmt_info_t& pmt_info, int adapter_no, int demux_device) {
	program_info_length = 0;
	// data.clear();
	for (uint8_t c : {0x9f, 0x80, 0x32, // AOT_CA_PMT (0x9F8032). see en50221.pdf p. 30
			0x82,							// indicates that length field will be 2 bytes; see en50221.pdf p. 11
			0x00, 0x00})			// zero initialised length field, to be updated later
		data.push_back(c);
	auto marker0 = data.size();
	auto* p_length = (uint8_t*)data.buffer() + data.size() - 2;
	data.append_raw((uint8_t)lm);
	data.append_raw(native_to_net(pmt_info.service_id)); // program number
	data.append_raw<uint8_t>(((pmt_info.version_number & 0x1f) << 1) | 0xc0 | (pmt_info.current_next & 1)); // 1 byte

	auto* p_info_len =
		(uint8_t*)data.buffer() + data.size(); // position of the last esinfo_len field which needs to be set
	auto* p_program_info_len = p_info_len;
	data.append_raw<uint16_t>(0x0000 | 0xf0); // 4 reserved bits + 12 bits program info length; needs to be updated later

	data.push_back((uint8_t)ca_pmt_cmd_id_t::ok_descrambling); // ca_pmt_cmd_id

	// the following are user defined descriptors for scam
	add_adapter_device_descriptor(adapter_no);
	add_pmt_pid_descriptor(pmt_info.pmt_pid);
	add_demux_device_descriptor(demux_device);
	uint8_t ca_device = demux_device;
	add_ca_device_descriptor(ca_device); // not recognized by oscam?

	auto* pstart = (uint8_t*)pmt_info.capmt_data.buffer();
	auto* pend = pstart + pmt_info.capmt_data.size();
	auto* p = pstart;

	// loop over all descriptors which were present in the original stream
	for (; p < pend;) {
		uint8_t& tag = p[0]; // descriptor tag
		uint8_t& len = p[1]; // descriptor length
		assert(p + 2 + len <= pend);
		/*
			The input is encoded as follows:
			0(tag) followed by stream_type, elementary_pid
			number of descriptors, each starting with a non-zero tag
		*/
		if (tag == 0) {
			/*update the es_info_len field; this will be updated
				once for each elementary_pid (so some updates are redundant)
			*/

			// existing descriptors
			auto info_len = (uint8_t*)data.buffer() + data.size() - p_info_len - 2;
			p_info_len[0] = ((info_len >> 8) & 0xf) | 0xf0; // 0xf0=reserved bits
			p_info_len[1] = (info_len & 0xff);

			// stream_type and stream_pid
			assert(len == 3);
			data.append_raw(p + 2, len);

			p_info_len = (uint8_t*)data.buffer() + data.size();
			data.append_raw<uint16_t>(0x0000 | 0xf0); // 4 reserved bits + 12 bits program info length;
																								// needs to be updated later
		} else if (tag == SI::CaDescriptorTag) {
			data.append_raw(p, 2 + len);
		} else {
			assert(0);
		}
		p += (2 + len);
	}
	assert(p == pend);

	if (p_program_info_len == p_info_len) {
		// not yet set; can happen if there is no elementary stream data
		auto info_len = (uint8_t*)data.buffer() + data.size() - p_info_len - 2;
		p_info_len[0] = ((info_len >> 8) & 0xf) | 0xf0; // 0xf0=reserved bits
		p_info_len[1] = (info_len & 0xff);
	}
	// set_length_field(data.size() - marker0);
	auto length = data.size() - marker0;
	p_length[0] = (length >> 8);
	p_length[1] = (length & 0xff);
}

void ca_pmt_t::add_raw_ca_descriptor(uint8_t* start, int len) {
	data.append_raw((char*)start, len);
	program_info_length += len;
}

#if 0
/*!header to prepend before a filtered section is sent to oscam

 */
struct dvbapi_filter_data_reply_t {
	uint32_t operation_code =  DVBAPI_FILTER_DATA;
	uint8_t demux_index;
	uint8_t filter_number;
	//followed by uint8_t[]  filtered data from demux

	dvbapi_filter_data_reply_t(uint8_t demux_index, uint8_t filter_number) :
		demux_index(demux_index), filter_number(filter_number)
		{}


};
#endif

#if 0
/*
	oscam informs us about selected pid
*/
struct dvbapi_ca_set_pid_result_t {
	uint32_t operation_code;
	uint8_t adapter_index;
	ca_pid_t cap_pid;

	void init_from_net() {
		//fix byte ordering problem
		operation_code = ntohl(operation_code);
		ca_pid.pid = ntohs(ca_pid.pid);
		ca_pid.index = ntohs(ca_pid.index);
		assert(operation_code == DVBAPI_CA_SET_PID);
	}
};
#endif

#if 0
/*
	oscam informs us about control words
*/
struct dvbapi_ca_set_descr_request_t {
	uint32_t operation_code;
	uint8_t adapter_index;
	ca_pid_t cap_pid;
	void init_from_net() {
		//fix byte ordering problem
		operation_code = ntohl(operation_code);
		ca_pid.pid = ntohs(ca_pid.pid);
		ca_pid.index = ntohs(ca_pid.index);
		assert(operation_code == DVBAPI_CA_SET_PID);
	}


};
#endif
