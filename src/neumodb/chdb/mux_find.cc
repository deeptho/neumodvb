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
#include "util/template_util.h"
#include "xformat/ioformat.h"
#include <iomanip>
#include <iostream>

#include "../util/neumovariant.h"

using namespace chdb;

/*find a matching mux, based on ts_id, network_id, ignoring  extra_id
	This is called by the SDT_other parsing code at the moment NIT as not been
	received and so sat_pos is not yet known. In this case, if the database contains
	a single valid mux, it will be accepted as correct. Also, if it contains multiple
	matching muxes, but only one close to the the tuned sat_pos it will also be accepted

	tuned_sat_pos can be sat_pos_none, in which cases only a globally unique match can be returned
*/
template <typename mux_t>
static get_by_nid_tid_unique_ret_t get_by_nid_tid_sat_unique_(db_txn& txn, uint16_t network_id,
																															uint16_t ts_id, int16_t tuned_sat_pos,
																															bool check_sat_pos) {
	using namespace chdb;

	auto c = mux_t::find_by_network_id_ts_id_sat_pos(txn, network_id, ts_id, find_type_t::find_geq,
																									 mux_t::partial_keys_t::network_id_ts_id);

	if (!c.is_valid()) {
		c.close();
		return {};
	}
	get_by_nid_tid_unique_ret_t ret;
	int close_sat_count{0};
	int count{0};

	for (auto const& cmux : c.range()) {
		/*There could be multiple muxes with the same sat_pos, network_id and ts_id.
			Therefore we count them
		*/
		if(check_sat_pos && std::abs(cmux.k.sat_pos - tuned_sat_pos) <= sat_pos_tolerance ) {
			if( close_sat_count++ > 0 ) {
				ret.unique =  get_by_nid_tid_unique_ret_t::NOT_UNIQUE;
				break;
			}
			ret.mux = cmux;
			count ++;
			continue;
		} else {
			if( count++ == 0)
				ret.mux = cmux;
		}
	}

	if (count==0)
		ret.unique =  get_by_nid_tid_unique_ret_t::NOT_FOUND;
	if (count==1)
		ret.unique =  get_by_nid_tid_unique_ret_t::UNIQUE;
	else if (close_sat_count == 1)
		ret.unique =  get_by_nid_tid_unique_ret_t::UNIQUE_ON_SAT;
	else
		ret.unique =  get_by_nid_tid_unique_ret_t::NOT_UNIQUE;
	c.close();
	return ret;
}


get_by_nid_tid_unique_ret_t chdb::get_by_nid_tid_sat_unique(db_txn& txn, uint16_t network_id,
																														uint16_t ts_id, int16_t tuned_sat_pos) {
	bool check_sat_pos{false};
	switch (tuned_sat_pos) {
	case sat_pos_dvbt:
		return get_by_nid_tid_sat_unique_<dvbt_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
	case sat_pos_dvbc:
		return get_by_nid_tid_sat_unique_<dvbc_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
	case sat_pos_none: {
		auto ret = get_by_nid_tid_sat_unique_<dvbs_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
		if(ret.unique != ret.NOT_FOUND)
			return ret;
		ret = get_by_nid_tid_sat_unique_<dvbc_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
		if(ret.unique != ret.NOT_FOUND)
			return ret;
		return get_by_nid_tid_sat_unique_<dvbt_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
	}
	default:
		check_sat_pos = true;
		//fall through
	case sat_pos_dvbs:
		return get_by_nid_tid_sat_unique_<dvbs_mux_t>(txn, network_id, ts_id, check_sat_pos, tuned_sat_pos);
	}
}

/*
	find a mux which matches exactly in sat_pos, polarisation, t2mi_pid and stream_id
	approximately in frequency, disregarding extra_id. If multiple matches occur (should not happen)
	then prefer the mux with matching extra_id

	Returns a possibly invalid cursor

	Used by: update_mux, find_by_mux_physical (init_si)
*/
template <typename mux_t>
requires (is_same_type_v<mux_t, chdb::dvbs_mux_t> || is_same_type_v<mux_t, chdb::dvbc_mux_t>
					|| is_same_type_v<mux_t, chdb::dvbt_mux_t>)
db_tcursor<mux_t> chdb::find_by_mux(db_txn& txn, const mux_t& mux) {
	using namespace chdb;
	auto c = mux_t::find_by_key(txn, mux.k, find_eq, mux_t::partial_keys_t::all);
	return c;
}


/*
	Look up a mux in the database with EXACT sat_pos, approximate frequency, correct polarisation
	and (unless ignore_stream_ids == true) correct stream_id and tsmi_pid
*/
static
db_tcursor_index<chdb::dvbs_mux_t> find_by_mux_fuzzy_helper(db_txn& txn, const chdb::dvbs_mux_t& mux,
																														bool ignore_stream_id, bool ignore_t2mi_pid)
{
	using namespace chdb;

	/*look up the first record with matching sat_pos and closeby frequency
		and create a range which iterates over all with the same sat_freq_pol
		find_leq is essential to find the first frequency below the wanted one if the wanted one does not exist
	*/
	auto c = dvbs_mux_t::find_by_sat_pol_freq(txn, mux.k.sat_pos, mux.pol, mux.frequency, find_leq,
																										 dvbs_mux_t::partial_keys_t::sat_pos_pol);
	auto temp = c.clone();
	/*c points to correct frequency or the mux with next lower frequency on the sat
	 */
	while (c.is_valid()) {
		/*
			handle the case of muxes with very similar frequency but different stream_id or t2mi_pid
			The desired mux might have a slightly lower frequency than the found one
		*/
		const auto& db_mux = c.current();
		assert(db_mux.k.sat_pos == mux.k.sat_pos);
		assert(db_mux.pol == mux.pol);
		auto tolerance = (((int)std::min(mux.symbol_rate, db_mux.symbol_rate))*1.35) / 2000;
		if (mux.frequency >= db_mux.frequency + tolerance) {
			//db_mux.frequency is too low
			c.next(); //return to last value
			break;
		}
		/*note that mux.frequency >= db_mux.frequency;
			Therefore, at this point, mux and db_mux overlap, but there may be more than one overlapping mux
		 */
		c.prev();
	}
	/* if c.valid(), then c points to a mux whose frequency is below mux.frequency and which does not
		 overlap in spectrum with mux
	 */
	if (!c.is_valid() && temp.is_valid()) {
		/*restore cursor to its starting value because we have moved beyond the list for current (sat_pos,pol),
			i.e., because there is no overlapping mux in the database.
		*/
		c = std::move(temp);
		//it is possible that c is still not valid, e.g., because the db has no muxes on the sat with required pol
	}
	temp.close();
	if (!c.is_valid()) {
		// no frequencies lower than the wanted one on this sat
		c.close();
		// perhaps there are closeby higher frequencies on this sat
		c = dvbs_mux_t::find_by_sat_pol_freq(txn, mux.k.sat_pos, mux.pol, mux.frequency, find_geq,
																									dvbs_mux_t::partial_keys_t::sat_pos_pol);
		if (!c.is_valid()) {
			// no frequencies higher than the wanted one on this sat
			c.close();
			return c;
		}
	}
	/*@todo
		There could be multiple matching muxes with very similar frequencies in the database
		(although we try to prevent this)

		at this point, c points to the bottom of the range (just below) of possibly matching frequencies
	*/
	int best = std::numeric_limits<int>::max();
	auto bestc = c.clone();
	for (auto const& db_mux : c.range()) {
		assert(db_mux.k.sat_pos == mux.k.sat_pos); // see above: iterator would be invalid in this case
		assert(db_mux.pol == mux.pol);

		if (db_mux.frequency == mux.frequency && db_mux.pol == mux.pol &&
				(ignore_stream_id || ( db_mux.k.stream_id == mux.k.stream_id)) &&
				(ignore_t2mi_pid || (db_mux.k.t2mi_pid == mux.k.t2mi_pid))
			) {
			return c; //exact match
		}
		auto tolerance = (((int)std::min(db_mux.symbol_rate, mux.symbol_rate))*1.35) / 2000;
		if ((int)mux.frequency - (int)db_mux.frequency >= tolerance)
			continue;
		if ((int)db_mux.frequency - (int)mux.frequency >= tolerance)
			break; //no overlap and we have reached the top of the range with possible overlap
		if (db_mux.pol != mux.pol)
			continue;
		if (!ignore_stream_id && (db_mux.k.stream_id != mux.k.stream_id))
			continue;
		if (!ignore_t2mi_pid && (db_mux.k.t2mi_pid != mux.k.t2mi_pid))
			continue;
		// delta will drop in each iteration and will start to rise after the minimum
		auto delta = std::abs((int)db_mux.frequency - (int)mux.frequency);
		if (delta > best) {
			return bestc;
		}
		best = delta;
		bestc = c;
	}
	if (best == std::numeric_limits<int>::max()) {
		bestc.close();
		c.close();
		return c;
	}
	c.close();
	return bestc;
}



/*
	Look up a mux in the database with approximate sat_pos and  frequency,
	but correct polarisation, and (unless ignore_stream_ids == true) correct t2mi_pid and stream_id

	Limitations: in case duplicate matching muxes exist, the code will not always detect this
	and might return the wrong one. Such cases should be avoided in the first place

	used by find_fuzzy_ (update_mux), find_by_mux_physical (init_si)
*/
db_tcursor_index<chdb::dvbs_mux_t> chdb::find_by_mux_fuzzy(db_txn& txn, const chdb::dvbs_mux_t& mux,
																													 bool ignore_stream_id, bool ignore_t2mi_pid)
{
	//first try with the given sat_pos. In most cases this will give the correct result
	auto c = find_by_mux_fuzzy_helper(txn, mux, ignore_stream_id, ignore_t2mi_pid);
	if (c.is_valid())
		return c;
	int sat_tolerance = sat_pos_tolerance;
	auto cs = sat_t::find_by_key(txn, mux.k.sat_pos-sat_tolerance, find_type_t::find_geq);
	for(const auto& sat:  cs.range()) {
		if (sat.sat_pos > mux.k.sat_pos + sat_tolerance)
			break;
		if (sat.sat_pos == mux.k.sat_pos)
			continue; //already tried
		dtdebugx("found sat_pos: %d\n", sat.sat_pos);
		auto c = find_by_mux_fuzzy_helper(txn, mux, ignore_stream_id, ignore_t2mi_pid);
		if (c.is_valid())
			return c;
	}
	assert(!c.is_valid());
	return c;
}

/*
	only for dvbc and dvbt muxes
	sed by find_fuzzy_ (update_mux), find_by_mux_physical
*/
template <typename mux_t>
requires (!is_same_type_v<mux_t, chdb::dvbs_mux_t>)
db_tcursor_index<mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance) {
	using namespace chdb;

	// look up the first record with matching sat_pos and closeby frequency
	// and create a range which iterates over all with the same sat_freq_pol
	// find_leq is essential to find the first frequency below the wanted one if the wanted one does not exist
	auto c = mux_t::find_by_freq(txn, frequency, find_leq);

	if (!c.is_valid()) {
		// no frequencies lower than the wanted one
		c.close();
		// perhaps there are closeby higher frequencies
		c = mux_t::find_by_freq(txn, frequency, find_geq);
		if (!c.is_valid()) {
			// no frequencies higher than the wanted one on this sat
			c.close();
			return c;
		}
	}
	/*@todo
		There could be multiple matching muxes with very similar frequencies in the database
		(although we try to prevent this)
	*/
	int best = std::numeric_limits<int>::max();
	for (auto const& mux : c.range()) {
		//@todo  double check that searching starts at a closeby cursor position
		if (mux.frequency == frequency) {
			return c;
		}
		if ((int)frequency - (int)mux.frequency > tolerance)
			continue;
		if ((int)mux.frequency - (int)frequency > tolerance)
			break;
		// delta will drop in each iteration and will start to rise after the mininum
		auto delta = std::abs((int)mux.frequency - (int)frequency);
		if (delta > best) {
			c.prev();
			return c;
		}
		best = delta;
	}
	if (best == std::numeric_limits<int>::max())
		c.close();
	else
		c.prev(); // to handle the case where only one mux is present and within tolerance
	return c;
}

bool chdb::matches_physical_fuzzy(const dvbs_mux_t& a, const dvbs_mux_t& b, bool check_sat_pos,
																	bool ignore_t2mi_pid) {
	if (((int)a.pol& ~0x2) != ((int)b.pol & ~0x2)) //we allow switch between L/H and R/V
		return false;
	if (check_sat_pos && (std::abs(a.k.sat_pos - b.k.sat_pos) > sat_pos_tolerance))
		return false;
	if (a.k.stream_id != b.k.stream_id || (!ignore_t2mi_pid && a.k.t2mi_pid != b.k.t2mi_pid))
		return false;
	auto tolerance = (((int)std::min(a.symbol_rate, b.symbol_rate))*1.35) / 2000;

	return (std::abs((int)a.frequency - (int)b.frequency) <= tolerance);

}

bool chdb::matches_physical_fuzzy(const dvbc_mux_t& a, const dvbc_mux_t& b, bool check_sat_pos,
																	bool ignore_t2mi_pid) {
	auto tolerance = 1000;
	if (a.k.stream_id != b.k.stream_id)
		return false;
	return (std::abs((int)a.frequency - (int)b.frequency) <= tolerance);
}

bool chdb::matches_physical_fuzzy(const dvbt_mux_t& a, const dvbt_mux_t& b, bool check_sat_pos,
																	bool ignore_t2mi_pid) {
	auto tolerance = 1000;
	if (a.k.stream_id != b.k.stream_id)
		return false;
	return (std::abs((int)a.frequency - (int)b.frequency) <= tolerance);
}

bool chdb::matches_physical_fuzzy(const chdb::any_mux_t& a, const chdb::any_mux_t& b, bool check_sat_pos,
																	bool ignore_t2mi_pid) {
	using namespace chdb;
	bool ret{false};
	std::visit([&](const auto& a) {
		auto* pb = std::get_if<typename std::remove_cvref<decltype(a)>::type>(&b);
		ret = pb? chdb::matches_physical_fuzzy(a, *pb, check_sat_pos, ignore_t2mi_pid): false;}, a);
	return ret;
}

bool chdb::matches_physical(const dvbs_mux_t& a, const dvbs_mux_t& b, bool check_sat_pos,
														bool ignore_stream_id) {
	if (((int)a.pol& ~0x2) != ((int)b.pol & ~0x2)) //we allow switch between L/H and R/V
		return false;
	if (check_sat_pos && (std::abs(a.k.sat_pos - b.k.sat_pos) > sat_pos_tolerance))
		return false;
	if (!ignore_stream_id && (a.k.stream_id != b.k.stream_id))
		return false;
	auto delta = std::abs((int)a.symbol_rate - (int)b.symbol_rate);
	if (delta * 20 > (int)std::min(a.symbol_rate, b.symbol_rate))
		return false;  //symbol rates differ by more than 5%

	delta = std::abs((int)a.frequency - (int)b.frequency);
	//frequencies must differe by less than 20%
 	return (delta * 5 <= (int)std::min(a.symbol_rate, b.symbol_rate));
}

bool chdb::matches_physical(const dvbc_mux_t& a, const dvbc_mux_t& b, bool check_sat_pos,
														bool ignore_stream_id) {
	if (!ignore_stream_id && (a.k.stream_id != b.k.stream_id))
		return false;
	auto delta = std::abs((int)a.symbol_rate - (int)b.symbol_rate);
	if (delta * 20 > (int)std::min(a.symbol_rate, b.symbol_rate))
		return false;  //symbol rates differ by more than 5%

	delta = std::abs((int)a.frequency - (int)b.frequency);
	//frequencies must differe by less than 250kHz
 	return (delta <= 250);
}

bool chdb::matches_physical(const dvbt_mux_t& a, const dvbt_mux_t& b, bool check_sat_pos,
														bool ignore_stream_id) {
	if (!ignore_stream_id && (a.k.stream_id != b.k.stream_id))
		return false;

	auto delta = std::abs((int)a.frequency - (int)b.frequency);
	//frequencies must differe by less than 250kHz
 	return (delta <= 250);
}

bool chdb::matches_physical(const chdb::any_mux_t& a, const chdb::any_mux_t& b, bool check_sat_pos,
														bool ignore_stream_id) {
	using namespace chdb;
	bool ret{false};
	std::visit([&](const auto& a) {
		auto* pb = std::get_if<typename std::remove_cvref<decltype(a)>::type>(&b);
		ret=chdb::matches_physical(a, *pb, check_sat_pos, ignore_stream_id);
	}, a);
	return ret;
}

std::optional<chdb::any_mux_t> chdb::get_by_mux_physical(db_txn& txn, chdb::any_mux_t& mux,
																												 bool ignore_stream_id, /*bool ignore_key,*/
																												 bool ignore_t2mi_pid)
{
	using namespace chdb;
	switch(mux_key_ptr(mux)->sat_pos) {
	case sat_pos_none:
	case sat_pos_dvbs:
		assert(0);
		break;
	case sat_pos_dvbc: {
		auto* pmux = std::get_if<dvbc_mux_t>(&mux);
		assert(pmux);
		auto c = find_by_mux_physical(txn, *pmux, ignore_stream_id, /*ignore_key, */ ignore_t2mi_pid);
		if(c.is_valid())
			return c.current();
	}
		break;
	case sat_pos_dvbt: {
		auto* pmux = std::get_if<dvbt_mux_t>(&mux);
		assert(pmux);
		auto c = find_by_mux_physical(txn, *pmux, ignore_stream_id, /*ignore_key,*/ ignore_t2mi_pid);
		if(c.is_valid())
			return c.current();
	}
		break;
	default: {
		auto* pmux = std::get_if<dvbs_mux_t>(&mux);
		assert(pmux);
		auto c = find_by_mux_physical(txn, *pmux, ignore_stream_id, /*ignore_key,*/ ignore_t2mi_pid);
		if(c.is_valid())
			return c.current();
	}
		break;
	}
	return {};
}


/*
	find a mux which matches approximately in sat_pos, and frequency
	and exactly in polarisation,
  and exactly in key except extra_id (unless ignore_key==True)
	and exactly in t2mi_pid and stream_id (unless ignore_stream_ids)

	@todo: ignore_stream_ids not yet used by dvbc/dvbt code
*/
template <typename mux_t> db_tcursor<mux_t> chdb::find_by_mux_physical(db_txn& txn, const mux_t& mux,
																																			 bool ignore_stream_id,
																																			 bool ignore_t2mi_pid) {
	/*TODO: this will not detect almost duplicates in frequency (which should not be present anyway) or
		handle small differences in sat_pos*/
	assert(!ignore_t2mi_pid);
	// find tps with matching frequency, but probably incorrect network_id/ts_id
	if constexpr (is_same_type_v<mux_t, chdb::dvbs_mux_t>) {
		// approx. match in sat_pos, frequency, exact match in  polarisation, t2mi_pid and stream_id
		auto c = chdb::find_by_mux_fuzzy(txn, mux, ignore_stream_id, ignore_t2mi_pid);
		return std::move(c.maincursor);
	} else {
		auto c = chdb::find_by_freq_fuzzy<mux_t>(txn, mux.frequency);
		return std::move(c.maincursor);
	}
}

//template instantiations
template db_tcursor_index<chdb::dvbt_mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance);
template db_tcursor_index<chdb::dvbc_mux_t> chdb::find_by_freq_fuzzy(db_txn& txn, uint32_t frequency, int tolerance);

template db_tcursor<chdb::dvbs_mux_t> chdb::find_by_mux_physical(db_txn& txn, const chdb::dvbs_mux_t& mux,
																																 bool ignore_stream_id, /*bool ignore_key,*/
																																 bool ignore_t2mi_pid);
template db_tcursor<chdb::dvbt_mux_t> chdb::find_by_mux_physical(db_txn& txn, const chdb::dvbt_mux_t& mux,
																																 bool ignore_stream_id, /*bool ignore_key,*/
																																 bool ignore_t2mi_pid);
template db_tcursor<chdb::dvbc_mux_t> chdb::find_by_mux_physical(db_txn& txn, const chdb::dvbc_mux_t& mux,
																																 bool ignore_stream_id, /*bool ignore_key,*/
																																 bool ignore_t2mi_pid);


template db_tcursor<dvbs_mux_t> chdb::find_by_mux(db_txn& txn, const dvbs_mux_t& mux);
template db_tcursor<dvbc_mux_t> chdb::find_by_mux(db_txn& txn, const dvbc_mux_t& mux);
template db_tcursor<dvbt_mux_t> chdb::find_by_mux(db_txn& txn, const dvbt_mux_t& mux);
