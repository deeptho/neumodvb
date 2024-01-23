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
#include <stdint.h>

namespace stream_type {
enum mpeg2_stream_type_t {
  Reserved1                        = 0x00,
  ISO_IEC_11172_2_VIDEO            = 0x01, //MPEG-1 video stream. (ISO/IEC 11172 video.)
  ISO_IEC_13818_2_VIDEO            = 0x02, //MPEG-2 video stream. (ISO/IEC 13818-2 video.)
  ISO_IEC_11172_3_AUDIO            = 0x03, //MPEG-1 audio stream. (ISO/IEC 11172 audio.)
  ISO_IEC_13818_3_AUDIO            = 0x04, //MPEG-2 audio stream. (ISO/IEC 13818-3 audio.)
  ISO_IEC_13818_1_PRIVATE_SECTION  = 0x05, //MPEG-2 private sections. (ISO/IEC 13818-1 private sections.)
  ISO_IEC_13818_1_PES              = 0x06, //MPEG-2 Packetized Elementary Stream (PES) packets containing private data. (ISO/IEC 13818-1 PES).
  ISO_IEC_13522_MHEG               = 0x07, //MHEG-5 Audio-Visual streams. (ISO/IEC 13522 MHEG.)
  ANNEX_A_DSM_CC                   = 0x08, //Digital Storage Media Command and Control (DSM-CC) stream. (ISO/IEC 13818-1 Annex A.)
  ITU_T_REC_H_222_1                = 0x09, //ITU-T Satellite Audio-Visual streams. (ITU-T Rec. H.222.1.)
  ISO_IEC_13818_6_TYPE_A           = 0x0A, //MPEG-2 Video Clip A streams. (ISO/IEC 13818-6 type A.)
  ISO_IEC_13818_6_TYPE_B           = 0x0B, //MPEG-2 Video Clip B streams. (ISO/IEC 13818-6 type B.)
  ISO_IEC_13818_6_TYPE_C           = 0x0C, //MPEG-2 Video Clip C streams. (ISO/IEC 13818-6 type C.)
  ISO_IEC_13818_6_TYPE_D           = 0x0D, //MPEG-2 Video Clip D streams. (ISO/IEC 13818-6 type D.)
  ISO_IEC_13818_1_AUXILIARY        = 0x0E, //MPEG-2 Auxiliary streams. (ISO/IEC 13818-1 auxiliary.)
  ISO_IEC_13818_1_RESERVED         = 0x0F, //MPEG-2 Reserved streams.
  USER_PRIVATE                     = 0x10, //This constant is not supported; use ISO_IEC_USER_PRIVATE instead.
  ISO_IEC_USER_PRIVATE             = 0x80, //User proprietary streams. This enumeration value matches the value given in ISO/IEC 13818-1.
  DOLBY_AC3_AUDIO                  = 0x81  //Dolby AC3 audio.
};



//---------------------------------------------------------------------
//! Stream type values, as used in the PMT.
//---------------------------------------------------------------------

enum class stream_type_t : uint8_t {
	  RESERVED      = 0x00, //!< Invalid stream type value, used to indicate an absence of value
		MPEG1_VIDEO   = 0x01, //!< MPEG-1 Video
		MPEG2_VIDEO   = 0x02, //!< MPEG-2 Video
		MPEG1_AUDIO   = 0x03, //!< MPEG-1 Audio
		MPEG2_AUDIO   = 0x04, //!< MPEG-2 Audio
		PRIV_SECT     = 0x05, //!< MPEG-2 Private sections
		PES_PRIV      = 0x06, //!< MPEG-2 PES private data
		MHEG          = 0x07, //!< MHEG
		DSMCC         = 0x08, //!< DSM-CC
		MPEG2_ATM     = 0x09, //!< MPEG-2 over ATM
		DSMCC_MPE     = 0x0A, //!< DSM-CC Multi-Protocol Encapsulation
		DSMCC_UN      = 0x0B, //!< DSM-CC User-to-Network messages
		DSMCC_SD      = 0x0C, //!< DSM-CC Stream Descriptors
		DSMCC_SECT    = 0x0D, //!< DSM-CC Sections
		MPEG2_AUX     = 0x0E, //!< MPEG-2 Auxiliary
		AAC_AUDIO     = 0x0F, //!< Advanced Audio Coding (ISO 13818-7)
		MPEG4_VIDEO   = 0x10, //!< MPEG-4 Video
		MPEG4_AUDIO   = 0x11, //!< MPEG-4 Audio
		MPEG4_PES     = 0x12, //!< MPEG-4 SL or FlexMux in PES packets
		MPEG4_SECT    = 0x13, //!< MPEG-4 SL or FlexMux in sections
		DSMCC_DLOAD   = 0x14, //!< DSM-CC Synchronized Download Protocol
		MDATA_PES     = 0x15, //!< MPEG-7 MetaData in PES packets
		MDATA_SECT    = 0x16, //!< MPEG-7 MetaData in sections
		MDATA_DC      = 0x17, //!< MPEG-7 MetaData in DSM-CC Data Carousel
		MDATA_OC      = 0x18, //!< MPEG-7 MetaData in DSM-CC Object Carousel
		MDATA_DLOAD   = 0x19, //!< MPEG-7 MetaData in DSM-CC Sync Downl Proto
		MPEG2_IPMP    = 0x1A, //!< MPEG-2 IPMP stream
		AVC_VIDEO     = 0x1B, //!< AVC video
		HEVC_VIDEO    = 0x24, //!< HEVC video
		HEVC_SUBVIDEO = 0x25, //!< HEVC temporal video subset of an HEVC video stream
		IPMP          = 0x7F, //!< IPMP stream
		AC3_AUDIO     = 0x81, //!< AC-3 Audio (ATSC only)
		SCTE35_SPLICE = 0x86, //!< SCTE 35 splice information tables
		EAC3_AUDIO    = 0x87, //!< Enhanced-AC-3 Audio (ATSC only)
		MPE_FEC      = 0x90, //DVB  - stream_type value for Time Slicing / MPE-FEC
    };


//----------------------------------------------------------------------------
// Check if an ST value indicates a PES stream
//----------------------------------------------------------------------------

inline bool is_pes (stream_type_t st)
{
	return
		st == stream_type_t::MPEG1_VIDEO ||
		st == stream_type_t::MPEG2_VIDEO ||
		st == stream_type_t::MPEG1_AUDIO ||
		st == stream_type_t::MPEG2_AUDIO ||
		st == stream_type_t::PES_PRIV    ||
		st == stream_type_t::MPEG2_ATM   ||
		st == stream_type_t::MPEG4_VIDEO ||
		st == stream_type_t::MPEG4_AUDIO ||
		st == stream_type_t::MPEG4_PES   ||
		st == stream_type_t::MDATA_PES   ||
		st == stream_type_t::AVC_VIDEO   ||
		st == stream_type_t::AAC_AUDIO   ||
		st == stream_type_t::AC3_AUDIO   ||
		st == stream_type_t::EAC3_AUDIO  ||
		st == stream_type_t::HEVC_VIDEO  ||
		st == stream_type_t::HEVC_SUBVIDEO ||
		st == stream_type_t::MPE_FEC;
}


//----------------------------------------------------------------------------
// Check if an ST value indicates a video stream
//----------------------------------------------------------------------------

inline bool is_video (stream_type_t st)
{
	return
		st == stream_type_t::MPEG1_VIDEO ||
		st == stream_type_t::MPEG2_VIDEO ||
		st == stream_type_t::MPEG4_VIDEO ||
		st == stream_type_t::AVC_VIDEO   ||
		st == stream_type_t::HEVC_VIDEO  ||
		st == stream_type_t::HEVC_SUBVIDEO;
}


inline bool is_mpeg2 (stream_type_t st)
{
	return
		st == stream_type_t::MPEG1_VIDEO ||
		st == stream_type_t::MPEG2_VIDEO;
}

#ifdef NOTWORKING
inline bool is_hevc (stream_type_t st)
{
	return
		st == stream_type_t::HEVC_VIDEO ||
		st == stream_type_t::HEVC_SUBVIDEO;
}
#endif


inline bool is_private(stream_type_t st)
{
	return
		st == stream_type_t::PES_PRIV;
}

enum class marker_t : uint8_t {
	illegal =0,
		i_frame='I',
		p_frame='P',
		b_frame='B',
		pes_other = 'O',
		pat = 'A',
		pmt = 'M'
		};

/*
"I haven't worked with multi-slice video in a long time, so it's always more useful to me to just read a
few more bytes and grab the actual frame type instead of the AUD set. The AUD set is only potentially useful
in the case where a frame has multiple slices and each slice has a different type and it's vitally important
to know that.
*/

//e T-REC-H.264-201304-S\!\!PDF-E.pdf  p. 104; possible slice_types for primary_pic_type

/*
instantaneous decoding refresh (IDR) access unit: An access unit in which the primary coded picture is an
IDR picture.

instantaneous decoding refresh (IDR) picture: A coded picture for which the variable IdrPicFlag is equal
to 1. An IDR picture causes the decoding process to mark all reference pictures as "unused for reference"
immediately after the decoding of the IDR picture. All coded pictures that follow an IDR picture in decoding
order can be decoded without inter prediction from any picture that precedes the IDR picture in decoding
order. The first picture of each coded video sequence in decoding order is an IDR picture.


 */


inline marker_t h264_frame_type(int pic_type) {
	switch(pic_type) {
	case 0: //slice type 2(I), 7(I)
	case 3: //slice type 4(SI), 9(SI)
	case 5: //slice type 2(I), 4(SI), 7(I), 9(SI)
		return marker_t::i_frame;
		break;
	case 1: //slice type 0(P), 2(I), 5(P), 7(I)
	case 4: //slice type 3(SP), 4(SI), 8(SP), 9(SI)
	case 6://slice type 0(P), 2(I), 3(SP), 4(SI), 5(P), 7(I), 8(SP), 9(SI)
		return marker_t::p_frame;
	case 2: //slice type 0(P), 1(B), 2(I), 5(P), 6(B), 7(I)
	case 7: //slice type 0(P), 1(B), 2(I), 3(SP), 4(SI), 5(P), 6(B), 7(I), 8(SP), 9(SI)
		return marker_t::b_frame;
	default:
		break;
	};
	return marker_t::illegal;
}

inline bool is_iframe(marker_t pic_type) {
	return pic_type == marker_t::i_frame;
}


inline marker_t mpeg2_frame_type(int pic_type) {
	switch(pic_type) {
	case 0:
		break;
	case 1:
		return marker_t::i_frame;
		break;
	case 2:
		return marker_t::p_frame;
	case 3:
		return marker_t::b_frame;
	default:
		break;
	};
	return marker_t::illegal;
}



}; //namespace stream_type
