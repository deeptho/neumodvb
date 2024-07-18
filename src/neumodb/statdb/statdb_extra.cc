/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
#include "neumodb/statdb/statdb_extra.h"
#include "../util/neumovariant.h"
#include "fmt/chrono.h"
#include "neumodb/chdb/chdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "receiver/devmanager.h"
#include "fmt/chrono.h"
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace fs = std::filesystem;
using namespace statdb;

fmt::format_context::iterator
fmt::formatter<signal_stat_entry_t>::format(const signal_stat_entry_t& e, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}",  "pow={:3.2f}dB snr={:3.2f}dB ber={:3.2}f",
												e.signal_strength / 1000., e.snr / 1000., (int)e.ber);
}

fmt::format_context::iterator
fmt::formatter<signal_stat_key_t>::format(const signal_stat_key_t& k, format_context& ctx) const {
	auto sat = chdb::sat_pos_str(k.sat_pos);
	return fmt::format_to(ctx.out(), "{:06x}_RF{:d} :{:5s}:{:5.3f}{:s} {:%F %T}",
												(int)k.rf_path.card_mac_address, k.rf_path.rf_input,
												sat, k.frequency / 1000.,
												to_str(k.pol), fmt::localtime(k.time));
}

fmt::format_context::iterator
fmt::formatter<signal_stat_t>::format(const signal_stat_t& stat, format_context& ctx) const {
	auto it = fmt::format_to(ctx.out(), "{}", stat.k);
	if(stat.stats.size() > 0) {
		auto &e = stat.stats[stat.stats.size()-1];
		it = fmt::format_to(ctx.out(), " pow={:3.2f}dB snr={:3.2f}dB ber={:3.2f}",
												e.signal_strength / 1000., e.snr / 1000., e.ber);
	}
	return it;
}

fmt::format_context::iterator
fmt::formatter<spectrum_key_t>::format(const spectrum_key_t& spectrum_key, format_context& ctx) const {
	auto sat = chdb::sat_pos_str(spectrum_key.sat_pos);
	return fmt::format_to(ctx.out(), "{:06x}_RF{:d} {:5s}: {:s} {:%F %H:%M:%S}" ,
												(int)spectrum_key.rf_path.card_mac_address,
												spectrum_key.rf_path.rf_input, sat, to_str(spectrum_key.pol),
												fmt::localtime(spectrum_key.start_time));
}

fmt::format_context::iterator
fmt::formatter<spectrum_t>::format(const spectrum_t& spectrum, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}", spectrum.k);
}

/*
	create a file name for a recording
*/
void statdb::make_spectrum_scan_filename(ss::string_& ret, const statdb::spectrum_t& spectrum) {
	using namespace std::chrono;
	auto sat = chdb::sat_pos_str(spectrum.k.sat_pos);
	auto* pol_ = to_str(spectrum.k.pol);
	ret.format("{}/{:%F_%H:%M:%S}_{}_dish{}_C{:06x}_A{:d}_RF{:d}", sat, fmt::localtime(spectrum.k.start_time), pol_,
						 (int)spectrum.k.rf_path.lnb.dish_id, (uint64_t)spectrum.k.rf_path.card_mac_address, spectrum.adapter_no,
						 spectrum.k.rf_path.rf_input);
}


/*
	append = True: append spectrum to already existing file
	min_freq: highest frequency present in already present file
 */
std::optional<statdb::spectrum_t> statdb::make_spectrum(const ss::string_& spectrum_path,
																														 const spectrum_scan_t& scan,
																														 bool append, int min_freq) {
	int num_freq = scan.freq.size();
	if(num_freq<=0)
		return {};
	statdb::spectrum_t spectrum{statdb::spectrum_key_t{devdb::rf_path_t{scan.rf_path},
			scan.sat.sat_pos, scan.band_pol.pol, system_clock_t::to_time_t(scan.start_time)},
		(uint32_t)scan.start_freq,
		(uint32_t)scan.end_freq,
		scan.resolution,
		scan.usals_pos,
		scan.adapter_no,
		false,/*is_complete*/
		{},
		scan.lof_offsets};
	make_spectrum_scan_filename(spectrum.filename, spectrum);

	return spectrum;
}

/*
	append = True: append spectrum to already existing file
	min_freq: highest frequency present in already present file
 */
std::optional<statdb::spectrum_t> statdb::save_spectrum_scan(const ss::string_& spectrum_path,
																														 const spectrum_scan_t& scan,
																														 bool append, int min_freq) {
	auto spectrum_ = make_spectrum(spectrum_path, scan, append, min_freq);
	if(!spectrum_)
		return spectrum_;
	auto & spectrum = *spectrum_;
	int num_freq = scan.freq.size();
	auto f = fs::path(spectrum_path.c_str()) / spectrum.filename.c_str();
	auto d = f.parent_path();
	std::error_code ec;
	create_directories(d, ec);
	if (ec) {
		dterrorf("Failed to created dir {:s}: error={:s}", d.c_str(), ec.message().c_str());
		return {};
	} else {
		auto fdata = f;
		auto fpeaks = f;
		fdata += "_spectrum.dat";
		fpeaks += "_peaks.dat";

		FILE* fpout = fopen(fdata.c_str(), append ? "a" : "w");
		if (!fpout)
			return {};

		bool inverted_spectrum = (scan.freq[0] > scan.freq[num_freq-1]);
		auto el = [inverted_spectrum, num_freq] (int i) {
			return inverted_spectrum ? ( num_freq-i-1) : i;
		};
		auto pel = [inverted_spectrum, &scan] (int i) {
			return inverted_spectrum ? ( scan.peaks.size()-i-1) : i;
		};

		int next_idx = 0;
		uint32_t candidate_freq = (next_idx < scan.peaks.size()) ? scan.peaks[pel(next_idx)].freq : -1;
		auto candidate_symbol_rate = (next_idx < scan.peaks.size()) ? scan.peaks[pel(next_idx)].symbol_rate : 0;

		if(!append)
			min_freq = 0;

		for (int i = 0; i < num_freq; ++i) {
			if((int)scan.freq[el(i)] <= min_freq)
				continue; //skip possible overlapping part between low and high band due to lnb lof offset
			auto candidate = (scan.freq[el(i)] == candidate_freq);
			if (candidate) {
				if (++next_idx >= scan.peaks.size())
					candidate_freq = -1;
				else {
					candidate_freq = scan.peaks[pel(next_idx)].freq;
					candidate_symbol_rate = scan.peaks[pel(next_idx)].symbol_rate;
				}
			}

			auto f = scan.freq[el(i)]; // in kHz
			fprintf(fpout, "%.6f %d %d\n", f * 1e-3, scan.rf_level[el(i)], candidate ? candidate_symbol_rate : 0);
		}
		if (fclose(fpout))
			return spectrum;

		fpout = fopen(fpeaks.c_str(), append ? "a" : "w");
		if (!fpout)
			return spectrum;
		for (int j=0; j < scan.peaks.size(); ++j) {
			auto& p = scan.peaks[pel(j)];
			fprintf(fpout, "%.6f %.6d\n", p.freq * 1e-3, p.symbol_rate);
		}
		if (fclose(fpout))
			return spectrum;
	}
	return spectrum;
}


/*
	update the status of all live stat_info_t records to non-live
 */
void statdb::clean_live(db_txn& wtxn) {
	bool live=true;
	auto c = signal_stat_t::find_by_key(wtxn, live, find_type_t::find_geq, signal_stat_t::partial_keys_t::live);
	for (; c.is_valid(); c.next()) {
		auto stat = c.current();
		assert(stat.k.live);
		stat.k.live = false;
		delete_record_at_cursor(c);
		put_record(wtxn, stat);
	}
}

ss::vector_<signal_stat_t> statdb::signal_stat::get_by_mux_fuzzy(
	db_txn& devdb_rtxn, int16_t sat_pos, chdb::fe_polarisation_t pol, int frequency, time_t start_time, int tolerance) {
	using namespace statdb;
	auto start_freq = frequency -tolerance;
	auto end_freq = frequency +tolerance;
	ss::vector_<signal_stat_t> ret;
	ret.reserve(16);
	// look up the first record with matching live, sat_pos, pol and closeby frequency
	// and create a range which iterates over all with the same live, sat_pos, pol combination
	for(int live=0; live < 2; ++live) {
		auto c = signal_stat_t::find_by_key(devdb_rtxn,
																				live, sat_pos, pol, start_freq,
																				find_type_t::find_geq,
																				signal_stat_t::partial_keys_t::live_sat_pos_pol);

		for (auto const& stat : c.range()) {
			if(stat.k.frequency >= end_freq)
				break;
			if(stat.k.time < start_time)
				continue;
			ret.push_back(stat);
		}
		c.close();
	}
	return ret;
}
