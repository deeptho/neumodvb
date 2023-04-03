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
#include <util/template_util.h>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <variant>

#include "neumodb/chdb/chdb_db.h"
#pragma GCC visibility push(default)

namespace devdb {
	struct lnb_t;
	struct lnb_network_t;
};

namespace chdb {
	using namespace chdb;

	typedef std::variant<chdb::dvbs_mux_t, chdb::dvbc_mux_t, chdb::dvbt_mux_t> any_mux_t;
	bool has_epg_type(const chdb::any_mux_t& mux, chdb::epg_type_t epg_type);
	const mux_key_t* mux_key_ptr(const chdb::any_mux_t& key);
	const mux_key_t* mux_key_ptr(const chdb::any_mux_t&& key) =delete; //cannot be used with temporaries

	mux_key_t* mux_key_ptr(chdb::any_mux_t& mux);
	mux_key_t* mux_key_ptr(chdb::any_mux_t&& mux) = delete; //cannot be used with temporaries

	template<typename mux_t> inline  const mux_key_t* mux_key_ptr(const mux_t& mux) { return &mux.k;}

	const mux_common_t* mux_common_ptr(const chdb::any_mux_t& key);
	mux_common_t* mux_common_ptr(chdb::any_mux_t& key);
	bool put_record(db_txn&txn, const chdb::any_mux_t& mux, unsigned int put_flags=0);
	void delete_record(db_txn&txn, const chdb::any_mux_t& mux);


  /*
		returns true if epg type was actually updated
	*/
	bool add_epg_type(chdb::any_mux_t& mux, chdb::epg_type_t tnew);

	/*
		returns true if epg type was actually updated
	*/
	bool remove_epg_type(chdb::any_mux_t& mux, chdb::epg_type_t tnew);


	enum class update_mux_ret_t : int {
		UNKNOWN, //not in db
		MATCHING_SI_AND_FREQ, //network_id, ts_id and sat_pos match, and frequencsy is close
		MATCHING_FREQ, //a mux exist with close frequenct, but with wrong network_id, ts_id
		NO_MATCHING_KEY, /*to save the mux, we would have to change the key, but this is not allowed
											 by caller, and this mux is not a template*/
		NEW, //the mux is new
		EQUAL //the mux exists and matches also in modulation parameters
	};


	namespace update_mux_preserve_t {
		enum flags : int {
			NONE = 0x0,
			SCAN_DATA  = 0x1,
			SCAN_STATUS  = 0x2,
			NUM_SERVICES  = 0x4, //directly used
			EPG_TYPES   = 0x8, // directly used
			NIT_SI_DATA = 0x10,
			MUX_COMMON = SCAN_DATA | SCAN_STATUS | NUM_SERVICES | EPG_TYPES | NIT_SI_DATA,

			MUX_KEY = 0x20, //directly used
			TUNE_DATA = 0x40,
			MTIME = 0x80,

			ALL = 0xffff,
		};
	};

	template<typename mux_t>
	void on_mux_key_change(db_txn&txn, const mux_key_t& old_mux_key,
												 mux_t& new_mux,
												 system_time_t now_);

	void on_mux_key_change(db_txn&txn, const mux_key_t& old_mux,
												 chdb::dvbs_mux_t& new_mux,
												 system_time_t now_);

	void on_mux_key_change(db_txn&txn, const mux_key_t& old_mux,
												 chdb::any_mux_t& new_mux,
												 system_time_t now_);
	/*
		if callback returns false; save is aborted
		if callback receives nullptr, record was not found
	*/
	update_mux_ret_t update_mux(db_txn&txn, chdb::any_mux_t& mux,
															system_time_t now, update_mux_preserve_t::flags preserve,
															std::function<bool(chdb::mux_common_t*, const chdb::mux_key_t*)> cb,
															bool ignore_key, bool ignore_t2mi_pid, bool must_exist);

	inline update_mux_ret_t update_mux(db_txn&txn, chdb::any_mux_t& mux,
																		 system_time_t now, update_mux_preserve_t::flags preserve,
																		 bool ignore_key, bool ignore_t2mi_pid, bool must_exist) {
		return update_mux(txn, mux, now, preserve,
											[](chdb::mux_common_t*, const chdb::mux_key_t*) { return true;}, ignore_key,
											ignore_t2mi_pid, must_exist);
	}

	template <typename mux_t> void clear_all_streams_pending_status(db_txn& chdb_wtxn,
																																	system_time_t now_, const mux_t& mux);

	inline void clear_all_streams_pending_status(db_txn& chdb_wtxn, system_time_t now_, const chdb::any_mux_t& mux) {
		std::visit([&](auto& mux){
			clear_all_streams_pending_status(chdb_wtxn, now_, mux);
		},mux);
	}

	/*! Put a mux record, taking into account that its key may have changed
		returns: true if this is a new mux (first time scanned); false otherwise
		if callback returns false; save is aborted
		if callback receives nullptr, record was not found

	*/
	template<typename mux_t>
	update_mux_ret_t update_mux(db_txn& txn, mux_t& mux,  system_time_t now, update_mux_preserve_t::flags preserve,
															std::function<bool(chdb::mux_common_t*, const chdb::mux_key_t*)> cb,
															bool ignore_key, bool ignore_t2mi_pid, bool must_exist);

	template<typename mux_t>
	update_mux_ret_t update_mux(db_txn& txn, mux_t& mux,  system_time_t now, update_mux_preserve_t::flags preserve,
															bool ignore_key, bool ignore_t2mi_pid, bool must_exist) {
		return update_mux(txn, mux, now, preserve, [](chdb::mux_common_t*, const chdb::mux_key_t*) { return true;},
											ignore_key, ignore_t2mi_pid, must_exist);
	}


	template<typename mux_t>
	inline bool is_template(const mux_t& mux) {
		return mux.c.tune_src == tune_src_t::TEMPLATE;
	}

	inline bool is_template(const chdb::any_mux_t& mux) {
		return mux_common_ptr(mux)->tune_src == tune_src_t::TEMPLATE;
	}

	inline bool is_template(const chg_t& chg) {
		return chg.k.bouquet_id == bouquet_id_template;
	}

	inline bool is_template(const chgm_t& chgm) {
		return chgm.k.channel_id == channel_id_template;
	}


	template<typename mux_t>
	uint16_t make_unique_id(db_txn& txn, mux_key_t key);
	int32_t make_unique_id(db_txn& txn, chdb::chg_key_t key);
	int32_t make_unique_id(db_txn& txn, chdb::chgm_key_t key);

	template<typename T>
	inline void make_unique_if_template(db_txn& txn, T& t );

	/*
		If the mux is a template (mux.k.network_id==0 && mux.k.mux.ts_id==and not yet has
		an extra_id (mux.k.extra_id == 0), then assign it a
		unique extra_id
	*/
	template<typename mux_t>
	inline void make_unique_if_template(db_txn& txn, mux_t& mux ) {
		if(is_template(mux) && mux.k.extra_id==0)
			mux.k.extra_id = chdb::make_unique_id<mux_t>(txn, mux.k);
	}

	template<>
	inline void make_unique_if_template<chg_t>(db_txn& txn, chg_t& chg ) {
		if(is_template(chg))
			chg.k.bouquet_id = chdb::make_unique_id(txn, chg.k);
	}

	template<>
	inline void make_unique_if_template<chgm_t>(db_txn& txn, chgm_t& chgm) {
		if(is_template(chgm))
			chgm.k.channel_id = chdb::make_unique_id(txn, chgm.k);
	}

	chdb::media_mode_t media_mode_for_service_type(uint8_t service_type);

	std::tuple<std::optional<chdb::dvbs_mux_t>, std::optional<chdb::sat_t>>
	select_sat_and_reference_mux(db_txn& rtxn, const devdb::lnb_t& lnb,
															 const chdb::dvbs_mux_t* proposed_mux);

	chdb::dvbs_mux_t
	select_reference_mux(db_txn& chdb_rtxn, const devdb::lnb_t& lnb, int16_t sat_pos);

};

namespace chdb {
	using namespace chdb;
	void to_str(ss::string_& ret, const scan_status_t& scan_status);
	void to_str(ss::string_& ret, const language_code_t& code);
	void to_str(ss::string_& ret, const sat_t& sat);
	void to_str(ss::string_& ret, const mux_key_t& mux_key);
	void to_str(ss::string_& ret, const dvbs_mux_t& mux);
	void to_str(ss::string_& ret, const dvbc_mux_t& mux);
	void to_str(ss::string_& ret, const dvbt_mux_t& mux);
	void to_str(ss::string_& ret, const any_mux_t& mux);
	void to_str(ss::string_& ret, const mux_key_t& k);
	void to_str(ss::string_& ret, const service_t& service);
	void to_str(ss::string_& ret, const chg_t& chg);
	void to_str(ss::string_& ret, const chgm_t& channel);
	void to_str(ss::string_& ret, tune_src_t tune_src);
	void to_str(ss::string_& ret, key_src_t key_src);


	inline void to_str(ss::string_& ret, const mux_common_t& t) {
		ret.sprintf("%p", &t);
	}
	inline void to_str(ss::string_& ret, const browse_history_t& t) {
		ret.sprintf("%p", &t);
	}
	inline void to_str(ss::string_& ret, const chgm_key_t& t) {
		ret.sprintf("%p", &t);
	}
	inline void to_str(ss::string_& ret, const fe_delsys_dvbs_t& t) {
		ret.sprintf("%p", &t);
	}
	inline void to_str(ss::string_& ret, const chg_key_t& t) {
		ret.sprintf("%p", &t);
	}
	inline void to_str(ss::string_& ret, const service_key_t& t) {
		ret.sprintf("%p", &t);
	}

	template<typename T>
	inline void to_str(ss::string_& ret, const T& t) {
	}


	template<typename T>
	inline auto to_str(T&& t)
	{
		ss::string<128> s;
		to_str((ss::string_&)s, (const T&) t);
		return s;
	}

	void sat_pos_str(ss::string_& s, int position);

	inline auto sat_pos_str(int position) {
		ss::string<8> s;
		sat_pos_str(s, position);
		return s;
	}

	void matype_str(ss::string_& s, int16_t matype, int rolloff=-1);
	inline auto matype_str(int16_t matype, int rolloff=-1) {
		ss::string<32> s;
		matype_str(s, matype, rolloff);
		return s;
	}

	std::ostream& operator<<(std::ostream& os, const scan_status_t& scan_status);
	std::ostream& operator<<(std::ostream& os, const scan_result_t& scan_result);
	std::ostream& operator<<(std::ostream& os, const language_code_t& code);
	std::ostream& operator<<(std::ostream& os, const sat_t& sat);
	std::ostream& operator<<(std::ostream& os, const mux_key_t& mux_key);
	std::ostream& operator<<(std::ostream& os, const dvbs_mux_t& mux);
	std::ostream& operator<<(std::ostream& os, const dvbc_mux_t& mux);
	std::ostream& operator<<(std::ostream& os, const dvbt_mux_t& mux);
	std::ostream& operator<<(std::ostream& os, const any_mux_t& mux);
	std::ostream& operator<<(std::ostream& os, const mux_key_t& k);
	std::ostream& operator<<(std::ostream& os, const service_t& service);
	std::ostream& operator<<(std::ostream& os, const service_key_t& k);
	std::ostream& operator<<(std::ostream& os, const fe_polarisation_t& pol);
	std::ostream& operator<<(std::ostream& os, const chg_t& chg);
	std::ostream& operator<<(std::ostream& os, const chgm_t& channel);
	std::ostream& operator<<(std::ostream& os, const tune_src_t tune_src);
	std::ostream& operator<<(std::ostream& os, const key_src_t key_src);

	inline bool is_same(const chgm_t &a, const chgm_t &b) {
		if (!(a.k == b.k))
			return false;
		if (!(a.chgm_order == b.chgm_order))
			return false;
		if (!(a.user_order == b.user_order))
			return false;
		if (!(a.media_mode == b.media_mode))
			return false;
		if (!(a.name == b.name))
			return false;
		return true;
	}

	bool is_same(const dvbs_mux_t &a, const dvbs_mux_t &b);
	bool is_same(const dvbc_mux_t &a, const dvbc_mux_t &b);
	bool is_same(const dvbt_mux_t &a, const dvbt_mux_t &b);
	bool tuning_is_same(const dvbs_mux_t& a, const dvbs_mux_t& b);
	bool tuning_is_same(const dvbc_mux_t& a, const dvbc_mux_t& b);
	bool tuning_is_same(const dvbt_mux_t& a, const dvbt_mux_t& b);

}

namespace chdb::sat {
	/*!
		find a satellite which is close to position; returns the best match
		We adopt a tolerance of sat_pos_tolerance.
	*/

	inline auto find_by_position_fuzzy(db_txn& txn, int position, int tolerance = sat_pos_tolerance) {
		using namespace chdb;
		auto c = sat_t::find_by_key(txn, position, find_leq);
		if (!c.is_valid()) {
			c.close();
			return c;
		}
		int best =std::numeric_limits<int>::max();

		for(auto const& sat: c.range()) {
			//@todo double check that searching starts at a closeby cursor position
			if (sat.sat_pos == position) {
				return c;
			}
			if (position - sat.sat_pos   > tolerance)
				continue;
			if (sat.sat_pos - position > tolerance)
				break;
			auto delta = std::abs(sat.sat_pos - position);
			if (delta> best) {
				c.prev();
				return c;
			}
			best = delta;
		}
		c.close();
		return c;
	}
};

namespace chdb {

	/* tuning parameters (sat_pos, polarisation, frequency) indicate "equal or overlapping" muxes
	 */
	bool matches_physical_fuzzy(const dvbs_mux_t& a, const dvbs_mux_t& b, bool check_sat_pos=true);
	bool matches_physical_fuzzy(const dvbc_mux_t& a, const dvbc_mux_t& b, bool check_sat_pos=true);
	bool matches_physical_fuzzy(const dvbt_mux_t& a, const dvbt_mux_t& b, bool check_sat_pos=true);
	bool matches_physical_fuzzy(const any_mux_t& a, const any_mux_t& b, bool check_sat_pos=true);

	/* tuning parameters (sat_pos, polarisation, frequency) indicate muxes with same bandwidth
		 and frequency with tight tolerance
	*/
	bool matches_physical(const dvbs_mux_t& a, const dvbs_mux_t& b, bool check_sat_pos, bool ignore_stream_id);
	bool matches_physical(const dvbc_mux_t& a, const dvbc_mux_t& b, bool check_sat_pos, bool ignore_stream_id);
	bool matches_physical(const dvbt_mux_t& a, const dvbt_mux_t& b, bool check_sat_pos, bool ignore_stream_id);
	bool matches_physical(const any_mux_t& a, const any_mux_t& b, bool check_sat_pos, bool ignore_stream_id);

	inline int dvb_type(const chdb::any_mux_t& mux) {
		auto sat_pos = chdb::mux_key_ptr(mux)->sat_pos;
		if (sat_pos == sat_pos_dvbc || sat_pos == sat_pos_dvbt)
			return sat_pos;
		else
			return sat_pos_dvbs;
	}



	inline bool usals_is_close(int sat_pos_a, int sat_pos_b) {
		return std::abs(sat_pos_a-sat_pos_b) <= 100;
	}

	/*!
		find a mux which the correct network_id and ts_id and closely matching frequency
		we adopt a tolerance of 1000kHz
	*/
	template<typename mux_t>
	db_tcursor<mux_t> find_by_mux(db_txn& txn, const mux_t& mux);

/*!
	find a matching mux, based on sat_pos, ts_id, network_id, ignoring  extra_id
	This is called by the SDT_other parsing code and will not work if multiple
	muxes exist on the same sat with the same (network_id, ts_id)
*/
	struct get_by_nid_tid_unique_ret_t {
		enum unique_type_t {
			UNIQUE,
			UNIQUE_ON_SAT,
			NOT_UNIQUE,
			NOT_FOUND
		};
		chdb::any_mux_t mux;
		unique_type_t unique{NOT_FOUND};
	};

	get_by_nid_tid_unique_ret_t get_by_nid_tid_unique(db_txn& txn, int16_t network_id, int16_t ts_id,
																										int16_t tuned_sat_pos);
	get_by_nid_tid_unique_ret_t get_by_network_id_ts_id(db_txn& txn, uint16_t network_id, uint16_t ts_id);
	template<typename mux_t>
	db_tcursor<mux_t> find_by_mux_physical(db_txn& txn, const mux_t& mux, bool ignore_stream_id,
																				 bool ignore_keys, bool ignore_t2mi_pid);

	std::optional<chdb::any_mux_t> get_by_mux_physical(db_txn& txn, chdb::any_mux_t& mux, bool ignore_stream_id,
																										 bool ignore_key, bool ignore_t2mi_pid);

	void clean_scan_status(db_txn& wtxn);
	void clean_expired_services(db_txn& wtxn, std::chrono::seconds age);
	void clean_chgms_without_services(db_txn& wtxn);
};

namespace chdb::dvbs_mux {
	/*!
		return all sats in use by muxes
	*/
	ss::vector_<int16_t> list_distinct_sats(db_txn &txn);

}




namespace chdb {

	template<typename mux_t>
	requires (!is_same_type_v<mux_t, chdb::dvbs_mux_t>)
	db_tcursor_index<mux_t>
	find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance=1000);

	db_tcursor_index<chdb::dvbs_mux_t>
	find_by_mux_fuzzy(db_txn& txn, const chdb::dvbs_mux_t& mux, bool ignore_stream_ids, bool ignore_t2mi_pid);

	inline bool is_t2mi_mux(const chdb::any_mux_t& mux) {
		const auto *dvbs_mux =  std::get_if<chdb::dvbs_mux_t>(&mux);
		return dvbs_mux && (dvbs_mux->k.t2mi_pid > 0);
	}
};

namespace chdb::dvbt_mux {

	/*!
		find a mux which with the correct network_id and ts_id and closely matching frequency
	*/
	db_tcursor<chdb::dvbt_mux_t> find_by_mux_key_fuzzy(db_txn& txn, const dvbt_mux_t& mux, int tolerance=1000);

	inline bool is_template(const dvbt_mux_t& mux)
	{
		return mux.c.tune_src == tune_src_t::TEMPLATE;
	}
	/*
		If the mux is a template (mux.k.network_id==0 && mux.k.mux.ts_id==and not yet has
		an extra_id (mux.k.extra_id == 0), then assign it a
		unique extra_id
	*/
	inline void make_unique_if_template(db_txn& txn, dvbt_mux_t& mux ) {
		if(is_template(mux) && mux.k.extra_id==0)
			mux.k.extra_id = chdb::make_unique_id<dvbt_mux_t>(txn, mux.k);
	}

}

namespace chdb::dvbc_mux {

	/*!
		find a mux which with the correct network_id and ts_id and closely matching frequency
	*/
	db_tcursor<chdb::dvbc_mux_t> find_by_mux_key_fuzzy(db_txn& txn, const dvbc_mux_t& mux, int tolerance=1000);

	inline bool is_template(const dvbc_mux_t& mux)
	{
		return mux.c.tune_src == tune_src_t::TEMPLATE;
	}
	/*
		If the mux is a template (mux.k.network_id==0 && mux.k.mux.ts_id==and not yet has
		an extra_id (mux.k.extra_id == 0), then assign it a
		unique extra_id
	*/
	inline void make_unique_if_template(db_txn& txn, dvbc_mux_t& mux ) {
		if(is_template(mux) && mux.k.extra_id==0)
			mux.k.extra_id = chdb::make_unique_id<dvbc_mux_t>(txn, mux.k);
	}
}

namespace chdb::service {

	inline auto find_by_mux_sid(db_txn &txn, const mux_key_t &mux_key, uint16_t service_id) {
		return service_t::find_by_key(txn, service_key_t(mux_key, service_id), find_eq);
	}



/*
	find first service on mux
	@todo this is used to loop over all services on a mux, but how do we stop the iteration
	when the cursor reaches the next mux?
	Currently caller must handle this by checking for change in mux
	set_prefix_key is the solution
*/
	inline auto find_by_mux_key(db_txn& txn, const mux_key_t& mux_key) {
		service_key_t service_key{};
		service_key.mux = mux_key;
		auto c = service_t::find_by_key(txn, service_key, find_geq, service_t::partial_keys_t::mux);
		return c;
	}



}

namespace chdb {

	void merge_services(db_txn& wtxn, const mux_key_t& src_key, const any_mux_t& dst);
	void remove_services(db_txn& wtxn, const mux_key_t& mux_key);


	template<typename mux_t> float min_snr(const mux_t& mux);

	float min_snr(const chdb::any_mux_t& mux);

	std::optional<chdb::any_mux_t> find_mux_by_key(db_txn& txn, const chdb::mux_key_t&mux_key);

	const char* lang_name(const language_code_t& code);
	inline bool is_same_language(language_code_t a, language_code_t b) {
		a.position =0;
		b.position =0;
		return a == b;
	}

	class history_mgr_t {
		bool inited=false;
		constexpr static int hist_size = 8;
		neumodb_t& db;
	public:
		chdb::browse_history_t h;

		history_mgr_t(neumodb_t& db_, int32_t user_id=0);
		void init();
		void clear();
		void save();
		void save(const service_t& service);
		void save(const chgm_t& channel);
		std::optional<chdb::service_t> last_service();
		std::optional<chdb::service_t> prev_service();
		std::optional<chdb::service_t> next_service();
		std::optional<chdb::service_t> recall_service();
		std::optional<chdb::chgm_t> last_chgm();
		std::optional<chdb::chgm_t> prev_chgm();
		std::optional<chdb::chgm_t> next_chgm();
		std::optional<chdb::chgm_t> recall_chgm();

	};

	delsys_type_t delsys_to_type(chdb::fe_delsys_t delsys);
	template<typename mux_t> inline constexpr delsys_type_t delsys_type_for_mux_type();

	template<> inline constexpr delsys_type_t delsys_type_for_mux_type<dvbs_mux_t>() { return  delsys_type_t::DVB_S;}
	template<> inline constexpr delsys_type_t delsys_type_for_mux_type<dvbc_mux_t>() { return  delsys_type_t::DVB_C;}
	template<> inline constexpr delsys_type_t delsys_type_for_mux_type<dvbt_mux_t>() { return  delsys_type_t::DVB_T;}


	bool bouquet_contains_service(db_txn& rtxn, const chdb::chg_t& chg, const chdb::service_key_t& service_key);

	bool toggle_service_in_bouquet(db_txn& wtxn, const chg_t& chg, const service_t& service);
	bool toggle_channel_in_bouquet(db_txn& wtxn, const chg_t& chg, const chgm_t& chgm);

}

namespace  chdb::service {
	void update_audio_pref(db_txn&txn, const chdb::service_t& service);
	void update_subtitle_pref(db_txn&txn, const chdb::service_t& service);
}

#pragma GCC visibility pop
