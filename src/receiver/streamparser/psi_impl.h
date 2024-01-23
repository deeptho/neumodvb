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
#include "neumodb/chdb/chdb_extra.h"

using namespace chdb;
using namespace dtdemux;
namespace dtypes { //dummy (empty) structures
	struct service_descriptor_t;
	struct data_broadcast_descriptor_t;
	struct multiprotocol_encapsulation_info_structure_t;
	struct service_list_descriptor_t;
	struct otv_service_descriptor_t;
	struct fst_service_descriptor_t;
	struct fst_region_list_descriptor_t;
	struct fst_channel_category_list_descriptor_t;
	struct fst_category_description_list_descriptor_t;
	struct otv_service_list_descriptor_t;
	struct fst_service_list_descriptor_t;
	struct frequency_list_descriptor_t;
	struct descriptor_t;
	struct ca_identifier_descriptor_t;
	struct linkage_descriptor_t;
	struct s2_satellite_delivery_system_descriptor_t;
	struct satellite_delivery_system_descriptor_t;
	struct cable_delivery_system_descriptor_t;
	struct terrestrial_delivery_system_descriptor_t;
	struct frequency_list_descriptor_t;
	struct linkage_descriptor_t;
	struct short_event_descriptor_t;
	struct extended_event_descriptor_t;
	struct opentv_title_descriptor_t;
	struct opentv_summary_descriptor_t;
	struct opentv_description_descriptor_t;
	struct opentv_serieslink_descriptor_t;
	struct time_shifted_event_descriptor_t;
	struct component_descriptor_t;
	struct content_descriptor_t;
	struct multilingual_component_descriptor_t;
	struct start_time_duration_t;
	struct mhw2_start_time_duration_t;
	struct mhw2_dvb_text_t;
	struct logical_channel_descriptor_t;
	struct dvb_text_t;
};

struct frequency_list_t {
	ss::vector<uint32_t, 16> frequencies;
};

struct service_list_t {
	uint16_t network_id{0xffff};
	uint16_t ts_id{0xffff};
	bouquet_t& bouquet;
};

struct fst_service_list_t {
	uint16_t network_id{0xffff};
	uint16_t ts_id{0xffff};
	int selected_region_id{1};
	bouquet_t& bouquet;
};


struct fst_region_list_t {
	int selected_region_id{1};
	ss::string_& bouquet_name;
};


struct fst_channel_category_list_t;

struct fst_channel_category_t;





static inline fe_rolloff_t dvbs_rolloff_from_stream(unsigned int val)
{
	return (fe_rolloff_t)(val&3);
}

static inline fe_modulation_t dvbs_modulation_from_stream(unsigned int val)
{
	static constexpr fe_modulation_t modulations[]={ fe_modulation_t::QAM_AUTO, fe_modulation_t::QPSK,
		fe_modulation_t::PSK_8, fe_modulation_t::QAM_16};
	return modulations[val&3];
}


static inline fe_modulation_t dvt_modulation_from_stream(unsigned int val)
{
	static constexpr fe_modulation_t modulations[] = { fe_modulation_t::QPSK, fe_modulation_t::QAM_16,
		fe_modulation_t::QAM_64,  fe_modulation_t::QAM_AUTO };
	return modulations[val&3];
}


static inline fe_modulation_t dvbc_modulation_from_stream(unsigned int val)
{
	static constexpr fe_modulation_t modulations[]={ fe_modulation_t::QAM_AUTO, fe_modulation_t::QAM_16,
		fe_modulation_t::QAM_32, fe_modulation_t::QAM_64, fe_modulation_t::QAM_128,
		fe_modulation_t::QAM_256, fe_modulation_t::QAM_AUTO, fe_modulation_t::QAM_AUTO};
	return modulations[val&7];
}



static inline fe_code_rate_t dvbs_code_rate_from_stream(unsigned int val)
{
	static constexpr fe_code_rate_t code_rates[] =
		{ fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_1_2, fe_code_rate_t::FEC_2_3,
			fe_code_rate_t::FEC_3_4, fe_code_rate_t::FEC_5_6,
			fe_code_rate_t::FEC_7_8, fe_code_rate_t::FEC_8_9, fe_code_rate_t::FEC_3_5, fe_code_rate_t::FEC_4_5,
			fe_code_rate_t::FEC_9_10, fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO,
			fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO };
	return code_rates[val&0xf];
}

static inline fe_code_rate_t dvbt_code_rate_from_stream(unsigned int val)
{
	assert(val<=10);
	//int Constellation = Constellations[sd->getConstellation()];
	static constexpr fe_code_rate_t code_rates[] =  { fe_code_rate_t::FEC_1_2, fe_code_rate_t::FEC_2_3,
		fe_code_rate_t::FEC_3_4, fe_code_rate_t::FEC_5_6, fe_code_rate_t::FEC_7_8,
		fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO};
	return code_rates[val&0x7];
}


static inline fe_code_rate_t dvbc_inner_code_rate_from_stream(unsigned int val)
{
	static constexpr fe_code_rate_t code_rates[] =  { fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_1_2,
		fe_code_rate_t::FEC_2_3, fe_code_rate_t::FEC_3_4,  fe_code_rate_t::FEC_5_6,
		fe_code_rate_t::FEC_7_8, fe_code_rate_t::FEC_8_9, fe_code_rate_t::FEC_3_5,
		fe_code_rate_t::FEC_4_5, fe_code_rate_t::FEC_9_10, fe_code_rate_t::FEC_AUTO,
		fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO,
		fe_code_rate_t::FEC_AUTO, fe_code_rate_t::FEC_AUTO };


	return code_rates[val&0xf];
}


static inline fe_transmit_mode_t dvbt_transmission_mode_from_stream(unsigned int val)
{
	static constexpr fe_transmit_mode_t modes[] = { fe_transmit_mode_t::TRANSMISSION_MODE_2K,
		fe_transmit_mode_t::TRANSMISSION_MODE_8K, fe_transmit_mode_t::TRANSMISSION_MODE_4K,
		fe_transmit_mode_t::TRANSMISSION_MODE_AUTO };
	return modes[val&3];
}

static inline fe_guard_interval_t dvbt_transmission_guard_interval_from_stream(unsigned int val)
{
	static constexpr fe_guard_interval_t intervals[] = { fe_guard_interval_t::GUARD_INTERVAL_1_32,
		fe_guard_interval_t::GUARD_INTERVAL_1_16,
		fe_guard_interval_t::GUARD_INTERVAL_1_8, fe_guard_interval_t::GUARD_INTERVAL_1_4 };
	return intervals[val&3];
}


static inline fe_bandwidth_t dvt_bandwidth_from_stream(unsigned int val)
{
	static constexpr fe_bandwidth_t bandwidths[] = { fe_bandwidth_t::BANDWIDTH_8_MHZ, fe_bandwidth_t::BANDWIDTH_7_MHZ,
		fe_bandwidth_t::BANDWIDTH_6_MHZ, fe_bandwidth_t::BANDWIDTH_5_MHZ, fe_bandwidth_t::BANDWIDTH_AUTO,
		fe_bandwidth_t::BANDWIDTH_AUTO,
		fe_bandwidth_t::BANDWIDTH_AUTO, fe_bandwidth_t::BANDWIDTH_AUTO };
	return bandwidths[val&7];
}


static inline fe_hierarchy_t dvbt_hierarchy_from_stream(unsigned int val)
{

	static constexpr fe_hierarchy_t hierarchies[]={
		fe_hierarchy_t::HIERARCHY_NONE, fe_hierarchy_t::HIERARCHY_1, fe_hierarchy_t::HIERARCHY_2,
		fe_hierarchy_t::HIERARCHY_4 };
	//TODO: the bit with value 4 selects between native and in depth interleaver
	return hierarchies[val&3];
}
