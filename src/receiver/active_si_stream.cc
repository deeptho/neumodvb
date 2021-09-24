/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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

#include "active_si_stream.h"
#include "active_adapter.h"
#include "scan.h"
#include "util/logger.h"

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

/*
	pat has been the same for stable_pat_timeout seconds

	If no pat is present, this is also considered a stable situation
	Note that "tuner not locked" will be handled higher up
*/
bool pat_data_t::stable_pat() {
	if (stable_pat_)
		return stable_pat_;
	return stable_pat_ && ((steady_clock_t::now() - last_pat_change_time) >= pat_change_timeout);
}

bool pat_data_t::stable_pat(uint16_t ts_id) {
	if (stable_pat_)
		return stable_pat_;
	assert(ts_id != 65535);
	auto& e = by_ts_id[ts_id];
	bool changed = (e.entries != e.last_entries);
	if (changed) {
		last_pat_change_time = steady_clock_t::now();
		e.last_entries = e.entries;
	}
	stable_pat_ = !changed && ((steady_clock_t::now() - last_pat_change_time) >= pat_change_timeout);
	return stable_pat_;
}

active_si_stream_t::active_si_stream_t
(	receiver_t& receiver,
	const std::shared_ptr<stream_reader_t>& reader,  bool is_embedded_si, ssize_t dmx_buffer_size_)
	: active_stream_t(receiver, reader)
	, active_si_data_t(is_embedded_si)
	, chdb(receiver.chdb)
	, epgdb(receiver.epgdb)
{
}

void active_si_stream_t::reset() { ::active_si_data_t::reset(); }

bool active_si_stream_t::abort_on_wrong_sat() const {
	return !is_embedded_si && wrong_sat_detected() && reader->tune_options().retune_mode == retune_mode_t::AUTO;
}

/*
	Add mux information received from the stream into maps facilitating lookup by
	network_id/ts_id and maintaina  map of sat positions referenced in the stream
*/
std::tuple<mux_data_t*, bool> nit_data_t::add_mux_from_si(const chdb::any_mux_t& mux, bool from_sdt) {
	bool sat_pos_changed{false};
	auto* key = chdb::mux_key_ptr(mux);
	ss::string<32> mux_desc;
	chdb::to_str(mux_desc, mux);
	auto [it, inserted] =
		by_network_id_ts_id.try_emplace(std::make_pair(key->network_id, key->ts_id), mux_data_t{*key, mux_desc});
	bool sat_pos_known = false;
	for (auto& p : nit_actual_sat_positions) {
		if (p == key->sat_pos) {
			sat_pos_known = true;
			break;
		}
	}
	if (!sat_pos_known)
		nit_actual_sat_positions.push_back(key->sat_pos);

	auto& m = it->second;
	auto& n = get_original_network(key->network_id);
	n.add_mux(m.mux_key.ts_id, from_sdt);
	if (!inserted) {
		if (m.mux_key.sat_pos != key->sat_pos) {
			dterror("Sat_pos for " << mux_desc << " changes from " << m.mux_key.sat_pos << "to sat_pos=" << key->sat_pos
							<< " earlier result was set by " << (m.from_si ? "SI" : "database"));
			m.mux_key.sat_pos = key->sat_pos;
			chdb::to_str(m.mux_desc, mux);
			sat_pos_changed = true;
		}
	}
	m.from_si = true;
	return {&m, sat_pos_changed};
}

/*
	create a new mux in the database  based on information NOT seen in the input stream.
	This is called when all hope is lost to find the corresponding mux in the NIT stream data,
	but we need it because it is referenced in the SDT, or because no data at all was received
	on the current transponder and we wish to record this "si-less" nature in the database.

	This function is called by
	-scan_report: after a timeout, confirming no SI data in the stream (scan_report) or
	confirming no NIT data in the stream (scan_report)
	-when sdt_section_cb has determined that nit_actual has been received, but and SDT record
	is not referenced in nit_actual, or nit_actual is known to be not present. This can occur
	both for the currenltly tuned mux or another mux

*/
mux_data_t* active_si_stream_t::add_fake_nit(db_txn& txn, uint16_t network_id, uint16_t ts_id,
																						 int16_t expected_sat_pos) {
	using namespace chdb;
	bool no_data = (network_id == 0 && ts_id == 0);
	dtdebugx("There is no nit_actual on this tp - faking one with sat_pos=%d network_id=%d, ts_id=%d", expected_sat_pos,
					 network_id, ts_id);
	auto mux = reader->tuned_mux();
	auto* mux_key = mux_key_ptr(mux);
	auto* mux_common = mux_common_ptr(mux);
	if (mux_common->is_template) {
		mux_common->is_template = false;
	}

	assert(expected_sat_pos == mux_key->sat_pos);
	mux_key->network_id = network_id;
	mux_key->ts_id = ts_id;
	mux_key->t2mi_pid = reader->embedded_stream_pid();

	// update the database; this may fail if tuners is not locked
	update_template_mux_parameters_from_frontend(txn, mux);
	if (no_data) {
		auto* mux_common = chdb::mux_common_ptr(mux);
		mux_common->scan_result = chdb::scan_result_t::NODATA;
		mux_common->scan_duration = scan_state.scan_duration();
		// assert(scan_state.scan_duration()>=0);
		mux_common->num_services = 0;
		mux_common->scan_time = system_clock_t::to_time_t(now);
	}
	namespace m = chdb::update_mux_preserve_t;
	// MUX_KEY should only be updated for the tuned mux
	auto preserve = no_data ? m::flags{m::MUX_KEY} : m::flags{m::MUX_COMMON};
	chdb::update_mux(txn, mux, now, preserve);
	assert(mux_key->ts_id == ts_id);
	if (!is_embedded_si)
		reader->set_current_tp(mux);
	ss::string<32> mux_desc;
	if (expected_sat_pos != sat_pos_none) {
		chdb::to_str(mux_desc, mux);
		// we overwrite any existing mux - if we are called, this means any existing mux must be wrong
		auto [it, inserted] = nit_data.by_network_id_ts_id.insert_or_assign(std::make_pair(network_id, ts_id),
																																				mux_data_t{*mux_key, mux_desc});
		it->second.is_tuned_mux = true;
		it->second.mux_key = *mux_key;
		return &it->second;
	}
	return nullptr;
}

/*
	if tuned_sat_pos is specified, only database records matching tuned_sat_pos will be considered
*/
mux_data_t* active_si_stream_t::lookup_nit(db_txn& txn, uint16_t network_id, uint16_t ts_id, int16_t expected_sat_pos) {
	/*
		Possible difficult cases include:
		-mux has moved to  somewhere else on the same sat=> no problem;
		-or moved to a different sat (difficult to detect/handle; handling this must be done after a timeout,
		but this is still tricky as the mux may be moved to another network. So deleting
		it and its services is danegrous)
		-a new mux has appeard; when lookup_nit was called no info was found on it. Either we ignore this error
		(a future tuning to the tuned mux will fix it), or we force a rescan of sdt_actual/other tables.

		At this point we can also detect muxes which are in the database but not longer in the NIT.
		We could then delete all correspodndign services (dangerous!)

	*/
	auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
	if (found)
		return &it->second; // already known
	auto ret = (expected_sat_pos != sat_pos_none)
		? chdb::find_by_mux_key_fuzzy(
			txn, chdb::mux_key_t{expected_sat_pos, reader->embedded_pid, network_id, ts_id, {}})
		: chdb::find_by_network_id_ts_id(txn, network_id, ts_id); // first time sdt or eit needs this mux
	if (ret.num_matches == 1) {
		auto* key = mux_key_ptr(ret.mux);
		if (key->sat_pos == expected_sat_pos || expected_sat_pos == sat_pos_none) {
			ss::string<32> mux_desc;
			if (expected_sat_pos != sat_pos_none) {
				chdb::to_str(mux_desc, ret.mux);
				auto& tuned_mux = reader->tuned_mux();
				auto is_tuned_mux = tuning_parameters_match(ret.mux, tuned_mux);
				auto [it, inserted] =
					nit_data.by_network_id_ts_id.try_emplace(std::make_pair(network_id, ts_id), mux_data_t{*key, mux_desc});
				it->second.is_tuned_mux = is_tuned_mux;
				return &it->second;
			} else {
				auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
				if (found)
					return &it->second;
				return nullptr;
			}
		}
	} else if (ret.num_matches > 1) {
		dtdebugx("Multiple muxes (%d) exist for network_id=%d ts_id=%d", ret.num_matches, network_id, ts_id);
		return nullptr;
	} else if (ret.num_matches == 0) {
		dtdebugx("No mux in db for network_id=%d ts_id=%d", network_id, ts_id);
		return nullptr;
	}
	return nullptr;
}

/*
	we always process a multiple of 188 bytes (ts_packet_t::size)
	read_pointer; index into buffer: at start of call points to zero or perhaps to a value <188
	in case we read an incomplete packet
*/
void active_si_stream_t::process_si_data() {
	now = system_clock_t::now();

	dttime_init();
	auto start = steady_clock_t::now();
	bool must_abort = false;
	for (int i = 0; i < 5; ++i) {
		if (steady_clock_t::now() - start > 500ms) {
			dtdebug("SKIPPING EARLY\n");
			break;
		}

		auto [buffer, ret] = reader->read();

		if (ret < 0) {
			if (errno == EINTR) {
				// dtdebug("Interrupt received (ignoring)");
				continue;
			}
			if (errno == EOVERFLOW) {
				dtdebug_nice("OVERFLOW");
				continue;
			}
			if (errno == EAGAIN) {
				break; // no more data
			} else {
				dterror("error while reading: " << strerror(errno));
				break;
			}
		}
		auto num_bytes_to_process = ret;

		// save read_pointer for next time, in case a partial packet has been read at the end
		auto delta = num_bytes_to_process % dtdemux::ts_packet_t::size;
		stream_parser.set_buffer(buffer, num_bytes_to_process - delta);
		dttime(-1);
		stream_parser.parse();
		if (abort_on_wrong_sat() || unstable_sat_detected()) {
			on_wrong_sat();
			must_abort = true;
			reader->discard(num_bytes_to_process); // skip all remaining data
			stream_parser.clear_data();
		}
		dttime(500);
		reader->discard(num_bytes_to_process - delta);
		break;
	}
	if (epgdb_txn_) {
		if (must_abort)
			epgdb_txn_->abort();
		else
			epgdb_txn_->commit();
		epgdb_txn_.reset();
	}
	if (chdb_txn_) {
		if (must_abort)
			chdb_txn_->abort();
		else
			chdb_txn_->commit();
		chdb_txn_.reset();
	}
	scan_report();
	dttime(200);
}

/*
	returns false if fd is not tha active_stream's fd
*/
bool active_si_stream_t::read_and_process_data_for_fd(int fd) {
	if (!reader->on_epoll_event(fd))
		return false;
	auto old = tune_confirmation;
	process_si_data();
	reader->data_tick();
	if (old != tune_confirmation) {
		reader->update_tuned_mux_tune_confirmation(tune_confirmation);
	}
	check_timeouts();
	return true;
}

/*
	check for lack of progress in case data is coming in
*/
void active_si_stream_t::check_timeouts() {
	log4cxx::NDC(name());
	thread_local ss::string<256> last;
	thread_local steady_time_t last_time;
	thread_local bool delayed_print{false};
	auto now = steady_clock_t::now();
	bool timedout = (now - last_time) >= 2000ms;

	ss::string<256> out;

	if (!scan_state.completed(scan_state_t::SDT_NETWORK)) {
		auto done = (nit_actual_done() && tune_confirmation.network_id_by != confirmed_by_t::NONE &&
								 tune_confirmation.ts_id_by != confirmed_by_t::NONE &&
								 (network_done(sdt_data.network_id) || (sdt_actual_done() && sdt_other_done())));
		if (done)
			scan_state.set_completed(scan_state_t::SDT_NETWORK);
	}

	ss::string<64> embedded;
	if (is_embedded_si)
		embedded.sprintf("embedded[%d] ", reader->embedded_pid);
	out.sprintf("%sscan=%s pat=%s nit_actual=%s nit_other=%s sdt_actual=%s sdt_other=%s bat=%s", embedded.c_str(),
							scan_state.scan_done() ? "DONE" : "not done", scan_state.str(scan_state_t::PAT),
							scan_state.str(scan_state_t::NIT_ACTUAL), scan_state.str(scan_state_t::NIT_OTHER),
							scan_state.str(scan_state_t::SDT_ACTUAL), scan_state.str(scan_state_t::SDT_OTHER),
							scan_state.str(scan_state_t::BAT));

	for (auto& [network_id, n] : nit_data.by_onid) {
		out.sprintf(" network[%d]=%d(sdt)/%d(nit)%s", network_id, n.sdt_num_muxes_present, n.nit_num_muxes_present,
								network_done(network_id) ? " (done)" : "");
	}
	for (auto& [pid, c] : eit_data.subtable_counts) {
		out.sprintf(" epg[0x%x]=%d/%d", pid, c.num_completed, c.num_known);
	}
	out.sprintf(" eit=%d/%d", eit_data.eit_other_existing_records, eit_data.eit_other_updated_records);

	if (delayed_print || last != out) {
		if (timedout) {
			dtdebug(out);
			delayed_print = false;
			last_time = now;
		} else
			delayed_print = true;
	} else
		delayed_print = false;
	last = out;
}

void active_si_stream_t::init(scan_target_t scan_target_) {
	log4cxx::NDC(name());
	if (is_open())
		deactivate();
	init_scanning(scan_target_);
	active_stream_t::open(dtdemux::ts_stream_t::PAT_PID, &active_adapter().tuner_thread.epx,
												EPOLLIN | EPOLLERR | EPOLLHUP);
	parsers.reserve(32);
	auto& tuned_mux = reader->tuned_mux();

	bool is_freesat_main = chdb::has_epg_type(tuned_mux, chdb::epg_type_t::FSTHOME);
	bool is_skyuk = chdb::has_epg_type(tuned_mux, chdb::epg_type_t::SKYUK);
	bool has_movistar = chdb::has_epg_type(tuned_mux, chdb::epg_type_t::MOVISTAR);
	bool has_viasat_baltic = chdb::has_epg_type(tuned_mux, chdb::epg_type_t::VIASAT);

	bool has_freesat = chdb::has_epg_type(tuned_mux, chdb::epg_type_t::FREESAT);

	bool do_standard = true;
	bool do_timeout = false;
	bool do_bat = false;
	bool do_epg = false;
	bool need_other = true;

	scan_state.reset();

	if (scan_target == scan_target_t::SCAN_FULL) {
		do_timeout = true;
		do_bat = true;
		need_other = true;
	}
	if (scan_target == scan_target_t::SCAN_MINIMAL) {
		do_timeout = true;
		need_other = false;
	}
	if (scan_target == scan_target_t::SCAN_MUX) {
	}
	if (scan_target == scan_target_t::DEFAULT) {
		do_epg = true;
		do_bat = true;
		need_other = true;
	}

	if (do_epg && is_skyuk) {
		do_bat = true;
		load_skyuk_bouquet();
	}

	if (do_standard) {
		add_parser<dtdemux::pat_parser_t>(dtdemux::ts_stream_t::PAT_PID)->section_cb =
			[this](const pat_services_t& pat_services, const subtable_info_t& i) {
				return this->pat_section_cb(pat_services, i);
			};
		scan_state.start(scan_state_t::completion_index_t::PAT, true);
		// note: PAT_PID already added
		add_parser<dtdemux::nit_parser_t>(dtdemux::ts_stream_t::NIT_PID)->section_cb =
			[this](nit_network_t& network, const subtable_info_t& i) { return this->nit_section_cb(network, i); };

		scan_state.start(scan_state_t::completion_index_t::NIT_ACTUAL, true);
		if (need_other)
			scan_state.start(scan_state_t::completion_index_t::NIT_OTHER, false);

		auto sdt_bat = add_parser<dtdemux::sdt_bat_parser_t>(dtdemux::ts_stream_t::SDT_PID);
		sdt_bat->bat_section_cb = [this](const bouquet_t& ret, const subtable_info_t& i) {
			return this->bat_section_cb(ret, i);
		};

		sdt_bat->sdt_section_cb = [this](const sdt_services_t& ret, const subtable_info_t& i) {
			return this->sdt_section_cb(ret, i);
		};

		scan_state.start(scan_state_t::completion_index_t::SDT_ACTUAL, true);
		scan_state.start(scan_state_t::completion_index_t::SDT_NETWORK, true);
		if (need_other) {
			scan_state.start(scan_state_t::completion_index_t::SDT_OTHER, false);
			scan_state.start(scan_state_t::completion_index_t::BAT, false);
		}
	}

	if (do_bat && is_freesat_main) {
		// note that regular bat table is always added (same pid as SDT)
		auto sdt_bat = add_parser<dtdemux::sdt_bat_parser_t>(3002);
		sdt_bat->fst_preferred_region_id = 1; // London @todo get this from options

		sdt_bat->bat_section_cb = [this](const bouquet_t& ret, const subtable_info_t& i) {
			return this->bat_section_cb(ret, i);
		};
		scan_state.start(scan_state_t::completion_index_t::FST_BAT, false);
	}

	if (do_epg) {
		auto eit_section_cb = [this](epg_t& epg, const subtable_info_t& i) { return this->eit_section_cb(epg, i); };

		if (is_skyuk) {
			for (auto pid = dtdemux::ts_stream_t::PID_SKY_TITLE_LOW; pid <= dtdemux::ts_stream_t::PID_SKY_TITLE_HIGH; ++pid) {
				add_parser<dtdemux::eit_parser_t>(pid, chdb::epg_type_t::SKYUK)->section_cb = eit_section_cb;
			}

			for (auto pid = dtdemux::ts_stream_t::PID_SKY_SUMMARY_LOW; pid <= dtdemux::ts_stream_t::PID_SKY_SUMMARY_HIGH;
					 ++pid) {
				add_parser<dtdemux::eit_parser_t>(pid, chdb::epg_type_t::SKYUK)->section_cb = eit_section_cb;
			}

			scan_state.start(scan_state_t::completion_index_t::SKYUK_EPG, false);

		} else if (is_freesat_main) {
			add_parser<dtdemux::eit_parser_t>(dtdemux::ts_stream_t::FREESAT_INFO_EIT_PF_PID, chdb::epg_type_t::FSTHOME)
				->section_cb = eit_section_cb;
			add_parser<dtdemux::eit_parser_t>(dtdemux::ts_stream_t::FREESAT_INFO_EIT_PID, chdb::epg_type_t::FSTHOME)
				->section_cb = eit_section_cb;
			scan_state.start(scan_state_t::scan_state_t::completion_index_t::FST_EPG, false);
		} else {
			add_parser<dtdemux::eit_parser_t>(dtdemux::ts_stream_t::EIT_PID, chdb::epg_type_t::DVB)->section_cb =
				eit_section_cb;

			scan_state.start(scan_state_t::scan_state_t::completion_index_t::EIT_ACTUAL_EPG, false);
			if (need_other)
				scan_state.start(scan_state_t::scan_state_t::completion_index_t::EIT_OTHER_EPG, false);

			if (has_freesat) {
				add_parser<dtdemux::eit_parser_t>(dtdemux::ts_stream_t::FREESAT_EIT_PID, chdb::epg_type_t::FREESAT)
					->section_cb = eit_section_cb;

				add_parser<dtdemux::eit_parser_t>(dtdemux::ts_stream_t::FREESAT_EIT_PF_PID, chdb::epg_type_t::FREESAT)
					->section_cb = eit_section_cb;

				scan_state.start(scan_state_t::scan_state_t::completion_index_t::FST_EPG, false);
			}

			if (has_movistar) {
				load_movistar_bouquet();
				/*
					MediaHighway 2 in use on movistar.
					Tune to  Bajo Demanda 19.2E 10847V.

					The epg exists in multiple versions (4 day vs 7 day, different types of summaries)
					on multiple pids. The  basic approach seems to be
					title sections contain channel number, start time duration and title. The channel number
					can be converted using a bouquet to service information.

					Each title has a unique (?) title_id (not used in the code) and a summary_id. The latter
					identifies the summary section in which the full epg story can be found

					Summaries contain the summary_id (in two versions where one overrides the other) and
					also at offsets 3, 10, 22, 29 after the story  4byte fields which are title_ids. Most
					likely they are for re-runs. However, only 60% of the summaries point to valid title sections
					if we use the title_id record at offset 10 (suggested by a ticket on tvheadend website) whereas
					100% of summary_ids in title records point to valid summaries in the stream.

					The general approach of the code is
					1. download the bouquet from the stream (or from the database)
					2. start downloading titles. For each received title, store a record mapping
					the summary_id to data needed to save a later received summary: the key of the service, the start_time
					and the end_time (the latter is needed because save_epg_record_if_better_update_input uses
					it to match the records. It could be avoided by giving end_time==0 a special meaning)
					3. Start downloading summaries in parallel. If the title for this summary has not yet been
					received, ignore the summary and request that the section in question is marked as not yet received
					(so it will be reprocessed later). This approach leads to longer processing, or missing summaries,
					but if epg is canned regularly (e.g., each day) the summary will be added the next day. This is
					probably fine, because new title records are probably for the last day.

					Processing title_ids is probably not needed

					table 200: channel names
					table 210
					table 225
					table 240

					auto mhw2_chl = add_parser<dtdemux::mhw2_parser_t>(561);

					pid 644: 7-day epg
					table 150: short summaries
					table 200: multiple bouquets?  channel list, names of channels,
					table 210: not interesting
					table 220: program titles
					table 221: INFORMACI.N NO DISPONIBLE
					table 230: program titles
					table 240: program titles
					table 242: program titles
					table 243: program titles
					table 250: not interesting?

				*/
				// auto mhw2_chl = add_parser<dtdemux::mhw2_parser_t>(564); //faster titles?
				auto mhw2_chl1 = add_parser<dtdemux::mhw2_parser_t>(644); // short summaries, titles, channels

				auto mhw2_chl2 = add_parser<dtdemux::mhw2_parser_t>(642); // long summaries
				mhw2_chl1->bat_section_cb = [this](const bouquet_t& ret, const subtable_info_t& i) {
					return this->bat_section_cb(ret, i);
				};
				mhw2_chl1->eit_section_cb = [this](epg_t& ret, const subtable_info_t& i) {
					return this->eit_section_cb(ret, i);
				};
				mhw2_chl2->bat_section_cb = [this](const bouquet_t& ret, const subtable_info_t& i) {
					return this->bat_section_cb(ret, i);
				};
				mhw2_chl2->eit_section_cb = [this](epg_t& ret, const subtable_info_t& i) {
					return this->eit_section_cb(ret, i);
				};
				scan_state.start(scan_state_t::completion_index_t::MHW2_EPG, false);
			}

			if (has_viasat_baltic) {
				/*
					viasat baltic 11823V pid 57 (0x39) part of program 958 (0x3be), pmt_pid=958
					* PMT, TID 2 (0x02), PID 958 (0x03BE)
					Version: 0, sections: 1, total size: 31 bytes
					- Section 0:
					Program: 958 (0x03BE), PCR PID: none
					Elementary stream: type 0x00 (unknown), PID: 33 (0x0021)
					Elementary stream: type 0x00 (unknown), PID: 57 (0x0039) -> epg
					Elementary stream: type 0x00 (unknown), PID: 58 (0x003A) -> bat/sdt
				*/
				add_parser<dtdemux::eit_parser_t>(57, chdb::epg_type_t::VIASAT)->section_cb = eit_section_cb;
				scan_state.start(scan_state_t::scan_state_t::completion_index_t::VIASAT_EPG, false);
			}
		}
	}
	auto k = chdb::mux_key_ptr(reader->tuned_mux());
	if (k->sat_pos == sat_pos_dvbc || k->sat_pos == sat_pos_dvbt) {
		tune_confirmation.sat_by = confirmed_by_t::AUTO;
	}
}

void active_si_stream_t::scan_report() {
	if (!is_open())
		return;
	auto now_ = steady_clock_t::now();
	if (now_ - scan_state.last_update_time <= 2s)
		return;
	scan_state.last_update_time = now_;

	bool no_data = reader->no_data();

	// first handle the case of no data
	if (no_data && !tune_confirmation.si_done) {
		dtdebug("No data on this mux; setting si_done = true");

		auto txn = chdb.wtxn();
		auto* tuned_key = mux_key_ptr(reader->tuned_mux());

		/*
			If a mux exists with a similar enough frequency, its tuning parameters will be updated.
			If no such mux exists one will be created with network_id=ts_id=0
		*/
		add_fake_nit(txn, 0, 0, tuned_key->sat_pos);
		assert(tune_confirmation.sat_by == confirmed_by_t::NONE);
		tune_confirmation.sat_by = confirmed_by_t::FAKE;
		txn.commit();
		tune_confirmation.si_done = true;
		reader->update_tuned_mux_tune_confirmation(tune_confirmation);
		// scan_target = scan_target_t::DONE;
		scan_done = true;
		return;
	}

	if (scan_done)
		return;

	auto done = scan_state.scan_done();

	if (!done && !scan_state.aborted)
		return;

	if (done) {
		dtdebug("SCAN DONE");
		scan_done = true;
		tune_confirmation.si_done = true;
		reader->update_tuned_mux_tune_confirmation(tune_confirmation);
	}

	// now update the mux's scan state
	ss::string<32> s;
	auto mux = reader->tuned_mux();

	to_str(s, mux);
	auto* mux_common = chdb::mux_common_ptr(mux);

	if (scan_state.aborted)
		mux_common->scan_result = chdb::scan_result_t::ABORTED;
	else if (scan_state.locked) {
		mux_common->scan_result = scan_state.scan_completed() ? chdb::scan_result_t::OK : chdb::scan_result_t::PARTIAL;
	} else {
		mux_common->scan_result = chdb::scan_result_t::FAILED;
	}
	mux_common->scan_status = chdb::scan_status_t::IDLE;
	mux_common->scan_duration = scan_state.scan_duration();
	mux_common->scan_time = system_clock_t::to_time_t(now);
	reader->set_current_tp(mux);
	if (nit_actual_done() || nit_actual_notpresent() || done) {
		auto txn = chdb.wtxn();
		if (mux_common->is_template) {
			dterror("mux is still a template mux (removing template status):" << mux);
			auto* key = mux_key_ptr(mux);
			add_fake_nit(txn, 0, 0, key->sat_pos);
			assert(tune_confirmation.sat_by == confirmed_by_t::NONE);
			tune_confirmation.sat_by = confirmed_by_t::FAKE;
		} else {
			namespace m = chdb::update_mux_preserve_t;
			chdb::update_mux(txn, mux, now, m::flags{m::ALL & ~m::SCAN_DATA});
		}
		txn.commit();
	}

	auto& receiver_thread = receiver.receiver_thread;
	receiver_thread.push_task([this, &receiver_thread, mux = std::move(mux)]() {
		cb(receiver_thread).on_scan_mux_end(&active_adapter(), mux);
		return 0;
	});
}

int active_si_stream_t::deactivate() {
	if (!is_open())
		return 0;
	log4cxx::NDC(name());
	int ret = 0;
	dtdebugx("deactivate si stream");
	// active_stream_t::close();
	stream_parser.exit(); // remove all fibers
	parsers.clear();			// remove all parser data (parsers will be reregistered)
	close();
	reset();
	return ret;
}

void active_si_stream_t::on_wrong_sat() {
	auto saved = tune_confirmation;
	bool preserve_wrong_sat = true;
	saved.clear(preserve_wrong_sat);
	pat_data.reset();
	nit_data.reset();
	sdt_data.reset();
	bat_data.reset();
	eit_data.reset();
	init(scan_target); // locked=true in case dish is still moving
	tune_confirmation = saved;
}

void active_si_stream_t::on_tuned_mux_key_change(db_txn& wtxn, const chdb::mux_key_t& si_mux_key, bool update_db,
																								 bool update_sat_pos) {
	reader->on_tuned_mux_key_change(wtxn, si_mux_key, update_db, update_sat_pos);
}

/*
	returns 1 if network  name matches database, 0 if no record was present and -1 if no match
*/
int active_si_stream_t::save_network(db_txn& txn, const nit_network_t& network, int sat_pos) {

	int ret = 0;
	chdb::chg_t group;
	group.k.group_type = chdb::group_type_t::NETWORK;
	group.k.bouquet_id = network.network_id;
	group.k.sat_pos = sat_pos;
	group.mtime = system_clock_t::to_time_t(now);
	auto c = chdb::chg_t::find_by_key(txn, group.k, find_eq);

	if (c.is_valid()) {
		group = c.current();
		ret = (group.name == network.network_name) ? 1 : -1;
	}
	if (ret != 1)
		group.name = network.network_name;
	put_record(txn, group);
	return ret;
}

bool active_si_stream_t::check_tuned_mux_key(db_txn& txn, const chdb::mux_key_t& si_key) {
	auto& tuned_key = *chdb::mux_key_ptr(reader->tuned_mux());
	assert(tuned_key.sat_pos == si_key.sat_pos);
	if (tuned_key.ts_id != si_key.ts_id || tuned_key.network_id != si_key.network_id) {
		dtdebugx("Transponder changed ids: nid/ts_id=%d,%d => %d,%d", tuned_key.network_id, tuned_key.ts_id,
						 si_key.network_id, si_key.ts_id);
		// update the network/ts_id for the currently tuned mux
		on_tuned_mux_key_change(txn, si_key, true, false); // this will only be done once
		return true;
	}
	return false;
}

void active_si_stream_t::add_sat(db_txn& txn, uint16_t sat_pos) {
	auto c = chdb::sat_t::find_by_key(txn, sat_pos);
	if (!c.is_valid()) {
		chdb::sat_t sat;
		sat.sat_pos = sat_pos;
		sat.name = chdb::sat_pos_str(sat_pos);
		put_record(txn, sat);
	}
}

dtdemux::reset_type_t active_si_stream_t::pat_section_cb(const pat_services_t& pat_services,
																												 const subtable_info_t& info) {
	auto cidx = scan_state_t::PAT;
	tune_confirmation.unstable_sat = false;
	if (info.timedout /*&& pat_data.stable_pat()*/) {
		scan_state.set_timedout(cidx);
		return dtdemux::reset_type_t::NO_RESET;
	} else
		scan_state.set_active(cidx);
	auto& pat_table = pat_data.by_ts_id[pat_services.ts_id];

	if (pat_table.subtable_info.version_number != info.version_number) {
		pat_table.entries.clear();
		// pat_data.last_pat_entries.clear();
		pat_table.subtable_info = info;
	}

	bool this_table_done = (++pat_table.num_sections_processed == pat_table.subtable_info.num_sections_present);
	tune_confirmation.pat_ok = true;
	if (this_table_done) {
		pat_table.entries = pat_services.entries;
		if (pat_table.last_entries.size() != 0 && pat_table.last_entries != pat_table.entries) {
			dtdebugx("PAT is unstable; force retune");
			tune_confirmation.unstable_sat = true;
			return dtdemux::reset_type_t::ABORT; // unstable PAT; must retune
		}
		if (!pat_data.stable_pat(pat_services.ts_id)) {
			dtdebug("PAT not stable yet");
			pat_table.num_sections_processed = 0;
			return dtdemux::reset_type_t::RESET; // need to check again for stability
		}
		dtdebugx("PAT is stable");

		if (scan_target == scan_target_t::SCAN_FULL)
			for (auto& s : pat_table.entries) {
				if (!is_embedded_si)
					add_pmt(s.service_id, s.pmt_pid);
			}

		bool all_done = true;
		for (auto& [ts_id, table] : pat_data.by_ts_id) {
			if (table.num_sections_processed != table.subtable_info.num_sections_present) {
				all_done = false;
				break;
			}
		}
		if (all_done) {
			dtdebugx("PAT fully done");
			active_adapter().on_stable_pat();
			scan_state.set_completed(cidx); // signal that pat is present
		} else {
			dtdebugx("PAT - table done, but some remaining");
		}
	}
	return dtdemux::reset_type_t::NO_RESET;
}

/*
	returns 0, sat_pos_dvbt or sat_pos_dvbc
*/
static inline int dvb_type(uint16_t sat_pos) {
	if (sat_pos == sat_pos_dvbc || sat_pos == sat_pos_dvbt)
		return sat_pos;
	else
		return 0;
}

/*
	nit_actual processing serves multiple purposes
	-detecting if we are on the currect sat (dish may be detuned, or diseqc swicth malfunctions)
	-detecting if we are tuned to the right mux (lnb-22kHz wrong, or polarisation wrong)
	-updateing the extact frequency and other tuning parameters

	The following cases must be considered
	1. tuner does not lock. In this case si processing si not started and spectrum/blind scan continues
	2. there is no nit data on this mux
	-action: after a timeout, assume that tune data from tuner is correct. Also assume sat_pos is correct.
	This is handled in scan_report (all other cases are handled in the nit_acrual_section_cb routines
	or in check_timeouts)
	3. there is data in nit_actual, but none of it agrees with the tuned frequency and/or the tuned sat
	-possible reasons: empty nit_actual,  nit_actual is for dvb-t (e.g., 5.0W French streams),
	nit_actaul contains the mux but frequency is wrong (4.0W 12353 reports 14.5 GHz)
	-action: after a timeout, assume that data from tuner is correct. Also assume sat_pos is correct
	4. there is data in nit_actual, it agrees with the tuned frequency, but the tuned sat_pos is incorrect
	-reasons: dish is still moving and we picked up a transient signal, reception from  a nearby sat,
	failed diseqc swicth has brught us to the same sat
	-action: restart nit_actual_processing and see if the result is stable; if it is not stable, then the dish is
	probablly moving and the problem will disappear. If the result is stable, then distinghuish betweem
	4.a. the difference in sat_pos is small (less than a degree).
	-action: assume that the data in nit_actual is correct, update tuned_mux and similar data structures in tuner and
	fe_monitor threads
	4.b. the difference in sat_pos is large: retune
	-action: assume that the data in nit_actual is correct, update tuned_mux and similar data structures in tuner and
	fe_monitor threads

	Notes:
	-never trust nit_actual data which does not agree (up to small differences in sat_pos and frequency)
	with the tuning parameters. Also, trust small differences only after proof that the result is stable

	-the "satellite stability check" will be skipped if sdt_actual confirms the mux, which it des by comparing
	services (speculatively assuming that tuning data is correct, to select a suitable mux_key to look up
	the services) to earlier found services in the database.


*/

dtdemux::reset_type_t active_si_stream_t::nit_section_cb_(nit_network_t& network, const subtable_info_t& info) {
	namespace m = chdb::update_mux_preserve_t;
	auto cidx = network.is_actual ? scan_state_t::NIT_ACTUAL : scan_state_t::NIT_OTHER;
	if (info.timedout) {
		scan_state.set_timedout(cidx);
		return dtdemux::reset_type_t::NO_RESET;
	} else {
		bool was_active = scan_state.set_active(cidx);
		if (!was_active && network.is_actual) {
			dtdebugx("First NIT_ACTUAL data");
		}
	}
	auto* p_network_data = &nit_data.get_network(network.network_id);
	if (p_network_data->subtable_info.version_number != -1 &&
			p_network_data->subtable_info.version_number != info.version_number) {
		nit_data.reset(); // forget everything...what else can we do?
		p_network_data = &nit_data.get_network(network.network_id);
	}
	auto& network_data = *p_network_data;
	if (network_data.subtable_info.version_number < 0) { // not yet initialised
		network_data.subtable_info = info;
		network_data.is_actual = network.is_actual;
	}
	auto tuned_mux = reader->tuned_mux();
	auto tuned_mux_key = mux_key_ptr(tuned_mux);

	auto txn = chdb_txn();
	int network_name_matches = 0; // matches data in db? 1=yes, -1=0, 0=not in db or could not be looked up
	if (network.network_name.size())
		network_name_matches = save_network(
			txn, network, tuned_mux_key->sat_pos); // TODO: sat_pos is meaningless; network can be on multiple sats

	using namespace chdb;
	dtdemux::reset_type_t ret = dtdemux::reset_type_t::NO_RESET;

	for (auto& mux : network.muxes) {
		auto* mux_key = mux_key_ptr(mux);
		if (dvb_type(mux_key->sat_pos) != dvb_type(tuned_mux_key->sat_pos)) {
			bool is_tuned_mux = false;
			nit_data.add_mux_from_nit(mux, is_tuned_mux); // save for later looking up sat_id and for counting services.
			continue;																			// skipping dvbt networks in dvbs mux
		}
		// update_mux_ret_t r;
		bool is_tuned_mux{false};
		if (network.is_actual) {
			p_network_data->num_muxes++;
			auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
			const bool disregard_networks{true};
			if (dvbs_mux && !chdb::lnb_can_tune_to_mux(active_adapter().current_lnb(), *dvbs_mux, disregard_networks)) {
				dtdebug("Refusing to insert a mux which cannot be tuned: " << mux);
			} else {
				// update_mux_ret_t r;
				std::tie(ret, is_tuned_mux) = nit_actual_save_and_check_confirmation(txn, mux);
				if (ret != dtdemux::reset_type_t::NO_RESET) {
					txn.abort();
					network_data.reset();
					return ret; // definitely on wrong sat
				}
			}
		} else {
			// MUX_KEY should only be updated for the tuned mux
			// r=
			chdb::update_mux(txn, mux, now, m::flags{m::MUX_COMMON | m::MUX_KEY});
		}
		add_sat(txn, mux_key->sat_pos);

		auto sat_pos_changed =
			nit_data.add_mux_from_nit(mux, is_tuned_mux); // save for later looking up sat_id and for counting services.
		if (sat_pos_changed) {
			chdb::update_mux_sat_pos(txn, mux);
			chdb::update_mux(txn, mux, now, m::flags{m::MUX_COMMON | m::MUX_KEY});
		}
	}
	bool done = nit_data.update_nit_completion(scan_state, info, network_data);
	if (done) {
		if (tune_confirmation.sat_by == confirmed_by_t::NONE && !pat_data.stable_pat()) {
			/*It is too soon to decide we are on the right/wrong sat;
				force nit_actual rescanning
			*/
			txn.abort();
			network_data.reset();
			return dtdemux::reset_type_t::RESET;
		}

		if (p_network_data->num_muxes == 0) {
			if (sdt_actual_done() && tune_confirmation.ts_id_by != confirmed_by_t::NONE) {
				// we cannot check the sat_pos, so we assume it is ok.
				tune_confirmation.sat_by = confirmed_by_t::NIT;
				if (network.is_actual) {
					tune_confirmation.nit_actual_ok = true;
					dtdebugx("Setting nit_actual_ok = true");
				}
			}
		} else if (tune_confirmation.sat_by == confirmed_by_t::NONE && network.is_actual) {
			dterror("ni_actual does not contain current mux");
			/*NOTE: pat is stable at this point
			 */
			auto sat_count = nit_data.nit_actual_sat_positions.size();
			dtdebugx("NIT_ACTUAL does not contain currently tuned mux; nit_actual contains %d sat_positions", sat_count);
			if (sat_count == 1) {
				/*
					all nit_actual muxes are for the same sat, and we assume it is correct
				*/
				auto sat_pos = nit_data.nit_actual_sat_positions[0];
				tune_confirmation.on_wrong_sat = std::abs(sat_pos - tuned_mux_key->sat_pos) > 100;
				if (abort_on_wrong_sat()) {
					/*
						This is a regular tune (not a band scan), so we must trigger a retune.
						@todo: 10930V 39.0E actually contains 30E. So we must devise an override in the database.
						otherwie we will never be able to tune this mux
					*/
					dtdebugx("NIT_ACTUAL only contains sat_pos=%d, which is very unexpected. Asking retune", sat_pos);
					return dtdemux::reset_type_t::ABORT;
				}
				dtdebugx("NIT_ACTUAL only contains sat_pos=%d (no retune allowed). confirming sat_pos", sat_pos);
				if (tuned_mux_key->sat_pos != sat_pos) {
					dtdebugx("Updating active_tp sat_pos from=%d to=%d", tuned_mux_key->sat_pos, sat_pos);
					const bool update_db = true;
					const bool update_sat_pos = true;
					on_tuned_mux_key_change(txn, *tuned_mux_key, update_db, update_sat_pos);
					tuned_mux_key->sat_pos = sat_pos;
					reader->set_current_tp(tuned_mux);
				}
				tune_confirmation.sat_by = confirmed_by_t::NIT;
				tune_confirmation.nit_actual_ok = true;
				dtdebugx("Setting nit_actual_ok = true");
			}
		}

		// do anything needed after a network has been fully loaded
		if (network.is_actual) {
			tune_confirmation.nit_actual_ok = true;
			dtdebugx("Setting nit_actual_ok = true");
		}
	} else { //! done: more nit_actual or nit_other data is coming
		if (network.is_actual)
			tune_confirmation.nit_actual_ok = true;
	}
	txn.commit();
	if (done && network.is_actual) { // for nit other, there may be multiple entries
		dtdebugx("NIT_ACTUAL completed");
		scan_state.set_completed(cidx);
		tune_confirmation.nit_actual_ok = true;
	}
	return ret;
}

dtdemux::reset_type_t active_si_stream_t::nit_section_cb(nit_network_t& network, const subtable_info_t& info) {
	if (network.is_actual)
		return nit_section_cb_(network, info);
	if (tune_confirmation.sat_by == confirmed_by_t::NONE)
		return dtdemux::reset_type_t::RESET;
	return nit_section_cb_(network, info);
}

bool active_si_stream_t::sdt_actual_check_confirmation(bool mux_key_changed, int db_correct, mux_data_t* p_mux_data) {
	if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
		// figure out if we can confirm sat
		if (!mux_key_changed && db_correct > 2) {
			dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: found=%d existing services",
							 p_mux_data->mux_key.sat_pos, p_mux_data->mux_key.network_id, p_mux_data->mux_key.ts_id, db_correct);
			tune_confirmation.sat_by = confirmed_by_t::SDT;
			tune_confirmation.on_wrong_sat = false;

			if (tune_confirmation.ts_id_by == confirmed_by_t::NONE)
				tune_confirmation.ts_id_by = confirmed_by_t::SDT;
			if (tune_confirmation.network_id_by == confirmed_by_t::NONE)
				tune_confirmation.network_id_by = confirmed_by_t::SDT;
		} else if (!mux_key_changed && nit_actual_done()) {
			dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: NIT_ACTUAL is done and will not confirm",
							 p_mux_data->mux_key.sat_pos, p_mux_data->mux_key.network_id, p_mux_data->mux_key.ts_id);
			return false;
		} else {
			tune_confirmation.sat_by = confirmed_by_t::NONE;
			tune_confirmation.ts_id_by = confirmed_by_t::NONE;
		}
	} else if (tune_confirmation.network_id_by == confirmed_by_t::NONE ||
						 tune_confirmation.ts_id_by == confirmed_by_t::NONE) {
		// this happens when nit_actual has seen muxes, but not the tuned mux yet
		if (tune_confirmation.ts_id_by == confirmed_by_t::NONE)
			tune_confirmation.ts_id_by = confirmed_by_t::SDT;
		tune_confirmation.on_wrong_sat = false;
		if (tune_confirmation.network_id_by == confirmed_by_t::NONE)
			tune_confirmation.network_id_by = confirmed_by_t::SDT;
	}

	if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
		dtdebug("Satellite cannot be confirmed; abort saving SDT services");
		return true;
	}

	return false;
}

/*
	returns update_mux_ret, is_tuned_tp
*/
std::tuple<dtdemux::reset_type_t, bool>
active_si_stream_t::nit_actual_save_and_check_confirmation(db_txn& txn, chdb::any_mux_t& mux) {
	auto ret = dtdemux::reset_type_t::NO_RESET;
	using namespace chdb;
	auto tuned_mux = reader->tuned_mux();
	auto tuned_mux_key = mux_key_ptr(tuned_mux);
	auto* mux_key = mux_key_ptr(mux);
	bool is_tuned_tp =
		chdb::matches_physical_fuzzy(mux, tuned_mux); // frequency, polarisation and sat match, perhaps not ts/network id

	tune_confirmation.on_wrong_sat = false;
#ifdef TODO
	bool is_wrong_dvb_type = (dvb_type(mux_key->sat_pos) != dvb_type(tuned_mux_key->sat_pos));
	if (is_wrong_dvb_type) {
		dtdebugx("Thus mux is for a different dvb type: %d %d", dvb_type(mux_key->sat_pos),
						 dvb_type(tuned_mux_key->sat_pos));
	} else
#endif
		if (tuned_mux_key->sat_pos != mux_key->sat_pos) {
			if (std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= 100) {
				// assert(!is_tuned_tp);
				dterror("close sat in NIT actual: tuned=" << *tuned_mux_key << " NIT contains: " << *mux_key);
			} else {
				dterror("wrong sat in NIT actual: tuned=" << *tuned_mux_key << " NIT contains: " << *mux_key);
			}
			chdb::update_mux_sat_pos(txn, mux);
		}
#ifdef TODO
	if (is_wrong_dvb_type) {

	} else
#endif
		if (!is_tuned_tp /*&& std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= 100*/) {
			bool is_tuned_freq = chdb::matches_physical_fuzzy(
				mux, tuned_mux, false); // frequency, polarisation match, perhaps not sat_pos or ts/network id

			if (is_tuned_freq) {
				if (pat_data.stable_pat()) {
					dtdebug("sat_pos is wrong but pat is stable.");
					is_tuned_tp = true;
					tune_confirmation.on_wrong_sat = std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= 100;
					if (abort_on_wrong_sat())
						ret = dtdemux::reset_type_t::ABORT;
					else
						ret = dtdemux::reset_type_t::NO_RESET;
				} else {
					reader->update_tuned_mux_nit(mux);
					ret = dtdemux::reset_type_t::RESET;
				}
			}
		}

	if (is_tuned_tp) {
		dtdebugx("NIT CONFIRMS sat=%d network_id=%d ts_id=%d", mux_key->sat_pos, mux_key->network_id, mux_key->ts_id);
		if (tune_confirmation.sat_by == confirmed_by_t::NONE)
			tune_confirmation.sat_by = confirmed_by_t::NIT;
		if (tune_confirmation.ts_id_by == confirmed_by_t::NONE || tune_confirmation.network_id_by == confirmed_by_t::NONE) {
			tune_confirmation.ts_id_by = confirmed_by_t::NIT;
			tune_confirmation.network_id_by = confirmed_by_t::NIT;
			sdt_data.network_id = mux_key->network_id;
			sdt_data.ts_id = mux_key->ts_id;
		}
		namespace m = chdb::update_mux_preserve_t;
		chdb::update_mux(txn, mux, now, m::flags{m::MUX_COMMON});
		if (!abort_on_wrong_sat()) {
			reader->set_current_tp(mux);
		}
	} else {
		namespace m = chdb::update_mux_preserve_t;
		/*to avoid cases where another another mux provides wrong network_id, ts_id
			network_id, ts_id for the tuned mux is considered authorative
		*/
		chdb::update_mux(txn, mux, now, m::flags{m::MUX_COMMON | m::MUX_KEY});
		if (tune_confirmation.sat_by == confirmed_by_t::NONE) {

			if (std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= 100) { // 1 degree
				dtdebugx("NIT CONFIRMS sat=%d", mux_key->sat_pos);
				tune_confirmation.sat_by = confirmed_by_t::NIT;
			}
		}
	}
	return {ret, is_tuned_tp};
}

bool nit_data_t::update_sdt_completion(scan_state_t& scan_state, const subtable_info_t& info, mux_data_t& mux_data,
																			 bool reset) {
	bool is_actual = info.is_actual;
	auto& sdt = mux_data.sdt[is_actual];
	if (!(reset || info.timedout || (sdt.num_sections_processed < sdt.subtable_info.num_sections_present))) {
		dterrorx("ERROR:  reset=%d timedout=%d num_sections_processed=%d num_sections_present=%d", reset, info.timedout,
						 sdt.num_sections_processed, sdt.subtable_info.num_sections_present);
		return true; // todo handle version change
	}
	if (reset)
		sdt.num_sections_processed = 0;
	if (!reset && !info.timedout)
		sdt.num_sections_processed++;
	bool empty = (sdt.num_sections_processed == 0);
	assert(sdt.num_sections_processed >= 0);
	bool done_now = (sdt.num_sections_processed == sdt.subtable_info.num_sections_present);
	if (done_now || empty) {
		auto& n = get_original_network(mux_data.mux_key.network_id);
		n.add_mux(mux_data.mux_key.ts_id, true);
		// n.sdt_num_muxes_present += empty;
		if (done_now) {
			dtdebugx("set_sdt_completed: ts_id=%d is_actual=%d vers=%d pid=%d proc=%d pres=%d", mux_data.mux_key.ts_id,
							 info.is_actual, info.version_number, info.pid, sdt.num_sections_processed,
							 sdt.subtable_info.num_sections_present);
			n.set_sdt_completed(mux_data.mux_key.ts_id);
		}
		if (n.sdt_num_muxes_completed > n.sdt_num_muxes_present)
			dterrorx("assertion failed: num_muxes_completed=%d; n.sdt_num_muxes_present=%d", n.sdt_num_muxes_completed,
							 n.sdt_num_muxes_present);
		assert(n.sdt_num_muxes_completed <= n.sdt_num_muxes_present);
		if (done_now) {
			dtdebug("SDT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " subtable completed: " << mux_data.mux_key << " "
							<< mux_data.service_ids.size() << " services; muxes now completed on network: "
							<< n.sdt_num_muxes_completed << "/" << n.sdt_num_muxes_present);
			if (info.is_actual) {
				scan_state.set_completed(scan_state_t::SDT_ACTUAL);
			}
			if (!info.is_actual) {
				if (n.sdt_num_muxes_completed == n.sdt_num_muxes_present) {
					dtdebug("Network completed: " << mux_data.mux_key.network_id);
				}
			}
		} else if (empty) {
			dtdebug("SDT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " subtable reset: " << mux_data.mux_key << " "
							<< mux_data.service_ids.size() << " services; muxes now completed on network: "
							<< n.sdt_num_muxes_completed << "/" << n.sdt_num_muxes_present);
		}

		assert(n.sdt_num_muxes_completed <= n.sdt_num_muxes_present);
		assert(n.sdt_num_muxes_completed >= 0);
	}
	return done_now;
}

bool nit_data_t::update_nit_completion(scan_state_t& scan_state, const subtable_info_t& info,
																			 network_data_t& network_data, bool reset) {
	assert(reset || info.timedout ||
				 (network_data.num_sections_processed < network_data.subtable_info.num_sections_present));
	if (!reset && !info.timedout)
		network_data.num_sections_processed++;
	assert(network_data.num_sections_processed <= network_data.subtable_info.num_sections_present);
	assert(network_data.num_sections_processed >= 0);
	bool done = (network_data.num_sections_processed == network_data.subtable_info.num_sections_present);
	bool empty = (network_data.num_sections_processed == 0);
	if (done) {
		dtdebug("NIT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " subtable completed: " << network_data.network_id << " "
						<< by_network_id_ts_id.size() << " muxes");
		if (info.is_actual) {
			dtdebug("NIT_ACTUAL subtable completed: " << network_data.network_id);

			scan_state.set_completed(scan_state_t::NIT_ACTUAL);
		}
	}
	if (empty) {
		dtdebug("NIT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " empty not: " << network_data.network_id);
	}

	return done;
}

dtdemux::reset_type_t active_si_stream_t::sdt_section_cb_(db_txn& txn, const sdt_services_t& services,
																													const subtable_info_t& info, mux_data_t* p_mux_data) {
	bool is_actual = services.is_actual;
	assert(tune_confirmation.sat_by != confirmed_by_t::NONE || is_actual);
	assert(p_mux_data);
	chdb::mux_key_t& mux_key = p_mux_data->mux_key;
	assert(mux_key.network_id == services.original_network_id);
	assert(mux_key.ts_id == services.ts_id);

	auto& service_ids = p_mux_data->service_ids;
	if (p_mux_data->sdt[is_actual].subtable_info.version_number != info.version_number) {
		// record which services have been found
		service_ids.clear();
		if (p_mux_data)
			nit_data.reset_sdt_completion(scan_state, info, *p_mux_data);

		p_mux_data->sdt[is_actual].subtable_info = info;
	}
	bool donotsave = false;
	if (!services.is_actual) {
		auto* tuned_mux_key = mux_key_ptr(reader->tuned_mux());
		if (tuned_mux_key->sat_pos != mux_key.sat_pos) {
			dtdebug("SDT_OTHER: ignore services for other sat: " << mux_key);
			donotsave = true;
		}
	}
	int db_found{0};
	int db_changed{0};
	int db_notfound{0};

	for (auto& service : services.services) {
		assert(mux_key.network_id == service.k.mux.network_id);
		assert(mux_key.ts_id == service.k.mux.ts_id);

		if (service.name.size() == 0) {
			// dtdebug("Skipping service without name "<< service.k);
			continue;
		}
		service_ids.push_back(service.k.service_id);

		auto c = chdb::service::find_by_mux_sid(txn, mux_key, service.k.service_id);
		if (c.is_valid()) {
			db_found++;
			auto ch = c.current();
			bool changed = false;

			if (service.name != ch.name) {
				ch.name = service.name;
				changed = true;
			}
			if (ch.provider != service.provider) {
				ch.provider = service.provider;
				changed = true;
			}
			// note: we do not check pmt_pid, as this can only be found in PAT (so we preserve
			changed |= (ch.expired || ch.media_mode != service.media_mode || ch.service_type != service.service_type ||
									ch.encrypted != service.encrypted);
			if (changed)
				db_changed++;
			// we do not count mux_desc changes, as they do not relate (ony) to SDT
			if (ch.mux_desc != p_mux_data->mux_desc) {
				ch.mux_desc = p_mux_data->mux_desc;
				assert((int)strlen(ch.mux_desc.c_str()) == ch.mux_desc.size());
				changed = true;
			}

			if (changed) {
				ch.expired = false;
				ch.media_mode = service.media_mode;
				ch.service_type = service.service_type;
				ch.encrypted = service.encrypted;
				ch.mtime = system_clock_t::to_time_t(now);
				ch.mux_desc = p_mux_data->mux_desc;
				assert((int)strlen(ch.mux_desc.c_str()) == ch.mux_desc.size());
				if (!donotsave) {
					dtdebug("SAVING changed service " << ch);
					put_record(txn, ch);
				}
			}
		} else {
			db_notfound++;
			auto ch = service;
			ch.mtime = system_clock_t::to_time_t(now);
			ch.k.mux = mux_key;
			ch.mux_desc = p_mux_data->mux_desc;
			assert((int)strlen(ch.mux_desc.c_str()) == ch.mux_desc.size());
			if (!donotsave) {
				dtdebug("SAVING new service " << ch);
				put_record(txn, ch);
			}
		}
	}

	if (services.has_freesat_home_epg)
		p_mux_data->has_freesat_home_epg = true;
	if (services.has_opentv_epg)
		p_mux_data->has_opentv_epg = true;

	if (is_actual) {
		// update current_tp in case ts_id or network_id have changed
		bool mux_key_changed = check_tuned_mux_key(txn, mux_key);

		bool must_reset = sdt_actual_check_confirmation(mux_key_changed, db_found - db_changed, p_mux_data);
		if (must_reset) {
			txn.abort();
			return dtdemux::reset_type_t::RESET;
		}
	}

	bool done_now = nit_data.update_sdt_completion(scan_state, info, *p_mux_data);
	tune_confirmation.sdt_actual_ok = done_now;
	if (done_now) {
		if (!donotsave)
			process_removed_services(txn, mux_key, service_ids);
		bool may_update = true;
		chdb::any_mux_t mux;
		auto tuned_mux = reader->tuned_mux();
		if (is_actual) {
			bool is_template = mux_common_ptr(tuned_mux)->is_template;
			assert(mux_key == *mux_key_ptr(tuned_mux) || is_template);
			if (is_template) {
				const bool update_db = false;
				on_tuned_mux_key_change(txn, mux_key, update_db, false);
			}
			// copy from tune_mux to avoid consulting the db
			mux = tuned_mux;
		} else {
			// we need the full mux, so we need to load it from the db
			// we only update if we found exactly 1 mux; otherwise we better wait for nit_actual/nit_other to tell us the
			// correct sar
			auto r = find_by_mux_key_fuzzy(txn, mux_key);
			may_update = r.num_matches == 1;
			mux = r.mux;
		}

		if (may_update && !donotsave) {
			auto& common = *chdb::mux_common_ptr(mux);
			bool changed = (common.num_services != service_ids.size());
			common.num_services = service_ids.size();

			auto has_movistar = (mux_key.sat_pos == 1920 && mux_key.network_id == 1 && mux_key.ts_id == 1058);
			auto has_viasat_baltic = (mux_key.sat_pos == 500 && mux_key.network_id == 86 && mux_key.ts_id == 27);

			changed |= has_movistar ? add_epg_type(mux, chdb::epg_type_t::MOVISTAR)
				: remove_epg_type(mux, chdb::epg_type_t::MOVISTAR);

			changed |= has_viasat_baltic ? add_epg_type(mux, chdb::epg_type_t::VIASAT)
				: remove_epg_type(mux, chdb::epg_type_t::VIASAT);

			changed |= p_mux_data->has_opentv_epg ? add_epg_type(mux, chdb::epg_type_t::SKYIT)
				: remove_epg_type(mux, chdb::epg_type_t::SKYIT);

			changed |= p_mux_data->has_freesat_home_epg ? add_epg_type(mux, chdb::epg_type_t::FSTHOME)
				: remove_epg_type(mux, chdb::epg_type_t::FSTHOME);

			if (changed) {
				namespace m = chdb::update_mux_preserve_t;
				chdb::update_mux(txn, mux, now, m::flags{m::ALL & ~(m::NUM_SERVICES | m::EPG_TYPES)});
				// dtdebug("freesat/skyuk epg flag changed on mux " << tuned_mux);
				if (is_actual) {
					auto* tuned_mux_key = mux_key_ptr(reader->tuned_mux());
					assert(tuned_mux_key->sat_pos == mux_key.sat_pos);
					reader->set_current_tp(mux);
				}
			}
		}
		if (is_actual) {
			auto cidx = scan_state_t::SDT_ACTUAL;
			scan_state.set_completed(cidx);
		}
	}
	if (donotsave)
		txn.abort();
	else
		txn.commit();

	return dtdemux::reset_type_t::NO_RESET;
}

dtdemux::reset_type_t active_si_stream_t::eit_section_cb_(epg_t& epg, const subtable_info_t& info) {
	auto cidx = epg.is_actual ? scan_state_t::EIT_ACTUAL_EPG : scan_state_t::EIT_OTHER_EPG;
	if (epg.is_sky)
		cidx = scan_state_t::SKYUK_EPG;
	else if (epg.is_freesat)
		cidx = scan_state_t::FST_EPG;

	auto& c = eit_data.subtable_counts[info.pid];
	c.num_completed = epg.num_subtables_completed;
	c.num_known = epg.num_subtables_known;

	assert(tune_confirmation.sat_by != confirmed_by_t::NONE);
	auto& updated_records = epg.is_actual ? eit_data.eit_actual_updated_records : eit_data.eit_other_updated_records;
	auto& existing_records = epg.is_actual ? eit_data.eit_actual_existing_records : eit_data.eit_other_existing_records;
	if (info.timedout) {
		scan_state.set_timedout(cidx);
		dtdebugx("EIT_%s: timedout unchanged=%d changed=%d\n", epg.is_actual ? "ACTUAL" : "OTHER", existing_records,
						 updated_records);
		return dtdemux::reset_type_t::NO_RESET;
	} else
		scan_state.set_active(cidx);

	auto tuned_mux = reader->tuned_mux();
	auto tuned_mux_key = mux_key_ptr(tuned_mux);

	auto epg_type = epg.epg_type;

	auto epg_source = epgdb::epg_source_t((epgdb::epg_type_t)(int)epg_type, info.table_id, info.version_number,
																				tuned_mux_key->sat_pos, tuned_mux_key->network_id, tuned_mux_key->ts_id);

	if (epg.is_sky || epg.is_mhw2_title) {
		auto* service_key = bat_data.lookup_opentv_channel(epg.channel_id);
		if (!service_key) {
			bool done = bat_done();
			dtdebugx("Cannot enter %s records, because channel with channel_id_id=%d has not been found in BAT%s",
							 epg.is_sky		 ? "SKYUK_EPG"
							 : epg.is_mhw2 ? "MHW2_EPG"
							 : "",
							 epg.channel_id, (done ? " (not retrying)" : " (retrying)"));
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
		epg.epg_service.sat_pos = service_key->mux.sat_pos;
		epg.epg_service.network_id = service_key->mux.network_id;
		epg.epg_service.ts_id = service_key->mux.ts_id;
		epg.epg_service.service_id = service_key->service_id;
	} else if (epg.is_mhw2_summary) {
		// service will be looked up later
	} else {
		auto txn = chdb.rtxn();
		auto* p_mux_data = lookup_nit(txn, epg.epg_service.network_id, epg.epg_service.ts_id, sat_pos_none);
		txn.abort();
		if (!p_mux_data) {
			bool done = network_done(epg.epg_service.network_id);
#if 0
			const auto* s = enum_to_str(epg_type);
			dtdebugx("Cannot enter EPG_%s (%s) records, because mux with network_id=%d and ts_id=%d has not been "
							 "found in SDT%s",
							 epg.is_actual ? "ACTUAL": "OTHER", s,
							 epg.epg_service.network_id, epg.epg_service.ts_id,
							 (done ? " (not retrying)" : " (retrying)"));
#endif
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}

		bool is_wrong_dvb_type = (dvb_type(p_mux_data->mux_key.sat_pos) != dvb_type(tuned_mux_key->sat_pos));
		if (is_wrong_dvb_type) {
			auto done = tune_confirmation.sat_by != confirmed_by_t::NONE;
			/*Hack: when the mux is for dvb-t and we are tuned to sat, assume that the
				mux is on the current sat. This will do the right thing for the French multistreams
				on 5.0W, but may  have unwanted consequences. We hope not...
			*/
			if (done)
				epg.epg_service.sat_pos = tuned_mux_key->sat_pos;
			else {
				const auto* s = enum_to_str(epg_type);
				dtdebugx("Cannot enter EPG_%s records (%s), because mux with network_id=%d and ts_id=%d has different "
								 "dvb type and sat not yet confirmed (retrying)",
								 epg.is_actual ? "ACTUAL" : "OTHER", s, epg.epg_service.network_id, epg.epg_service.ts_id);
				return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
			}
		} else
			epg.epg_service.sat_pos = p_mux_data->mux_key.sat_pos;
	}
	auto txn = epgdb_txn();
	for (auto& epg_record : epg.epg_records) {
		// assert(!epg.is_sky || p_mux_data->mux_key.network_id == epg_record.k.service.network_id);
		// assert(!epg.is_sky || p_mux_data->mux_key.ts_id == epg_record.k.service.ts_id);
		if (epg.is_sky || epg.is_mhw2)
			epg_record.k.service = epg.epg_service;
		else
			epg_record.k.service.sat_pos = epg.epg_service.sat_pos;
		epg_record.source = epg_source;
		epg_record.mtime = system_clock_t::to_time_t(now);
		if (epg.is_sky_title) {

			eit_data.otv_times_for_event_id.try_emplace(std::make_tuple(epg.channel_id, epg_record.k.event_id),
																									std::make_tuple(epg_record.k.start_time, epg_record.end_time));

		} else if (epg.is_mhw2_title) {
			eit_data.mhw2_key_for_event_id.try_emplace(
				epg_record.k.event_id, std::make_tuple(epg_record.k.service, epg_record.k.start_time, epg_record.end_time));

		} else if (epg.is_sky_summary) {
			auto [it, found] =
				find_in_map(eit_data.otv_times_for_event_id, std::make_tuple(epg.channel_id, epg_record.k.event_id));
			if (!found) {
				bool done = false;
				dtdebug_nicex("Cannot enter SKYUK_EPG summary records, because title with channel_id_id=%d and  event_id=%d"
											" has not been found yet%s",
											epg.channel_id, epg_record.k.event_id, (done ? " (not retrying)" : " (retrying)"));
				txn.abort();
				return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
			}
			std::tie(epg_record.k.start_time, epg_record.end_time) = it->second;
		} else if (epg.is_mhw2_summary) {
			auto [it, found] = find_in_map(eit_data.mhw2_key_for_event_id, epg_record.k.event_id);
			if (!found) {
				bool done = false;
				dtdebugx("Cannot enter MHW2_EPG summary records, because title with event_id=%d"
								 " has not been found yet%s",
								 epg_record.k.event_id, (done ? " (not retrying)" : " (retrying)"));
				txn.abort();
				return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
			}
			std::tie(epg_record.k.service, epg_record.k.start_time, epg_record.end_time) = it->second;
		}
		bool updated = epgdb::save_epg_record_if_better_update_input(txn, epg_record);

		if (updated) {
			active_adapter().tuner_thread.on_epg_update(txn, now, epg_record);
			updated_records++;
		} else
			existing_records++;
	}
	txn.commit();
	return dtdemux::reset_type_t::NO_RESET;
}

void active_si_stream_t::process_removed_services(db_txn& txn, chdb::mux_key_t& mux_key,
																									ss::vector_<uint16_t>& service_ids) {
	auto c = chdb::service::find_by_mux_key(txn, mux_key);

	for (const auto& service : c.range()) {
		if (service.k.mux != mux_key)
			break; // TODO: currently there is no way to iterate over services on a single mux
		if (service.expired)
			continue; // already expired
		if (std::find(std::begin(service_ids), std::end(service_ids), service.k.service_id) != std::end(service_ids))
			continue; // service is present in si stream
		/*
			Case of BBC four: during daytime, its service_id is not in pat, but it is in the sdt_actual
			table. There it is marked with running status 1 (not running)


			At 19:58 first pmt for bbc4 (pmt 259) is received. current_next=1

			Running status changes from "not running" to
			"running".

		*/
		dtdebug("Expiring service " << service);
		auto s = service;
		s.expired = true;
		s.mtime = system_clock_t::to_time_t(now);
		put_record(txn, s);
	}
}

void active_si_stream_t::process_removed_channels(db_txn& txn, const chdb::chg_key_t& chg_key,
																									ss::vector_<uint16_t>& channel_ids) {
	chdb::chgm_key_t k{};
	k.chg = chg_key;
	auto c = chdb::chgm_t::find_by_key(txn, k, find_geq, chdb::chgm_t::partial_keys_t::chg);

	for (const auto& channel : c.range()) {
		if (channel.k.chg != chg_key)
			break; // TODO: currently there is no way to iterate over services on a single mux
		if (channel.expired)
			continue; // already expired
		if (std::find(std::begin(channel_ids), std::end(channel_ids), channel.k.channel_id) != std::end(channel_ids))
			continue; // service is present in si stream
		dtdebug("Expiring channel " << channel);
		auto s = channel;
		s.expired = true;
		s.mtime = system_clock_t::to_time_t(now);
		put_record(txn, s);
	}
}

dtdemux::reset_type_t active_si_stream_t::sdt_section_cb(const sdt_services_t& services, const subtable_info_t& info) {
	auto cidx = services.is_actual ? scan_state_t::SDT_ACTUAL : scan_state_t::SDT_OTHER;
	if (info.timedout) {
		scan_state.set_timedout(cidx);

		if (!services.is_actual) { // SDT_ACTUAL is flagged as completed elsewhere
			if (all_known_muxes_completed())
				scan_state.set_completed(scan_state_t::SDT_OTHER);
		}

		return dtdemux::reset_type_t::NO_RESET;
	} else
		scan_state.set_active(cidx);

	if (services.is_actual &&
			(tune_confirmation.ts_id_by == confirmed_by_t::NONE || tune_confirmation.network_id_by == confirmed_by_t::NONE)) {
		tune_confirmation.ts_id_by = confirmed_by_t::SDT;
		tune_confirmation.network_id_by = confirmed_by_t::SDT;
		sdt_data.network_id = services.original_network_id;
		sdt_data.ts_id = services.ts_id;
	}

	if (!info.timedout && services.original_network_id == sdt_data.network_id)
		scan_state.set_active(scan_state_t::SDT_NETWORK);
	auto txn = chdb_txn();
	auto* tuned_key = mux_key_ptr(reader->tuned_mux());
	auto* p_mux_data = lookup_nit(txn, services.original_network_id, services.ts_id,
																services.is_actual ? tuned_key->sat_pos : sat_pos_none);

	if (services.is_actual && p_mux_data && !p_mux_data->is_tuned_mux) {
		// happens on 4.0W 12353H, which reports wrong frequency in nit_actual
		dterrorx("sdt_actual mux is not the tuned mux");
		if (nit_actual_done()) {
			if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
				tune_confirmation.sat_by = confirmed_by_t::FAKE;
			}
			p_mux_data = add_fake_nit(txn, services.original_network_id, services.ts_id, tuned_key->sat_pos);
		}
	}

	if (services.is_actual && p_mux_data && (!p_mux_data || !p_mux_data->is_tuned_mux) && nit_actual_notpresent()) {
		p_mux_data = add_fake_nit(txn, services.original_network_id, services.ts_id, tuned_key->sat_pos);
		assert(tune_confirmation.sat_by == confirmed_by_t::NONE);
		tune_confirmation.sat_by = confirmed_by_t::FAKE;
		// there is no way to confirm the sat
		// Note that setting tune_confirmation will also cause the transaction to be actually committed
	}

	if (!p_mux_data) {
		/*for SDT_ACTUAL: This must mean that network/ts in sdt_actual is not in the database. This can happen if
			network/ts of the tuned
			mux has changed, or (as a special case) that the mux is one created by the user, resulting in storage in the db
			with the wrong network/ts, or even not storing the mux in the db at all.

			In both cases we cannot enter information in the database.
			It is also very unlikely that services with services.original_network_id, services.ts_id exits in the database,
			so satellite confirmation does not work either. The best thing to do is wait for nit_actual to retrieve mu info
			and to retry later

			for SDT_OTHER: we also have to wait
		*/
		;

		bool nit_done = (nit_actual_done());
		if (services.is_actual && nit_done) {
			// assert(tune_confirmation.sat_by ==  confirmed_by_t::NONE);
			/* This can be a boundary case: a new mux was entered by the user, but the mux does not have
				 a valid nit_actual (e.g., French DVBT multistreams on 5.0W) and so the database
				 does not contain an entry with the correct network_id/ts_id.
				 No hope of finding this in nit  and we have found no useful info in the database either
			*/
			auto mux = reader->tuned_mux();
			auto& mux_key = *mux_key_ptr(mux);
			mux_key.network_id = services.original_network_id;
			mux_key.ts_id = services.ts_id;

			// update the database; this may fail if tuners is not locked
			bool updated = update_template_mux_parameters_from_frontend(txn, mux);

			if (!updated) {
				txn.abort();
				return dtdemux::reset_type_t::RESET;
			}
			mux_key = *mux_key_ptr(reader->tuned_mux()); // to update extra_id

			nit_data.add_mux_from_sdt(mux);
			// now try again; we must succeed
			p_mux_data = lookup_nit(txn, services.original_network_id, services.ts_id, mux_key.sat_pos);

			if (p_mux_data)
				tune_confirmation.sat_by = confirmed_by_t::SDT;
		}
		if (!p_mux_data) {
#if 0
			dtdebugx("Cannot enter SDT_%s services, because mux with network_id=%d and ts_id=%d has not been found in NIT%s",
							 services.is_actual ? "ACTUAL": "OTHER",
							 services.original_network_id, services.ts_id,
							 (nit_done ? " (not retrying)" : " (retrying)"));
#endif
			txn.abort();
			// if not nit_done: will reparse later; we could also store these records (would be faster)
			return nit_done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
	}

	if (services.is_actual && !p_mux_data->is_tuned_mux) {
		// this happens on 5.0W 11455.5H: NIT_ACTUAL reports a completely different frequency than the true one
		p_mux_data = add_fake_nit(txn, services.original_network_id, services.ts_id, tuned_key->sat_pos);
		if (tune_confirmation.sat_by == confirmed_by_t::AUTO) {
		} else if (tune_confirmation.sat_by != confirmed_by_t::NONE) {
			/*
				One reason this could happen is when the lnb receives he wrong polarisation
				@todo: further investigate
			*/
			dterrorx("Unexpected: tune_confirmation.sat_by=%d", tune_confirmation.sat_by);
		} else
			tune_confirmation.sat_by = confirmed_by_t::FAKE;
	}

	if (!services.is_actual && tune_confirmation.sat_by == confirmed_by_t::NONE) {
		/*
			We only enter services after sat has been confirmed, but for SDT_ACTUAL
			we also test if we can confirm the sat, so we must run  sdt_section_cb_,
			which will abort databse writing if needed.

			We could do this also for SDT_OTHER....
		*/
		return dtdemux::reset_type_t::RESET;
	}
	return sdt_section_cb_(txn, services, info, p_mux_data);
}

dtdemux::reset_type_t active_si_stream_t::bat_section_cb(const bouquet_t& bouquet, const subtable_info_t& info) {
	auto cidx = scan_state_t::BAT;

	if (tune_confirmation.sat_by == confirmed_by_t::NONE)
		return dtdemux::reset_type_t::RESET; // delay processing until sat_confirmed.

	if (info.timedout) {
		scan_state.set_timedout(cidx);

		if (bat_all_bouquets_completed())
			scan_state.set_completed(cidx);

		return dtdemux::reset_type_t::NO_RESET;
	} else
		scan_state.set_active(cidx);

	auto tuned_mux = reader->tuned_mux();
	auto tuned_mux_key = mux_key_ptr(tuned_mux);
	auto txn = chdb_txn();
	auto bouquet_id = bouquet.is_mhw2 ? bouquet_id_movistar : bouquet.bouquet_id;
	chdb::chg_t chg(chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id, tuned_mux_key->sat_pos), 0, bouquet.name,
									system_clock_t::to_time_t(now));
	if (chg.name.size() == 0)
		chg.name << "Bouquet " << (int)bouquet.bouquet_id;

	auto c = chdb::chg_t::find_by_key(
		txn, chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id, tuned_mux_key->sat_pos), find_eq);
	bool chg_changed = true;
	if (c.is_valid()) {
		auto tmp = c.current();
		chg_changed = (chg.name != tmp.name);
	}

	for (const auto& [channel_id, channel] : bouquet.channels) {
		if (channel_id == 0xff)
			continue;
		chdb::chgm_t chgm;
		//		channel.service_key.mux.sat_pos = tuned_mux_key->sat_pos;
		/*we assume channel_ids are unique even when multiple bouquets exist in BAT
		 */
		auto* p_mux_data = lookup_nit(txn, channel.service_key.mux.network_id, channel.service_key.mux.ts_id, sat_pos_none);
		if (!p_mux_data) {
			bool done = network_done(channel.service_key.mux.network_id);
#if 0
			dtdebug("Cannot enter BAT channel, because mux " << channel.service_key << " has not been found in NIT"
							<< (done ? " (not retrying)" : " (retrying)")
				)
#endif
				if (done)
					continue; // go for partial bouquet
			bat_data.reset_bouquet(bouquet.bouquet_id);
			txn.abort();
			// if not done: will reparse later; we could also store these records (would be faster)
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
		chgm.k.chg = chg.k;
		chgm.service.mux = p_mux_data->mux_key;
		chgm.service.service_id = channel.service_key.service_id;
		chgm.service_type = channel.service_type;
		chgm.user_order = channel.lcn;
		chgm.chgm_order = channel.lcn;
		if (channel.is_opentv_or_mhw2) {
			auto [it, inserted] = bat_data.opentv_service_keys.try_emplace(channel_id, chgm.service);
			if (chgm.service != it->second) {
				dtdebug("channel_id=" << int(channel_id) << ": service changed from " << it->second << " to " << chgm.service);
			}
		}

		auto c1 = chdb::service::find_by_mux_sid(txn, chgm.service.mux, chgm.service.service_id);
		if (!c1.is_valid()) {
			bool done = mux_sdt_done(chgm.service.mux.network_id, chgm.service.mux.ts_id);
#if 0
			dtdebug("Could not find service for key=" << chgm.service
							<< (done ? " (not retrying)" : " (retrying after sdt loaded)"));
#endif
			if (done)
				continue;
			bat_data.reset_bouquet(bouquet.bouquet_id);
			txn.abort();
			return dtdemux::reset_type_t::RESET;
		}
		chdb::service_t service{c1.current()};

		auto c = chdb::chgm_t::find_by_key(txn, chgm.k, find_eq);
		bool changed = true;
		chgm.media_mode = service.media_mode;
		chgm.encrypted = service.encrypted;
		chgm.expired = false;
		chgm.chgm_order = channel.lcn;
		chgm.k.channel_id = channel_id;
		chgm.mtime = system_clock_t::to_time_t(now);
		chgm.name = service.name;

		if (c.is_valid()) {
			// existing record
			changed = false;
			const auto& old = c.current();
			changed |= (chgm.media_mode != old.media_mode);
			changed |= (chgm.chgm_order != old.chgm_order);
			changed |= (chgm.k.channel_id != old.k.channel_id);
			changed |= (chgm.name != old.name);
			changed |= (chgm.encrypted != old.encrypted);
			changed |= (chgm.expired != old.expired);
			chgm.user_order = old.user_order;
		} else {
			chgm.user_order = channel.lcn; // default value
		}
		if (changed)
			put_record(txn, chgm);
	}

	auto& bouquet_data = bat_data.get_bouquet(chg);
	if (bouquet_data.subtable_info.version_number != info.version_number) {
		// record which services have been found
		dtdebugx("BAT: subtable version changed from %d to %d\n", bouquet_data.subtable_info.version_number,
						 info.version_number);
		bouquet_data.channel_ids.clear();
		bouquet_data.num_sections_processed = 0;
		bouquet_data.subtable_info = info;
	}

	for (const auto& [channel_id, channel] : bouquet.channels) {
		bouquet_data.channel_ids.push_back(channel_id);
	}
	assert(bouquet_data.num_sections_processed <= bouquet_data.subtable_info.num_sections_present);

	if (++bouquet_data.num_sections_processed == bouquet_data.subtable_info.num_sections_present) {
		dtdebug("BAT subtable completed for bouquet=" << (int)bouquet.bouquet_id << " " << bouquet_data.channel_ids.size()
						<< " channels");
		process_removed_channels(txn, chg.k, bouquet_data.channel_ids);
		if (chg.num_channels != bouquet_data.channel_ids.size()) {
			chg.num_channels = bouquet_data.channel_ids.size();
			chg_changed = true;
		}
	}
	if (chg_changed) {
		chg.mtime = system_clock_t::to_time_t(now);
		put_record(txn, chg);
	}

	txn.commit();
	//@todo: can we check for completion?
	return dtdemux::reset_type_t::NO_RESET;
}

dtdemux::reset_type_t active_si_stream_t::eit_section_cb(epg_t& epg, const subtable_info_t& i) {

	if (tune_confirmation.sat_by == confirmed_by_t::NONE)
		return dtdemux::reset_type_t::RESET; // delay processing until sat_confirmed.
	return eit_section_cb_(epg, i);
}

void active_si_stream_t::init_scanning(scan_target_t scan_target_) {
	scan_done = false;
	tune_start_time = now;
	// inited_ = true;
	scan_target = scan_target_ == scan_target_t::NONE ? scan_target_t::DEFAULT : scan_target_;
	scan_state = scan_state_t();

	// add_table should be called in order of importance and urgency.

	// if nit.sat_id is not set, some tables will wait with parsing
	// until the correct nit_actual table is received.
	// We probably should deactivate this feature after a certain
	// timeout
}

active_si_stream_t::~active_si_stream_t() { deactivate(); }

/*
	Assumptions:
	-nit_actual table  contains only entries with the same orbital position as the currently tuned mux
	This assumption is vilated on skyitalia 13.0E 11862H ts_id=5700. It contains an entry for 99.9 E
	with an obviously invalid mux in nit_actual.  This violates dvb guidelines.
	As a workaround we add another check: detection a bad sat_pos is only allowed for closely enough
	matching tuning parameters
	-nit_actual is tranmitted fully at least every 10 seconds, in compliance with DVB standards
	-sdt_actual is transmitted fully at least every 2 seconds, in compliance with DVB standards
	-sdt_other, if present is transmitted eveyr 10 seconds (less critical, but determines
	required timeouts)
	-sdt_other is consistent with the data in nit_actual (and nit_other): if a (original_network_id, ts_id)
	pair is present in NIT, then it refers to the same mux as in sdt_atcual and sdt_other


	Principles of processing si data:
	1.  for determining if tuned to current sat/mux verify is the correct one (up to a tolerance of 0.3 degrees)
	we check the following criterua  (one satisfied criterion is enough to conclude the sat is correct)
	-if nit_actual is received, the orbital position of any entry is the correct sat position.
	This can take up to 10 seconds
	-when sdt_actual is received, we check for each transport stream if its services agree with those
	in the database, assuming the sat is the correct one (original_network_id and ts_id are provided
	by the dt_aactual table). If a sufficient number of matching service is
	discovered, the sat position is considered verified. This test us fast (< 2 seconds), but can only
	work for muxes which have been scanned or tuned to before (otherwise there is no database data).

	Note that the currently tuned mux is allowed to have a changed network_id, ts_id compared to the database.
	This is important in case one transponder is replaced with another (which could cause ts_id to change)

	-when pat is received, we can perform a similar check: pat provides ts_id, but we can again check the database,
	for matching services, this time assuming that both network_id and sat_pos of the mux we tune it are the correct
	ones. This again only works if the mux was scanned before. It also assumes that network_id of the mux has
	not changed since the last visited.
	2. As long as sat_pos has not been verified, sat_pos of the currently tuned mux should be considered unreliable
	(it is the position we want to tune to, but perhaps the dish is still moving, or a diseqc switch has failed...)
	In this case we can still process nit_actiual and nit_other tables as they always contain sat_pos information

	-However, we have to be careful with sdt_actual, so we delay processing this table until we have confirmed the
	sat_pos. This can be fast (2 seconds; as soon as sdt_actual is received) if we can compare to services in the
	database, or slower if we have to wait for nit_actual to be received (10 seconds).

	3. When processing sdt_other, we always have to be careful: it contains (original_network_id, ts_id) pairs which may
	be for another satellite. It is possible for the same such pair to be present on multiple satellites (e.g., feeds). So
	we only process sdt_other records with (original_network_id, ts_id) which has already been in the nit_actual or
	nit_other table. Otherwise we delay processing of this section. Problems will arise if not nit data is present,
	and slow processing will occur if nit or sdt data is not transmitted fast enough. We handle this with timeouts.

	To speed up things we could also assume that most entries are for the current sat. So if we have not yet received nit
	information for mux, we could check for matching services in the database. The test will fail if the sdt_other mux
	happens to be for a different sat. The danger is that we might make some mistakes, but it speeds up scanning.

	4. Processing EIT data:
	-EIT data only contains network_id, ts_id identifiers, so we need to wait for sat_pos to be confirmed.
	-Sky EPG (opentv) tables use channel_id instead. These channel ids are associated with network_id, ts_id in the sky
	bat table and need to be looked up in the special SKY BAT, which can be slow to receive.
	To speed up things, we can concult the database instead - as soon as sat_pos has been confirmed: if we find a
	matching record, we can simply assume it is correct. This could temporarily lead to bad epg (if a channel_id
	now points to a a different channel) and can be fixed by erasing the "bad epg" when the change is discovered


	One problem with sdt_other, nit_other and eit tables is that we have no way of knowing how many subtables (=muxes, epg
	channels) exist in this table. The only way to know if we received them all is to assume that sdt_other is transmitted
	within 10 seconds. If during 10 seconds no transport stream errors are detected, we assume we have received
	everything. Unfortunately, the 10 seconds rule is not always respected.

	We could also implement some additional checks: e.g., if EIT lists some services not yet seen in sdt_actual we could
	delay timing out. Does not seem worth the complexity.
*/

/*@todo freesat is on all freesat transponders. Therefore we should remember
	when freesat was last scanned and not repeat the scan, irrespective of transponder
	This test should be based on successful completion

	@todo: The presence of freesat pids can be checked in the pmt of any freesat channel:
	this will contain MPEG-2 Private sections refering to the pids below anbd
	with private data specifier equal to  0x46534154 (BBC)

	@todo: register free_sat table only on freesat transponders. Freesat transponders could be
	found from pid 3840 of 3841?

	PID = 3840 = Data-FSAT-Tunnelled NIT
	PID = 3841 = Data-FSAT-Tunnelled BAT/SDT
	PID = 3842 = Data-FSAT-Tunnelled EIT
	PID = 3843 = Data-FSAT-Tunnelled EIT pf
	PID = 3844 = Data-FSAT-Tunnelled TOT/TDT

	freesat transponder 11425H TRP1 ts_id==2315 should have fast freesat epg
	Probably pids are as below
	0x0BB9  MPEG-2 Private sections ...................... C        1,613 b/s  |   NIT?
	0x0BBA  MPEG-2 Private sections ...................... C      144,839 b/s  | BAT/SDT?
	0x0BBB  MPEG-2 Private sections ...................... C    4,656,188 b/s  | EIT?
	0x0BBC  MPEG-2 Private sections ...................... C      411,304 b/s  | EIT pf?
	0x0BBD  MPEG-2 Private sections ...................... C        3,047 b/s  | TOT/TDT?
	0x0BBE  MPEG-2 Private sections ...................... C       63,994 b/s  |
	0x0BBF  MPEG-2 Private sections ...................... C    1,719,428 b/s  |
	0x0BC0  MPEG-2 Private sections ...................... C      667,461 b/s


*/

bool active_si_stream_t::update_template_mux_parameters_from_frontend(db_txn& wtxn, chdb::any_mux_t& mux) {
	auto& c = *mux_common_ptr(mux);
	auto monitor = active_adapter().current_fe->get_monitor_thread();
	chdb::signal_info_t signal_info;
	if (monitor) {
		/*refresh signal info; we need to be sure that frequency is up to date. Using older data
			from a cache in dvb_frontend_t would not be a good idea because it may be outdated (data race),
			so there is no cache
		*/
		monitor
			->push_task([&signal_info, &monitor]() {
				signal_info = cb(*monitor).get_signal_info();
				return 0;
			})
			.wait();

		if (signal_info.lock_status & FE_HAS_LOCK) {
			// assert(c.is_template); //otherwise we should not be called
			c.is_template = false;
			*mux_key_ptr(signal_info.mux) = *mux_key_ptr(mux);			 // overwrite key
			*mux_common_ptr(signal_info.mux) = *mux_common_ptr(mux); // overwrite common
			namespace m = chdb::update_mux_preserve_t;
			assert(mux_key_ptr(signal_info.mux)->sat_pos != sat_pos_none);
			chdb::update_mux(wtxn, signal_info.mux, now, m::flags{m::NONE});
			reader->set_current_tp(signal_info.mux);
			reader->update_tuned_mux_nit(signal_info.mux);
			mux = signal_info.mux;
			return true;
		}
	}
	return false;
}

void active_si_stream_t::load_movistar_bouquet() {
	auto txn = chdb.rtxn();
	chdb::chgm_key_t k{};
	auto sat_pos = reader->get_sat_pos();
	k.chg = (chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id_movistar, sat_pos));
	auto c = chdb::chgm_t::find_by_key(txn, k, find_geq, chdb::chgm_t::partial_keys_t::chg);
	for (const auto& chgm : c.range()) {
		bat_data.opentv_service_keys.try_emplace(chgm.k.channel_id, chgm.service);
	}
	txn.abort();
}

void active_si_stream_t::load_skyuk_bouquet() {
	auto txn = chdb.rtxn();
	chdb::chgm_key_t k{};
	auto sat_pos = reader->get_sat_pos();
	k.chg = (chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id_sky_opentv, sat_pos));
	auto c = chdb::chgm_t::find_by_key(txn, k, find_geq, chdb::chgm_t::partial_keys_t::chg);
	for (const auto& chgm : c.range()) {
		bat_data.opentv_service_keys.try_emplace(chgm.k.channel_id, chgm.service);
	}
	txn.abort();
}

void active_si_stream_t::pmt_section_cb(const pmt_info_t& pmt, bool isnext) {

	dtdebugx("pmt received for sid=%d: stopping stream", pmt.service_id);
	auto& p = pmt_data.by_service_id.at(pmt.service_id);
	p.parser.reset();
	p.pmt_analysis_finished = true;
	for (const auto& desc : pmt.pid_descriptors) {
		bool is_t2mi = desc.t2mi_stream_id >= 0;
		if (is_t2mi) {
			auto& aa = reader->active_adapter;
			bool start = true;
			aa.add_embedded_si_stream(desc.stream_pid, start);
		}
	}
}
void active_si_stream_t::add_pmt(uint16_t service_id, uint16_t pmt_pid) {
	auto [it, inserted] = pmt_data.by_service_id.try_emplace(service_id, pat_service_t{service_id, pmt_pid});
	auto& p = it->second;
	if (inserted) {
		dtdebugx("Adding pmt for analysis: service=%d pmt_pid=%d", (int)service_id, (int)pmt_pid);

		auto pmt_section_cb = [this](const pmt_info_t& pmt, bool isnext) { return this->pmt_section_cb(pmt, isnext); };

		add_parser<dtdemux::pmt_parser_t>(pmt_pid, service_id)->section_cb = pmt_section_cb;

		p.pmt_analysis_started = true;
	}
}
