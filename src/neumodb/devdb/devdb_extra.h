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
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <variant>
#include <optional>


#include "neumodb/devdb/devdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#pragma GCC visibility push(default)


unconvertable_int(int64_t, adapter_mac_address_t);
unconvertable_int(int64_t, card_mac_address_t);
unconvertable_int(int, adapter_no_t);
unconvertable_int(int, frontend_no_t);

namespace devdb {

	using namespace devdb;

	int16_t make_unique_id(db_txn& txn, devdb::lnb_key_t key);

	inline void make_unique_if_template(db_txn& txn, lnb_t& lnb ) {
		if(lnb.k.lnb_id<0)
			lnb.k.lnb_id = devdb::make_unique_id(txn, lnb.k);
	}

};

namespace devdb {
	using namespace devdb;

	void to_str(ss::string_& ret, const lnb_t& lnb);
	void to_str(ss::string_& ret, const lnb_key_t& lnb_key);
	void to_str(ss::string_& ret, const lnb_network_t& lnb_network);
	void to_str(ss::string_& ret, const lnb_connection_t& lnb_connection);
	void to_str(ss::string_& ret, const fe_band_pol_t& band_pol);
	void to_str(ss::string_& ret, const fe_t& fe);
	void to_str(ss::string_& ret, const fe_key_t& fe_key);
	void to_str(ss::string_& ret, const fe_subscription_t& sub);

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

	std::ostream& operator<<(std::ostream& os, const lnb_key_t& lnb_key);
	std::ostream& operator<<(std::ostream& os, const lnb_t& lnb);
	std::ostream& operator<<(std::ostream& os, const lnb_connection_t& con);
	std::ostream& operator<<(std::ostream& os, const lnb_network_t& lnb_network);
	std::ostream& operator<<(std::ostream& os, const fe_band_pol_t& band_pol);
	std::ostream& operator<<(std::ostream& os, const chdb::fe_polarisation_t& pol);
	std::ostream& operator<<(std::ostream& os, const fe_key_t& fe_key);
	std::ostream& operator<<(std::ostream& os, const fe_subscription_t& sub);
	std::ostream& operator<<(std::ostream& os, const fe_t& fe);

}


namespace devdb::dish {
	//dish objects do not really exist in the database, but curent state (usals_pos) is stored in all relevant lnbs
	int update_usals_pos(db_txn& wtxn, devdb::lnb_t&lnb, int usals_pos, const devdb::usals_location_t& loc, int sat_pos);
	bool dish_needs_to_be_moved(db_txn& rtxn, int dish_id, int16_t sat_pos);
};


namespace devdb {
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

//need for use as key in map
	inline bool operator<(const devdb::fe_key_t a , const devdb::fe_key_t b) {
		return a.adapter_mac_address == b.adapter_mac_address
			? (a.frontend_no < b.frontend_no) :
			(a.adapter_mac_address < b.adapter_mac_address);
	}


	bool lnb_can_tune_to_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool disregard_networks, ss::string_ *error=nullptr);

	struct resource_subscription_counts_t {
		int dish{0};
		int rf_path{0};
		int rf_coupler{0};
		int tuner{0};
	};

}

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
	int angle_to_sat_pos(int angle, const devdb::usals_location_t& loc);
	int sat_pos_to_usals_par(int angle, int my_longitude, int my_latitude);
	int sat_pos_to_angle(int angle, int my_longitude, int my_latitude);
	void set_lnb_offset_angle(devdb::lnb_t&  lnb, const devdb::usals_location_t& loc);
	std::tuple<bool, int, int, int>  has_network(const lnb_t& lnb, int16_t sat_pos);

	inline bool dish_needs_to_be_moved(const lnb_t& lnb, int16_t sat_pos) {
		auto [has_network_, priority, usals_move_amount, usals_pos] = has_network(lnb, sat_pos);
		return !has_network_ || usals_move_amount != 0 ;
	}

	const devdb::lnb_network_t* get_network(const lnb_t& lnb, int16_t sat_pos);
	devdb::lnb_network_t* get_network(lnb_t& lnb, int16_t sat_pos);

	std::tuple<std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>>
	select_lnb(db_txn& rtxn, const chdb::sat_t* sat, const chdb::dvbs_mux_t* proposed_mux);

	std::optional<rf_path_t> select_rf_path(const devdb::lnb_t& lnb, int16_t sat_pos = sat_pos_none);

	/*
		band = 0 or 1 for low or high (22Khz off/on)
		voltage = 0 (V,R, 13V) or 1 (H, L, 18V) or 2 (off)
		freq: frequency after LNB local oscilllator compensation
	*/
	std::tuple<int, int, int> band_voltage_freq_for_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux);
	devdb::fe_band_t band_for_freq(const devdb::lnb_t& lnb, int32_t frequency);

	inline devdb::fe_band_t band_for_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
		return band_for_freq(lnb, mux.frequency);
	}

	int voltage_for_pol(const devdb::lnb_t& lnb, const chdb::fe_polarisation_t pol);

  /*
		translate driver frequency to real frequency
		tone = 0 if off
		voltage = 1 if high
		@todo: see linuxdvb_lnb.c for more configs to support
		@todo: uniqcable
	*/
	int uncorrected_freq_for_driver_freq(const devdb::lnb_t& lnb, int frequency, bool high_band);
	int freq_for_driver_freq(const devdb::lnb_t& lnb, int frequency, bool high_band);
	int driver_freq_for_freq(const devdb::lnb_t& lnb, int frequency);

	std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, bool>
	band_frequencies(const devdb::lnb_t& lnb, devdb::fe_band_t band);

	bool add_or_edit_network(devdb::lnb_t& lnb, const devdb::usals_location_t& loc, devdb::lnb_network_t& network);
	bool add_or_edit_connection(db_txn& devdb_txn, devdb::lnb_t& lnb, devdb::lnb_connection_t& connection);

	bool update_lnb_from_positioner(db_txn& devdb_wtxn, devdb::lnb_t&  lnb, const devdb::usals_location_t& loc,
																	int16_t current_sat_pos, devdb::lnb_connection_t* curr_connection, bool save);
	bool update_lnb_from_lnblist(db_txn& devdb_wtxn, devdb::lnb_t&  lnb, bool save);
	bool update_lnb_from_db(db_txn& wtxn, devdb::lnb_t& lnb, const std::optional<devdb::usals_location_t>& loc,
													devdb::update_lnb_preserve_t::flags preserve, bool save, int16_t current_sat_pos,
													devdb::lnb_connection_t* cur_conn);
	void reset_lof_offset(db_txn& devdb_wtxn, devdb::lnb_t&  lnb);
	std::tuple<uint32_t, uint32_t> lnb_frequency_range(const devdb::lnb_t& lnb);

	bool can_pol(const devdb::lnb_t &  lnb, chdb::fe_polarisation_t pol);
	chdb::fe_polarisation_t pol_for_voltage(const devdb::lnb_t& lnb, int voltage);
	inline bool swapped_pol(const devdb::lnb_t &  lnb) {
		return lnb.pol_type == devdb::lnb_pol_type_t::VH || lnb.pol_type == devdb::lnb_pol_type_t::RL;
	}

	void update_lnb_adapter_fields(db_txn& wtxn, const devdb::fe_t& fe);
	void update_lnbs(db_txn& devdb_wtxn);

	inline bool can_move_dish(const devdb::lnb_connection_t& conn) {
		switch(conn.rotor_control) {
		case devdb::rotor_control_t::ROTOR_MASTER_MANUAL:
		case devdb::rotor_control_t::ROTOR_MASTER_USALS:
		case devdb::rotor_control_t::ROTOR_MASTER_DISEQC12:
			return true; /*this means we will send usals commands. At reservation time, positioners which
										 will really move have already been penalized, compared to positioners already on the
										 correct sat*/
			break;
		default:
			return false;
		}
	}

	inline bool on_positioner(const devdb::lnb_t& lnb)
	{
#if 0
		for(const auto& conn: lnb.connections) {
			switch(conn.rotor_control) {
			case devdb::rotor_control_t::ROTOR_MASTER_USALS:
			case devdb::rotor_control_t::ROTOR_MASTER_DISEQC12:
			case devdb::rotor_control_t::ROTOR_SLAVE:
				return true;
				break;
		default:
			break;
			}
		}
#endif
		return lnb.on_positioner;
	}
}

namespace devdb::fe {
	std::optional<devdb::fe_t>
	find_best_fe_for_lnb(db_txn& rtxn, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
											 const devdb::fe_key_t* fe_to_release,
											 bool need_blindscan, bool need_spectrum, bool need_multistream,
											 chdb::fe_polarisation_t pol, fe_band_t band,
											 int sat_pos, bool ignore_subscriptions, bool lnb_on_positioner);

	std::optional<devdb::fe_t>
	find_best_fe_for_dvtdbc(db_txn& rtxn,
													const devdb::fe_key_t* fe_to_release,
													bool need_blindscan, bool need_spectrum, bool need_multistream,
													chdb::delsys_type_t delsys_type, bool ignore_subscriptions);

	std::tuple<std::optional<devdb::fe_t>,
						 std::optional<devdb::rf_path_t>,
						 std::optional<devdb::lnb_t>,
						 resource_subscription_counts_t>
	find_fe_and_lnb_for_tuning_to_mux(db_txn& rtxn,
																		const chdb::dvbs_mux_t& mux, const devdb::rf_path_t* required_rf_path,
																		const devdb::fe_key_t* fe_key_to_release,
																		bool may_move_dish, bool use_blind_tune,
																		int dish_move_penalty, int resource_reuse_bonus, bool ignore_subscriptions);

	resource_subscription_counts_t  subscription_counts(db_txn& rtxn,
																											const devdb::rf_path_t& rf_path,
																											int rf_coupler_id,
																											const devdb::fe_key_t* fe_key_to_release);

	bool is_subscribed(const fe_t& fe);

	inline bool has_rf_in(const fe_t& fe, int rf_in) {
		for(auto& r: fe.rf_inputs) {
			if (r == rf_in)
				return true;
		}
		return false;
	}

	inline bool suports_delsys_type(const devdb::fe_t& fe, chdb::delsys_type_t delsys_type) {
		for (auto d: fe.delsys) {
			if (chdb::delsys_to_type(d) == delsys_type)
				return  true;
		}
		return false;
	}

	int unsubscribe(db_txn& wtxn, const fe_key_t& fe_key, fe_t* fe_ret=nullptr);
	int unsubscribe(db_txn& wtxn, fe_t& fe);

	int reserve_fe_lnb_for_mux(db_txn& wtxn, devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
														 const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux);


	int reserve_fe_lnb_exclusive(db_txn& wtxn, devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
															 const devdb::lnb_t& lnb);
	template<typename mux_t>
	int reserve_fe_for_mux(db_txn& wtxn, devdb::fe_t& fe, const mux_t& mux);

	template<typename mux_t>
	std::tuple<std::optional<devdb::fe_t>, int>
	subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, const devdb::fe_key_t* fe_key_to_release,
														 bool use_blind_tune);

	bool can_subscribe_lnb_band_pol_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
																			const devdb::rf_path_t* required_conn_key,
																			bool use_blind_tune, bool may_move_dish,
																			int dish_move_penalty, int resource_reuse_bonus);

	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
						 resource_subscription_counts_t, int>
	subscribe_lnb_band_pol_sat(db_txn& wtxn, const chdb::dvbs_mux_t& mux,
														 const devdb::rf_path_t* required_conn_key, const devdb::fe_key_t* fe_key_to_release,
														 bool use_blind_tune, bool may_move_dish, int dish_move_penalty, int resource_reuse_bonus);

	template<typename mux_t> bool can_subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, const mux_t& mux, bool use_blind_tune);

		std::tuple<std::optional<devdb::fe_t>, int>
		subscribe_lnb_exclusive(db_txn& wtxn,  const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
													const devdb::fe_key_t* fe_key_to_release,
													bool need_blind_tune, bool need_spectrum);
	std::tuple<devdb::fe_t, int> subscribe_fe_in_use(db_txn& wtxn, const fe_key_t& fe_key,const chdb::mux_key_t &mux_key,
																									 const devdb::fe_key_t* fe_key_to_release);
};



#pragma GCC visibility pop
