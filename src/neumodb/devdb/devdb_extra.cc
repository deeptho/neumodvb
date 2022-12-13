/*
 * Neumo dvb (C) 2019-2022 deeptho@gmail.com
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

#include "neumodb/chdb/chdb_extra.h"
#include "receiver/neumofrontend.h"
#include "stackstring/ssaccu.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace devdb;


template <typename cursor_t> static int16_t make_unique_id(db_txn& txn, lnb_key_t key, cursor_t& c) {
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

int16_t devdb::make_unique_id(db_txn& txn, lnb_key_t key) {
	key.lnb_id = 0;
	auto c = devdb::lnb_t::find_by_k(txn, key, find_geq);
	return ::make_unique_id(txn, key, c);
}

static inline const char* lnb_type_str(const lnb_key_t& lnb_key) {
	const char* t = (lnb_key.lnb_type == lnb_type_t::C) ? "C" :
		(lnb_key.lnb_type == lnb_type_t::UNIV) ? "unv" :
		(lnb_key.lnb_type == lnb_type_t::KU) ? "Ku" :
		(lnb_key.lnb_type == lnb_type_t::KaA) ? "KaA" :
		(lnb_key.lnb_type == lnb_type_t::KaB) ? "KaB" :
		(lnb_key.lnb_type == lnb_type_t::KaB) ? "KaC" :
		(lnb_key.lnb_type == lnb_type_t::KaB) ? "KaD" : "unk";
	return t;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_key_t& lnb_key) {
	const char* t = lnb_type_str(lnb_key);
	stdex::printf(os, "D%d%d %s[%d]", (int)lnb_key.dish_id,
								t, (int)lnb_key.lnb_id);
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_t& lnb) {
	using namespace chdb;
	os << lnb.k;
	auto sat = sat_pos_str(lnb.usals_pos); // in this case usals pos equals one of the network sat_pos
	os << " " << sat;
	return os;
}

std::ostream& devdb::operator<<(std::ostream& os, const lnb_connection_t& con) {
	using namespace chdb;
	//os << lnb.k;
	stdex::printf(os, "C%d #%d ", (int)con.card_no, (int)con.rf_input);
	switch (con.rotor_control) {
	case rotor_control_t::FIXED_DISH: {
	} break;
	case rotor_control_t::ROTOR_MASTER_USALS:
	case rotor_control_t::ROTOR_MASTER_DISEQC12:
		stdex::printf(os, " rotor");
		break;
	case rotor_control_t::ROTOR_SLAVE:
		stdex::printf(os, " slave");
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
	stdex::printf(os, "C%dA%d F%d", (int)fe.adapter_no, fe.card_no, (int)fe.k.frontend_no);
	stdex::printf(os, " %s;%s", fe.adapter_name, fe.card_address);
	stdex::printf(os, " enabled=%s%s%s available=%d",
								fe.enable_dvbs ? "S" :"", fe.enable_dvbt ? "T": "", fe.enable_dvbc ? "C" : "", fe.can_be_used);
	return os;
}


std::ostream& devdb::operator<<(std::ostream& os, const fe_subscription_t& sub) {
	stdex::printf(os, "%d.%d%c use_count=%d ", sub.frequency/1000, sub.frequency%1000,
								sub.pol == chdb::fe_polarisation_t::H ? 'H': 'V', sub.use_count);
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
	if (it != lnb.networks.end()) {
		auto usals_pos = lnb.usals_pos - lnb.k.offset_pos;
		if (devdb::lnb::on_positioner(lnb)) {
			usals_amount = std::abs(usals_pos - it->usals_pos);
		}
		return std::make_tuple(true, it->priority, usals_amount, usals_pos);
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
		freq_mid = freq_low;
		lof_low = 5150000;
		lof_high = 5150000;
	} break;
	case lnb_type_t::WDB: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = freq_high;
		lof_low = 10400000;
		lof_high = 10400000;
	} break;
	case lnb_type_t::WDBUK: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = freq_high;
		lof_low = 10410000;
		lof_high = 1041000;
	} break;
	case lnb_type_t::UNIV: {
		freq_low = lnb.freq_low < 0 ? 10700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12750000 : lnb.freq_high;
		freq_mid = (lnb.freq_mid < 0) ? 11700000 : lnb.freq_mid;
		lof_low =  9750000;
		lof_high = 10600000;
	} break;
	case lnb_type_t::KU: {
		freq_low = lnb.freq_low < 0 ?  11700000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 12200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low = 9750000;
		lof_high = 9750000;
	} break;
	case lnb_type_t::KaA: {
		freq_low = lnb.freq_low < 0 ?   18200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 19200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low =  17250000;
		lof_high =  17250000;
	} break;
	case lnb_type_t::KaB: {
		freq_low = lnb.freq_low < 0 ?   19200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 20200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low =   18250000;
		lof_high =  18250000;
	} break;
	case lnb_type_t::KaC: {
		freq_low = lnb.freq_low < 0 ?   20200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 21200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low =   19250000;
		lof_high =  19250000;
	} break;
	case lnb_type_t::KaD: {
		freq_low = lnb.freq_low < 0 ?   21200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 22200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low =   20250000;
		lof_high =  20250000;
	} break;
	case lnb_type_t::KaE: {
		freq_low = lnb.freq_low < 0 ?   17200000 : lnb.freq_low;
		freq_high = lnb.freq_high < 0 ? 18200000 : lnb.freq_high;
		freq_mid = freq_low;
		lof_low =   16250000;
		lof_high =  16250000;
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



chdb::dvbs_mux_t devdb::lnb::select_reference_mux(db_txn& rtxn, const devdb::lnb_t& lnb,
																								 const chdb::dvbs_mux_t* proposed_mux) {
	auto return_mux = [&rtxn, &lnb](const devdb::lnb_network_t& network) {
		auto c = chdb::dvbs_mux_t::find_by_key(rtxn, network.ref_mux);
		if (c.is_valid()) {
			auto mux = c.current();
			if (devdb::lnb::can_pol(lnb, mux.pol))
				return mux;
		}
			c = chdb::dvbs_mux_t::find_by_key(rtxn, network.sat_pos, 0, 0, 0, find_type_t::find_geq,
																	chdb::dvbs_mux_t::partial_keys_t::sat_pos);
		if (c.is_valid()) {
			auto mux = c.current();
			if (devdb::lnb::can_pol(lnb, mux.pol))
				return mux;
		}
		auto mux = chdb::dvbs_mux_t();
		mux.k.sat_pos = network.sat_pos; //handle case where reference mux is absent
		mux.pol = devdb::lnb::pol_for_voltage(lnb, 0); //select default
		return mux;
	};

	using namespace chdb;
	const bool disregard_networks{false};
	if (proposed_mux && lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
		return *proposed_mux;

	if (!devdb::lnb::on_positioner(lnb)) {
		for (auto& network : lnb.networks) {
			if (usals_is_close(lnb.usals_pos, network.usals_pos)) { // dish is tuned to the right sat
				return return_mux(network);
			}
		}
		if (lnb.networks.size() > 0)
			return return_mux(lnb.networks[0]);

	} else {
		auto best = std::numeric_limits<int>::max();
		const devdb::lnb_network_t* bestp{nullptr};
		for (auto& network : lnb.networks) {
			auto delta = std::abs(network.usals_pos - lnb.usals_pos);
			if (delta < best) {
				best = delta;
				bestp = &network;
			}
		}
		if (bestp && usals_is_close(bestp->usals_pos, lnb.usals_pos)) {
			return return_mux(*bestp);
		}
		return chdb::dvbs_mux_t(); //  has sat_pos == sat_pos_none; no network present
	}
	return chdb::dvbs_mux_t(); // has sat_pos == sat_pos_none;
}

lnb_t devdb::lnb::select_lnb(db_txn& rtxn, const chdb::sat_t* sat_, const chdb::dvbs_mux_t* proposed_mux) {
	using namespace chdb;
	if (!sat_ && !proposed_mux)
		return lnb_t();
	chdb::sat_t sat;
	if (sat_) {
		sat = *sat_;
	} else {
		auto c = sat_t::find_by_key(rtxn, proposed_mux->k.sat_pos);
		if (!c.is_valid())
			return lnb_t();
		sat = c.current();
	}
	/*
		Loop over all lnbs to find a suitable one.
		First give preference to rotor
	*/
	auto c = find_first<devdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, sat.sat_pos);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if (!has_network || !lnb.enabled)
			continue;
		if (devdb::lnb::on_positioner(lnb)) {
			const bool disregard_networks{false};
			if (!proposed_mux || lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
				// we prefer a rotor, which is most useful for user
				return lnb;
		}
	}

	/*
		Try without rotor
	*/
	c = find_first<devdb::lnb_t>(rtxn);
	for (auto const& lnb : c.range()) {
		auto [has_network, network_priority, usals_move_amount, usals_pos] = devdb::lnb::has_network(lnb, sat.sat_pos);
		assert (usals_move_amount == 0);
		/*priority==-1 indicates:
			for lnb_network: lnb.priority should be consulted
			for lnb: front_end.priority should be consulted
		*/
		if (!has_network || !lnb.enabled)
			continue;
		const bool disregard_networks{false};
		if (!proposed_mux || lnb_can_tune_to_mux(lnb, *proposed_mux, disregard_networks))
			return lnb;
	}
	// give up
	return lnb_t();
}

bool devdb::lnb::add_network(devdb::lnb_t& lnb, devdb::lnb_network_t& network) {
	using namespace chdb;
	for (auto& n : lnb.networks) {
		if (n.sat_pos == network.sat_pos)
			return false; // cannot add duplicate network
	}
	if (network.usals_pos == sat_pos_none)
		network.usals_pos = network.sat_pos;
	lnb.networks.push_back(network);
	if (lnb.usals_pos == sat_pos_none)
		lnb.usals_pos = (network.usals_pos == sat_pos_none) ? network.sat_pos : network.usals_pos;
	std::sort(lnb.networks.begin(), lnb.networks.end(),
						[](const lnb_network_t& a, const lnb_network_t& b) { return a.sat_pos < b.sat_pos; });
	return true;
}


int dish::update_usals_pos(db_txn& wtxn, int dish_id, int usals_pos)
{
	auto c = devdb::find_first<devdb::lnb_t>(wtxn);
	int num_rotors = 0; //for sanity check
	for(auto lnb : c.range()) {
		if(lnb.k.dish_id != dish_id || !devdb::lnb::on_positioner(lnb))
			continue;
		num_rotors++;
		lnb.usals_pos = usals_pos;
		put_record(wtxn, lnb);
	}
	if (num_rotors == 0) {
		dterrorx("None of the LNBs for dish %d seems to be on a rotor", dish_id);
		return -1 ;
	}
	return 0;
}



/*
	Find the current usals_posi for the desired sat_pos and compare it with the
	current usals_pos of the dish.

	As all lnbs on the same dish agree on usals_pos, we can stop when we find the first one
 */
bool devdb::dish::dish_needs_to_be_moved(db_txn& rtxn, int dish_id, int16_t sat_pos)
{
	auto c = devdb::find_first<devdb::lnb_t>(rtxn);
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

void devdb::lnb::update_lnb(db_txn& wtxn, devdb::lnb_t&  lnb, bool save)
{
	bool found=false;
	for(auto &conn: lnb.connections) {
		auto c = fe_t::find_by_card_mac_address(wtxn, conn.card_mac_address);
		if(c.is_valid()) {
			const auto& fe = c.current();
		conn.connection_name.clear();
		conn.card_no = fe.card_no;
		if (conn.card_no >=0)
			conn.connection_name.sprintf("%s C%d#%d", fe.card_short_name.c_str(), conn.card_no, conn.rf_input);
		else
			conn.connection_name.sprintf("%s C??#%d", fe.card_short_name.c_str(), conn.rf_input);
		lnb.can_be_used = fe.can_be_used;
		}
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
	if(save)
		put_record(wtxn, lnb);
}


void devdb::lnb::reset_lof_offset(devdb::lnb_t&  lnb)
{
	lnb.lof_offsets.resize(2);
	lnb.lof_offsets[0] = 0;
	lnb.lof_offsets[1] = 0;
}


static void invalidate_lnb_adapter_fields(db_txn& devdb_wtxn, devdb::lnb_t& lnb) {
	ss::string<32> name;
	name.clear();
	for (auto& conn: lnb.connections) {
		if(conn.card_no >=0) {
			name.sprintf("C%d#?? %06x", conn.card_no, conn.card_mac_address);
		} else {
		name.sprintf("C??#?? %06x", conn.card_mac_address);
		}
		auto can_be_used =  false;
		bool changed = (conn.connection_name != name) || (lnb.can_be_used != can_be_used);
		if (!changed)
		return;
		conn.connection_name = name;
		lnb.can_be_used = can_be_used;
	}
	put_record(devdb_wtxn, lnb);
}

static void update_lnb_adapter_fields(db_txn& wtxn, devdb::lnb_t& lnb, const devdb::fe_t& fe) {
	ss::string<32> name;
	auto can_be_used =  fe.can_be_used;
	bool any_change{lnb.can_be_used != can_be_used};

	for(auto& conn: lnb.connections) {
		if(conn.card_mac_address != fe.card_mac_address)
			continue;
		name.clear();
		auto valid_rf_input = fe.rf_inputs.contains(conn.rf_input);
		auto card_no = valid_rf_input ? fe.card_no : -1;
		if (card_no >=0) {
			name.sprintf("C%d#%d %s", card_no, conn.rf_input, fe.card_short_name.c_str());
		} else {
			name.sprintf("C??#%d %s", conn.rf_input, fe.card_short_name.c_str());
		}
		assert (conn.card_mac_address == fe.card_mac_address);
		bool changed = (conn.connection_name != name) ||(conn.card_no != card_no);
		any_change |= changed;
		if(!changed)
			continue;
		conn.connection_name = name;
		conn.card_no = card_no;
	}
	lnb.can_be_used = can_be_used;
	if(!any_change)
		return;
	put_record(wtxn, lnb);
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
			//return fe.present && fe.can_be_used;
		}
		return {}; //no fe found with lnb's rf_input
	};


	auto c = devdb::find_first<devdb::lnb_t>(devdb_wtxn);
	for(auto lnb: c.range()) {
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

void devdb::lnb::on_mux_key_change(db_txn& wtxn, const chdb::mux_key_t& old_mux_key, chdb::dvbs_mux_t& new_mux,
																	 system_time_t now_) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace chdb;
	auto& new_mux_key = *mux_key_ptr(new_mux);
	{
		auto c = find_first<lnb_t>(wtxn);
		for(auto lnb: c.range()) {
			auto* n = lnb::get_network(lnb, old_mux_key.sat_pos);
			if(n) {
				lnb.mtime = now;
				if (n->ref_mux == old_mux_key) {
					n->ref_mux = new_mux_key;
					put_record(wtxn, lnb);
				}
			}
			break;
		}
	}
}
