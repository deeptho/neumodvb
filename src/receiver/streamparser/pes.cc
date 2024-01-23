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

#include "pes.h"
#include "mpeg.h"
#include "packetstream.h"

#define UNUSED __attribute__((unused))
#define NAL_AUD 0x9

using namespace dtdemux;

void video_parser_t::on_pes_start() {
	this->last_pts = pts;
	this->last_dts = dts;
	auto new_last_play_time = parent.event_handler.pcr_play_time();
	assert(new_last_play_time >= this->last_play_time);
	this->last_play_time = new_last_play_time;
}

void audio_parser_t::on_pes_start() {
	this->last_pts = pts;
	this->last_dts = dts;
	auto new_last_play_time = parent.event_handler.pcr_play_time();
	assert(new_last_play_time >= this->last_play_time);
	this->last_play_time = new_last_play_time;
}

/*
	If the payload_unit_start_indicator is set to '1', then one and only one PES packet starts in this
	Transport Stream packet.

	3 or 4 byte start code: see
	https://stackoverflow.com/questions/24884827/possible-locations-for-sequence-picture-parameter-sets-for-h-264-stream
*/
bool pes_parser_t::parse_pes_header() {
	// Parsing a pes header
	if (!current_ts_packet->get_payload_unit_start()) {
		return false;
	}
	pes_stream_id = get_start_code();
	// wait_for_unit_start = false;
	pes_packet_len = this->get<uint16_t>();
	// TODO: See p.49:  flags1/flags not present in some pes streams, such as ecm/emm!!!
	flags1 = this->get<uint8_t>();
	if ((flags1 & 0xc0) != 0x80) { // must start with 0b10
		throw_bad_data();
		return false;
	}
	flags2 = this->get<uint8_t>();
	int pts_dts = (flags2 >> 6) & 0x3;
	pes_header_data_len = this->get<uint8_t>();
	int toskip = pes_header_data_len;

	pes_header_len = pes_header_data_len + 8;

	switch (pts_dts) {
	case 0: // no pts, no dts
		pts = pts_dts_t();
		dts = pts_dts_t();
		break;
	case 2: // only pts
		pts = get_pts_or_dts();
		dts = pts;
		toskip -= 5;
		break;
	case 3: // pts and dts
		pts = get_pts_or_dts();
		auto old = dts;
		dts = get_pts_or_dts();
		if (old.is_valid()) {
			clock_period = parent.event_handler.check_pes_discontinuity(clock_period, old, dts);
		}
		toskip -= 10;
#if 0
		dtinfof("DIFF DTS-PCR={} PCR={} DTS={}", (dts - (pts_dts_t) event_handler.last_pcr),
						event_handler.last_pcr, dts);
#endif

		break;
	}
	if (toskip < 0) {
		throw_bad_data();
		return false;
	}
	if (toskip > 0)
		this->skip(toskip);
	return true;
}

void h264_parser_t::parse_sps(int nal_unit_type) {
	LOG4CXX_DEBUG(logger, "Parse SPS");
}

void h264_parser_t::parse_sei(int nal_unit_type) {
	LOG4CXX_DEBUG(logger, "Parse SEI");
}

void h264_parser_t::parse_slice_header(int nal_unit_type) {
	// p. 72
	uint8_t byte = this->get<uint8_t>();
	int startbit = 7;
	auto first_mb_in_slice UNUSED = this->get_Golomb_UE(byte, startbit);
	auto slice_type UNUSED = this->get_Golomb_UE(byte, startbit);
	auto pic_parameter_set_id UNUSED = this->get_Golomb_UE(byte, startbit);
	const bool separate_colour_plane_flag = 0; //@todo p. 72.  This really needs to come from parsing the stream!
																						 // WE need to parse SPS first

	const int log2_max_frame_num_minus4 = 0; //@todo p. 72.  This really needs to come from parsing the stream!
																					 // WE need to parse SPS first
	const bool idr_pic_flag = (nal_unit_type == 5);

	const bool frame_mbs_only_flag = false;					//@todo
	const int pic_order_cnt_type = 1;								//@todo
	const int delta_pic_order_always_zero_flag = 0; // TOOD
	const bool pic_order_present_flag = 1;					//@todo
	int log2_max_pic_order_cnt_lsb_minus4 = 0;
	int colour_plane_id = 0;
	if (separate_colour_plane_flag) {
		colour_plane_id = this->get_bits(byte, startbit, 2);
		dtdebugf("colour_plane_id={}", colour_plane_id);
	}

	auto frame_num UNUSED = this->get_bits(byte, startbit, log2_max_frame_num_minus4 + 4);
	dtinfof("frame_num={}", frame_num);
	int field_pic_flag = 0;
	int bottom_field_flag = 0;
	if (!frame_mbs_only_flag) {
		field_pic_flag = this->get_bits(byte, startbit, 1);
		if (field_pic_flag)
			bottom_field_flag = this->get_bits(byte, startbit, 1);
	}

	if (idr_pic_flag) {
		auto idr_pic_id UNUSED = this->get_Golomb_UE(byte, startbit);
	}
	int pic_order_cnt_lsb = 0;

	if (pic_order_cnt_type == 0) {
		pic_order_cnt_lsb = this->get_bits(byte, startbit, log2_max_pic_order_cnt_lsb_minus4 + 4);
	}

	int delta_pic_order_cnt_0 = 0;
	int delta_pic_order_cnt_1 = 0;
	if (pic_order_cnt_type == 1 && !delta_pic_order_always_zero_flag) {
		delta_pic_order_cnt_0 = this->get_Golomb_SE(byte, startbit);
		if (pic_order_present_flag && !field_pic_flag)
			delta_pic_order_cnt_1 = this->get_Golomb_SE(byte, startbit);
	}
}

void h264_parser_t::parse_payload_unit() {
	bool is_payload_unit_start = parse_pes_header();
	RETURN_ON_ERROR;
	if (!is_payload_unit_start)
		return;
	on_pes_start();
	uint8_t code = this->get_nalu_start_code();
	bool forbidden_zero_bit = code & 0x80;
	if (forbidden_zero_bit)
		RETURN_ON_ERROR;

	uint8_t nal_ref_idc UNUSED = (code & 0x60) >> 5; // See T-REC-H.264-201704-I!!PDF-E.pdf p. 85
	uint8_t nal_unit_type = code & 0x1f;
	/*
		http://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/
		nal_ref_idc indicates whether this NAL unit is a reference field / frame / picture.
		On one hand, if it is a reference field / frame / picture, nal_ref_idc is not equal to 0.
		According to the Recommendation, non-zero nal_ref_idc specifies that the content of the NAL
		unit contains a sequence parameter set (SPS), a SPS extension, a subset SPS, a picture parameter set (PPS),
		a slice of a reference picture, a slice data partition of a reference picture, or a prefix NAL
		unit preceding a slice of a reference picture.
		On the other hand, if it is a non-reference field / frame / picture, nal_ref_idc is equal to 0.

		For any non-zero value, the larger the value, the more the importance of the NAL unit.
	*/
	if (nal_ref_idc > 0)
		dtdebugf("Possible reference field: ref_idx={:d}", nal_ref_idc);

	if (nal_unit_type != NAL_AUD) { // Access unit delimiter
		/*
			See T-REC-H.264-201704-I!!PDF-E.pdf p. 92  figure 7-1
			Either there is an access unit delimiter or a SEI at the start
		*/
		dtdebug_nicef("nal_unit_type!=NAL_AUD: 0x{:x}", nal_unit_type);
		RETURN_ON_ERROR;
	}
	// NAL_AUD means  access_unit_delimiter_rbsp( )
	bool has_svc_extension_flag = (nal_unit_type == 4 || nal_unit_type == 20);
	bool has_avc_3d_extension_flag = (nal_unit_type == 21);
	bool extension_flag = code & 1;
	if (has_svc_extension_flag || has_avc_3d_extension_flag) {
		int toskip = 0;
		if (extension_flag && has_svc_extension_flag)
			toskip += 3;
		else if (extension_flag && has_avc_3d_extension_flag)
			toskip += 2;
		else
			toskip += 3;
		// svc_extension_flag (if not 21) or avc_3d_extension_flag (if 21)
		// see T-REC-H.264-201704-I!!PDF-E.pdf p. 64
		this->skip(toskip);
	}
	// access unit delimiter
	// See T-REC-H.264-201704-I!!PDF-E.pdf p. 92
	uint8_t primary_pic_type = this->get<uint8_t>();
	// filler bits to fill until the end of a byte boundary. T he pattern is always 10...0
	// See http://yumichan.net/video-processing/video-compression/introduction-to-h264-2-sodb-vs-rbsp-vs-ebsp/
	uint8_t rbsp = primary_pic_type & 0x1f;
	if (rbsp != 0x10) { // 5 trailing bits; first should be 1; rest zero
		dtdebugf("rbsp != 0x10: 0x{:x}", rbsp);
		THROW_BAD_DATA;
	}
	primary_pic_type >>= 5;
	/*the following will probably never occur*/
	if (primary_pic_type > 5)
		primary_pic_type -= 5;
	/*Primary pic_type indicates what type of slices may be present in the current picture
		See table 7-5 p. 105 of  T-REC-H.264-201704-I!!PDF-E.pdf
		0: only I-slices
		3: only SI-slices (use prediction but w.r.t. same slice data)
		5: I and SI
		All other values require reference to other frames

	*/

	this->current_unit_type = stream_type::h264_frame_type(primary_pic_type);

	if (is_iframe(this->current_unit_type)) {
		if (has_pts()) {
		} else {
#ifndef NDEBUG
			dtdebugf("Unexpected: I-frame without pts");
#endif
			THROW_BAD_DATA;
		}
	}
	// p. 64

#if 0
	/*The following code is a first attempt at parsing parameter sets and such,
		the idea being that we might be able to later modify these parameters so that
		we can achieve reverse play of h.264 i-frames. It is probably too complex: because
		of the use of Golomb codecs, the length of data may change for example.
	*/
	for(;;) {
		code =next_start_code();
		nal_unit_type = code & 0x1f;
		switch(nal_unit_type) {
		case 1:
		case 5: {
			/* 1: non-IDR slice_layer_without_partitioning_rbsp( )
				 2: slice_data_partition_a_layer_rbsp( )
				 3: slice_data_partition_b_layer_rbsp( )
				 4: slice_data_partition_c_layer_rbsp( )
				 5: IDR slice_layer_without_partitioning_rbsp( )
			*/
			parse_slice_header(nal_unit_type);
		}
			break;
		case 6:
			parse_sei(nal_unit_type);
			break;
		case 7:
			parse_sps(nal_unit_type);
			break;
		default:
			dtdebugf("Unhandled nal unit type: {}", (int) nal_unit_type);


		}
	}
#endif
	return;
}

#ifdef NOTWORKING
void hevc_parser_t::parse_payload_unit() {
	bool is_payload_unit_start = parse_pes_header();
	RETURN_ON_ERROR;
	if (!is_payload_unit_start)
		return;
	on_pes_start();
	return;
}
#endif

static const char* frame_rates[] = {"invalid",
	"23.976",
	"24",
	"25",
	"29.97",
	"30",
	"50"
	"59.94",
	"60"};

static const char* aspect_ratios[] = {"invalid", "square", "4/3", "16/9", "2.21/1"};

void dtdemux::mpeg2_parser_t::parse_payload_unit() {
	bool is_payload_unit_start = parse_pes_header();
	RETURN_ON_ERROR;
	if (!is_payload_unit_start)
		return;
	on_pes_start();
	for (;;) {
		// we parse until we find a picture (@todo: is this needed?)
		uint8_t code = this->next_start_code();
		RETURN_ON_ERROR;
		RETURN_ON_ENCRYPTED;
	dont_read:
		switch (code) {
		case 0x0: { // picture header
			auto temporal_reference = this->get<uint16_t>();
			auto picture_coding_type = ((temporal_reference >> 3) & 7);
			auto frame_type = stream_type::mpeg2_frame_type(picture_coding_type);
#if 0
			dtdebugf("{}: {}: {}-frame: temporal ref={}", current_ts_packet->range, pts,
							 (char) frame_type, temporal_reference);
#endif
			this->current_unit_type = frame_type;
#ifndef NDEBUG
			if (is_iframe(this->current_unit_type)) {
				if (has_pts()) {
				} else {
					LOG4CXX_ERROR(logger, "Unexpected: I-frame without pts");
				}
			}
#endif

			return;
		} break;
		case 0xb3: { // 179, sequence header See 'T-REC-H.262-200002-I!!PDF-E.pdf'
			int horizontal_size = this->get<uint8_t>();
			int byte2 = this->get<uint8_t>();
			horizontal_size = (horizontal_size << 4) | byte2 >> 4;
			int vertical_size = this->get<uint8_t>();
			vertical_size = vertical_size | ((byte2 & 0xf) << 8);
			int aspect_ratio_code = this->get<uint8_t>();
			int frame_rate_code = aspect_ratio_code & 0x0f;
			aspect_ratio_code >>= 4;
			auto frame_rate =
				frame_rate_code < sizeof(frame_rates) / sizeof(frame_rates[0]) ? frame_rates[frame_rate_code] : "invalid";
			auto aspect_ratio = aspect_ratio_code < sizeof(aspect_ratios) / sizeof(aspect_ratios[0])
				? aspect_ratios[aspect_ratio_code]
				: "invalid";
#if 0
			dtdebugf("{}: {}: SEQ header: frame_rate={} aspect_ratio={}", current_ts_packet->range, pts,
							 frame_rate, aspect_ratio);
#endif
			this->skip(3);																	// bit_rate and part of vbv_buffer_size
			uint8_t quantiser_flags = this->get<uint8_t>(); /* 5 bits of vbv_buffer_size,
																												 1 bit constrained_parameter_flags
																												 1 bit load_intra_quantiser_matrix
																												 1 bit either load_non_intra_quantiser_matrix
																												 or a bit of the quantisation table
																											*/
			bool load_intra_quantiser_matrix = quantiser_flags & 0x2;
			if (load_intra_quantiser_matrix) {
				this->skip(63);
				quantiser_flags = this->get<uint8_t>();
#if 0
				// load_non_intra_quantiser_matrix is now again the last (=least significant) bit of quantiser_flags
				LOG4CXX_DEBUG(logger, "Sequence header: skipped intra quantiser matrix");
#endif
			}
			bool load_non_intra_quantiser_matrix = quantiser_flags & 0x01;
			if (load_non_intra_quantiser_matrix) {
				this->skip(64);
#if 0
				// load_non_intra_quantiser_matrix is now again the last (=least significant) bit of quantiser_flags
				LOG4CXX_DEBUG(logger, "Sequence header: skipped non-intra quantiser matrix");
#endif
			}
			continue;

		} break;
		case 0xb5: {		 // 181, extension start code
			this->skip(6); // skip the complete extension header
			/*There may be optional fields with additional start codes 0xb5.
				Search for the next different start code
			*/
			while ((code = next_start_code()) == 0xb5)
				continue;
			goto dont_read;
		}
		case 0xb7: // 183, sequence end
			break;
		case 0xb8: { // 184, group start
			uint32_t time_code = this->get<uint32_t>();
			bool closed_gop = time_code & 0x40;
			bool broken_link = time_code & 0x20;
			time_code >>= 7; // time code is for video recorders
#if 0
			LOG4CXX_DEBUG(logger, current_ts_packet->range << ": " << pts << ": "
										<< "GOP header: closed_gop=" << closed_gop
										<< " broken_link=" << broken_link);
#endif
			/*There may be optional fields with additional start codes 0xb5.
				Search for the next different start code
			*/
			while ((code = next_start_code()) == 0xb5) {
				continue;
			}
			goto dont_read;
		} break;
		default: {
			if (code >= 0x01 && code <= 0xaf) // slice start code
				break;
			else if (code >= 0xB9 && code < 0xff) { // system start codes
			} else if (code == 0xB2) {							// user data
				dtdebugf("user data start code: {}", (int)code);
			} else {
				dtdebugf("unrecognised start code: {}", (int)code);
			}
		}
		}
	}
	return;
#if 0
	assert((code&0x80)==0);
	uint8_t nal_ref_idc UNUSED = code & 0x60;

	uint8_t nal_unit_type = code & 0x1f;
	if(nal_unit_type!= NAL_AUD)
		throw "Expected access unit delimiter not found";

	uint8_t primary_pic_type = this->get<uint8_t>();

	uint8_t rbsp = primary_pic_type & 0x1f;
	assert(rbsp == 0x10);
	primary_pic_type >>=5;
	/*the following will probably never occor*/
	if(primary_pic_type>5)
		primary_pic_type -=5;
	/*Primary pic_type indicates what type of slices may be present in the current picture
		See table 7-5 p. 98 of mpeg2_iso-iec_14496-10.pdf
		0: only I-slices
		3: only SI-slices (use prediction but w.r.t. same slice data)
		5: I and SI
		All other values require reference to other frames

	*/
	return;
#endif
}

/*!
	update  byte position markets when a current pes packet or a current section has been completed
*/
void pes_parser_t::unit_completed_cb() {
	/*
		for H.264 video, current_unit_start_bytepos points to the start of pes packet containing an i-frame like picture

		current_unit_end_bytepos points to the first byte of the pes packet containing the next access unit of the
		same type and NOT to the last byte of the access unit (it would require more work to find)

	*/
#if 0
	dtdebugf("INDEX: %c: [{:d}, {:d}[", 	(char)current_unit_type, current_unit_start_bytepos,
					 current_unit_end_bytepos);
#endif
	if (current_unit_type != stream_type::marker_t::illegal)
		parent.event_handler.index_event(current_unit_type, this->last_play_time, this->last_pts, this->last_dts,
																		 this->current_unit_start_bytepos, this->current_unit_end_bytepos, this->name);
	current_unit_start_bytepos = current_ts_packet->range.start_bytepos();
}

/*
	pes packet: see iso13818-1.pdf p. 49
*/

void dtdemux::audio_parser_t::parse_payload_unit() {
	bool is_payload_unit_start = parse_pes_header();
	RETURN_ON_ERROR;
	RETURN_ON_ENCRYPTED;
	if (!is_payload_unit_start)
		return;
	on_pes_start();
	if (pes_packet_len == 0) {
		dterrorf("unexpected: pes_packet_len=0 in audio stream");
	}
	this->current_unit_type = stream_type::marker_t::pes_other;
	this->skip(pes_packet_len - pes_header_data_len - 2 /*flags*/ - 1 /*pes_header_data_len*/);

	return;
}

extern int last_count;

/*
	See https://www.quora.com/What-is-the-difference-between-an-I-Frame-and-a-Keyframe-in-video-encoding

	What is the difference between an I-Frame and a Keyframe in video encoding?
	2 Answers
	Keith Winstein
	Keith Winstein
	Answered Mar 5, 2018

	They’re closely related!

	Summary: an “I-frame picture” (MPEG-2) (also known as an “intra-only
	frame” (VP9) or a “coded picture in which all slices are I or SI
	slices” (H.264)) is a compressed frame that doesn’t depend on the
	contents of any earlier decoded frame.

	In video coding, a “key frame” (VP8, VP9) (also known as an “IDR
	picture” (H.264) or a “stream access point” (MPEG-4 part 12)) is a
	place in the video where the decoder can start decoding. It will start
	with a coded I-frame, but with additional restrictions to make sure it
	and subsequent frames can be decoded.

	Full answer: there are two concepts at play here.

	(1) Whether the coded frame depends on the contents of any previously
	decoded frame.

	Video encoding schemes save bits by taking advantage of the
	similarities between nearby frames. Generally speaking, each frame
	gets divided into little rectangular pieces (known as
	macroblocks). For each macroblock, the coded frame first instructs the
	decoder on how to form a prediction about what the pixel values will
	be, and then (optionally) tells the decoder what the difference is
	between the prediction and the intended output.

	The prediction can generally be formed in one of two ways: (1) based
	on some part of the same frame (usually required to be above or to the
	left of the macroblock in question, so it will have already been
	encoded), or (2) based on some part of a decoded frame that the
	decoder has already received and saved for later use.

	A macroblock that is predicted in the first way (within the same
	frame) is called an “intra-coded macroblock.” And a coded picture that
	contains only intra-coded macroblocks (i.e., none of the macroblocks
	depend on any picture contents outside the current frame) is known as
	an “intra-coded picture” or “I-picture” in MPEG-2 part 2 (H.262)
	video, or an “intra-only frame” in VP9. A similar concept is known as
	an “I-slice” in MPEG-4 part 10 (H.264) video.

	Bottom line: All of these terms mean what people say when they say
	“I-frame”—a coded frame that (unlike most frames in a compressed
	video) isn’t predicted from the pieces of any earlier-coded frame.

	(Ultrapedantic note: No video format that I’m aware of has a precise
	thing called an “I-frame” — unfortunately, interlacing makes the
	terminology more complicated. In H.262 / MPEG-2 part 2, they want to
	be clear about whether they’re referring to a single intra-coded
	picture that happens to code a frame [a frame that might be interlaced
	or might be progressive], or if they just mean a frame coded in a way
	that doesn’t depend on any decoded pictures outside itself, whether
	that means one intra-coded picture for the whole frame, or two coded
	pictures each coding one field. MPEG-2 uses “I-frame picture” for the
	first concept, and “coded I-frame” for the second concept. H.264 gets
	rid of these terms and defines “I-slice” instead. VP9 calls it an
	“intra-only frame.”)

	(2) Whether a decoder can start playing the video at a given point.

	Just because a coded frame is completely intra-coded (i.e. it’s an
	I-frame picture (H.262 / MPEG-2 part 2) or intra-only frame (VP9) or
	consists solely of an I-slice (H.264 / MPEG-4 part 10), it doesn’t
	mean that:

	it stands alone (i.e., that frame can be correctly decoded without
	the decoder having seen some earlier part of the bitstream), or it
	represents an entry point in the video, so that if the decoder
	starts at the intra-coded frame, it can then successfully decode
	all the subsequent frames.

	Even though an intra-only frame doesn’t depend on the image contents
	of a previously decoded frame, it can still depend on the decoder
	having been set up with a particular state (e.g. probability tables
	for compression or quantization matrices) by earlier parts of the
	bitstream. So even in MPEG-2, an I-frame picture cannot necessarily be
	decoded all by itself.

	Furthermore, just because a bitstream has one I-frame picture,
	subsequent frames can continue to depend on the contents of earlier
	frames—even ones decoded long before the I-frame picture was inserted
	into the bitstream. So even if the decoder can decode the I-frame
	picture, that doesn’t mean it can decode any subsequent frames in the
	video.

	To represent a place where the decoder can actually join the video and
	start decoding, MPEG-2 uses the term “point of random access,” H.264
	uses the term “instantaneous decoding refresh (IDR) picture,” and VP8
	and VP9 use the term “key frame.” These are places in the video that
	not only start with an intra-only frame, but also make sure that the
	intra-only frame can be decoded with the “default” decoder state, and
	that every subsequent frame can be decoded without reference to
	anything that came before the decoder joined.

	The general term (defined in MPEG-4 part 12) is a “stream access
	point.”
*/
