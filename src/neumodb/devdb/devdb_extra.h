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
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <variant>
#include <optional>


#include "neumodb/devdb/devdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "neumodb/devdb/tune_options.h"

#pragma GCC visibility push(default)


unconvertable_int(int64_t, adapter_mac_address_t);
unconvertable_int(int64_t, card_mac_address_t);
unconvertable_int(int, adapter_no_t);
unconvertable_int(int, frontend_no_t);

namespace devdb::dish {
	devdb::dish_t get_dish(db_txn& devdb_wtxn, int dish_id);
	devdb::dish_t schedule_move(db_txn& devdb_wtxn, devdb::lnb_t& lnb_,
															int target_usals_pos, int target_lnb_sat_pos,
															const devdb::usals_location_t& loc, bool move_has_finished);
	void end_move(db_txn& devdb_wtxn, devdb::dish_t dish);

	bool dish_needs_to_be_moved(db_txn& rtxn, int dish_id, int16_t sat_pos);
	ss::vector_<int8_t> list_dishes(db_txn& devdb_rtxn);

};


namespace devdb {
	void clean(db_txn& devdb_wtxn);
	int16_t make_unique_id(db_txn& txn, const devdb::lnb_key_t& key);
	int16_t make_unique_id(db_txn& txn, const devdb::scan_command_t& scan_command);
	int16_t make_unique_id(db_txn& txn, const devdb::stream_t& streamer);

	devdb::lnb_t lnb_for_lnb_id(db_txn& devdb_rtxn, int8_t dish_id, int16_t lnb_id);

	inline rf_path_t rf_path_for_connection(const devdb::lnb_key_t& lnb_key,
																					const devdb::lnb_connection_t& lnb_connection) {
		return devdb::rf_path_t{lnb_key, lnb_connection.card_mac_address, lnb_connection.rf_input};
	}

	inline const lnb_connection_t* connection_for_rf_path(const devdb::lnb_t& lnb, const devdb::rf_path_t& rf_path) {
		for(auto& conn: lnb.connections) {
			if (conn.card_mac_address == rf_path.card_mac_address && conn.rf_input == rf_path.rf_input)
				return & conn;
		}
		return nullptr;
	}
	inline lnb_connection_t* connection_for_rf_path(devdb::lnb_t& lnb, devdb::rf_path_t& rf_path) {
		return const_cast<lnb_connection_t*>(
			connection_for_rf_path(const_cast<const devdb::lnb_t&>(lnb), const_cast<const devdb::rf_path_t&>(rf_path)));
	}

	bool lnb_can_tune_to_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
													 bool disregard_networks, ss::string_ *error=nullptr);

	bool lnb_can_scan_sat_band(const devdb::lnb_t& lnb, const chdb::sat_t& sat,
														 const chdb::band_scan_t& band_scan,
														 bool disregard_networks, ss::string_* error=nullptr);

};

struct subscribe_ret_t {

	struct aa_t {
		std::optional<devdb::fe_t> updated_old_dbfe;
		std::optional<devdb::fe_t> updated_new_dbfe;
		std::optional<devdb::rf_path_t> rf_path;
		std::optional<devdb::lnb_t> lnb;
		bool is_new_aa () const {
			return !updated_old_dbfe || !updated_new_dbfe || updated_old_dbfe->k != updated_new_dbfe->k;
		}; //true if old_fe.adapter_no differs from new_fe.adapter_no
	};
	bool failed{false};

	subscription_id_t subscription_id{subscription_id_t::NONE}; /*Current or existing subscription_id_t*/
	bool was_subscribed{false};

	subscription_id_t sub_to_reuse{subscription_id_t::NONE}; /*use this active_adapter without retuning
																														 but may need to create a new active_service*/
	bool retune{false}; /*retune adapter to different mux?
												only relevant if aa_sub_to_reuse == our existing subscription_id*/

	bool change_service{false}; /*if false, use the service of subscription with id aa_sub_to_reuse,
																if true, then add a new service on this active_adapter;
																only relevant if aa_sub_to_reuse != subscription_id_t::NONE*/

	//value below only relevant if aa_sub_to_reuse  == subscription_id_t::NONE
	aa_t aa;
	static std::atomic_int next_subscription_id; //initialised in fe_subscribe.cc

	tune_pars_t tune_pars;
	inline  bool subscription_failed() const {
		return failed;
	}

	subscribe_ret_t(subscription_id_t subscription_id_, bool failed) :
		subscription_id(subscription_id_)
		{
			was_subscribed = ((int)subscription_id >=0);
			if(subscription_id == subscription_id_t::NONE) {
				subscription_id = (subscription_id_t) next_subscription_id.fetch_add(1, std::memory_order_relaxed);
			}
		}
	subscribe_ret_t()
		{}
};

namespace devdb {
	namespace update_lnb_preserve_t {
		enum flags : int {
			NONE = 0x0,
			KEY = 0x1,
			GENERAL  = 0x2, //enabled, pol_type, freq_mid....
			USALS  = 0x4,
			CONNECTIONS = 0x8,
			NETWORKS = 0x10,
			REF_MUX = 0x20,
			ALL = 0xffff,
		};
	};
};

namespace devdb::lnb {
	//int current_sat_pos(devdb::lnb_t& lnb, const devdb::usals_location_t& loc);
	int angle_to_sat_pos(int angle, const usals_location_t& loc);
	int sat_pos_to_usals_par(int angle, int my_longitude, int my_latitude);
	int sat_pos_to_angle(int angle, int my_longitude, int my_latitude);
	void set_lnb_offset_angle(lnb_t&  lnb, const usals_location_t& loc);

	std::tuple<bool, int, int, int>  has_network(const lnb_t& lnb, int16_t sat_pos);

	inline bool dish_needs_to_be_moved(const lnb_t& lnb, int16_t sat_pos) {
		auto [has_network_, priority, usals_move_amount, usals_pos] = has_network(lnb, sat_pos);
		return !has_network_ || usals_move_amount != 0 ;
	}

	const lnb_network_t* get_network(const lnb_t& lnb, int16_t sat_pos);
	lnb_network_t* get_network(lnb_t& lnb, int16_t sat_pos);

	/*
		band = 0 or 1 for low or high (22Khz off/on)
		voltage = 0 (V,R, 13V) or 1 (H, L, 18V) or 2 (off)
		freq: frequency after LNB local oscilllator compensation
	*/
	std::tuple<int, int, int> band_voltage_freq_for_mux(const lnb_t& lnb, const chdb::dvbs_mux_t& mux);
	chdb::sat_sub_band_t band_for_freq(const lnb_t& lnb, int32_t frequency);

	inline chdb::sat_sub_band_t band_for_mux(const lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
		return band_for_freq(lnb, mux.frequency);
	}

	int voltage_for_pol(const lnb_t& lnb, const chdb::fe_polarisation_t pol);

  /*
		translate driver frequency to real frequency
		tone = 0 if off
		voltage = 1 if high
		@todo: see linuxdvb_lnb.c for more configs to support
		@todo: uniqcable
	*/
	int uncorrected_freq_for_driver_freq(const lnb_t& lnb, int frequency, bool high_band);

	int freq_for_driver_freq(const lnb_t& lnb, int frequency, bool high_band);
	int driver_freq_for_freq(const lnb_t& lnb, int frequency);


	std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, bool>
	band_frequencies(const lnb_t& lnb, chdb::sat_sub_band_t band);

	bool add_or_edit_network(lnb_t& lnb, const usals_location_t& loc, lnb_network_t& network);
	bool add_or_edit_connection(db_txn& devdb_txn, lnb_t& lnb, lnb_connection_t& connection);

	bool update_lnb_network_from_positioner(db_txn& devdb_wtxn, lnb_t&  lnb, int16_t current_sat_pos);
	bool update_lnb_connection_from_positioner(db_txn& devdb_wtxn, devdb::lnb_t&  lnb,
																						 devdb::lnb_connection_t& curr_connection);

	bool update_lnb_from_lnblist(db_txn& devdb_wtxn, lnb_t&  lnb, bool save);
	bool update_lnb_from_db(db_txn& wtxn, lnb_t& lnb, const std::optional<usals_location_t>& loc,
													devdb::update_lnb_preserve_t::flags preserve, bool save, int16_t current_sat_pos,
													lnb_connection_t* cur_conn);
	void reset_lof_offset(db_txn& devdb_wtxn, lnb_t&  lnb);

	std::tuple<uint32_t, uint32_t> lnb_frequency_range(const lnb_t& lnb);

	void update_lnb_adapter_fields(db_txn& wtxn, const fe_t& fe);


	void update_lnbs(db_txn& devdb_wtxn);

	inline bool can_move_dish(const lnb_connection_t& conn) {
		switch(conn.rotor_control) {
		case rotor_control_t::ROTOR_MASTER_MANUAL:
		case rotor_control_t::ROTOR_MASTER_USALS:
		case rotor_control_t::ROTOR_MASTER_DISEQC12:
			return true; /*this means we will send usals commands. At reservation time, positioners which
										 will really move have already been penalized, compared to positioners already on the
										 correct sat*/
			break;
		default:
			return false;
		}
	}

	chdb::sat_band_t sat_band(const devdb::lnb_t& lnb);
}

namespace devdb::fe {

	std::optional<fe_t> unsubscribe(db_txn& wtxn, subscription_id_t subscription_id);

	bool can_subscribe_mux(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
																			const subscription_options_t& tune_options);
	bool can_subscribe_sat_band(db_txn& wtxn, const chdb::sat_t& sat,
																			const chdb::band_scan_t& band_scan,
																			const subscription_options_t& tune_options);


	template<typename mux_t> bool can_subscribe_dvbc_or_dvbt_mux(db_txn& wtxn,
																															 const mux_t& mux, bool use_blind_tune);
	subscribe_ret_t subscribe_rf_path(db_txn& wtxn, subscription_id_t subscription_id,
																		const subscription_options_t& tune_options,
																		const rf_path_t& rf_path,
																		std::optional<int16_t> sat_pos_to_move_to);

	subscribe_ret_t subscribe_sat_band(db_txn& wtxn, subscription_id_t subscription_id,
																		 const subscription_options_t& tune_options,
																		 const chdb::sat_t& sat,
																		 const chdb::band_scan_t& band_scan,
															 bool do_not_unsubscribe_on_failure);

	template<typename mux_t>
	subscribe_ret_t
	subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
								const subscription_options_t& tune_options,
								const mux_t& mux,
								const chdb::service_t* service,
								bool do_not_unsubscribe_on_failure);

	inline subscribe_ret_t
	subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id,
								const subscription_options_t& tune_options,
								const chdb::any_mux_t& mux,
								const chdb::service_t* service,
								bool do_not_unsubscribe_on_failure) {
		return std::visit([&](auto&mux) -> subscribe_ret_t {
			return subscribe_mux(wtxn, subscription_id, tune_options, mux, service, do_not_unsubscribe_on_failure);
		}, mux);
	}
};

namespace devdb::fe_subscription {
	inline bool may_move_dish(const fe_subscription_t& sub) {
		return sub.dish_usals_pos == sat_pos_none;
	}

	inline bool may_change_lnb(const fe_subscription_t& sub) {
		return sub.usals_pos == sat_pos_none ||
			sub.sat_pos == sat_pos_none ||
			sub.pol == chdb::fe_polarisation_t::NONE ||
			sub.band == chdb::sat_sub_band_t::NONE;
	}

	inline bool is_exclusive(const fe_subscription_t& sub) {
		return may_change_lnb(sub) || may_move_dish(sub);
	}

};
#ifdef declfmt
#undef declfmt
#endif
#define declfmt(t)																											\
	template <> struct fmt::formatter<t> {																\
		inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) { \
			return ctx.begin();																								\
		}																																		\
																																				\
		format_context::iterator format(const t&, format_context& ctx) const ; \
	}

declfmt(devdb::lnb_key_t);
declfmt(devdb::lnb_t);
declfmt(devdb::lnb_connection_t);
declfmt(devdb::lnb_network_t);
declfmt(devdb::fe_key_t);
declfmt(devdb::fe_subscription_t);
declfmt(devdb::fe_t);
declfmt(devdb::run_type_t);
declfmt(devdb::run_status_t);
declfmt(devdb::run_result_t);
declfmt(devdb::tune_mode_t);
declfmt(devdb::stream_t);
#if 0 //not yet implemented
declfmt(devdb::tuned_frequency_offsets_key_t);
declfmt(devdb::tuned_frequency_offsets_t);
declfmt(devdb::tuned_frequency_offset_t);
declfmt(devdb::fe_supports_t);
declfmt(devdb::user_options_t);
declfmt(devdb::usals_location_t);
declfmt(devdb::rf_path_t);
declfmt(devdb::subscription_data_t);
#endif


#undef declfmt

#pragma GCC visibility pop
