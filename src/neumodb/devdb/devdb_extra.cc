/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
 *
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
#include "util/dtassert.h"
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/neumofrontend.h"
#include "stackstring/ssaccu.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace devdb;


template <typename cursor_t> static int16_t make_unique_id(lnb_key_t key, cursor_t& c) {
	key.lnb_id = 0;
	int gap_start = 1; // start of a potential gap of unused extra_ids
	for (const auto& lnb : c.range()) {
		if (lnb.k.lnb_id > gap_start) {
			/*we reached the first matching mux; assign next available lower  value to lnb.k.lnb_id
				this can only fail if about 65535 lnbs
				In that case the loop continues, just in case some of these  65535 muxes have been deleted in the mean time,
				which has left gaps in numbering */
			// easy case: just assign the next lower id
			return lnb.k.lnb_id - 1;
		} else {
			// check for a gap in the numbers
			gap_start = lnb.k.lnb_id + 1;
			assert(gap_start > 0);
		}
	}

	if (gap_start >= std::numeric_limits<decltype(key.lnb_id)>::max()) {
		// all ids exhausted
		// The following is very unlikely. We prefer to cause a result on a
		// single mux rather than throwing an error
		dterror("Overflow for extra_id");
		assert(0);
	}

	// we reach here if this is the very first mux with this key
	return std::numeric_limits<decltype(key.lnb_id)>::max(); // highest possible value
}

int16_t devdb::make_unique_id(db_txn& devdb_rtxn, lnb_key_t key) {
	auto c = devdb::lnb_t::find_by_key(devdb_rtxn, key.dish_id, find_geq);
	return ::make_unique_id(key, c);
}

static inline const char* lnb_type_str(const lnb_key_t& lnb_key) {
	const char* t = (lnb_key.lnb_type == lnb_type_t::C) ? "C" :
		(lnb_key.lnb_type == lnb_type_t::UNIV) ? "unv" :
		(lnb_key.lnb_type == lnb_type_t::KU) ? "Ku" :
		(lnb_key.lnb_type == lnb_type_t::KaA) ? "KaA" :
		(lnb_key.lnb_type == lnb_type_t::KaB) ? "KaB" :
		(lnb_key.lnb_type == lnb_type_t::KaC) ? "KaC" :
		(lnb_key.lnb_type == lnb_type_t::KaD) ? "KaD" : "unk";
	return t;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_key_t& lnb_key) {
	const char* t = lnb_type_str(lnb_key);
	stdex::printf(os, "D%d %s [%d]", (int)lnb_key.dish_id,
								t, (int)lnb_key.lnb_id);
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_t& lnb) {
	using namespace chdb;
	os << lnb.k;
	auto sat = sat_pos_str(lnb.cur_sat_pos == sat_pos_none ? lnb.usals_pos : lnb.cur_sat_pos); // in this case usals pos equals one of the network sat_pos
	os << " " << sat;
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_connection_t& con) {
	using namespace chdb;
	//os << lnb.k;
	stdex::printf(os, "C%d #%d ", (int)con.card_no, (int)con.rf_input);
	switch (con.rotor_control) {
	case rotor_control_t::ROTOR_MASTER_MANUAL: {
		stdex::printf(os, " man");
	} break;
	case rotor_control_t::ROTOR_MASTER_USALS:
	case rotor_control_t::ROTOR_MASTER_DISEQC12:
		stdex::printf(os, " rotor");
		break;
	case rotor_control_t::ROTOR_NONE:
		stdex::printf(os, " none");
	}
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_network_t& lnb_network) {
	auto s = chdb::sat_pos_str(lnb_network.sat_pos);
	// the int casts are needed (bug in std::printf?
	stdex::printf(os, "[%p] pos=%s enabled=%d", &lnb_network, s.c_str(), lnb_network.enabled);
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const fe_band_pol_t& band_pol) {
	// the int casts are needed (bug in std::printf?
	stdex::printf(os, "%s-%s",
								band_pol.pol == chdb::fe_polarisation_t::H	 ? "H"
								: band_pol.pol == chdb::fe_polarisation_t::V ? "V"
								: band_pol.pol == chdb::fe_polarisation_t::L ? "L"
								: "R",
								band_pol.band == fe_band_t::HIGH ? "High" : "Low");
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const fe_key_t& fe_key) {
	stdex::printf(os, "A[0x%06x]", (int)fe_key.adapter_mac_address);
	return os;
}


std::ostream& devdb::operator<<(std::ostream& os, const fe_t& fe) {
	using namespace chdb;
	stdex::printf(os, "C%dA%d F%d", fe.card_no, (int)fe.adapter_no, (int)fe.k.frontend_no);
	stdex::printf(os, " %s;%s", fe.adapter_name, fe.card_address);
	stdex::printf(os, " enabled=%s%s%s available=%d",
								fe.enable_dvbs ? "S" :"", fe.enable_dvbt ? "T": "", fe.enable_dvbc ? "C" : "", fe.can_be_used);
	return os;
}


std::ostream& devdb::operator<<(std::ostream& os, const fe_subscription_t& sub) {
	stdex::printf(os, "%d.%d%s-%d %d use_count=%d ", sub.frequency/1000, sub.frequency%1000,
								pol_str(sub.pol), sub.mux_key.stream_id, sub.mux_key.mux_id, sub.use_count);
	return os;

}



void devdb::to_str(ss::string_& ret, const fe_subscription_t& sub) {
	ret.clear();
	ret << sub;
}

void devdb::to_str(ss::string_& ret, const lnb_key_t& lnb_key) {
	ret.clear();
	ret << lnb_key;
}

void devdb::to_str(ss::string_& ret, const lnb_t& lnb) {
	ret.clear();
	ret << lnb;
}

void devdb::to_str(ss::string_& ret, const lnb_network_t& lnb_network) {
	ret.clear();
	ret << lnb_network;
}

void devdb::to_str(ss::string_& ret, const lnb_connection_t& lnb_connection) {
	ret.clear();
	ret << lnb_connection;
}

void devdb::to_str(ss::string_& ret, const fe_band_pol_t& band_pol) {
	ret.clear();
	ret << band_pol;
}

void devdb::to_str(ss::string_& ret, const fe_key_t& fe_key) {
	ret.clear();
	ret << fe_key;
}

void devdb::to_str(ss::string_& ret, const fe_t& fe) {
	ret.clear();
	ret << fe;
}
/*
	returns
	  network_exists
		priority (if network_exists else -1)
		usals_amount: how much does the dish need to be rotated for this network
*/
std::tuple<bool, int, int, int> devdb::lnb::has_network(const lnb_t& lnb, int16_t sat_pos) {
	int usals_amount{0};
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](const devdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	/*@todo allow closeby networks
	 */
	if (it != lnb.networks.end()) {
		auto usals_pos = lnb.usals_pos == sat_pos_none ? it->usals_pos :  lnb.usals_pos;
		if (devdb::lnb::on_positioner(lnb)) {
			usals_amount = std::abs(usals_pos - it->usals_pos);
		}
		return std::make_tuple(true, it->priority,  lnb.usals_pos == sat_pos_none ? sat_pos_none : usals_amount, usals_pos);
	}
	else
		return std::make_tuple(false, -1, 0, sat_pos_none);
}

/*
	returns the network if it exists
*/
const devdb::lnb_network_t* devdb::lnb::get_network(const lnb_t& lnb, int16_t sat_pos) {
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](const devdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	if (it != lnb.networks.end())
		return &*it;
	else
		return nullptr;
}

devdb::lnb_network_t* devdb::lnb::get_network(lnb_t& lnb, int16_t sat_pos) {
	auto it = std::find_if(lnb.networks.begin(), lnb.networks.end(),
												 [&sat_pos](devdb::lnb_network_t& network) { return network.sat_pos == sat_pos; });
	if (it != lnb.networks.end())
		return &*it;
	else
		return nullptr;
}


static std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, bool> lnb_band_helper(const devdb::lnb_t& lnb) {
	auto freq_low = std::numeric_limits<int32_t>::min();
	auto freq_high = std::numeric_limits<int32_t>::min();
	auto freq_mid = freq_low;
	int32_t lof_low{0};
	int32_t lof_high{0};
	using namespace devdb;
	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		freq_low = lnb.freq_low < 0 ? 3400000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 4200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low = lnb.lof_low < 0 ? 5150000 :  lnb.lof_low;
		lof_high = lnb.lof_high < 0 ? 5150000 : lnb.lof_high;
	} break;
	case lnb_type_t::WDB: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low = lnb.lof_low < 0 ? 10400000 : lnb.lof_low;
		lof_high = lnb.lof_high < 0 ? 10400000 : lnb.lof_high;
	} break;
	case lnb_type_t::WDBUK: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low = lnb.lof_low < 0 ? 10410000 : lnb.lof_low;
		lof_high = lnb.lof_high < 0 ? 1041000 : lnb.lof_high;
	} break;
	case lnb_type_t::UNIV: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = (lnb.freq_mid < 0) ? 11700000 : lnb.freq_mid;
		lof_low =  lnb.lof_low < 0 ? 9750000 : lnb.lof_low;
		lof_high = lnb.lof_high < 0 ? 10600000 : lnb.lof_high;
	} break;
	case lnb_type_t::KU: {
		freq_low = lnb.freq_low < 0 ?  11700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low = lnb.lof_low < 0 ? 9750000 : lnb.lof_low;
		lof_high = lnb.lof_high < 0 ? 9750000 : lnb.lof_high;
	} break;
	case lnb_type_t::KaA: {
		freq_low = lnb.freq_low < 0 ?   18200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 19200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low =  lnb.lof_low < 0 ? 17250000  : lnb.lof_low;
		lof_high =  lnb.lof_high < 0 ?17250000 : lnb.lof_high;
	} break;
	case lnb_type_t::KaB: {
		freq_low = lnb.freq_low < 0 ?   19200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 20200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low =   lnb.lof_low < 0 ? 18250000 : lnb.lof_low;
		lof_high =  lnb.lof_high < 0 ?18250000 : lnb.lof_high;
	} break;
	case lnb_type_t::KaC: {
		freq_low = lnb.freq_low < 0 ?   20200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 21200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low =   lnb.lof_low < 0 ? 19250000 : lnb.lof_low;
		lof_high =  lnb.lof_high < 0 ?19250000 : lnb.lof_high;
	} break;
	case lnb_type_t::KaD: {
		freq_low = lnb.freq_low < 0 ?   21200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 22200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low =   lnb.lof_low < 0 ? 20250000 : lnb.lof_low;
		lof_high =  lnb.lof_high < 0 ?20250000 : lnb.lof_high;
	} break;
	case lnb_type_t::KaE: {
		freq_low = lnb.freq_low < 0 ?   17200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 18200000 : lnb.freq_high;
		freq_mid = lnb.freq_mid < 0 ? freq_high : lnb.freq_mid;
		lof_low =   lnb.lof_low < 0 ? 16250000 : lnb.lof_low;
		lof_high =  lnb.lof_high < 0 ? 16250000 : lnb.lof_high;
	} break;
	default:
		assert(0);
	}

	bool inverted_spectrum = lof_low >= freq_low;
	return {freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum};
}


std::tuple<uint32_t, uint32_t> devdb::lnb::lnb_frequency_range(const devdb::lnb_t& lnb)
{
	auto [low, mid, high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);
	return {low, high};
}

bool devdb::lnb_can_tune_to_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, bool disregard_networks, ss::string_* error) {
	auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);
	if ((int)mux.frequency < freq_low || (int)mux.frequency >= freq_high) {
		if(error) {
		error->sprintf("Frequency %.3fMhz out for range; must be between %.3fMhz and %.3fMhz",
							 mux.frequency/(float)1000, freq_low/float(1000), freq_high/(float)1000);
		}
		return false;
	}
	if (!devdb::lnb::can_pol(lnb, mux.pol)) {
		if(error) {
			*error << "Polarisation " << mux.pol << " not supported";
		}
		return false;
	}
	if (disregard_networks)
		return true;
	for (auto& network : lnb.networks) {
		if (network.sat_pos == mux.k.sat_pos)
			return true;
	}
	if(error) {
		*error << "No network for  " << chdb::sat_pos_str(mux.k.sat_pos);
	}
	return false;
}

/*
	band = 0 or 1 for low or high (22Khz off/on)
	voltage = 0 (H,L, 13V) or 1 (V, R, 18V) or 2 (off)
	freq: frequency after LNB local oscilllator compensation

	Reasons why lnb cannot tune mux: c_band lnb cannot tune ku-band mux; lnb has wrong polarisation
*/
std::tuple<int, int, int> devdb::lnb::band_voltage_freq_for_mux(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux) {
	using namespace chdb;
	int band = -1;
	int voltage = -1;
	int frequency = -1;
	const bool disregard_networks{true};
	if (!lnb_can_tune_to_mux(lnb, mux, disregard_networks))
		return std::make_tuple(band, voltage, frequency);

	frequency = driver_freq_for_freq(lnb, mux.frequency);
	switch (lnb.k.lnb_type) {
	case lnb_type_t::C: {
		band = 0;
	} break;
	case lnb_type_t::WDB: {
		band = 0;
	} break;
	case lnb_type_t::WDBUK: {
		band = 0;
	} break;
	case lnb_type_t::UNIV:
	case lnb_type_t::KaA:
	case lnb_type_t::KaB:
	case lnb_type_t::KaC:
	case lnb_type_t::KaD:  {
		auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);
		band = ((int)mux.frequency >= freq_mid);
	} break;
	case lnb_type_t::KU: {
		band = 0;
	} break;
	default:
		assert(0);
	}
	voltage = voltage_for_pol(lnb, mux.pol);
	return std::make_tuple(band, voltage, frequency);
}

devdb::fe_band_t devdb::lnb::band_for_freq(const devdb::lnb_t& lnb, int32_t frequency) {
	using namespace chdb;

	auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);

	if (frequency < freq_low || frequency > freq_high)
		return devdb::fe_band_t::NONE;
	return (signed)(frequency >= freq_mid) ? devdb::fe_band_t::HIGH : devdb::fe_band_t::LOW;
}

int devdb::lnb::driver_freq_for_freq(const devdb::lnb_t& lnb, int frequency) {
	using namespace chdb;
	auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);
	int band = (signed)frequency >= freq_mid;
	frequency = band ? frequency - lof_high : frequency - lof_low;
	assert( (frequency <0) == inverted_spectrum);
	frequency = std::abs(frequency);
	if (band < lnb.lof_offsets.size()) {
		if (std::abs(lnb.lof_offsets[band]) < 5000)
			frequency += lnb.lof_offsets[band];
	}
	return frequency;
}

std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, bool>
devdb::lnb::band_frequencies(const devdb::lnb_t& lnb, devdb::fe_band_t band) {
	return lnb_band_helper(lnb);
}

/*
	translate driver frequency to real frequency
	voltage_high = true if high
	@todo: see linuxdvb_lnb.c for more configs to support
	@todo: uniqcable
*/
int devdb::lnb::uncorrected_freq_for_driver_freq(const devdb::lnb_t& lnb, int frequency, bool high_band) {
	using namespace chdb;

	auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);
	return (inverted_spectrum ? -frequency : frequency)+ (high_band ? lof_high: lof_low);
}

/*
	translate driver frequency to real frequency
	voltage_high = true if high
	@todo: see linuxdvb_lnb.c for more configs to support
	@todo: uniqcable
*/
int devdb::lnb::freq_for_driver_freq(const devdb::lnb_t& lnb, int frequency, bool high_band) {
	using namespace chdb;

	auto [freq_low, freq_mid, freq_high, lof_low, lof_high, inverted_spectrum] = lnb_band_helper(lnb);

	auto correct = [&lnb](int band, int frequency, bool inverted_spectrum) {
		if (band >= lnb.lof_offsets.size()) {
			//dterror("lnb_loffsets too small for lnb: " << lnb);
			return frequency;
		}
		if (std::abs(lnb.lof_offsets[band]) < 5000) {
			if(inverted_spectrum)
				frequency += lnb.lof_offsets[band];
			else
				frequency -= lnb.lof_offsets[band];
		}
		return frequency;
	};



	return correct(high_band, (inverted_spectrum ? -frequency : frequency)
								 + (high_band ? lof_high: lof_low), inverted_spectrum);
}



namespace devdb::lnb {
	static std::tuple<std::optional<rf_path_t>, std::optional<lnb_t>>
	select_lnb(db_txn& devdb_rtxn, const chdb::dvbs_mux_t& proposed_mux);
};

/*
	find the best lnb for tuning to a sat and possibly to a specific mux oin the sat sat
 */
static std::tuple<std::optional<rf_path_t>, std::optional<lnb_t>>
devdb::lnb::select_lnb(db_txn& devdb_rtxn, const chdb::dvbs_mux_t& proposed_mux) {
	using namespace chdb;


	int dish_move_penalty{0};
	int resource_reuse_bonus{0};

	//first try to find an lnb not in use, which does not require moving a dish
	{ auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
			fe::find_fe_and_lnb_for_tuning_to_mux(devdb_rtxn, proposed_mux, nullptr /*required_rf_path*/,
																						nullptr /*fe_key_to_release*/,
																						false /*may_move_dish*/, true /*use_blind_tune*/,
																						dish_move_penalty, resource_reuse_bonus, false /*ignore_subscriptions*/);

		if(best_lnb) {
			assert(best_rf_path);
			return {*best_rf_path, *best_lnb};
		}
	}

	//now try to find an lnb not in use, which does require moving a dish
	{ auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
			fe::find_fe_and_lnb_for_tuning_to_mux(devdb_rtxn, proposed_mux, nullptr /*required_rf_path*/,
																						nullptr /*fe_key_to_release*/,
																						true /*may_move_dish*/, true /*use_blind_tune*/,
																						dish_move_penalty, resource_reuse_bonus, false /*ignore_subscriptions*/);

		if(best_lnb) {
			assert(best_rf_path);
			return {*best_rf_path, *best_lnb};
		}
	}

	//now try to find an lnb which can be in use, and which can move a dish, also allowing non blindtune rf_paths
	{ auto[best_fe, best_rf_path, best_lnb, best_use_counts] =
			fe::find_fe_and_lnb_for_tuning_to_mux(devdb_rtxn, proposed_mux, nullptr /*required_rf_path*/,
																						nullptr /*fe_key_to_release*/,
																						true /*may_move_dish*/, false /*use_blind_tune*/,
																						dish_move_penalty, resource_reuse_bonus, true /*ignore_subscriptions*/);

		if(best_lnb) {
			assert(best_rf_path);
			return {*best_rf_path, *best_lnb};
		}
	}

	return {};
}

std::optional<rf_path_t> devdb::lnb::select_rf_path(const devdb::lnb_t& lnb, int16_t sat_pos) {
	if (!lnb.enabled || lnb.connections.size()==0)
		return {};
	bool lnb_is_on_rotor = devdb::lnb::on_positioner(lnb);
	int usals_move_amount{0};
	if( sat_pos != sat_pos_none)  {
		auto [has_network, network_priority, usals_move_amount_, usals_pos] = devdb::lnb::has_network(lnb, sat_pos);
		usals_move_amount = usals_move_amount_;
	}

	for(int pass=0; pass < 2; ++pass) {
		const lnb_connection_t* best_conn{nullptr};
		bool may_move_dish = pass >=1;
		for(const auto& lnb_connection:  lnb.connections) {
			if(!lnb_connection.enabled)
				continue;
			bool conn_can_control_rotor = devdb::lnb::can_move_dish(lnb_connection);
			if (lnb_is_on_rotor && (usals_move_amount > sat_pos_tolerance) &&
					(!may_move_dish || !conn_can_control_rotor)
				)
				continue; //skip because dish movement is not allowed or  not possible
			if(!best_conn || lnb_connection.priority > best_conn->priority)
				best_conn = &lnb_connection;
		}
		if(best_conn) {
			return rf_path_t{lnb.k, best_conn->card_mac_address, best_conn->rf_input};
		}
		if(usals_move_amount == 0)
			break; //pass 2 will not help to improve
	}
	return {};
}

std::tuple<std::optional<rf_path_t>, std::optional<lnb_t>>
devdb::lnb::select_lnb(db_txn& devdb_rtxn, const chdb::sat_t* sat_, const chdb::dvbs_mux_t* proposed_mux) {
	using namespace chdb;
	if(proposed_mux)
		return select_lnb(devdb_rtxn, *proposed_mux);
	if(!sat_)
		return {}; //too little info to make  choice

	chdb::sat_t sat = *sat_;
	int best_lnb_prio = std::numeric_limits<int>::min();
	std::optional<devdb::lnb_t> best_lnb;
	std::optional<rf_path_t> best_rf_path;
	/*
		Loop over all lnbs which can tune to sat
	*/

	for(int pass=0; pass <2; ++pass) {
		bool may_move_dish = pass >=1;
		auto c = find_first<devdb::lnb_t>(devdb_rtxn);
		for (auto const& lnb : c.range()) {
			auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, sat.sat_pos);
			/*priority==-1 indicates:
				for lnb_network: lnb.priority should be consulted
				for lnb: front_end.priority should be consulted
			*/
			if (!has_network || !lnb.enabled || lnb.connections.size()==0)
				continue;
			bool lnb_is_on_rotor = devdb::lnb::on_positioner(lnb);
			if (lnb_is_on_rotor && (usals_move_amount > sat_pos_tolerance) && !may_move_dish)
				continue; //skip because dish movement is not allowed or  not possible

			//		auto dish_needs_to_be_moved_ = usals_move_amount != 0;
			auto lnb_priority = network_priority >= 0 ? network_priority : lnb.priority;
			if (!has_network ||
					(lnb_priority >= 0 && lnb_priority < best_lnb_prio) //we already have a better lnb
				)
				continue;

			auto rf_path = select_rf_path(lnb, sat.sat_pos);
			if(!rf_path)
				continue;

			best_lnb_prio = lnb_priority;
			best_lnb = lnb;
			best_rf_path = rf_path;
		}
		if(best_lnb) {
			assert(best_rf_path);
			return {best_rf_path, best_lnb};
		}
	}
	return {};
}

bool devdb::lnb::add_or_edit_network(devdb::lnb_t& lnb, const devdb::usals_location_t& loc, devdb::lnb_network_t& network)
{
	using namespace devdb;
	if (network.usals_pos == sat_pos_none) {
		if (!lnb.on_positioner)
			network.usals_pos  = network.sat_pos;
		else {
			devdb::lnb::set_lnb_offset_angle(lnb, loc); //redundant, but safe
			//angle for central lnb
			auto angle = devdb::lnb::sat_pos_to_angle(network.sat_pos, loc.usals_longitude, loc.usals_latitude);
			//computed for offset lnb
			network.usals_pos = devdb::lnb::angle_to_sat_pos(angle - lnb.offset_angle, loc);
		}
	}
	if(network.sat_pos == sat_pos_none) {
		if (!lnb.on_positioner)
			network.sat_pos  = network.usals_pos;
		else {
			devdb::lnb::set_lnb_offset_angle(lnb, loc); //redundant, but safe
			//angle for central lnb
			auto angle = devdb::lnb::sat_pos_to_angle(network.usals_pos, loc.usals_longitude, loc.usals_latitude);
			//computed for offset lnb
			network.sat_pos = devdb::lnb::angle_to_sat_pos(angle + lnb.offset_angle, loc);
		}
	}

	for (auto& n : lnb.networks) {
		if (n.sat_pos == network.sat_pos) {
			bool changed = network != n;
			n = network;
			return changed;
		}
	}
	if (network.usals_pos == sat_pos_none) {
			network.usals_pos = network.sat_pos;
	}
	if (network.sat_pos == sat_pos_none)
		network.sat_pos = network.usals_pos;
	lnb.networks.push_back(network);
	if (lnb.usals_pos == sat_pos_none)
		lnb.usals_pos = (network.usals_pos == sat_pos_none) ? network.sat_pos : network.usals_pos;
	return true;
}


bool devdb::lnb::add_or_edit_connection(db_txn& devdb_txn, devdb::lnb_t& lnb,
																				devdb::lnb_connection_t& lnb_connection) {
	using namespace devdb;
	using p_t = update_lnb_preserve_t::flags;
	auto preserve = p_t::ALL;
	preserve = (p_t) ((int) preserve & (~(int)p_t::CONNECTIONS));
	//if connection already exists, update it
	bool exists{false};
	for (auto& conn : lnb.connections) {
		if (conn.card_mac_address == lnb_connection.card_mac_address &&
				conn.rf_input == lnb_connection.rf_input) {
			exists = true;
			conn = lnb_connection;
			break;
		}
	}
	//new connection; add it
	if(!exists) {
		lnb.connections.push_back(lnb_connection);
	}
#if 0
	if(on_positioner(lnb) && 	lnb_connection.rotor_control == rotor_control_t::FIXED_DISH) {
		lnb_connection.rotor_control = rotor_control_t::ROTOR_SLAVE;
		preserve = p_t(preserve & p_t::ALL & ~p_t::GENERAL);
	}
#endif
	bool save = false;
	auto changed = lnb::update_lnb_from_db(devdb_txn, lnb, {} /*loc*/, preserve, save,
																				 sat_pos_none, nullptr /*curr_conn*/);

	//update the input argument
	for (auto& conn : lnb.connections) {
		if (conn.card_mac_address == lnb_connection.card_mac_address &&
				conn.rf_input == lnb_connection.rf_input) {
			lnb_connection = conn;
			break;
		}
	}

	return changed;
}

int dish::update_usals_pos(db_txn& wtxn, devdb::lnb_t& lnb_, int usals_pos,
													 const devdb::usals_location_t& loc, int sat_pos) {
	auto c = devdb::find_first<devdb::lnb_t>(wtxn);
	int num_rotors = 0; //for sanity check

	auto angle = devdb::lnb::sat_pos_to_angle(usals_pos, loc.usals_longitude, loc.usals_latitude);

	for(auto lnb : c.range()) {
		if(lnb.k.dish_id != lnb_.k.dish_id || !devdb::lnb::on_positioner(lnb))
			continue;
		num_rotors++;
		devdb::lnb::set_lnb_offset_angle(lnb, loc); //redundant, but safe
		lnb.usals_pos = usals_pos;
		lnb.cur_sat_pos = devdb::lnb::angle_to_sat_pos(angle + lnb.offset_angle, loc);
		if(lnb.k == lnb_.k) {
			if(sat_pos != sat_pos_none)
				lnb.cur_sat_pos = sat_pos;
			lnb_ = lnb;
		}
		put_record(wtxn, lnb);
	}
	if (num_rotors == 0) {
		dterrorx("None of the LNBs for dish %d seems to be on a rotor", lnb_.k.dish_id);
		return -1;
	}
	return 0;
}



/*
	Find the current usals_pos for the desired sat_pos and compare it with the
	current usals_pos of the dish.

	As all lnbs on the same dish agree on usals_pos, we can stop when we find the first one
 */
bool devdb::dish::dish_needs_to_be_moved(db_txn& devdb_rtxn, int dish_id, int16_t sat_pos)
{
	auto c = devdb::find_first<devdb::lnb_t>(devdb_rtxn);
	int num_rotors = 0; //for sanity check

	for(auto lnb : c.range()) {
		if(lnb.k.dish_id != dish_id || !devdb::lnb::on_positioner(lnb))
			continue;
		num_rotors++;
		auto [h, priority, usals_amount, usals_pos] =  devdb::lnb::has_network(lnb, sat_pos);
		if(h) {
			return usals_amount != 0;
		}
	}
	if (num_rotors == 0) {
		dterrorx("None of the LNBs for dish %d seems to be on a rotor", dish_id);
	}
	return false;
}

int devdb::lnb::voltage_for_pol(const devdb::lnb_t& lnb, const chdb::fe_polarisation_t pol) {
	if(lnb::swapped_pol(lnb))
		return
		(pol == chdb::fe_polarisation_t::V || pol == chdb::fe_polarisation_t::R)
			? SEC_VOLTAGE_18 : SEC_VOLTAGE_13;
	else
		return
			(pol == chdb::fe_polarisation_t::V || pol == chdb::fe_polarisation_t::R)
			? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;
}

chdb::fe_polarisation_t devdb::lnb::pol_for_voltage(const devdb::lnb_t& lnb, int voltage_) {
	auto voltage = (fe_sec_voltage_t) voltage_;
	if(voltage != SEC_VOLTAGE_18 && voltage != SEC_VOLTAGE_13)
		return  chdb::fe_polarisation_t::NONE;
	bool high_voltage = (voltage == SEC_VOLTAGE_18);
	switch(lnb.pol_type) {
	case devdb::lnb_pol_type_t::HV:
		return high_voltage 	? chdb::fe_polarisation_t::H : chdb::fe_polarisation_t::V;
	case devdb::lnb_pol_type_t::VH:
		return (!high_voltage) 	? chdb::fe_polarisation_t::H : chdb::fe_polarisation_t::V;
	case devdb::lnb_pol_type_t::LR:
		return high_voltage 	? chdb::fe_polarisation_t::L : chdb::fe_polarisation_t::R;
	case devdb::lnb_pol_type_t::RL:
		return (!high_voltage) 	? chdb::fe_polarisation_t::L : chdb::fe_polarisation_t::R;
	case devdb::lnb_pol_type_t::H:
		return chdb::fe_polarisation_t::H;
	case devdb::lnb_pol_type_t::V:
		return chdb::fe_polarisation_t::V;
	case devdb::lnb_pol_type_t::L:
		return chdb::fe_polarisation_t::L;
	case devdb::lnb_pol_type_t::R:
		return chdb::fe_polarisation_t::R;
	default:
		return chdb::fe_polarisation_t::H;
	}
}

bool devdb::lnb::can_pol(const devdb::lnb_t &  lnb, chdb::fe_polarisation_t pol)
{
	switch(lnb.pol_type) {
	case devdb::lnb_pol_type_t::HV:
	case devdb::lnb_pol_type_t::VH:
		return pol == chdb::fe_polarisation_t::H || pol == chdb::fe_polarisation_t::V;
		break;
	case devdb::lnb_pol_type_t::H:
		return pol == chdb::fe_polarisation_t::H;
		break;
	case devdb::lnb_pol_type_t::V:
		return pol == chdb::fe_polarisation_t::V;
		break;
	case devdb::lnb_pol_type_t::LR:
	case devdb::lnb_pol_type_t::RL:
		return pol == chdb::fe_polarisation_t::L || pol == chdb::fe_polarisation_t::R;
		break;
	case devdb::lnb_pol_type_t::L:
		return pol == chdb::fe_polarisation_t::L;
		break;
	case devdb::lnb_pol_type_t::R:
		return pol == chdb::fe_polarisation_t::R;
		break;
	default:
		return false;
	}
}


int devdb::lnb::sat_pos_to_usals_par(int angle, int my_longitude, int my_latitude) {
	double g8, g9, g10;
	double g14, g15, g16, g17, g18, g19, g20, g21, g22, g23, g24, g25;
	double g26, g27, g28, g29, g30, g31, g32, g33, g34, g35, g36, g37;
	int posi_angle = 0;
	int tmp;
	char z_conversion[10] = {0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x0a, 0x0b, 0x0d, 0x0e};

	g8 = ((double)angle) / 10.;
	g9 = ((double)my_longitude) / 10.;
	g10 = ((double)my_latitude) / 10.;

	g14 = (fabs(g8) > 180.) ? 1000. : g8;
	g15 = (fabs(g9) > 180.) ? 1000. : g9;
	g16 = (fabs(g10) > 90.) ? 1000. : g10;

	if (g14 > 999. || g15 > 999. || g16 > 999.) {
		g17 = 0.;
		g18 = 0.;
		g19 = 0.;
	} else {
		g17 = g14;
		g18 = g15;
		g19 = g16;
	}

	g20 = fabs(g17 - g18);
	g21 = g20 > 180. ? -(360. - g20) : g20;
	if (fabs(g21) > 80.) {
		g22 = 0;
	} else
		g22 = g21;

	g23 = 6378. * sin(M_PI * g19 / 180.);
	g24 = sqrt(40678884. - g23 * g23);
	g25 = g24 * sin(M_PI * g22 / 180.);
	g26 = sqrt(g23 * g23 + g25 * g25);
	g27 = sqrt(40678884. - g26 * g26);
	g28 = 42164.2 - g27;
	g29 = sqrt(g28 * g28 + g26 * g26);
	g30 = sqrt((42164.2 - g24) * (42164.2 - g24) + g23 * g23);
	g31 = sqrt(3555639523. - 3555639523. * cos(M_PI * g22 / 180.));
	g32 = acos((g29 * g29 + g30 * g30 - g31 * g31) / (2 * g29 * g30));
	g33 = g32 * 180. / M_PI;
	if (fabs(g33) > 80.) {
		g34 = 0.;
	} else
		g34 = g33;

	g35 = ((g17 < g18 && g19 > 0.) || (g17 > g18 && g19 < 0.)) ? g34 : -g34;
	g36 = (g17 < -89.9 && g18 > 89.9) ? -g35 : g35;
	g37 = (g17 > 89.9 && g18 < -89.9) ? -g35 : g36;

	if (g37 > 0.) {
		tmp = (int)((g37 + 0.05) * 10); //+0.05 means: round up
		posi_angle |= 0xD000;
	} else {
		tmp = (int)((g37 - 0.05) * 10); //-0.05 means: round down
		posi_angle |= 0xE000;
		tmp = -tmp;
	}

	posi_angle |= (tmp / 10) << 4;
	posi_angle |= z_conversion[tmp % 10]; // computes decimal fraction of angle in 1/16 of a degree
	return posi_angle;
}

/*
	angle in units of 1/16th of a degree; sat_pos in units of 1/100th of a degree
*/
int devdb::lnb::sat_pos_to_angle(int sat_pos, int my_longitude, int my_latitude) {
	auto par = devdb::lnb::sat_pos_to_usals_par((sat_pos+5)/10, (my_longitude+5)/10, (my_latitude+5)/10);
	bool is_east = !(par &(1<<13));
	auto angle = (par & 0xfff);
	return is_east ? -10*angle: 10*angle;
}


/*compute the inverse of the above function
	crude solution; value returned is in unit of 0.01 degree but with accuracy of 0.1 degree
*/
int devdb::lnb::angle_to_sat_pos(int angle, const devdb::usals_location_t& loc) {
	int longitude = loc.usals_longitude;
	int latitude = loc.usals_latitude;
	int sat_pos_low = -7000; //in 0.01 degree unit
	int sat_pos_high = 7000; //in 0.01 degree unit
	auto angle_low = sat_pos_to_angle(sat_pos_low, longitude, latitude);
	auto angle_high =  sat_pos_to_angle(sat_pos_high, longitude, latitude);
	auto sat_pos = (sat_pos_low + sat_pos_high) / 2; // best estimate
	while (sat_pos_high - sat_pos_low > 1) {
		sat_pos = (sat_pos_low + sat_pos_high) / 2; // best estimate
		auto angle_ =  sat_pos_to_angle(sat_pos, longitude, latitude);
		if(!(angle_ >= angle_low && angle_ <= angle_high))
		  return sat_pos_none;
		if (angle_ == angle)
			break;
		if(angle > angle_) {
			angle_low = angle_;
			sat_pos_low = sat_pos;
		} else {
			angle_high = angle_;
			sat_pos_high = sat_pos;
		}
	}
	return sat_pos;
}

/*
	saves the correction to the rotor angle applied to point an offset lnb to the correct sat
	Note that usals_pos always refers to the position the center lnb on the dish is pointing to.
	The offset lnb then points to sat_pos_of(usals_value sent to motor + set_lnb_offset_pos(...))

	The result is only used to define an initial usals value for a never tuned before network.
	Afterwards the usals_pos (of the dish) can be adjusted by the user and will be remmbered.
	That value will not depend on the value computed here
 */
void devdb::lnb::set_lnb_offset_angle(devdb::lnb_t&  lnb, const devdb::usals_location_t& loc) {
	int offset_angle = 0;
	int num = 0;

	for (auto& n : lnb.networks) {
		auto angle_dish = sat_pos_to_angle(n.usals_pos, loc.usals_longitude, loc.usals_latitude);
		auto angle_sat = sat_pos_to_angle(n.sat_pos, loc.usals_longitude, loc.usals_latitude);
		offset_angle += angle_sat-angle_dish;

		num++;
	}
	lnb.offset_angle = num ? offset_angle / num : 0;
}

/*
	Bring an lnb used by the GUI uptodate with the most recent information.
	If save==true update database
	devdb_wtxn can also be a readonly transaction if db is not updated
 */
bool devdb::lnb::update_lnb_from_db(db_txn& devdb_wtxn, devdb::lnb_t&  lnb,
																		const std::optional<devdb::usals_location_t>& loc,
																		devdb::update_lnb_preserve_t::flags preserve, bool save,
																		int16_t cur_sat_pos,
																		devdb::lnb_connection_t* cur_conn)
{
	using namespace devdb;
	using p_t = update_lnb_preserve_t::flags;
	bool found=false;
	bool can_be_used{false};
	if(!lnb.on_positioner && lnb.networks.size() >0) {
		lnb.usals_pos = lnb.networks[0].usals_pos;
		lnb.cur_sat_pos = lnb.networks[0].usals_pos;
	}
	if(lnb.usals_pos == sat_pos_none && lnb.networks.size() >0 && loc) {
		lnb.usals_pos = lnb.networks[0].sat_pos;
		devdb::lnb::set_lnb_offset_angle(lnb, *loc);
	}

	if(loc) {
		if(!lnb.on_positioner)
			lnb.cur_sat_pos = lnb.usals_pos;
		else {
		devdb::lnb::set_lnb_offset_angle(lnb, *loc); //redundant, but safe
		//angle for central lnb
		auto angle = devdb::lnb::sat_pos_to_angle(lnb.usals_pos, loc->usals_longitude, loc->usals_latitude);
		//computed for offset lnb
		lnb.cur_sat_pos = devdb::lnb::angle_to_sat_pos(angle + lnb.offset_angle, *loc);
		if(lnb.cur_sat_pos == sat_pos_none)
		  lnb.cur_sat_pos = lnb.usals_pos;
		}
	}
	std::optional<lnb_t> db_lnb;
	auto c = lnb_t::find_by_key(devdb_wtxn, lnb.k, find_type_t::find_eq, devdb::lnb_t::partial_keys_t::all);
	if(c.is_valid()) {
		db_lnb = c.current();
	}

	if(db_lnb) {
		if((preserve & p_t::KEY))
			lnb.k = db_lnb->k;
		if(preserve & p_t::USALS) {
			lnb.usals_pos = db_lnb->usals_pos;
			lnb.cur_sat_pos = db_lnb->cur_sat_pos;
			lnb.on_positioner = db_lnb->on_positioner;
			lnb.offset_angle = db_lnb->offset_angle;
		}
		if(preserve & p_t::GENERAL) {
			lnb.pol_type = db_lnb->pol_type;
			lnb.enabled = db_lnb->enabled;
			lnb.priority = db_lnb->priority;
			lnb.lof_low = db_lnb->lof_low;
			lnb.lof_high = db_lnb->lof_high;
			lnb.freq_low = db_lnb->freq_low;
			lnb.freq_mid = db_lnb->freq_mid;
			lnb.freq_high = db_lnb->freq_high;
		}

		if (preserve & p_t::NETWORKS) {
			if(cur_sat_pos == sat_pos_none) {
				lnb.networks = db_lnb->networks;
			} else { //still take usals and ref mux set from positioner dialog
				for(const auto & n: lnb.networks) {
					if (n.sat_pos != cur_sat_pos)
						continue;
					for(auto & dbn: db_lnb->networks) {
						if (dbn.sat_pos != cur_sat_pos)
							continue;
						//found
						if(!(preserve & p_t::REF_MUX))
							dbn.ref_mux = n.ref_mux;
						if(!(preserve & p_t::USALS)) {
							dbn.usals_pos = n.usals_pos;
						}
						lnb.networks = db_lnb->networks;
						break;
					}
					break;
				}
			}
		}

		if (preserve & p_t::CONNECTIONS) {
   //still take usals and ref mux set from positioner dialog
			if(cur_conn)
				for(auto& db_conn: db_lnb->connections) {
					if(db_conn.card_mac_address == cur_conn->card_mac_address &&
						 db_conn.rf_input == cur_conn->rf_input) {
						db_conn.rotor_control = cur_conn->rotor_control;
						break;
					}
				}
			lnb.connections = db_lnb->connections;
			lnb.can_be_used = db_lnb->can_be_used;
		}
	}

	if (!db_lnb || !(preserve & p_t::CONNECTIONS)) {
		for(auto &conn: lnb.connections) {
			auto c = fe_t::find_by_card_mac_address(devdb_wtxn, conn.card_mac_address);
			if(c.is_valid()) {
				const auto& fe = c.current();
				conn.connection_name.clear();
				conn.card_no = fe.card_no;
				if (conn.card_no >=0)
					conn.connection_name.sprintf("C%d#%d %s", conn.card_no, conn.rf_input, fe.card_short_name.c_str());
				else
					conn.connection_name.sprintf("C??#%d %s", conn.rf_input, fe.card_short_name.c_str());
				conn.can_be_used = fe.can_be_used;
				can_be_used = true;
			} else
				conn.can_be_used = false;
			switch(conn.rotor_control) {
			case devdb::rotor_control_t::ROTOR_MASTER_USALS:
				//replace all diseqc12 commands with USALS commands
				for(auto& c: conn.tune_string) {
					if (c=='X') {
						c = 'P';
					} found = true;
				}
				if (!found)
					conn.tune_string.push_back('P');
				break;
			case devdb::rotor_control_t::ROTOR_MASTER_DISEQC12:
				//replace all usals commands with diseqc12 commands
				for(auto& c: conn.tune_string) {
					if (c=='P') {
						c = 'X';
					} found = true;
				}
				if (!found)
					conn.tune_string.push_back('X');
				break;
			default:
				break;
			}
		}
		lnb.can_be_used = can_be_used;
	}
	if(save) { /*we deliberately do not sort or remove duplicate data  when we are not really
							 saving. This is needed to provide a stable editing GUI for connections and networks
						 */
		std::sort(lnb.networks.begin(), lnb.networks.end(),
							[](const lnb_network_t& a, const lnb_network_t& b) { return a.sat_pos < b.sat_pos; });
		for(int i=1; i< lnb.networks.size(); ) {
			if(lnb.networks[i-1].sat_pos == lnb.networks[i].sat_pos)
				lnb.networks.erase(i); //remove duplicate network
			else
				++i;
		}

		std::sort(lnb.connections.begin(), lnb.connections.end(),
							[](const lnb_connection_t& a, const lnb_connection_t& b) {
								if(a.card_mac_address == b.card_mac_address)
									return a.rf_input < b.rf_input;
								else
									return a.card_mac_address < b.card_mac_address;
							}
			);
		for(int i=1; i< lnb.connections.size(); ) {
			if(lnb.connections[i-1].card_mac_address == lnb.connections[i].card_mac_address &&
				 lnb.connections[i-1].rf_input == lnb.connections[i].rf_input)
				lnb.networks.erase(i); //remove duplicate connection
			else
				++i;
		}
		if(db_lnb && db_lnb->k != lnb.k) {
			delete_record(devdb_wtxn, *db_lnb);
		}
		put_record(devdb_wtxn, lnb);
	}
	auto changed = (!db_lnb || (lnb != *db_lnb));
	return changed;
}


bool devdb::lnb::update_lnb_from_positioner(db_txn& devdb_wtxn, devdb::lnb_t&  lnb,
																						const devdb::usals_location_t& loc,
																						int16_t cur_sat_pos,
																						devdb::lnb_connection_t* curr_connection,
																						bool save) {
	using p_t = devdb::update_lnb_preserve_t::flags;
	auto preserve = p_t(p_t::ALL & ~(p_t::USALS | p_t::REF_MUX));
	return devdb::lnb::update_lnb_from_db(devdb_wtxn, lnb, loc, preserve, save,
																				cur_sat_pos, curr_connection);
}

bool devdb::lnb::update_lnb_from_lnblist(db_txn& devdb_wtxn, devdb::lnb_t&  lnb, bool save) {
	using p_t = devdb::update_lnb_preserve_t::flags;
	auto preserve = p_t(p_t::ALL & ~p_t::GENERAL);
	/*note: the following "preserve" has no effect as caller first deletes any existing record
	 */
	return devdb::lnb::update_lnb_from_db(devdb_wtxn, lnb, {} /*loc*/, preserve, save,
																				sat_pos_none, nullptr /*cur_conn*/);
}

void devdb::lnb::reset_lof_offset(db_txn& devdb_wtxn, devdb::lnb_t&  lnb)
{

	tuned_frequency_offsets_key_t k{lnb.k, {}};
	auto c = devdb::tuned_frequency_offsets_t::find_by_key(devdb_wtxn, k, find_type_t::find_geq,
																												 tuned_frequency_offsets_t::partial_keys_t::lnb_key,
																												 tuned_frequency_offsets_t::partial_keys_t::lnb_key);
	for(; c.is_valid(); c.next()) {
#ifndef NDEBUG
		auto tst = c.current();
		assert (tst.k.lnb_key == lnb.k);
#endif
		delete_record_at_cursor(c);
	}

	lnb.lof_offsets.resize(2);
	lnb.lof_offsets[0] = 0;
	lnb.lof_offsets[1] = 0;
}


static void invalidate_lnb_adapter_fields(db_txn& devdb_wtxn, devdb::lnb_t& lnb) {
	ss::string<32> name;
	name.clear();
	bool any_change{lnb.can_be_used == true};
	for (auto& conn: lnb.connections) {
		if(conn.card_no >=0) {
			name.sprintf("C%d#?? %06x", conn.card_no, conn.card_mac_address);
		} else {
			name.sprintf("C??#?? %06x", conn.card_mac_address);
		}
		auto can_be_used =  false;
		bool changed = (conn.connection_name != name) || (conn.can_be_used != can_be_used);
		any_change |= changed;
		if (!changed)
			continue;
		conn.connection_name = name;
		conn.can_be_used = can_be_used;
	}
	lnb.can_be_used = false;
	if(any_change)
		put_record(devdb_wtxn, lnb);
}

static void update_lnb_adapter_fields(db_txn& devdb_wtxn, devdb::lnb_t& lnb, const devdb::fe_t& fe) {
	ss::string<32> name;
	auto can_be_used{false};
	bool any_change{false};

	bool on_positioner = devdb::lnb::on_positioner(lnb);
	any_change |= on_positioner != lnb.on_positioner;
	lnb.on_positioner = on_positioner;

	for(auto& conn: lnb.connections) {
		if(conn.card_mac_address != fe.card_mac_address) {
			can_be_used |= conn.can_be_used;
			continue;
		}
		name.clear();
		auto valid_rf_input = fe.rf_inputs.contains(conn.rf_input);
		auto card_no = valid_rf_input ? fe.card_no : -1;
		if (card_no >=0) {
			name.sprintf("C%d#%d %s", card_no, conn.rf_input, fe.card_short_name.c_str());
		} else {
			name.sprintf("C??#%d %s", conn.rf_input, fe.card_short_name.c_str());
		}
		assert (conn.card_mac_address == fe.card_mac_address);
		bool changed = (conn.connection_name != name) ||(conn.card_no != card_no) ||
			(conn.can_be_used != fe.can_be_used);
		any_change |= changed;
		can_be_used |= fe.can_be_used;
		conn.can_be_used = fe.can_be_used;

		if(!changed)
			continue;
		conn.connection_name = name;
		conn.card_no = card_no;
	}
	any_change |= (lnb.can_be_used != can_be_used);
	lnb.can_be_used = can_be_used;
	if(!any_change)
		return;
	put_record(devdb_wtxn, lnb);
}


/*
	When an adapter changes name, update fields "name" and "adapter_no" in all related lnb's
 */
void devdb::lnb::update_lnb_adapter_fields(db_txn& devdb_wtxn, const devdb::fe_t& fe) {
	auto c = find_first<lnb_t>(devdb_wtxn);
	for(auto lnb: c.range()) {
		update_lnb_adapter_fields(devdb_wtxn, lnb, fe);
	}
}


/*
	find all lnbs for which no fe is currently available
 */
void devdb::lnb::update_lnbs(db_txn& devdb_wtxn) {

	auto find_fe = [&] (const auto& conn) ->std::optional<devdb::fe_t> {
		auto c1 = fe_t::find_by_card_mac_address(devdb_wtxn, conn.card_mac_address,
																						 find_type_t::find_geq, fe_t::partial_keys_t::card_mac_address);
		for(auto fe: c1.range()) {
			auto valid_rf_input = fe.rf_inputs.contains(conn.rf_input);
			if (valid_rf_input)
				return fe;
		}
		return {}; //no fe found with lnb's rf_input
	};

	auto c = devdb::find_first<devdb::lnb_t>(devdb_wtxn);
	for(auto lnb: c.range()) {
		if(lnb.cur_sat_pos == sat_pos_none) {
			//hack to correct older database records
			lnb.cur_sat_pos = lnb.usals_pos;
			put_record(devdb_wtxn, lnb);
		}
		for(auto conn: lnb.connections) {
			auto found = find_fe(conn);
			if(found) {
				auto &fe = *found;
				update_lnb_adapter_fields(devdb_wtxn, lnb, fe);
			} else {
				invalidate_lnb_adapter_fields(devdb_wtxn, lnb);
			}
		}
	}
}

#if 0
void devdb::lnb::find_freq_offset_for_mux(db_txn& devdb_rtxn,  const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux)
{
	auto now = system_clock_t::to_time_t(now_);
	using namespace devdb;
	auto c = tuned_frequency_offset_t::find_by_key(devdb_rtxn, lnb.k, mux.k.sat_pos, mux.frequency, find_type_t::find_eq);
	if(c.valid()) {
		return c.current.frequency_offset;
	}
	else {
		auto [band_, pol_, freq] = lnb::band_voltage_freq_for_mux(lnb, mux);
		band = band_;
		pol = dvbs_mux->pol;
		if(band < lnb.lof_offsets.size())
			return lnb.lof_offsets[band];
		else
			return 0;
	}
}
#endif
