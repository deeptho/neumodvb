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
#include "stackstring.h"
#include "active_stream.h"
#include "receiver.h"
#include "scan.h"

#include <functional>
#include <map>


using namespace dtdemux;



struct pat_service_t {
	bool pmt_analysis_started{false};
	bool pmt_analysis_finished{false};
	bool encrypted{false};
	pmt_info_t pmt;
	std::shared_ptr<pmt_parser_t> parser;

};


struct pmt_data_t {
	std::map<uint16_t, pat_service_t> by_service_id; //services in pat indexed by service_id
	reset_type_t pmt_section_cb(const pmt_info_t& pmt, bool isnext);
	bool saved{false};  //pmts saved to db?
	int num_pmts_received{0};
	bool all_received() const {
		return num_pmts_received == (int) by_service_id.size();
	}
};

struct pat_data_t {

	struct pat_table_t : public pat_services_t {
		int num_sections_processed{0};
		ss::vector<pat_entry_t> last_entries; //from the previous complete PAT
		subtable_info_t subtable_info;
		void reset() {
			*this = pat_table_t();
		}

	};



	std::map<uint16_t, pat_table_t> by_ts_id; //indexed by ts_id

	inline bool has_ts_id(uint16_t ts_id) const {
		return by_ts_id.find(ts_id) != by_ts_id.end();
	}


	bool stable_pat_{false};
	constexpr static  std::chrono::duration pat_change_timeout = 5000ms; //in ms
	steady_time_t last_pat_change_time{};

	void reset() {
		*this = pat_data_t();
	}


	/*
		pat has been the same for stable_pat_timeout seconds

		If no pat is present, this is also considered a stable situation
		Note that "tuner not locked" will be handled higher up

	*/
	bool stable_pat(uint16_t ts_id);
	// check all pat tables
	bool stable_pat();

};


struct mux_presence_t {
	bool in_nit{false};
	bool in_sdt{false};
	bool sdt_completed{false};
};

struct network_data_t {
	uint16_t network_id{0xffff};
	bool is_actual{false};
	int num_muxes{0};
	subtable_info_t subtable_info{};
	int num_sections_processed{0};
	network_data_t(uint16_t network_id)
		: network_id(network_id)
		{}

	inline void reset() {
		*this = network_data_t(network_id);
	}
};

struct onid_data_t {
	int sdt_num_muxes_completed{0}; //number marked completed by sdt
	int sdt_num_muxes_present{0}; //number retrieved by sdt
	int nit_num_muxes_present{0}; //number found in NIT table
	uint16_t original_network_id;

	std::map<uint16_t, mux_presence_t> mux_presence;

	mux_presence_t& add_mux(int ts_id, bool from_sdt) {
		auto[it, inserted] = mux_presence.emplace(ts_id, mux_presence_t{});
		auto& p = it->second;
		if(from_sdt) {
			sdt_num_muxes_present += !p.in_sdt;
			p.in_sdt = true;
		} else {
			nit_num_muxes_present += !p.in_nit;
			p.in_nit = true;
		}
		return p;
	}

	inline bool set_sdt_completed(int ts_id) {
		auto &p = mux_presence[ts_id];
		assert(p.in_sdt);
		bool ret = p.sdt_completed;
		p.sdt_completed = true;
		sdt_num_muxes_completed += !ret;
		if(ret) {
			dtdebugx("sdt: ts_id=%d completed multiple times", ts_id);
		}
		return ret;
	}


#ifndef NDEBUG
	inline bool completed_() const {
		for(const auto& [ts_id, p]: mux_presence) {
			if(!p.in_sdt || !p.in_nit || !p.sdt_completed)
				return false;
		}
		return true;
	}
#endif

	inline bool completed() const {
		auto ret = sdt_num_muxes_present == (int)mux_presence.size() &&
			sdt_num_muxes_completed == (int)mux_presence.size() &&
			nit_num_muxes_present == (int)mux_presence.size();
#ifndef NDEBUG
		auto tst = completed_();
		assert(tst==ret);
#endif
		return ret;
	}


//	subtable_info_t subtable_info;
//	int num_sections_processed{0};
	onid_data_t(uint16_t original_network_id)
		: original_network_id(original_network_id)
		{}
};

struct mux_sdt_data_t {
	subtable_info_t subtable_info;
	int num_sections_processed{0};
};

struct mux_data_t  {

	enum source_t {
		NIT,
		SDT,
		NONE, //can only happen for current mux; any other lookup is initiated from sdt or nit
	};

	//data maintained by nit section code
	bool have_sdt{false};
	bool have_nit{false};

	chdb::any_mux_t mux{};

	bool is_active_mux{false}; //the active mux is the one processing si data (embedded mux for t2mi; else tuned mux)

	bool is_tuned_freq{false};
	//data maintained by sdt section code
	bool has_freesat_home_epg{false};
	bool has_opentv_epg{false};

	mux_sdt_data_t sdt[2]{{}}; //indexed by sdt.is_actual
	subtable_info_t sdt_actual_subtable_info;

	ss::vector<uint16_t, 32> service_ids; //service ids seen


	mux_data_t(const chdb::any_mux_t& mux)
		: mux(mux) {}
};

struct sdt_data_t {
	chdb::mux_key_t mux_key;
	int actual_network_id{-1};
	int actual_ts_id{-1};
	ss::vector<chdb::service_t, 32> actual_services;

	void reset() {
		*this = sdt_data_t();
	}
};

struct active_si_data_t;

struct nit_data_t {

	/*
		The following map contains either data received in NIT_ACTUAL or data
		retrieved from the database.
		network_id, ts_id may be incorrect, but has best-effort values; these values are invalid id
		has_valid_nit_tid(mux.src)
	 */
	std::map <std::pair<uint16_t,uint16_t>, mux_data_t> by_network_id_ts_id;

	ss::vector<int16_t, 4> nit_actual_sat_positions; //sat_positions encountered in any mux listed in nit_actual
	//mux_data_t * tuned_mux_data{nullptr};

	/*
		for each network: record how many muxes sdt should process, and how many
		it has processed.
	 */
	std::map<uint16_t, onid_data_t> by_onid; //indexed by original_network_id
	std::map<uint16_t, network_data_t> by_network_id; //indexed by network_id

	inline onid_data_t& get_original_network(uint16_t network_id) {
		auto [it, inserted] = by_onid.try_emplace(network_id, onid_data_t{network_id});
		return it->second;
	}

	inline network_data_t& get_network(uint16_t network_id) {
		auto [it, inserted] = by_network_id.try_emplace(network_id, network_data_t{network_id});
		return it->second;
	}


	//mux_data_t* tuned_nit_data(const chdb::any_mux_t& tuned_mux);
	//int count_completed();
	void reset() {
		*this = nit_data_t();
	}
	bool update_sdt_completion(scan_state_t& scan_state, const subtable_info_t&info,
														 mux_data_t& mux_data, bool reset=false);

	void reset_sdt_completion(scan_state_t& scan_state, const subtable_info_t&info, mux_data_t& mux_data) {
		auto& sdt = mux_data.sdt[info.is_actual];
		if(sdt.subtable_info.version_number >= 0) //otherwise we have an init
			update_sdt_completion(scan_state, info, mux_data, true);
	}

	bool update_nit_completion(scan_state_t& scan_state, const subtable_info_t&info,
														 network_data_t& network_data, bool reset=false);

};



struct eit_data_t {
	int eit_actual_updated_records{0};
	int eit_actual_existing_records{0};
	int eit_other_updated_records{0};
	int eit_other_existing_records{0};

	struct subtable_count_t {
		int num_known{0};
		int num_completed{0};
	};

	std::map<uint16_t, subtable_count_t> subtable_counts; //indexed by pid

	std::map<std::tuple<uint16_t, uint16_t>,
					 std::tuple<time_t, time_t>> otv_times_for_event_id; //start and end time indexed by channel_id, event_id

	std::map<std::tuple<uint32_t>,
					 std::tuple<epgdb::epg_service_t, time_t, time_t>> mhw2_key_for_event_id; //key and start/end time indexed by summary_id


	void reset() {
		*this = eit_data_t();
	}

};


struct bat_data_t {
	struct bouquet_data_t  {
		int db_num_channels{0}; //nunmber of services stored in db
		ss::vector<uint16_t, 256> channel_ids; //service ids seen on sreen

		//data used by bat
		subtable_info_t subtable_info;
		int num_sections_processed{0};

		bouquet_data_t(int db_num_channels)
			: db_num_channels(db_num_channels)
			{}
	};

	///////////data

	std::map <uint16_t, bouquet_data_t> by_bouquet_id;
	std::map<uint16_t, chdb::service_key_t> opentv_service_keys; //indexed by channel_id


	bouquet_data_t& get_bouquet(const chdb::chg_t& chg) {
		auto [it, found] = by_bouquet_id.try_emplace(chg.k.bouquet_id,
																								 bouquet_data_t{chg.num_channels});
		return it->second;
	}

	inline void reset_bouquet(int bouquet_id) {
		by_bouquet_id.erase(bouquet_id);
	}


	chdb::service_key_t* lookup_opentv_channel(uint16_t channel_id) {
		auto[it, found] = find_in_map(opentv_service_keys, channel_id);
		return found ? &it->second : nullptr;
	}


	void reset() {
		*this = bat_data_t();
	}
};


struct active_si_data_t {
	tune_confirmation_t tune_confirmation;

	pat_data_t pat_data;
	pmt_data_t pmt_data;
	nit_data_t nit_data;
	sdt_data_t sdt_data;
	bat_data_t bat_data;
	eit_data_t eit_data;

	bool is_embedded_si{false};

	scan_state_t scan_state;
	bool scan_in_progress{false};

	inline bool ts_id_in_pat(uint16_t ts_id) {
		return pat_data.by_ts_id.find(ts_id) != pat_data.by_ts_id.end();
	}

	bool pmts_can_be_saved() const {
		return ! pmt_data.saved &&
			nit_actual_done() &&
			sdt_actual_done() &&
			pmt_data.all_received();
	}


	active_si_data_t(bool is_embedded_si)
		: is_embedded_si(is_embedded_si)
		{}

	void reset() {
		*this = active_si_data_t(is_embedded_si);
	}


	/*
		done means: timedout or completed
		completed means: we have retrieved all available info
	*/
	bool pat_done() const {
		return scan_state.done(scan_state_t::completion_index_t::PAT);
	}

	bool nit_actual_done() const {
		return scan_state.done(scan_state_t::completion_index_t::NIT_ACTUAL);
	}

	bool nit_other_done() const {
		return scan_state.done(scan_state_t::completion_index_t::NIT_OTHER);
	}

	bool sdt_actual_done() const {
		return scan_state.done(scan_state_t::completion_index_t::SDT_ACTUAL);
	}

	bool sdt_other_done() const {
		return scan_state.done(scan_state_t::completion_index_t::SDT_OTHER);
	}

	bool bat_done() const {
		return scan_state.done(scan_state_t::completion_index_t::BAT);
	}

	bool pat_completed() const { //PAT as been correctly received
		return scan_state.completed(scan_state_t::completion_index_t::PAT);
	}

	bool nit_actual_completed() const { //NIT_ACTUAL as been correctly received
		return scan_state.completed(scan_state_t::completion_index_t::NIT_ACTUAL);
	}

	bool nit_actual_notpresent() const {
		return scan_state.notpresent(scan_state_t::completion_index_t::NIT_ACTUAL);
	}

	bool nit_other_completed() const {
		return scan_state.completed(scan_state_t::completion_index_t::NIT_OTHER);
	}

	bool sdt_actual_notpresent() const {
		return scan_state.notpresent(scan_state_t::completion_index_t::SDT_ACTUAL);
	}

	bool sdt_actual_completed() const {
		return scan_state.completed(scan_state_t::completion_index_t::SDT_ACTUAL);
	}

	bool sdt_other_completed() const {
		return scan_state.completed(scan_state_t::completion_index_t::SDT_OTHER);
	}

	bool bat_completed() const {
		return scan_state.completed(scan_state_t::completion_index_t::BAT);
	}

	/*
		sdt has foiund complete service data for the same number of muxes
		as were discovered in nit_actual
	 */
	bool network_sdt_completed(uint16_t network_id) const {
		if(!nit_actual_done()) //Note: done() instead of completed()
			return false; //more muxes may be discored in nit; we do not care about nit_other
		auto [it, found] = find_in_map(nit_data.by_onid, network_id);
		if(found) {
			return it->second.completed();
		}
		return false;
	}

	/*
		either we have all nit and sdt info for this network, or it is no longer possible
		that we will receive it because nit_actual has done (completed ot not)
		and sdt is still not done
	 */
	bool network_done(uint16_t original_network_id) const {
		auto [it, found] = find_in_map(nit_data.by_onid, original_network_id);
		if(found)
			return it->second.completed() || nit_actual_done();

		/*
			if nit_actual_done() and nit knows the network, then found should have been true
		 */
		return nit_actual_done();
	}

	bool bouquet_done(uint16_t bouquet_id) const {
		auto [it, found] = find_in_map(bat_data.by_bouquet_id, bouquet_id);
		if(found)
			return it->second.num_sections_processed == it->second.subtable_info.num_sections_present;
		return bat_done();
	}


	bool nit_other_all_networks_completed() const {
		/*@todo: this includes also nit_actual, which is incorrect.

			instead we may have to mainat a per network_id (instead of per onid)
			datastructure and record how many sections we have loaded (and if more could follow)
		 */
		for(auto& [network_id, n]: nit_data.by_network_id) {
			if (!n.is_actual && n.num_sections_processed != n.subtable_info.num_sections_present)
				return false;
		}
		return true;
	}

	bool bat_all_bouquets_completed() const {
		for(auto& [bouquet_id, b]: bat_data.by_bouquet_id) {
			if(b.num_sections_processed != b.subtable_info.num_sections_present)
				return false;
		}
		return true;
	}

	/*
		Usually a mux is only has entries in either SDT_actual or sdt_other,
		but sometimes it is present in both; so we employ checking subtable_info.num_sections_present>0
		as a heuristic to choose between sdt_actual and sdt_other, with a preference for sdt_actual
		in case of doubt.

		This function is oly used in bat processing
	 */
	bool mux_sdt_done(uint16_t network_id, uint16_t ts_id) const {
		auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
		if(found) {
			int result_for_actual= -1;
			for(auto& sdt: it->second.sdt) {
				if (result_for_actual <0)
					result_for_actual = (sdt.num_sections_processed == sdt.subtable_info.num_sections_present);
				if(sdt.subtable_info.num_sections_present>0) {
					return sdt.num_sections_processed == sdt.subtable_info.num_sections_present;
				}
			}
			return result_for_actual;
		}
		return nit_actual_done();
	}

	bool all_known_muxes_completed() const {
		for(auto& [nit_tid, m]: nit_data.by_network_id_ts_id) {
			for(const auto& sdt: m.sdt) {
			if (sdt.num_sections_processed != sdt.subtable_info.num_sections_present)
				return false;
			}
		}
		return true;
	}


};



class active_si_stream_t final : /*public std::enable_shared_from_this<active_stream_t>,*/
	public active_stream_t, active_si_data_t
{
	friend class active_adapter_t;


	friend class tuner_thread_t;

	chdb::chdb_t& chdb;
	epgdb::epgdb_t& epgdb;

	std::optional<db_txn> epgdb_txn_;
	std::optional<db_txn> chdb_txn_;

	inline chdb::mux_key_t stream_mux_key() const {
		auto tmp = reader->stream_mux();
		return *chdb::mux_key_ptr(tmp);
	}

	inline db_txn epgdb_txn() {
		if(!epgdb_txn_)
			epgdb_txn_.emplace(epgdb.wtxn());
		return epgdb_txn_->child_txn();
	}

	inline db_txn chdb_txn() {
		if(!chdb_txn_)
			chdb_txn_.emplace(chdb.wtxn());
		return chdb_txn_->child_txn();
	}

	dtdemux::ts_stream_t stream_parser;
	scan_target_t scan_target; //which SI tables should be scanned?
	bool si_processing_done{false};
	bool call_scan_mux_end{false};
	/*we need one parser per pid; within each pid multiple tables may exist
		but those are transmitted sequentially. Between pids, they are not
		"""Within TS packets of any single PID value, one section is finished before the next one is allowed to be started,
		"""
	*/
	struct parser_slot_t {
		std::shared_ptr<dtdemux::psi_parser_t> p;
		int use_count{0};
	};
	std::map<dvb_pid_t, parser_slot_t>  parsers;

	dtdemux::reset_type_t pat_section_cb(const pat_services_t& pat_services, const subtable_info_t& i);
	reset_type_t pmt_section_cb(const pmt_info_t& pmt, bool isnext);

	dtdemux::reset_type_t nit_section_cb_(nit_network_t& network, const subtable_info_t& i);
	dtdemux::reset_type_t nit_section_cb(nit_network_t& network, const subtable_info_t& i);

	void add_pmt(uint16_t service_id, uint16_t pmt_pid);
	bool sdt_actual_check_confirmation(bool mux_key_changed, int db_corrrect,mux_data_t* p_mux_data);

	dtdemux::reset_type_t
		nit_actual_update_tune_confirmation(chdb::any_mux_t& mux, bool is_active_mux);
	dtdemux::reset_type_t on_nit_section_completion(db_txn& wtxn, network_data_t& network_data,
																					dtdemux::reset_type_t ret, bool is_actual,
																					bool on_wrong_sat, bool done);
	std::tuple<bool, bool>
	sdt_process_service(db_txn& wtxn, const chdb::service_t& service, mux_data_t* p_mux_data,
											bool donotsave, bool is_actual);

	dtdemux::reset_type_t sdt_section_cb_(db_txn& txn, const sdt_services_t&services, const subtable_info_t& i,
																				mux_data_t* p_mux_data);
	dtdemux::reset_type_t sdt_section_cb(const sdt_services_t&services, const subtable_info_t& i);

	dtdemux::reset_type_t bat_section_cb(const bouquet_t& bouquet, const subtable_info_t& i);

	dtdemux::reset_type_t eit_section_cb_(epg_t& epg, const subtable_info_t& i);

	dtdemux::reset_type_t eit_section_cb(epg_t& epg, const subtable_info_t& i);

	mux_data_t* add_reader_mux_from_sdt(db_txn& txn, uint16_t network_id, uint16_t ts_id);

	mux_data_t*
	lookup_mux_data_from_sdt(db_txn& txn, uint16_t network_id, uint16_t ts_id);

	mux_data_t* add_fake_nit(db_txn& txn, uint16_t network_id, uint16_t ts_id, int16_t expected_sat_pos,
													 bool from_sdt);

	bool read_and_process_data_for_fd(const epoll_event* evt);

	void process_si_data();
	bool abort_on_wrong_sat() const;

	void load_movistar_bouquet();
	void load_skyuk_bouquet();

	void on_wrong_sat();

/*
	returns 1 if network  name matches database, 0 if no record was present and -1 if no match
*/
	int save_network(db_txn& txn, const nit_network_t& network, int sat_pos);

	void init_scanning(scan_target_t scan_target_);
	bool init(scan_target_t scan_target_);

	template<typename parser_t, typename... Args>
	auto add_parser(int pid, Args... args) {
		auto & slot = parsers[dvb_pid_t(pid)];
		if (slot.use_count == 0) {
			slot.p = stream_parser.register_pid<parser_t>(pid, args...);
			if(pid!=dtdemux::ts_stream_t::PAT_PID)
				add_pid(pid);
		}
		slot.use_count++;
		return static_cast<parser_t*>(slot.p.get());
	}

	void remove_parser(dtdemux::psi_parser_t* parser) {
		dvb_pid_t pid{(uint16_t) parser->get_pid()};
		auto & slot = parsers[pid];
		if(--slot.use_count == 0) {
			dtdebugx("removing parser for pid %d; use_count=%d\n", (uint16_t)pid, slot.use_count);
			remove_pid(parser->get_pid());
			parsers.erase(pid);
		} else {
			dtdebugx("not removing parser for pid %d; use_count=%d\n", (uint16_t)pid, slot.use_count);
		}
	}
	mux_data_t* add_mux(db_txn& wtxn, chdb::any_mux_t& mux, bool is_actual, bool is_active_mux,
											bool is_tuned_freq, bool from_sdt);

	void process_removed_services(db_txn& txn, chdb::any_mux_t& mux, ss::vector_<uint16_t>& service_ids);

	//for bouquets
	void process_removed_channels(db_txn& txn, const chdb::chg_key_t& chg_key, ss::vector_<uint16_t>& channel_ids);

	void check_timeouts();

	void scan_report();

	bool wrong_sat_detected() const {
		return tune_confirmation.on_wrong_sat;
	}

	bool unstable_sat_detected() const {
		return tune_confirmation.unstable_sat;
	}

	bool fix_mux(chdb::any_mux_t& mux);

	//true if this mux equals the currently streamed mux (embedded mux for t2mi, or tuned mux)
	bool matches_reader_mux(const chdb::any_mux_t& mux, bool from_sdt);

	inline bool is_reader_mux(const chdb::any_mux_t& mux) const {
		auto stream_mux = reader->stream_mux();
		return *mux_key_ptr(mux) == *mux_key_ptr(stream_mux);
	}

	std::tuple<bool, bool> 	update_reader_mux_parameters_from_frontend(chdb::any_mux_t& mux);
	bool update_mux(db_txn& wtxn, chdb::any_mux_t& mux, system_time_t now,
									bool is_active_mux, bool is_tuned_freq,
									bool from_sdt, chdb::update_mux_preserve_t::flags preserve);

	bool fix_tune_mux_template();
	void finalize_scan();
	mux_data_t* tuned_mux_in_nit();
	void update_stream_ids_from_pat(db_txn& wtxn, chdb::any_mux_t& mux);
	void save_pmts(db_txn& wtxn);
	void activate_scan(chdb::any_mux_t& mux, subscription_id_t subscription_id, uint32_t scan_id);
	void check_scan_mux_end();
public:
	void reset_si(bool close_streams);
	void end();

	//void process_psi(int pid, unsigned char* payload, int payload_size);
	active_si_stream_t(receiver_t& receiver,
										 const std::shared_ptr<stream_reader_t>& reader, bool is_embedded_si,
										 ssize_t dmx_buffer_size_=32*1024L*1024);
public:
		virtual ~active_si_stream_t();
};
