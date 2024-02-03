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

namespace devdb {

	using namespace devdb;

	namespace lnb {
		inline void make_unique_if_template(db_txn& rtxn, lnb_t& lnb ) {
			if(lnb.k.lnb_id<0)
				lnb.k.lnb_id = devdb::make_unique_id(rtxn, lnb.k);
		}
	};

	namespace scan_command {
		inline void make_unique_if_template(db_txn& wtxn, scan_command_t& scan_command) {
			if(scan_command.id<0)
				scan_command.id = devdb::make_unique_id(wtxn, (const scan_command_t&) scan_command);
		}
	};

	namespace stream {
		inline void make_unique_if_template(db_txn& wtxn, stream_t& stream) {
			if(stream.stream_id<0)
				stream.stream_id = devdb::make_unique_id(wtxn, (const stream_t&) stream);
		}
	};
};

namespace devdb::dish {
	bool dish_needs_to_be_moved(db_txn& rtxn, int dish_id, int16_t sat_pos);
};


namespace devdb {

//need for use as key in map
	inline bool operator<(const devdb::fe_key_t a , const devdb::fe_key_t b) {
		return a.adapter_mac_address == b.adapter_mac_address
			? (a.frontend_no < b.frontend_no) :
			(a.adapter_mac_address < b.adapter_mac_address);
	}

	struct resource_subscription_counts_t {
		int positioner{0};
		int lnb{0};
		int rf_coupler{0};
		int tuner{0};

		/*
			if true, then no diseqc can be used, voltage and tone cannot be changed
		*/
		bool is_shared() const {
			return
				(lnb > 0) || //we share tuner and lnb
				(tuner > 0) || //we share tuner and lnb
				(rf_coupler > 0) || //we share an rf_coupler
				(positioner > 0); //we share a positioner
				}

		bool shares_positioner() const {
			return (positioner > 0); // same frontend
		}
	};
}

namespace devdb::lnb {
	std::tuple<std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>>
	select_lnb(db_txn& rtxn, const chdb::sat_t* sat, const chdb::dvbs_mux_t* proposed_mux);

	std::optional<rf_path_t> select_rf_path(const devdb::lnb_t& lnb, int16_t sat_pos = sat_pos_none);



	bool can_pol(const devdb::lnb_t &  lnb, chdb::fe_polarisation_t pol);
	chdb::fe_polarisation_t pol_for_voltage(const devdb::lnb_t& lnb, int voltage);
	inline bool swapped_pol(const devdb::lnb_t &  lnb) {
		return lnb.pol_type == devdb::lnb_pol_type_t::VH || lnb.pol_type == devdb::lnb_pol_type_t::RL;
	}

}

namespace devdb::fe {
	std::optional<std::tuple<devdb::fe_t, resource_subscription_counts_t>>
	find_best_fe_for_lnb(db_txn& rtxn, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
											 const devdb::fe_key_t* fe_to_release,
											 bool need_blindscan, bool need_spectrum, bool need_multistream,
											 int sat_pos, chdb::fe_polarisation_t pol, chdb::sat_sub_band_t band,
											 int usals_pos, bool ignore_subscriptions);

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
																		const chdb::dvbs_mux_t& mux,
																		const subscription_options_t& tune_options,
																		const devdb::fe_key_t* fe_key_to_release,
																		bool ignore_subscriptions);

	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
						 devdb::resource_subscription_counts_t>
	find_fe_and_lnb_for_tuning_to_band(db_txn& rtxn,
																		 const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
																		 const subscription_options_t& tune_options,
																		 const devdb::fe_key_t* fe_key_to_release,
																		 bool ignore_subscriptions);

	std::optional<resource_subscription_counts_t>
	check_for_resource_conflicts(db_txn& rtxn,
															 const fe_subscription_t& s, //desired subscription_parameter
															 const devdb::fe_key_t* fe_key_to_release, bool on_positioner);

	bool is_subscribed(const fe_t& fe);

	inline bool has_rf_in(const fe_t& fe, int rf_in) {
		for(auto& r: fe.rf_inputs) {
			if (r == rf_in)
				return true;
		}
		return false;
	}

	inline bool supports_delsys_type(const devdb::fe_t& fe, chdb::delsys_type_t delsys_type) {
		for (auto d: fe.delsys) {
			if (chdb::delsys_to_type(d) == delsys_type)
				return  true;
		}
		return false;
	}

	template<typename mux_t>
	int reserve_fe_in_use(db_txn& wtxn, subscription_id_t subscription_id,
												devdb::fe_t& fe,  const mux_t& mux, const chdb::service_t* service);

	int reserve_fe_lnb_for_mux(db_txn& wtxn, subscription_id_t subscription_id,
														 devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
														 const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux,
														 const chdb::service_t* service);


	int reserve_fe_lnb_for_sat_band(db_txn& wtxn, subscription_id_t subscription_id,
																	const subscription_options_t& tune_options,
																	devdb::fe_t& fe, const devdb::rf_path_t& rf_path,
																	const devdb::lnb_t& lnb,
																	const chdb::sat_t*sat,
																	const chdb::band_scan_t* band_scan);
	template<typename mux_t>
	int reserve_fe_for_mux(db_txn& wtxn, subscription_id_t subscription_id, devdb::fe_t& fe,
												 const mux_t& mux, const chdb::service_t* service);

	template <typename mux_t>
	devdb::fe_t subscribe_fe_in_use(db_txn& wtxn, subscription_id_t subscription_id,
																	fe_t& fe, const mux_t& mux,
																	const chdb::service_t* service);


	template<typename mux_t>
	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>>
	subscribe_dvbc_or_dvbt_mux(db_txn& wtxn, subscription_id_t subscription_id, const mux_t& mux,
														 const chdb::service_t* service,
														 const std::optional<devdb::fe_t>& oldfe, const devdb::fe_key_t* fe_key_to_release,
														 const subscription_options_t& tune_options,
														 bool do_not_unsubscribe_on_failure);

	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
						 resource_subscription_counts_t, std::optional<devdb::fe_t>>
	subscribe_mux(db_txn& wtxn, subscription_id_t subscription_id, const chdb::dvbs_mux_t& mux,
														 const chdb::service_t* service,
														 const subscription_options_t& tune_options,
														 const std::optional<fe_t>& oldfe,
														 const devdb::fe_key_t* fe_key_to_release,
														 bool do_not_unsubscribe_on_failure);

	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::rf_path_t>, std::optional<devdb::lnb_t>,
						 devdb::resource_subscription_counts_t, std::optional<devdb::fe_t> >
	subscribe_sat_band(db_txn& wtxn, subscription_id_t subscription_id,
										 const chdb::sat_t& sat, const chdb::band_scan_t& band_scan,
										 const subscription_options_t& tune_options,
										 const std::optional<devdb::fe_t>& oldfe,
										 const devdb::fe_key_t* fe_key_to_release,
										 bool do_not_unsubscribe_on_failure);

	std::tuple<std::optional<devdb::fe_t>, std::optional<devdb::fe_t>>
	subscribe_lnb(db_txn& wtxn,  subscription_id_t subscription_id,
								const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
								const subscription_options_t&  tune_options,
								std::optional<devdb::fe_t>& oldfe, const devdb::fe_key_t* fe_key_to_release
		);
	devdb::fe_t subscribe_fe_in_use(db_txn& wtxn, subscription_id_t subscription_id,
																	const fe_key_t& fe_key,const chdb::mux_key_t &mux_key);


	template<typename mux_t>
	std::tuple<std::optional<devdb::fe_t>, int>
	matching_existing_subscription(db_txn& wtxn,
																 const subscription_options_t& tune_options,
																 const mux_t* mux,
																 const chdb::service_t* service,
																 bool match_mux_only);

};

#pragma GCC visibility pop
