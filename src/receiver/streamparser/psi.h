/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include "neumodb/epgdb/epgdb_extra.h"
#include "si_state.h"

#include "stackstring.h"

namespace dtdemux {
	struct stored_section_t;
	struct pmt_info_t;
	struct pat_services_t;

	struct pmt_writer_t;
	enum class reset_type_t {
		NO_RESET,
		RESET,
		ABORT
	};

	struct subtable_info_t {
		uint16_t pid{0x1fff};
		bool is_actual{false};
		uint8_t table_id{0xff};
		int16_t version_number{-1};
		uint8_t num_sections_present{0};
		bool completed{false};
		bool timedout{false};
		subtable_info_t() = default;
		subtable_info_t(int pid, bool is_actual,
										int table_id, int version_number, int num_sections_present, bool completed, bool timedout)
			: pid(pid)
			, is_actual(is_actual)
			,	table_id(table_id)
			, version_number(version_number)
			, num_sections_present(num_sections_present)
			, completed(completed)
			, timedout(timedout)
			{}
	};

	struct bouquet_t {
		struct chgm_t {
			chdb::service_key_t service_key;
			uint16_t lcn;
			uint8_t service_type;
			uint8_t is_opentv_or_mhw2{0};
		};
		uint16_t bouquet_id{0xffff};
		bool is_sky{false};
		bool is_mhw2{false};
		ss::string<32> name;
		std::map<uint16_t, chgm_t> channels; //map of channel_id to (service_id, lcn)
	};

	struct ca_info_t {
		unsigned short stream_pid; //null_pid or specific pid to which info applies
		unsigned short ca_system_id;
		uint16_t ca_pid;
	};



	struct service_move_info_t  {
		uint16_t stream_pid; //null_pid or specific pid to which info applies
		uint16_t new_original_network_id;
		uint16_t new_transport_stream_id;
		uint16_t new_service_id;
	};



	struct subtitle_info_t  {
		//uint16_t stream_pid; //null_pid or specific pid to which info applies
		uint8_t subtitle_type;
		char lang_code[4];
		uint16_t composition_page_id;
		uint16_t ancillary_page_id;
	};

	struct audio_language_info_t {
		char lang_code[4]{};
		uint8_t audio_type{}; //0=undefined, 1=clean effects, 2=hearing impaired,  3=visual impaired commentary, reserved
		bool ac3{};
		ss::bytebuffer<8> ac3_descriptor_data;
	};

	struct pid_info_t {
		uint16_t stream_pid{null_pid};
		stream_type::stream_type_t stream_type{stream_type::stream_type_t::RESERVED};
		int8_t t2mi_stream_id{-1};
		int8_t num_t2mi_streams_minus_one{-1};
		audio_language_info_t audio_lang {};
		ss::vector<subtitle_info_t, 8> subtitle_descriptors;

		pid_info_t(uint16_t stream_pid, stream_type::stream_type_t stream_type) :
			stream_pid(stream_pid), stream_type(stream_type) {}

		pid_info_t(uint16_t stream_pid, int stream_type) :
			stream_pid(stream_pid), stream_type((stream_type::stream_type_t)stream_type) {}

		pid_info_t() = default;

		bool is_t2mi() const {
			return t2mi_stream_id>=0;
		}

		inline bool has_subtitles() const {
			return subtitle_descriptors.size() > 0;
		}
	};


	/*!
		info about all substreams in the program management table
	*/
	struct pmt_info_t {
		ca_info_t get_ca(stored_section_t& s, const descriptor_t& desc, uint16_t stream_pid);
		void parse_descriptors(stored_section_t& s, pid_info_t& info, bool in_es_loop);
		uint16_t service_id = 0x00;
		uint16_t pcr_pid = null_pid;
		uint16_t video_pid = null_pid;
		uint16_t pmt_pid = null_pid;
		uint16_t version_number = 0;
		uint8_t current_next = 1;
		uint8_t num_sky_title_pids{0};
		uint8_t num_sky_summary_pids{0};
		uint8_t num_freesat_pids{0};
		uint64_t stream_packetno_end{0};//position of start of last packet of pmt in stream
		bool has_freesat_epg = false; //this means a local freesat epg, not the main freesat home transponder
		bool has_skyuk_epg = false;
		/*!
			Stores ca descriptors
			Format:
			// program_descriptors
			desc1 ... descn (stored contiguously); always with descriptor_tag 9
			//elementary stream descriptors
			dec_tag=0x0; desc_len =  0x3 [stream_type, reserved, elementary_pid]
			desc1 ... descn (stored contiguously); always with descriptor_tag 9
		*/
		ss::bytebuffer<1024> capmt_data;
		ss::vector<pid_info_t, 16> pid_descriptors;
		ss::vector<ca_info_t, 16> ca_descriptors;
		ss::vector<service_move_info_t, 4> service_move_descriptors;

		chdb::media_mode_t estimated_media_mode;

		pmt_info_t& operator=(const pmt_info_t& other) = default;

		bool has_ca_pid(uint16_t ca_pid) const;
		bool has_es_pid(uint16_t ca_pid) const;

		bool is_encrypted() const {
			return ca_descriptors.size() >0;
		}
		ss::vector<chdb::language_code_t, 8> audio_languages() const;
		ss::vector<chdb::language_code_t, 8> subtitle_languages() const;

		std::tuple<const pid_info_t*,chdb::language_code_t>
		best_audio_language(const ss::vector_<chdb::language_code_t>&prefs) const;

		std::tuple<const pid_info_t*, const subtitle_info_t*, chdb::language_code_t>
		best_subtitle_language(const ss::vector_<chdb::language_code_t>& prefs) const;

		bool is_ecm_pid(uint16_t pid);

		std::tuple<chdb::language_code_t, chdb::language_code_t>
		make_preferred_pmt_ts(ss::bytebuffer_& output,
													const ss::vector_<chdb::language_code_t>& audio_prefs,
													const ss::vector_<chdb::language_code_t>& subtitle_prefs);
	};

	struct bouquet_linkage_t {
		uint16_t network_id{0xffff};
		uint16_t ts_id{0xffff};
	};


	struct nit_network_t {
		bool is_actual;
		//int tuned_mux_idx{-1}; //if positive points to the mux descriptor for the tuned mux, as received in the not
		int32_t network_id{-1}; //non-original network_id
		ss::string<64> network_name;
		ss::vector<bouquet_linkage_t, 2> bouquet_linkage;
		bool sat_set{false}; //a satellite index has been set
		//bool is_dvbs{false};
		//bool is_dvbc{false};
		//bool is_dvbt{false};
		ss::vector<chdb::any_mux_t,128> muxes;
	};

	struct sdt_services_t {
		bool is_actual{false};
		uint16_t ts_id{0xffff};
		uint16_t original_network_id{0xffff};

		bool has_opentv_epg{false};
		bool has_freesat_home_epg{false};

		ss::vector<chdb::service_t,32> services;
	};


	struct pat_entry_t {
		uint16_t service_id{0xffff};
		uint16_t pmt_pid{null_pid};
		bool operator== (const pat_entry_t& other) const {
			return service_id == other.service_id && pmt_pid == other.pmt_pid;
		}

		bool operator!= (const pat_entry_t& other) const {
			return service_id != other.service_id || pmt_pid != other.pmt_pid;
		}

		pat_entry_t() = default;
		pat_entry_t(uint16_t service_id, uint16_t  pmt_pid) :
			service_id(service_id), pmt_pid(pmt_pid)
			{}

		pat_entry_t(const pat_entry_t& other) = default;
		pat_entry_t(pat_entry_t&& other) = default;
		pat_entry_t& operator=(const pat_entry_t& other) = default;
	};

	struct pat_services_t {
		uint16_t ts_id{0xffff};
		uint16_t version_number{0};
		ss::vector<pat_entry_t,32> entries;
	};

	struct epg_t {
		bool is_actual{false};
		bool is_freesat{false};
		bool is_sky{false};
		bool is_mhw2{false};
		bool is_sky_title{false};
		bool is_sky_summary{false};
		bool is_mhw2_title{false};
		bool is_mhw2_summary{false};
		chdb::epg_type_t epg_type;
		/*
			In eit a subtable corresponds to a service, table_id combination: a service
			can be present in the EIT_PF or EIT_SCHEDULE table and even in an EIT_SCHEDULE_TABLE
			it can be present multiple times, corresponding to to multiple 4-day time periods
		 */
		int num_subtables_known{0};
		int num_subtables_completed{0};

		uint16_t channel_id{0xffff}; //only for opentv/skyuk
		epgdb::epg_service_t epg_service;

		ss::vector<epgdb::epg_record_t, 64> epg_records;
	};

	class section_parser_t : public ts_substream_t
	{
		/*iso13818-1.pdf p. 60
			The maximum number of bytes in a section of a ITU-T Rec. H.222.0 | ISO/IEC 13818-1
			defined PSI table is 1024 bytes.
			The maximum number of bytes in a private_section is 4096 bytes.
			This also applies to EIT section
		*/
		section_header_t parsed_header;
		bool section_complete = false;
	protected:
		ss::bytebuffer<4096> payload; //4096 = maximum size of any section
		int pid = -1;
		//int table_id = -1;
		section_header_t* header();
	private:

		/*
			find table_id, length, section_syntax_indicator and private_bit
		*/

		void parse_section_header(section_header_t& ret);

		/*
			find table_id_extension, version_number, current_next, section_number, last_section_number,
			but not segment_last_section_number
		*/
		void parse_table_header(section_header_t& ret);

		//void write_section_header(const section_header_t& hdr, ss::bytebuffer_& out);
	protected:
		virtual void parse_payload_unit_(bool parse_only_section_header);
		virtual void parse_payload_unit() override;
	public:
		section_parser_t(ts_stream_t& parent, int pid, const char*name)
			: ts_substream_t(parent, true, name)
			, pid(pid)
			{}
		int get_pid() const {
			return pid;
		}
		section_parser_t(const section_parser_t& other) = delete;

		virtual ~section_parser_t() {
		}

		virtual void unit_completed_cb() final;
	};

	struct psi_parser_t : public section_parser_t
	{

		virtual void parse_payload_unit() override;

		psi_parser_t(ts_stream_t& parent, int pid, const char* name = "SECT") :
			section_parser_t(parent, pid, name)
			{}

		psi_parser_t(const psi_parser_t& other);

		virtual ~psi_parser_t() {
		}

		void parse_payload_unit_init();
	};

	struct nit_parser_t : public psi_parser_t
	{
		parser_status_t  parser_status;
		std::function<reset_type_t(nit_network_t&, const subtable_info_t&)>
		section_cb = [](const nit_network_t& network, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};

		nit_parser_t(ts_stream_t& parent, int pid) :
			psi_parser_t(parent, pid, "NIT")
			{
				this->current_unit_type = stream_type::marker_t::illegal;
			}

		nit_parser_t(const nit_parser_t& other) = delete;

		virtual ~nit_parser_t() {
		}

		bool parse_nit_section(stored_section_t& section, nit_network_t& network);

		virtual void parse_payload_unit() final;
	};


	struct sdt_bat_parser_t : public psi_parser_t
	{

		parser_status_t  parser_status;
		int fst_preferred_region_id{-1}; //needed to select bouquets


		std::function<reset_type_t(const sdt_services_t&, const subtable_info_t&)>
		sdt_section_cb = [](const sdt_services_t& services, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};

		std::function<reset_type_t(const bouquet_t&, const subtable_info_t&)>
		bat_section_cb = [](const bouquet_t& bouquet, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};


		sdt_bat_parser_t(ts_stream_t& parent, int pid) :
			psi_parser_t(parent, pid, "SDT_BAT")
			{
				this->current_unit_type = stream_type::marker_t::illegal;
			}

		sdt_bat_parser_t(const sdt_bat_parser_t& other) = delete;

		virtual ~sdt_bat_parser_t() {
		}

		bool parse_sdt_section(stored_section_t& section, sdt_services_t& ret);
		bool parse_bat_section(stored_section_t& section,  bouquet_t& bouquet);
		virtual void parse_payload_unit() final;
	};

	struct mhw2_parser_t : public psi_parser_t
	{

		parser_status_t  parser_status;
		int fst_preferred_region_id{-1}; //needed to select bouquets

		chdb::epg_type_t epg_type{chdb::epg_type_t::MOVISTAR};

		std::function<reset_type_t(const bouquet_t&, const subtable_info_t&)>
		bat_section_cb = [](const bouquet_t& bouquet, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};


		std::function<reset_type_t(epg_t&, const subtable_info_t&)>
		eit_section_cb = [](epg_t& epg, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};


		mhw2_parser_t(ts_stream_t& parent, int pid) :
			psi_parser_t(parent, pid, "MHW2")
			{
				this->current_unit_type = stream_type::marker_t::illegal;
			}

		mhw2_parser_t(const sdt_bat_parser_t& other) = delete;

		virtual ~mhw2_parser_t() {
		}

		bool parse_mhw2_channel_section(stored_section_t& section,  bouquet_t& bouquet);
		bool parse_mhw2_title_section(stored_section_t& section, epg_t& epg);
		bool parse_mhw2_short_summary_section(stored_section_t& section, epg_t& epg);
		bool parse_mhw2_long_summary_section(stored_section_t& section, epg_t& epg);
		virtual void parse_payload_unit() final;
	};

	struct eit_parser_t : public psi_parser_t
	{
		parser_status_t  parser_status;
		chdb::epg_type_t epg_type{chdb::epg_type_t::UNKNOWN};
		std::function<reset_type_t(epg_t&, const subtable_info_t&)>
		section_cb = [](epg_t& epg, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};



		eit_parser_t(ts_stream_t& parent, int pid, chdb::epg_type_t epg_type) :
			psi_parser_t(parent, pid, "EIT")
			, epg_type(epg_type)
			{
				this->current_unit_type = stream_type::marker_t::illegal;
			}

		eit_parser_t(const sdt_bat_parser_t& other) = delete;

		virtual ~eit_parser_t() {
		}

		bool parse_eit_section(stored_section_t& section, epg_t& ret);
		bool parse_sky_section(stored_section_t& section, epg_t& ret);

		virtual void parse_payload_unit() final;
	};

	struct pat_parser_t : public psi_parser_t
	{
		parser_status_t  parser_status;
		std::function<reset_type_t(const pat_services_t&, const subtable_info_t&)>
		section_cb = [](const pat_services_t& services, const subtable_info_t& subtable_info)
			{return reset_type_t::NO_RESET;};

		pat_parser_t(ts_stream_t& parent, int pid=0x0) :
			psi_parser_t(parent, pid, "PAT")
			{
				this->current_unit_type = stream_type::marker_t::pat;
			}

		pat_parser_t(const pat_parser_t& other) = delete;

		virtual ~pat_parser_t() {
		}

		bool parse_pat_section(stored_section_t& section, pat_services_t& entries);
		virtual void parse_payload_unit() final;
	};

	struct pmt_parser_t : public psi_parser_t
	{
		parser_status_t  parser_status;
		int current_version_number{-1};
		int service_id {-1};

		std::function<reset_type_t(pmt_parser_t*, const pmt_info_t&, bool,
															 const ss::bytebuffer_& p_sec_data)>
		section_cb = [](pmt_parser_t* parser, const pmt_info_t&, bool isnext,
										const ss::bytebuffer_& sec_data)
			{return reset_type_t::NO_RESET;};

		pmt_parser_t(ts_stream_t& parent, int pid, int service_id)
			: psi_parser_t(parent, pid, "PMT")
			, service_id (service_id)
			{
				this->current_unit_type = stream_type::marker_t::pmt;
			}

		pmt_parser_t(const pmt_parser_t& other) = delete;
		virtual ~pmt_parser_t() {
		}

		bool parse_pmt_section(stored_section_t& section, pmt_info_t& pmt);
		virtual void parse_payload_unit() final;
		void restart() {
			current_version_number = -1;
		}
	};


	bool pmt_ca_changed(const pmt_info_t& a,  const pmt_info_t& b);
	uint32_t crc32(const uint8_t* data, int size);

	inline bool is_audio (const pid_info_t& pidinfo) {
		using namespace stream_type;
		stream_type_t st = pidinfo.stream_type;
		if(pidinfo.audio_lang.ac3_descriptor_data.size() > 0)
			return true;
		return
			st == stream_type_t::MPEG1_AUDIO ||
			st == stream_type_t::MPEG2_AUDIO ||
			st == stream_type_t::MPEG4_AUDIO ||
			st == stream_type_t::AAC_AUDIO   ||
			st == stream_type_t::AC3_AUDIO   ||
			st == stream_type_t::EAC3_AUDIO;
	}



	std::ostream& operator<<(std::ostream& os, const pid_info_t& info);
	std::ostream& operator<<(std::ostream& os, const pmt_info_t& pmt);

} //namespace dtdemux
const char* lang_name(const char* code);
const char* lang_name(uint32_t code);
