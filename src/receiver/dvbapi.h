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

//-----------------------------------------------------------------------------
// constants used in socket communication
//-----------------------------------------------------------------------------

#define DVBAPI_PROTOCOL_VERSION   2
#define DVBAPI_MAX_PACKET_SIZE    262        // maximum possible packet size

#define DVBAPI_CA_GET_DESCR_INFO  0x80086F83
#define DVBAPI_CA_SET_DESCR       0x40106F86
#define DVBAPI_CA_SET_DESCR_AES   0x40106f87
#define DVBAPI_CA_SET_PID         0x40086F87
#define DVBAPI_CA_SET_DESCR_MODE  0x400C6F88
#define DVBAPI_CA_SET_DESCR_DATA  0x40186F89
//#define DVBAPI_DMX_START          0x00006F29 // in case we ever need this
#define DVBAPI_DMX_STOP           0x00006F2A
#define DVBAPI_DMX_SET_FILTER     0x403C6F2B

#define DVBAPI_AOT_CA             0x9F803000
#define DVBAPI_AOT_CA_PMT         0x9F803200 // least significant byte is length (ignored)
#define DVBAPI_AOT_CA_STOP        0x9F803F04
#define DVBAPI_FILTER_DATA        0xFFFF0000
#define DVBAPI_CLIENT_INFO        0xFFFF0001
#define DVBAPI_SERVER_INFO        0xFFFF0002
#define DVBAPI_ECM_INFO           0xFFFF0003

#define DVBAPI_INDEX_DISABLE      0xFFFFFFFF // only used for ca_pid_t



//-----------------------------------------------------------------------------
// CA PMT defined values according to EN 50221
// https://www.dvb.org/resources/public/standards/En50221.V1.pdf
// https://www.dvb.org/resources/public/standards/R206-001.V1.pdf
//-----------------------------------------------------------------------------

// ca_pmt_list_management: This parameter is used to indicate whether the user has selected a single program or several
// programs. The following values can be used:

#define CA_PMT_LIST_MORE   0x00 // The CA PMT object is neither the first one, nor the last one of the list.

#define CA_PMT_LIST_FIRST  0x01 // The CA PMT object is the first one of a new list of more than one CA PMT object.
								// All previously selected programs are being replaced by the programs of the new list.

#define CA_PMT_LIST_LAST   0x02 // The CA PMT object is the last of the list.

#define CA_PMT_LIST_ONLY   0x03 // The list is made of a single CA PMT object.

#define CA_PMT_LIST_ADD    0x04 // The CA PMT has to be added to an existing list, that is, a new program has been seleced
								// by the user, but all previously selected programs remain selected.

#define CA_PMT_LIST_UPDATE 0x05 // The CA PMT of a program already in the list is sent again because the version_number or
								// the current_next_indicator has changed.

//-----------------------------------------------------------------------------
// api used for internal device communication
//-----------------------------------------------------------------------------

// The following is part of the linux dvb api
// https://www.kernel.org/doc/html/latest/media/uapi/dvb/ca.html
// https://github.com/torvalds/linux/blob/master/include/uapi/linux/dvb/ca.h


struct dvbapi_ecm_info_t {
	uint8_t adapter_index;
	uint16_t service_id;
	uint16_t caid;
	uint16_t pid;
	uint32_t provider_id;
	uint32_t ecm_time_ms;
	uint8_t card_system_name_size; //size of followed string (255 bytes max)
	ss::string<256> cardsystem_name;
	ss::string<256> reader_name;
	ss::string<256> from_source_name;
	ss::string<256> protocol_name;
	uint8_t hops;
	};

// ca_pid has been removed from the api, but we still use it
struct ca_pid_t
{
	uint32_t pid;
	int32_t index; /* -1 == disable */
};

enum ca_descr_algo
{
	CA_ALGO_DVBCSA,
	CA_ALGO_DES,
	CA_ALGO_AES128,
};

struct ca_descr_aes_t {
	uint32_t index;
	uint32_t parity;
	uint8_t cw[16];
};

enum ca_descr_cipher_mode
{
	CA_MODE_ECB,
	CA_MODE_CBC,
};

// Structs "ca_descr_mode" and "ca_descr_data" and respective ioctl
// commands are part of a custom api

/*
* struct ca_descr_mode - Used to select a crypto algorithm and mode
* for a key slot.
*
* @index: Key slot allocated for a PID or service.
* See CA_SET_PID and struct ca_pid.
* @algo: Algorithm to select for @index.
* @cipher_mode: Cipher mode to use with @algo.
*/

typedef struct ca_descr_mode
{
	uint32_t index;
	enum ca_descr_algo algo;
	enum ca_descr_cipher_mode cipher_mode;
} ca_descr_mode_t;

/*
* struct ca_descr_data - Used to write Keys and IVs to a descrambler.
*
* @index: Key slot allocated for a PID or service.
* See CA_SET_PID and struct ca_pid.
* @parity: Indicates even or odd parity for control words.
* @data_type: Key or IV.
* @length: Size of @data array; depends on selected algorithm and
* key or block size.
* @data: Pointer to variable @length key or initialization vector data.
*/

enum ca_descr_data_type
{
	CA_DATA_IV,
	CA_DATA_KEY,
};

enum ca_descr_parity
{
	CA_PARITY_EVEN,
	CA_PARITY_ODD,
};

typedef struct ca_descr_data
{
	uint32_t index;
	enum ca_descr_parity parity;
	enum ca_descr_data_type data_type;
	uint32_t length;
	uint8_t *data;
} ca_descr_data_t;
