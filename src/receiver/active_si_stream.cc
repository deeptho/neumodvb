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

#include "active_si_stream.h"
#include "active_adapter.h"
#include "scan.h"
#include "util/logger.h"
#include "../util/neumovariant.h"
#include "../util/template_util.h"
#include "scan.h"

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

/*
	returns 0, sat_pos_dvbt or sat_pos_dvbc
*/
static inline int dvb_type(int16_t sat_pos) {
	if (sat_pos == sat_pos_dvbc || sat_pos == sat_pos_dvbt)
		return sat_pos;
	else if (std::abs(sat_pos) <= 18000)
		return sat_pos_dvbs;
	else
		return sat_pos_none;
}

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
		if(e.last_entries.size() !=0)
			dtdebugx("pat changed %d -> %d\n", e.last_entries.size(), e.entries.size());
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
	, epgdb(receiver.epgdb) {
	dtdebug("setting scan_done=false (init)");
}

void active_si_stream_t::reset(bool force_finalize, bool tune_failed) {
	if((force_finalize || tune_failed) && !scan_done) {
		dtdebugx("tune failed? force_finalize=%d tune_failed=%d scan_done=%d\n",
						force_finalize, tune_failed, scan_done);
		finalize_scan(true /*done*/, tune_failed);
	}
	::active_si_data_t::reset();
	scan_done = force_finalize || tune_failed;
	call_scan_mux_end = true;
	reader->reset();
}

bool active_si_stream_t::abort_on_wrong_sat() const {
	return !is_embedded_si && wrong_sat_detected() && reader->tune_options().retune_mode == retune_mode_t::AUTO;
}

/*
	For a mux received in nit_actual, nit_other or for the currently tuned_mux, with corrected
	network_id, ts_id
	-if the mux has been looked up before in the database by the SDT code and thus is found in the cache,
	 check if its parameters (sat_pos, network_id, ts_id, extra_id) are still valid. If they are not,
	 fix the most important related errors in the databas
	 Possible reasons for invalid information:
      1) sdt knows only network_id, ts_id and guesses sat_pos
			and extra_id. As a precacution, it does so only when the database offers a unique match, but it
			can still be wrong.
			2) the satellite provider has updated a transponder or replaced a mux
			3) the user has entered wrong info into the database
      4) the information came from the wrong sat or (e.g., dish pointed wrongly or diseqc swicth malfunctioning,
         or tuner connected to the wrong lnb)
			polarisation/band (lnb did not properly switch)
	-if the mux is the currently tuned mux, perform a similar check and correction. In this case an
	 additional source for error occurs on first tune: network_id and ts_id need not be set correctly by
	 the user, so the code initially uses zero values. This needs to be corrected. Even sat_pos risks to
	 be different. E.g., user tunes to a mux on 0.8W but the mux itself reports that it is on 1.0W
	-if the mux is not yet in the cache, look it up in the database. Again, check for any discrepancies
	 and correct the database

	 After the function has run, set a flag to indicate that the cached entry has been verified from
	 the received nit information


 */
void active_si_stream_t::add_mux(db_txn& wtxn, chdb::any_mux_t& mux, bool is_actual,
																 bool is_active_mux, bool is_tuned_freq, bool from_sdt)
{
	using namespace chdb;
	namespace m = chdb::update_mux_preserve_t;

	auto* mux_key = mux_key_ptr(mux);
	auto* mux_common = mux_common_ptr(mux);
	auto stream_mux = reader->stream_mux();
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	if(is_active_mux) {
		auto tmp = *mux_common;
		*mux_common = *mux_common_ptr(stream_mux); //copy scan_id and such
		mux_common->nit_network_id = tmp.nit_network_id;
		mux_common->nit_ts_id = tmp.nit_ts_id;
		mux_key->t2mi_pid = reader->embedded_stream_pid();
	}
	if(from_sdt) {
		assert(is_actual);
		assert(is_active_mux);
		mux_common->key_src = key_src_t::SDT_TUNED;
		mux_common->tune_src = tune_src_t::UNKNOWN;
	} else { //from nit_actual
		mux_common->key_src = key_src_t::NIT_TUNED;
		mux_common->tune_src =  is_active_mux
			? tune_src_t::NIT_TUNED
			: is_actual ?  tune_src_t::NIT_ACTUAL : tune_src_t::NIT_OTHER;
	}

	update_mux_preserve_t::flags preserve = m::flags(m::MUX_COMMON);
	if(is_active_mux) {
		/*NIT: allow overwriting key, but we will only do that if the existing key_src!= SDT_TUNED
			     allow overwriting tuning data
					 allow overwriting nit_network_id and nit_ts_id
					 do not allow overwriting the muxes own scan_status
			SDT: allow overwriting key
			     do not allow overwriting tuning data
					 do not allow overwriting nit_network_id and nit_ts_id
					 do not allow overwritung scan_status as we cannot be sure of tuning data
		 */
		preserve = from_sdt ? m::flags((m::MUX_COMMON /* & ~ m::SCAN_STATUS*/)| m::TUNE_DATA)
			: m::flags(m::MUX_COMMON & ~ m::NIT_SI_DATA /* & ~m::SCAN_STATUS*/);
	} else { //!is_active_mux
		/*NIT: allow overwriting key, but we will only do that if the existing key_src!= SDT_TUNED
			     allow overwriting tuning data, but we will only do that if the existing tune_src is NIT_ACTUAL
					   or NIT_OTHER, i.e., if the mux was never tuned (avoid overwriting data confirmed by tuning)
					 allow overwriting nit_network_id and nit_ts_id, but we will only do that if the existing tune_src
					    is NIT_ACTUAL or NIT_OTHER, i.e., if the mux was never tuned (avoid overwriting data confirmed by tuning)
					 allow overwriting nit_network_id and nit_ts_id
					 allow overwriting the muxes scan_status to propagate scanning
		*/
		assert(!from_sdt);
		bool propagate_scan = reader->tune_options().propagate_scan;
		preserve = propagate_scan
			? m::flags(m::MUX_COMMON /*& ~ m::SCAN_STATUS*/ & ~ m::NIT_SI_DATA)
			: m::flags(m::MUX_COMMON & ~ m::NIT_SI_DATA);
	}
	if(!this->update_mux(wtxn, mux, now, is_active_mux /*is_reader_mux*/, from_sdt, preserve))
		return; //something went wrong, e.g., on wrong sat
	assert(mux_key->extra_id != 0);
	ss::string<32> mux_desc;
	assert (mux_key->sat_pos != sat_pos_none);
	chdb::to_str(mux_desc, mux);
#ifndef NDEBUG
	if(is_active_mux) {
		auto& c  = *mux_common_ptr(mux);
		assert( c.tune_src == tune_src_t::NIT_TUNED);
		assert((c.key_src == chdb::key_src_t::SDT_TUNED) ||(!from_sdt  && c.key_src == key_src_t::NIT_TUNED));
	}
#endif

	auto [it, inserted] =
		nit_data.by_network_id_ts_id.try_emplace(std::make_pair(mux_key->network_id, mux_key->ts_id),
																						 mux_data_t{*mux_key, mux_desc});
	auto* p_mux_data = & it->second;
	if(!inserted) {
		if(p_mux_data->source == mux_data_t::SDT) {
			dtdebugx("Updated data earlier set by SDT: is_active_mux=%d is_tuned_freq=%d",
							 is_active_mux, is_tuned_freq);
			p_mux_data->is_active_mux = is_active_mux;
			p_mux_data->is_tuned_freq = is_tuned_freq;
		}
#if 0
		/*
			The following assertion fails on 300W 12130H because the reader_mux is in the NIT_OTHER table
		 */
		assert (p_mux_data->is_active_mux == is_active_mux);
#else
		if(p_mux_data->is_active_mux != is_active_mux) {
			dtdebug("is_active differs: " << (int) p_mux_data->is_active_mux << "!=" << (int) is_active_mux <<
							 " key=" << p_mux_data->mux_key << " changed to " << *mux_key);
		}
#endif
		if(p_mux_data->mux_key != *mux_key){
			dtdebug("Strange: key=" << p_mux_data->mux_key << " changed to " << *mux_key);
			p_mux_data->mux_key = *mux_key;
		}
	} else {
		p_mux_data->is_active_mux = is_active_mux;
		p_mux_data->is_tuned_freq = is_tuned_freq;
	}

	p_mux_data->source = mux_data_t::NIT;

	//force loading database data, allowing  small differences in sat_pos
	bool sat_pos_known = false;
	for (auto& p : nit_data.nit_actual_sat_positions) {
		if (p == mux_key->sat_pos) {
			sat_pos_known = true;
			break;
		}
	}
	if (!sat_pos_known)
		nit_data.nit_actual_sat_positions.push_back(mux_key->sat_pos);

	auto& n = nit_data.get_original_network(mux_key->network_id);
	n.add_mux(p_mux_data->mux_key.ts_id, false /*from_sdt*/);
}

mux_data_t* active_si_stream_t::tuned_mux_in_nit()
{
	using namespace chdb;
	auto [it, found ] = find_in_map_if(nit_data.by_network_id_ts_id, [](const auto&x) {
		auto [key_pair, mux_data] = x;
		return mux_data.is_tuned_freq;
	});

	return found ? &it->second : nullptr;
}

/*
	create a new mux in the database  based on information NOT seen in the input stream.
	This is called when all hope is lost to find the corresponding mux in the NIT stream data,
	but we need it because it is referenced in the SDT, or because no SI data at all was received
	on the current transponder and we wish to record this "si-less" nature in the database.

	This function is called by
	-scan_report: after a timeout, confirming no SI data in the stream (scan_report) or
	when SDT and or NIT data is present, but for some reason it was not possble to determine
	that this data is valid. In this case network_id and ts_id will be set to 0

	-when sdt_section_cb has determined that nit_actual has been received, but an SDT record
	is not referenced in nit_actual, or nit_actual is known to be not present.

	The inserted mux is always for the currently tuned mux, but is lacking tuning data from SI
	(rather they come from the driver) ts_id and network_id are taken from SDT

*/
mux_data_t* active_si_stream_t::add_fake_nit(db_txn& wtxn, uint16_t network_id, uint16_t ts_id,
																						 int16_t expected_sat_pos, bool from_sdt)
{
	using namespace chdb;
	namespace m = chdb::update_mux_preserve_t;
	bool no_data = (network_id == 0 && ts_id == 0);
	dtdebugx("There is no nit_actual on this tp - faking one with sat_pos=%d network_id=%d, ts_id=%d tuned_mux=%s",
					 expected_sat_pos, network_id, ts_id, chdb::to_str(reader->stream_mux()).c_str());
	auto mux = reader->stream_mux();
	auto* mux_key = mux_key_ptr(mux);
	auto* mux_common = mux_common_ptr(mux);
	auto preserve = m::MUX_COMMON;
	if(!is_embedded_si) {
		mux_common->tune_src = tune_src_t::TEMPLATE;
	}
	assert(expected_sat_pos == mux_key->sat_pos);
	mux_key->network_id = network_id;
	mux_key->ts_id = ts_id;
	mux_key->t2mi_pid = reader->embedded_stream_pid();

	if (no_data) {
		auto* mux_common = chdb::mux_common_ptr(mux);
		mux_common->scan_result = chdb::scan_result_t::NODATA;
		mux_common->scan_duration = scan_state.scan_duration();
		// assert(scan_state.scan_duration()>=0);
		mux_common->num_services = 0;
		mux_common->scan_time = system_clock_t::to_time_t(now);
		preserve = m::flags{ preserve & ~ m::MUX_COMMON};
	}

	this->update_mux(wtxn, mux, now, true /*is_reader_mux*/, from_sdt,  m::flags{ m::MUX_COMMON /*& ~m::SCAN_STATUS*/ } /*preserve*/);
	//assert(mux_key->ts_id == ts_id);
	if (!from_sdt && !is_embedded_si) {
		reader->on_stream_mux_change(mux);
	}
	ss::string<32> mux_desc;

	if (expected_sat_pos != sat_pos_none) {
		chdb::to_str(mux_desc, mux);
		// we overwrite any existing mux - if we are called, this means any existing mux must be wrong
		auto [it, inserted] = nit_data.by_network_id_ts_id.insert_or_assign(std::make_pair(network_id, ts_id),
																																				mux_data_t{*mux_key, mux_desc});
		it->second.is_active_mux = true;
		it->second.mux_key = *mux_key;
		it->second.source = from_sdt ? mux_data_t::SDT : mux_data_t::NIT;
		return &it->second;
	}

	return nullptr;
}

/*
	called from eit and bat
	Lookup in the database if a mux exist with
  -the stated network_id and ts_id.
  -and if such a found mux is sufficiently unique: either it is unique in the database,
   or (for expected_sat_pos==sat_pos_none) it is unique on the currently tuned sat
	 (with a possible small difference in sat_pos)

*/
std::optional<chdb::mux_key_t>
active_si_stream_t::lookup_nit_key(db_txn& txn, uint16_t network_id, uint16_t ts_id) {
	auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
	if (found)
		return it->second.mux_key; // nit has been received and mux is already known in cache, this is authorative

	auto [it1, found1] = find_in_map(mux_key_by_network_id_ts_id, std::make_pair(network_id, ts_id));
	if (found1)
		return it1->second; // nit has already been looked up. Return from cache

	/*
	 sat_pos is usually not known for sure by the code calling lookup_nit (eit, sdt, bat)
	 If nit has been received then it is the authorative source for sat_pos, but
	 if we end up here, nit is not or not yet available

	 At this point: if we find a unique matching network_id, ts_id we can gamble that it will be
	 the correct one, even if it is for a sat very different than the tunes one
	 In case of multiple matches, but a unique match for a sat position close to the tuned one,
	 we can also gamble that it is the correct one
	 */
	auto stream_mux_key = this->stream_mux_key();
	auto ret = chdb::get_by_nid_tid_unique(txn, network_id, ts_id, stream_mux_key.sat_pos);

	if ( (ret.unique == chdb::get_by_nid_tid_unique_ret_t::UNIQUE) ||
			 ret.unique == chdb::get_by_nid_tid_unique_ret_t::UNIQUE_ON_SAT) {
		auto mux_key = *mux_key_ptr(ret.mux);
		auto [it, inserted] =
			mux_key_by_network_id_ts_id.try_emplace(std::make_pair(network_id, ts_id), mux_key);
		assert (inserted);
		return mux_key;
	}
	return {};
}


/*
	code called from sdt.
 */
mux_data_t* active_si_stream_t::lookup_mux_from_sdt(db_txn& txn, uint16_t network_id, uint16_t ts_id) {
	auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));

	auto* p_mux_data = found ? &it->second : nullptr;
	if(p_mux_data)
		return p_mux_data;
	/*guestimate that the network may be on the current sat; if it exists in the database
		from an earlier tune, accept it as correct
	*/
	auto sat_pos = this->stream_mux_key().sat_pos;
	auto ret = chdb::get_by_nid_tid_unique(txn, network_id, ts_id, sat_pos);
	if (ret.unique == chdb::get_by_nid_tid_unique_ret_t::UNIQUE_ON_SAT) {
		auto& db_mux = ret.mux;
		auto* mux_key = mux_key_ptr(db_mux);
		ss::string<32> mux_desc;
		assert (mux_key->sat_pos != sat_pos_none);
		chdb::to_str(mux_desc, db_mux);

		auto [it, inserted] =
			nit_data.by_network_id_ts_id.try_emplace(std::make_pair(
																								 mux_key->network_id, mux_key->ts_id), mux_data_t{*mux_key, mux_desc});
		p_mux_data = & it->second;
		assert (inserted);
		p_mux_data->source = mux_data_t::SDT;
	}
	return p_mux_data;
}

/*
	code called from sdt.
 */
mux_data_t* active_si_stream_t::add_reader_mux_from_sdt(db_txn& wtxn, uint16_t network_id, uint16_t ts_id) {
	namespace m = chdb::update_mux_preserve_t;

	auto [it1, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
	auto* p_mux_data = found ? &it1->second : nullptr;
	if(p_mux_data) {
		if(sdt_data.actual_network_id == -1 || sdt_data.actual_ts_id ==-1) {
			/*
				This means that sdt_data has been reset and we should check again for mux_key changes
			 */
			printf("here\n");
		}
		else
			return p_mux_data; //e.g., NIT and SDT agree on network_id, ts_id and NIT already entered the mux
	}

	auto mux = this->reader->stream_mux();
	auto* mux_key = mux_key_ptr(mux);
	if(mux_key->network_id != network_id || mux_key->ts_id != ts_id) {
		mux_key->network_id = network_id;
		mux_key->ts_id = ts_id;
		dtdebug("key change detected: current=" <<  this->stream_mux_key() << " sdt=" << *mux_key);
		auto preserve = m::flags(m::ALL & ~m::MUX_KEY/* &~ m::SCAN_STATUS*/);
		if(!this->update_mux(wtxn, mux, now, true /*is_reader_mux*/, true /*from_sdt*/, preserve)) {
			dtdebugx("Could not update mux_key");
			return nullptr; //something went wrong, e.g., on wrong sat
		}
	}

	ss::string<32> mux_desc;
	assert (mux_key->sat_pos != sat_pos_none);
	chdb::to_str(mux_desc, mux);
	auto [it, inserted] =
		nit_data.by_network_id_ts_id.try_emplace(std::make_pair(
																							 mux_key->network_id, mux_key->ts_id), mux_data_t{*mux_key, mux_desc});
	p_mux_data = & it->second;
	if(!inserted)
		dterrorx("Unexpected: entry already present in nit_data.by_network_id_ts_id\n");
	p_mux_data->source = mux_data_t::SDT;
	return p_mux_data;
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
			dtdebugx("SKIPPING EARLY i=%d\n", i);
			break;
		}

		auto [buffer, ret] = reader->read();
#if 0
		{
			static FILE*fp =fopen("/tmp/test.ts", "w");
			fwrite(buffer, ret, 1, fp);
			fflush(fp);
		}
#endif
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
				dterrorx("error while reading ret=%d errno=%d: %s", (int)ret, errno, strerror(errno));
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
		} else {
			dttime(500);
			reader->discard(num_bytes_to_process - delta);
		}
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
	returns false if fd is not the active_stream's fd
*/
bool active_si_stream_t::read_and_process_data_for_fd(int fd) {
	if (!reader->on_epoll_event(fd))
		return false;
	auto old = tune_confirmation;
	process_si_data();
	reader->data_tick();
	if (old != tune_confirmation) {
		reader->update_stream_mux_tune_confirmation(tune_confirmation);
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
								 (network_done(sdt_data.actual_network_id) || (sdt_actual_done() && sdt_other_done())));
		if (done) {
			scan_state.set_completed(scan_state_t::SDT_NETWORK);
		}
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
	out.sprintf(" tuned_mux=%s", chdb::to_str(reader->stream_mux()).c_str());
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

void active_si_stream_t::fix_tune_mux_template() {
	using namespace chdb;
	namespace m = chdb::update_mux_preserve_t;
	auto stream_mux = reader->stream_mux();
	auto& c = *mux_common_ptr(stream_mux);
	bool is_template = c.tune_src == chdb::tune_src_t::TEMPLATE;
	bool is_active = c.scan_status == scan_status_t::ACTIVE;
	//TODO: check if this does the right thing for t2mi
	if (is_template) {
		dtdebug("Fixing stream_mux template status: " << stream_mux);
		c.scan_time = time_t(0);
		c.num_services = 0;
		c.nit_network_id = 0;
		c.nit_ts_id = 0;
		c.key_src = key_src_t::NONE;
		c.mtime = time_t{0};
		c.epg_types = {};
		auto &key = *mux_key_ptr(stream_mux);
		key.network_id = key.ts_id = key.extra_id = 0;
		auto locked = update_reader_mux_parameters_from_frontend(stream_mux);
		assert(c.tune_src != chdb::tune_src_t::TEMPLATE);
		if(!locked) {
			dterrorx("lock lost\n");
		}
		//at this stage we must be locked (si processing only starts after lock)
	}
	if(is_active ||is_template) { /*we  need to set the active status*/
		auto wtxn = receiver.chdb.wtxn();
		chdb::update_mux(wtxn, stream_mux, now,  m::flags{ (m::MUX_COMMON|m::MUX_KEY)/* & ~m::SCAN_STATUS*/},
										 true  /*ignore_key*/, false /*ignore_t2mi_pid*/, false /*must_exist*/);
		wtxn.commit();
	}
	assert( c.tune_src != chdb::tune_src_t::TEMPLATE);
	reader->on_stream_mux_change(stream_mux);
}

void active_si_stream_t::init(scan_target_t scan_target_) {
	log4cxx::NDC(name());
	if (is_open())
		deactivate(false /*tune_failed*/);
	init_scanning(scan_target_);
	active_stream_t::open(dtdemux::ts_stream_t::PAT_PID, &active_adapter().tuner_thread.epx,
												EPOLLIN | EPOLLERR | EPOLLHUP);
	fix_tune_mux_template();
	auto stream_mux = reader->stream_mux();
	auto* mux_common = chdb::mux_common_ptr(stream_mux);
	assert( mux_common->tune_src != chdb::tune_src_t::TEMPLATE);
	scan_in_progress = (mux_common->scan_status == chdb::scan_status_t::ACTIVE);
	assert(!scan_in_progress || mux_common->scan_id>0);
	dtdebug("scan_done= " << (int) scan_done << " " << stream_mux);
	bool is_freesat_main = chdb::has_epg_type(stream_mux, chdb::epg_type_t::FSTHOME);
	bool is_skyuk = chdb::has_epg_type(stream_mux, chdb::epg_type_t::SKYUK);
	bool has_movistar = chdb::has_epg_type(stream_mux, chdb::epg_type_t::MOVISTAR);
	bool has_viasat_baltic = chdb::has_epg_type(stream_mux, chdb::epg_type_t::VIASAT);

	bool has_freesat = chdb::has_epg_type(stream_mux, chdb::epg_type_t::FREESAT);

	bool do_standard = true;
	bool do_bat = false;
	bool do_epg = false;
	bool need_other = true;

	scan_state.reset();
	switch(scan_target) {
	case  scan_target_t::SCAN_MINIMAL: { //PAT, SDT_ACTUAL, NIT_ACTUAL during limited time
		need_other = false;
	}
		break;
	case scan_target_t::SCAN_FULL: { //PAT, SDT_ACTUAL, NIT_ACTUAL, BAT, SDT_OTHER, PMT during limited time
		need_other = true;
		do_bat = true;
	}
		break;
	case scan_target_t::SCAN_FULL_AND_EPG: { /*
																						 PAT, SDT_ACTUAL, NIT_ACTUAL, BAT, SDT_OTHER, PMT, EPG for ever
																						 used during normal viewing
																					 */
		need_other = true;
		do_epg = true;
		do_bat = true;
	}
		break;
	default:
		break;
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
	auto k = this->stream_mux_key();
	if (k.sat_pos == sat_pos_dvbc || k.sat_pos == sat_pos_dvbt) {
		tune_confirmation.sat_by = confirmed_by_t::AUTO;
	}
	{
		auto m=reader->stream_mux();
		assert(mux_common_ptr(m)->tune_src != chdb::tune_src_t::TEMPLATE);
	}
}


/*
	will_retune => scan must be set to the failed state
 */
void active_si_stream_t::scan_report() {
	if (!is_open())
		return;
	auto now_ = steady_clock_t::now();
	if (now_ - scan_state.last_update_time <= 2s)
		return;
	scan_state.last_update_time = now_;

	if (scan_done)
		return;

	bool no_data = reader->no_data();

	// first handle the case of no data
	if (no_data && !tune_confirmation.si_done) {
		dtdebug("No data on this mux; setting si_done = true");
		auto txn = chdb.wtxn();
		auto tuned_key = this->stream_mux_key();

		/*
			If a mux exists with a similar enough frequency, its tuning parameters will be updated.
			If no such mux exists one will be created with network_id=ts_id=0
		*/
		add_fake_nit(txn, 0, 0, tuned_key.sat_pos, false /*from_sdt*/);
		assert(tune_confirmation.sat_by == confirmed_by_t::NONE || tune_confirmation.sat_by == confirmed_by_t::AUTO);
		tune_confirmation.sat_by = confirmed_by_t::FAKE;
		txn.commit();
		tune_confirmation.si_done = true;
		reader->update_stream_mux_tune_confirmation(tune_confirmation);
		// scan_target = scan_target_t::DONE;
		dtdebug("setting scan_done=true mux="  << reader->stream_mux());
		scan_done = true;
		return;
	}

	auto done = scan_state.scan_done();

	if (!done && !scan_state.aborted)
		return;
	if (done) {
		dtdebug("SCAN DONE:" << reader->stream_mux());
		auto active_mux = mux_common_ptr(reader->stream_mux())->scan_status == chdb::scan_status_t::ACTIVE;
		assert(!active_mux ||  mux_common_ptr(reader->stream_mux())->scan_id > 0);
		dtdebug("setting scan_done=true mux=" << reader->stream_mux() << " " << (active_mux? "ACTIVE" :"NOT ACTIVE"));
		scan_done = true;
		tune_confirmation.si_done = true;
		reader->update_stream_mux_tune_confirmation(tune_confirmation);
	}
	finalize_scan(done, false /*tune_failed*/);
}

/*
	tune_failed=true when tune failed due to unsupported tune parameters,
	as opposed to not having locked
 */
void active_si_stream_t::finalize_scan(bool done, bool tune_failed)
{
	scan_done = true;
	// now update the mux's scan state
	ss::string<32> s;
	auto mux = reader->stream_mux();
	to_str(s, mux);
	auto* mux_common = chdb::mux_common_ptr(mux);
	if(scan_state.temp_tune_failure) {
		dtdebug("finalize_scan NOSAVE temp_tune_failure scan_in_progress=" << scan_in_progress << " " << mux);
		auto scan_start_time = receiver.scan_start_time();
		call_scan_mux_end  = (scan_in_progress && scan_start_time >=0);
		return;
	}
	if(! scan_state.locked && chdb::is_template(mux)) {
		dtdebug("finalize_scan NOSAVE scan_in_progress=" << scan_in_progress << " " << mux);
		auto scan_start_time = receiver.scan_start_time();
		call_scan_mux_end  = (scan_in_progress && scan_start_time >=0);
		return;
	}
	dtdebug("finalize_scan scan_in_progress=" << scan_in_progress << " " << mux <<
					" tuned_nit_key=" << (tune_confirmation.nit_actual_mux_key ?
					*tune_confirmation.nit_actual_mux_key : chdb::mux_key_t{})
					<< " tuned_sdt_key=" << (tune_confirmation.sdt_actual_mux_key ?
					*tune_confirmation.sdt_actual_mux_key : chdb::mux_key_t{}));
	if (scan_state.aborted)
		mux_common->scan_result = chdb::scan_result_t::ABORTED;
	else if (is_embedded_si ? active_adapter().si.scan_state.locked : scan_state.locked) {
		if(scan_state.is_not_ts) {
			mux_common->scan_result = chdb::scan_result_t::NOTS;
		} else {
			mux_common->scan_result = scan_state.scan_completed() ? chdb::scan_result_t::OK : chdb::scan_result_t::PARTIAL;
		}
		if(mux_common->tune_src == chdb::tune_src_t::UNKNOWN || mux_common->tune_src == chdb::tune_src_t::AUTO
			 || mux_common->tune_src == chdb::tune_src_t::TEMPLATE)
			mux_common->tune_src = chdb::tune_src_t::UNKNOWN;
	} else {
		/*
			init_si was never called, so we need to recompute scan_in_progress
		*/
		scan_in_progress = (mux_common->scan_status == chdb::scan_status_t::ACTIVE);
		mux_common->scan_result = tune_failed? chdb::scan_result_t::BAD : chdb::scan_result_t::NOLOCK;
		mux_common->tune_src = chdb::tune_src_t::AUTO;
	}
	dtdebug("SET IDLE " << mux);
	dttime_init();
	mux_common->scan_status = chdb::scan_status_t::IDLE;
	mux_common->scan_duration = scan_state.scan_duration();
	mux_common->scan_time = system_clock_t::to_time_t(now);
	if(mux_common->scan_duration == 0) { //needed in case tuning fails
		mux_common->scan_duration =
			std::chrono::duration_cast<std::chrono::seconds>(now - active_adapter().tune_start_time).count();
	}
	reader->on_stream_mux_change(mux); //needed to ensure that mux_common changes are not overwritten in save_pmts
	dttime(200);
	auto wtxn = chdb.wtxn();
	dttime(200);
	if(mux_common->key_src == chdb::key_src_t::NONE || (nit_actual_notpresent() && sdt_actual_notpresent()))
		update_stream_ids_from_pat(wtxn, mux);
	else {
		namespace m = chdb::update_mux_preserve_t;
		this->update_mux(wtxn, mux, now, true /*is_reader_mux*/, false /*from_sdt*/, m::MUX_KEY /*preserve*/);
	}
	dttime(200);
	if (nit_actual_done() || nit_actual_notpresent() || done) {
		if(pmts_can_be_saved())
			save_pmts(wtxn);
	}
	wtxn.commit();
	auto scan_start_time = receiver.scan_start_time();
	call_scan_mux_end  = (scan_in_progress && scan_start_time >=0);
	dttime(200);
}


int active_si_stream_t::deactivate(bool tune_failed) {
	if (!is_open()) {
		this->reset(false, tune_failed);
		return 0;
	}
	log4cxx::NDC(name());
	int ret = 0;
	dtdebugx("deactivate si stream");
	// active_stream_t::close();
	stream_parser.exit(); // remove all fibers
	parsers.clear();			// remove all parser data (parsers will be reregistered)
	close();
	this->reset(true, tune_failed);
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

dtdemux::reset_type_t active_si_stream_t::pat_section_cb(const pat_services_t& pat_services,
																												 const subtable_info_t& info) {
	auto cidx = scan_state_t::PAT;
	tune_confirmation.unstable_sat = false;
	if (info.timedout) {
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
	tune_confirmation.pat_received = true;
	if (this_table_done) {
		active_adapter().on_first_pat();
		pat_table.ts_id = pat_services.ts_id;
		pat_table.entries = pat_services.entries;
		if (pat_table.last_entries.size() != 0 && pat_table.last_entries != pat_table.entries) {
			dtdebugx("PAT is unstable; force retune");
			tune_confirmation.unstable_sat = true;
			return dtdemux::reset_type_t::ABORT; // unstable PAT; must retune
		}
		if (!is_embedded_si && !pat_data.stable_pat(pat_services.ts_id)) {
			//dtdebug("PAT not stable yet");
			pat_table.num_sections_processed = 0;
			return dtdemux::reset_type_t::RESET; // need to check again for stability
		} else
			pat_data.stable_pat(pat_services.ts_id); //cause timer to be updated
		dtdebugx("PAT found");

		//if (scan_target == scan_target_t::SCAN_FULL || scan_target == scan_target_t::SCAN_FULL_AND_EPG)
		for (auto& s : pat_table.entries) {
			if (!is_embedded_si && s.service_id != 0x0 /*skip pat*/)
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

dtdemux::reset_type_t active_si_stream_t::on_nit_section_completion(
	db_txn& wtxn, network_data_t& network_data, dtdemux::reset_type_t ret, bool is_actual,
	bool on_wrong_sat, bool done)
{
	if (done) {
		if (!is_embedded_si && tune_confirmation.sat_by == confirmed_by_t::NONE && !pat_data.stable_pat()) {
			/*It is too soon to decide we are on the right/wrong sat;
				force nit_actual rescanning
			*/
			network_data.reset();
			return dtdemux::reset_type_t::RESET;
		}

		if (network_data.num_muxes == 0 && sdt_actual_done() && tune_confirmation.ts_id_by != confirmed_by_t::NONE) {
				// we cannot check the sat_pos, so we assume it is ok.

			tune_confirmation.sat_by = confirmed_by_t::TIMEOUT;
			if (is_actual) {
				tune_confirmation.nit_actual_received = true;
					dtdebugx("Setting nit_actual_ok = true");
			}
		} else if (tune_confirmation.sat_by == confirmed_by_t::NONE && is_actual) {
			dterror("ni_actual does not contain current mux");
			/*NOTE: pat is stable at this point
			 */
			auto sat_count = nit_data.nit_actual_sat_positions.size();
			dtdebugx("NIT_ACTUAL does not contain currently tuned mux; nit_actual contains %d sat_positions", sat_count);
			auto sat_pos = sat_count >=1 ? nit_data.nit_actual_sat_positions[0]: sat_pos_none;
			/*
				all nit_actual muxes are for the same sat, and we assume it is correct
			*/
			tune_confirmation.on_wrong_sat = on_wrong_sat;
			if (abort_on_wrong_sat()) {
				/*
					This is a regular tune (not a band scan), so we must trigger a retune.
					@todo: 10930V 39.0E actually contains 30E. So we must devise an override in the database.
					otherwie we will never be able to tune this mux
					*/
				dtdebugx("NIT_ACTUAL only contains sat_pos=%d, which is very unexpected. Asking retune", sat_pos);
				return dtdemux::reset_type_t::ABORT;
			}
			if(sat_pos == sat_pos_none) {
				dtdebugx("NIT_ACTUAL does not contain any sat_pos");
			} else {
				dtdebugx("NIT_ACTUAL only contains sat_pos=%d (no retune allowed). confirming sat_pos", sat_pos);
				auto stream_mux = reader->stream_mux(); //copy!
				auto* stream_mux_key = mux_key_ptr(stream_mux);
				auto* p_mux_data = tuned_mux_in_nit();
				bool is_wrong_dvb_type = dvb_type(sat_pos) != dvb_type(stream_mux_key->sat_pos);
				if(! is_wrong_dvb_type) {
					if(p_mux_data) {
						assert(*stream_mux_key == p_mux_data->mux_key);
					} else {
						stream_mux_key->sat_pos = sat_pos_none;
					}
					reader->on_stream_mux_change(stream_mux);
				}
				tune_confirmation.sat_by = confirmed_by_t::TIMEOUT;
			}
			tune_confirmation.nit_actual_received = true;
			dtdebugx("Setting nit_actual_ok = true");
			return dtdemux::reset_type_t::NO_RESET;
		}
		// do anything needed after a network has been fully loaded
		if (is_actual) {
			tune_confirmation.nit_actual_received = true;
			dtdebugx("Setting nit_actual_ok = true");
		}
	} else { //! done: more nit_actual or nit_other data is coming
		if (is_actual)
			tune_confirmation.nit_actual_seen = true;
		//tune_confirmation.nit_actual_ok = true;
	}
	return ret;
}

/*
	nit_actual processing serves multiple purposes
	-detecting if we are on the currect sat (dish may be detuned, or diseqc swicth malfunctions)
	-detecting if we are tuned to the right mux (lnb-22kHz wrong, or polarisation wrong)
	-updating the exact frequency and other tuning parameters
  -updating the unique mux key in case we discover it is wrong

	The following cases must be considered
	1. tuner does not lock. In this case si processing si not started and spectrum/blind scan continues
	2. there is no nit data on this mux
  	->action: after a timeout, assume that tune data from tuner is correct. Also assume sat_pos is correct.
   	This is handled in scan_report (all other cases are handled in the nit_actual_section_cb routines
   	or in check_timeouts)
	3. there is data in nit_actual, but none of it agrees with the tuned frequency and/or the tuned sat
	  -possible reasons: empty nit_actual,  nit_actual is for dvb-t (e.g., 5.0W French streams),
	   nit_actual contains the mux but frequency is wrong (4.0W 12353 reports 14.5 GHz)
	  ->action: after a timeout, assume that data from tuner is correct. Also assume sat_pos is correct
	4. there is data in nit_actual, it agrees with the tuned frequency, but the tuned sat_pos is incorrect
  	-reasons: dish is still moving and we picked up a transient signal, reception from  a nearby sat,
	   failed diseqc swicth has brught us to the same sat
	  ->action: restart nit_actual_processing and see if the result is stable; if it is not stable, then the dish is
    	probablly moving and the problem will disappear. If the result is stable, then distinghuish between
    	4.a. the difference in sat_pos is small (less than a degree).
	    ->action: assume that the data in nit_actual is correct, update tuned_mux and similar data structures in tuner and
    	fe_monitor threads
	    4.b. the difference in sat_pos is large: retune
	    -><action: assume that the data in nit_actual is correct, update tuned_mux and similar data structures in tuner and
			fe_monitor threads

	Notes:
	-never trust nit_actual data which does not agree (up to small differences in sat_pos and frequency)
	with the tuning parameters. Also, trust small differences only after proof that the result is stable

	-the "satellite stability check" will be skipped if sdt_actual confirms the mux, which it des by comparing
	services (speculatively assuming that tuning data is correct, to select a suitable mux_key to look up
	the services) to earlier found services in the database.


*/

dtdemux::reset_type_t active_si_stream_t::nit_section_cb_(nit_network_t& network, const subtable_info_t& info)
{
	namespace m = chdb::update_mux_preserve_t;
	auto cidx = network.is_actual ? scan_state_t::NIT_ACTUAL : scan_state_t::NIT_OTHER;
	if (info.timedout) {
		scan_state.set_timedout(cidx);
		return dtdemux::reset_type_t::NO_RESET;
	} else {
		bool was_active = scan_state.set_active(cidx);
		if (!was_active && network.is_actual) {
			dtdebug("First NIT_ACTUAL data tuned_mux=" << reader->stream_mux());
			reader->update_bad_received_si_mux(std::optional<chdb::any_mux_t>{});
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
	auto stream_mux = reader->stream_mux(); //the mux whose SI data is being analyzed here
	auto* stream_mux_key = mux_key_ptr(stream_mux);

	auto wtxn = chdb_txn();
	if (network.network_name.size())
		save_network(wtxn, network, stream_mux_key->sat_pos); // TODO: sat_pos is meaningless; network can be on multiple sats
	using namespace chdb;
	dtdemux::reset_type_t ret = dtdemux::reset_type_t::NO_RESET;

	for (auto& mux : network.muxes) {
		auto* mux_key = mux_key_ptr(mux);
		bool is_wrong_dvb_type = dvb_type(mux_key->sat_pos) != dvb_type(stream_mux_key->sat_pos);
		if(is_wrong_dvb_type)
			continue;
		/*
			!can_be_tuned means that the frequency is out of range for the current lnb

			This means that we ignore DVBT and DVBC muxes references in a dvbs nit_actual
			and also C-band muxes referenced in dvbs nit_actual
		 */
		bool can_be_tuned = fix_mux(mux);
		bool is_tuned_freq = matches_physical_fuzzy(mux, stream_mux, false /*check_sat_pos*/); //correct pol, stream_id, t2mid_pid, frequency; sat_pos may be off
		bool is_active_mux = this->matches_reader_mux(mux) && network.is_actual;
		/* update database: tune_src, mux_key, tuning parameters;
			 perform overall database changes when mux_key changes
			 check and fix modulation parameters by consulting driver
			 insert/update mux in nit_data.by_network_id_ts_id
			 insert mux in nit_data.get_original_network
			 add sat entry if none present yet
			 updates reader->current_mux
		 */

		bool bad_sid_mux = is_tuned_freq && ! is_active_mux; //not tsid in pat or tsid differs from the one in reader_mux
		if(bad_sid_mux)
			reader->update_bad_received_si_mux(mux); //store the bad mux

		if (!can_be_tuned) {
			continue;
		}
		if(can_be_tuned)
			add_mux(wtxn, mux, network.is_actual, is_active_mux, is_tuned_freq, false /*from_sdt*/);
		if (network.is_actual) {
			p_network_data->num_muxes++;
			ret = nit_actual_update_tune_confirmation(mux, is_active_mux);
			if (ret != dtdemux::reset_type_t::NO_RESET) {
				wtxn.abort();
				network_data.reset();
				return ret; // definitely on wrong sat
			}
			tune_confirmation.nit_actual_mux_key = *mux_key;
		}
	}
	bool done = nit_data.update_nit_completion(scan_state, info, network_data);
	auto sat_pos = nit_data.nit_actual_sat_positions.size()>=1 ? nit_data.nit_actual_sat_positions[0] : sat_pos_none;
	bool is_wrong_dvb_type = dvb_type(sat_pos) != dvb_type(stream_mux);
	bool on_wrong_sat = !is_wrong_dvb_type //ignore dvbt/dvbc in dvbs muxes for example
		&& sat_pos != sat_pos_none && std::abs(sat_pos - stream_mux_key->sat_pos) > sat_pos_tolerance;

	ret = on_nit_section_completion(wtxn, network_data, ret, network.is_actual, on_wrong_sat, done);
	if(ret== dtdemux::reset_type_t::RESET ||
		 ret == dtdemux::reset_type_t::ABORT
		) {
		wtxn.abort();
		return ret;
	}

	if (done && network.is_actual) { // for nit other, there may be multiple entries
		dtdebugx("NIT_ACTUAL completed");
		{
			auto stream_mux = reader->stream_mux();
			assert(!chdb::is_template(stream_mux));
			assert(chdb::mux_key_ptr(stream_mux)->extra_id >0 ||
						 chdb::mux_common_ptr(stream_mux)->key_src == key_src_t::NONE);
		}
		if(tune_confirmation.sat_by == confirmed_by_t::NONE)
			tune_confirmation.sat_by = confirmed_by_t::TIMEOUT;

		scan_state.set_completed(cidx); //signal that nit_actual has been stored
		tune_confirmation.nit_actual_received = true;
		if(pmts_can_be_saved())
			save_pmts(wtxn);
	}
	wtxn.commit();
	return ret;
}


/*
	fixes some ad hoc known errors on various muxes
	Returns true if the mux can be tuned with the current lnb type, which means that
	frequency and polarisation are compatible with the lnb after the fixes

	sat_pos must also be close to the currently tuned on, or we refuse to consider this mux.
	This prevents  pollutng the database because of bad information on a mux, at the expense
	of missing some data for other sats

 */
bool active_si_stream_t::fix_mux(chdb::any_mux_t& mux)
{
	bool can_be_tuned{true};
	auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
	if(!dvbs_mux)
		return can_be_tuned; //nothing to fix (yet)
	auto tuned_mux = reader->stream_mux();
	auto* mux_key = chdb::mux_key_ptr(mux);
	auto tuned_mux_key = mux_key_ptr(tuned_mux);

	if(mux_key->sat_pos == -4200 && tuned_mux_key->sat_pos == 4200) {
		//hack for 42.0W on which 42E is reported as 42W
			mux_key->sat_pos = 4200;
	}

	const bool disregard_networks{true};
	if (dvbs_mux && !devdb::lnb_can_tune_to_mux(active_adapter().current_lnb(), *dvbs_mux, disregard_networks)) {
		auto tmp = *dvbs_mux;
		if(tmp.frequency == 0) {
			if(pat_data.has_ts_id(tmp.k.ts_id)) {
					//happens on 26.0E: 12034H and 14.0W: 11623V, 14.0W: 11638H
				dtdebug("Fixing zero frequency: " << mux);
				tmp = *std::get_if<chdb::dvbs_mux_t>(&tuned_mux);
				tmp.k.ts_id = dvbs_mux->k.ts_id;
				tmp.k.network_id = dvbs_mux->k.network_id;
				if(std::abs(dvbs_mux->k.sat_pos - tmp.k.sat_pos) <= sat_pos_tolerance)
					tmp.k.sat_pos = dvbs_mux->k.sat_pos;
				can_be_tuned = true;
				*dvbs_mux = tmp;
				return can_be_tuned;
			}
		}
		if (tmp.pol == chdb::fe_polarisation_t::H)
			tmp.pol = chdb::fe_polarisation_t::L;
		else if (tmp.pol == chdb::fe_polarisation_t::V)
			tmp.pol = chdb::fe_polarisation_t::R;
		else if (tmp.pol == chdb::fe_polarisation_t::L)
			tmp.pol = chdb::fe_polarisation_t::H;
		else if (tmp.pol == chdb::fe_polarisation_t::R)
			tmp.pol = chdb::fe_polarisation_t::V;
		bool can_be_tuned_with_pol_change =
			devdb::lnb_can_tune_to_mux(active_adapter().current_lnb(), tmp, disregard_networks);
		if(can_be_tuned_with_pol_change) {//happens on 20E:  4.18150L which claims "H"
			dtdebug("Found a mux which differs in circular/linear polarisation: " << mux);
			*dvbs_mux = tmp;
			can_be_tuned = true;
		}
		else {
			dtdebug("Refusing to insert a mux which cannot be tuned: " << mux);
			can_be_tuned = false;
		}
	}
	if (can_be_tuned) {
		// update_mux_ret_t r;
		if(mux_key->network_id==65 && mux_key->ts_id == 65 && dvbs_mux->pol ==  chdb::fe_polarisation_t::L
			 && dvbs_mux->k.sat_pos == -2200 ) {
			//reuters on 22W 4026R reports the wrong polarisation
			dvbs_mux->pol =  chdb::fe_polarisation_t::R;
		}
		can_be_tuned = std::abs((int) mux_key->sat_pos - (int) tuned_mux_key->sat_pos) <= sat_pos_tolerance;
	}
	return can_be_tuned;
}

dtdemux::reset_type_t active_si_stream_t::nit_section_cb(nit_network_t& network, const subtable_info_t& info) {
	if(!is_embedded_si && !pat_data.stable_pat()) {
		dtdebugx("Waiting for pat to stabilize");
		return dtdemux::reset_type_t::RESET;
	}
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
		dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: ",
						 p_mux_data->mux_key.sat_pos, p_mux_data->mux_key.network_id, p_mux_data->mux_key.ts_id);
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
	returns update_mux_ret, is_active_mux
*/
dtdemux::reset_type_t
active_si_stream_t::nit_actual_update_tune_confirmation(chdb::any_mux_t& mux, bool is_active_mux) {
	using namespace chdb;
	auto ret = dtdemux::reset_type_t::NO_RESET;
	using namespace chdb;
	auto tuned_mux = reader->stream_mux();
	auto* tuned_mux_key = mux_key_ptr(tuned_mux);
	auto* mux_key = mux_key_ptr(mux);
	bool is_wrong_dvb_type = dvb_type(mux) != dvb_type(tuned_mux);
	tune_confirmation.on_wrong_sat = false; // clear earlier decision
	/*
		on_wrong_sat:
		tuned to the right frequency and polarisation (matches, sat_pos is wrong (leading to !is_active_mux)
	*/
	bool on_wrong_sat = !is_wrong_dvb_type //ignore dvbt/dvbc in dvbs muxes for example
		&& (std::abs((int)mux_key->sat_pos - (int)tuned_mux_key->sat_pos) > sat_pos_tolerance);
	if (is_wrong_dvb_type)
		return  dtdemux::reset_type_t::NO_RESET;
	if (on_wrong_sat) {
		if (is_embedded_si || pat_data.stable_pat()) {
			//permanently on wrong sat (dish stopped moving)
			dtdebug("sat_pos is wrong but pat is stable.");
				is_active_mux = true;
				tune_confirmation.on_wrong_sat = std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= 100;
				if (abort_on_wrong_sat())
					ret = dtdemux::reset_type_t::ABORT;
				else
					ret = dtdemux::reset_type_t::NO_RESET;
			} else {
			//temporarily on wrong sat (dish still moving)
				ret = dtdemux::reset_type_t::RESET;
		}
		if(ret != dtdemux::reset_type_t::NO_RESET)
			return ret;
	}
	/* At this point, sat_pos is stable and if it is incorrect, it is because the
		 user does not want automatic retuning and  nothing can be done about it,
		 so we may as well continue. dvb_type is the correct one
	*/


	if (is_active_mux) {
		/*if is_active_mux=true, then we already know that the sat_pos matches within a tolerance,
			otherwise we need to check if we are on the wrong sat
		*/
		dtdebugx("NIT CONFIRMS sat=%d network_id=%d ts_id=%d", mux_key->sat_pos, mux_key->network_id, mux_key->ts_id);
		if (tune_confirmation.sat_by == confirmed_by_t::NONE)
			tune_confirmation.sat_by = confirmed_by_t::NIT;

		if (tune_confirmation.ts_id_by == confirmed_by_t::NONE ||
				tune_confirmation.network_id_by == confirmed_by_t::NONE) {
			tune_confirmation.ts_id_by = confirmed_by_t::NIT;
			tune_confirmation.network_id_by = confirmed_by_t::NIT;
			sdt_data.actual_network_id = mux_key->network_id;
			sdt_data.actual_ts_id = mux_key->ts_id;
		}
	} else {
		namespace m = chdb::update_mux_preserve_t;
		/*to avoid cases where another another mux provides wrong network_id, ts_id
			network_id, ts_id for the tuned mux is considered authorative
		*/
		if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
			assert(std::abs(tuned_mux_key->sat_pos - mux_key->sat_pos) <= sat_pos_tolerance ||
						 !on_wrong_sat);
			dtdebugx("NIT CONFIRMS sat=%d", mux_key->sat_pos);
			tune_confirmation.sat_by = confirmed_by_t::NIT;
		}
	}
	return ret;
}

/*
	mux : typically an SI mux. Usually its key cannot be relied upon
	is_reader_mux: when true, we know for sure that we are processing si data retrieved for this mux (this is
	either the tuned mux or an embedded t2mi mux).

	from_sdt:
	 *if false, then the code looks for a mux with matching frequency, prefering
	  one with also a matching key. If no matching key is found, but another mux wth matching frequency but differing
	  key, then the old key is replaced with the new one.
		It also propagates scan states

	 *if true, then the code looks for a mux with matching frequency, requiring a matching key. If no matching key
    is found, then a new mux is inserted. No scan satets are propagates

	Note that in exceptional cases, multiple muxes may be considered
	as is_reader_mux. For instance when SI data for SDT and NIT disagree on network_id because of incorrect SI data
	In this case, muxes discovered in NIT are inserted with from_sdt=false, but
	muxes discovered in SDT are inserted with dont_change_existing_keys=true

	returns true if mux was updated
 */
bool active_si_stream_t::update_mux(
	db_txn& chdb_wtxn, chdb::any_mux_t& mux, system_time_t now,
	bool is_reader_mux, bool from_sdt, chdb::update_mux_preserve_t::flags preserve) {
	using namespace chdb;
	namespace m = chdb::update_mux_preserve_t;
	auto reader_mux = reader->stream_mux();

	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);

	bool is_wrong_dvb_type = dvb_type(mux) != dvb_type(reader_mux);
	bool on_wrong_sat = !is_wrong_dvb_type //ignore dvbt/dvbc in dvbs muxes for example
		&& std::abs(mux_key_ptr(reader_mux)->sat_pos - mux_key_ptr(mux)->sat_pos) > sat_pos_tolerance;
	if(on_wrong_sat)
		return false;

	auto& c =  *mux_common_ptr(reader_mux);
	bool reader_mux_is_scanning = c.scan_status == scan_status_t::ACTIVE;
	bool propagate_scan = reader->tune_options().propagate_scan;
	auto scan_id = c.scan_id;
	assert(!reader_mux_is_scanning || scan_id>0);
	auto scan_start_time = receiver.scan_start_time();
	auto saved = mux;

	if(is_reader_mux && ! is_embedded_si) {
		//fixes things like modulation in case nit provides incorrect data
		assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
					 chdb::mux_common_ptr(mux)->scan_id >0);

		/*after receiving an sdt_actual mux, correct some obviously bad data in a mux,
			based on what driver reports basically: always trust the tuning parameters that
			were used to successfully tune, except for frequency and symbolrate (the values
			reported by the driver can be slightly off)

			Note that reader_mux already has been updated with tuning data in case the mux was a template
			but there is always a risk that due to a non-lock status, this was not performed
		*/

		update_reader_mux_parameters_from_frontend(mux);
		assert(chdb::mux_common_ptr(mux)->tune_src != chdb::tune_src_t::TEMPLATE);
		assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
					 chdb::mux_common_ptr(mux)->scan_id >0);

	}

	auto cb = [&](chdb::mux_common_t* pdbc, const chdb::mux_key_t* pdbk) {
		//the mux we are updating is also the reader_mux, which is where si data comes from (e.g., the tuned mux)
		auto* pc = mux_common_ptr(mux);
		auto tmp = *chdb::mux_key_ptr(mux);
		if(pdbk) {
			tmp.extra_id = pdbk->extra_id; //at this stage extra_id may not yet have been copied
			if(is_reader_mux && pdbc && (
					 pdbc->key_src == chdb::key_src_t::SDT_TUNED ||
					 pdbc->key_src == chdb::key_src_t::PAT_TUNED
					 )) {
				*mux_key_ptr(mux) = *pdbk; //SDT and pat have priority in setting key
				pc->key_src = pdbc->key_src;
			}
			else if (*pdbk != tmp) {
				if(!is_reader_mux && pdbc->scan_status == chdb::scan_status_t::ACTIVE) {
					dtdebug("NOT changing key on active mux on other adapter; changing to IDLE; mux=" << mux);
					pc->scan_status = chdb::scan_status_t::IDLE; /*make the local copy of the mux idle to avoid dangling
																												 ACTIVE states in case of races; leave database alone*/
					return false;
				}

				/* when detecting a key change we need to adjust services
					 and perform various other cleaning
				*/
				dtdebug("database key change detected: si-reader-mux: db="<< mux << " db=" << *pdbk);
				if(pdbc->tune_src ==  tune_src_t::USER)
					*mux_key_ptr(mux) = *pdbk;
				else if (!(preserve & update_mux_preserve_t::MUX_KEY)) {
					//fix services and other non-mux data
					chdb::on_mux_key_change(chdb_wtxn, *pdbk, mux, now);
					{
						auto devdb_wtxn = receiver.devdb.wtxn();
						auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
						if(dvbs_mux) {
							devdb::lnb::on_mux_key_change(devdb_wtxn, *pdbk, *dvbs_mux, now);
						}
						devdb_wtxn.commit();
					}
				}
			} else
				*chdb::mux_key_ptr(mux) = tmp; //copy extra_id
		} else {
		}
		if (is_reader_mux //this is the mux generating the si data; don't change its own scan_status
				|| !reader_mux_is_scanning //we are not scanning
				|| !propagate_scan //tune_options request no setting of pending states on discovered muxes
				|| (pdbc && (
							pdbc->scan_status == chdb::scan_status_t::ACTIVE //the mux is being scanned
							|| pdbc->scan_status == chdb::scan_status_t::PENDING //scanning already planned
							|| pdbc->scan_status == chdb::scan_status_t::RETRY //scanning already planned
							))
			)
			return true; //update the mux,
		//do not

		/*ensure that all found muxes are scanned in future, unless they have been scanned already
			in the current run*/
		//do not scan transponders on the wrong sat
		if((!pdbc || pdbc->mtime < scan_start_time) && ! on_wrong_sat) {
			dtdebug("SET PENDING " << mux);
			pc->scan_status = scan_status_t::PENDING;
			assert(scan_id > 0);
			pc->scan_id = scan_id;
			if(pdbc) {
				pdbc->scan_status = scan_status_t::PENDING;
				pdbc->scan_id = scan_id;
			}
			assert((pc->scan_status != chdb::scan_status_t::ACTIVE &&
							pc->scan_status != chdb::scan_status_t::PENDING &&
							pc->scan_status != chdb::scan_status_t::RETRY) ||
						 pc->scan_id >0);
		}

		return true;
	};
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	/*
		The following will first attempt to find a mux in the database with
     -matching frequency, pol, sat_pos AND stream_id
		 -with the correct key (if one exists) or with a key that
		  differs only in extra_id (which allows extra_id==0), or failing that a mux witout matching key but
			still matching frequency, pol, sat_pos AND stream_id
		In case the found db_mux  differs in key, db_mux will be deleted before entering the callback function,
		also in this case mux.k.extra_id is changed to a valid value if needed

		Then it will call update_scan_status, which can compate the found mux (if any) to check if the
		change involves a key change and/or to adjust mux_common

	 */

	/*
		In rare cases, SDT and NIT disagree on network_id for reader_mux.
		In this case we do not want the database to constantly switch between the two versions, depening on the order in which
		SDT and NIT are received.

		So, when  tune_confirmation.network_id_by != confirmed_by_t::NONE, we have already entered one of the muxes (we are
		processing the 2nd SI table (SDT or NIT) now).
		In this case we set must_exist=true and add m::MUX_KEY to preserve, which causes update_mux to return NO_MATCHING KEY
		instead of changing the mux we have inserted when the first table arrived.

		We then just add the 2nd active mux.

		So the sequence (starting from an empty db is)
		SDT or NIT arrives with nid=0/1 and ts_id=48 -> record (0/1,48) is inserted
		NIT or SDT arrives with nid=1/0 and ts_id=48 -> update_mux returns m::MUX_KEY; we insert the record(1/0,48)

		If we start from a bad db which contains e.g. record(2,48) then:
		SDT or NIT arrives with nid=0/1 and ts_id=48 -> record(2,48) is replaced by record (0/1,48)
		NIT or SDT arrives with nid=1/0 and ts_id=48 -> update_mux returns m::MUX_KEY; we insert the record(1/0,48)

		In future tunes:
		SDT or NIT arrives with nid=0/1 and ts_id =48 -> record (0/1,48) is discovered in db


	*/
	bool must_exist{false};

	if(from_sdt) {
		auto *c = chdb::mux_common_ptr(mux);
		if(is_reader_mux) { //SDT_ACTUAL
			c->key_src = key_src_t::SDT_TUNED;
#ifndef NDEBUG
		auto testpreserve  = m::flags((m::SCAN_DATA | m::NIT_SI_DATA | m::TUNE_DATA));
#endif
		assert((preserve &testpreserve) == testpreserve);
		} else {
			c->key_src = key_src_t::SDT_OTHER;
#ifndef NDEBUG
			auto testpreserve  = m::ALL & ~m::NUM_SERVICES;
#endif
		assert((preserve &testpreserve) == testpreserve);
		}
		chdb::update_mux(chdb_wtxn, mux, now,  preserve, true /*ignore_key*/,
										 false /*ignore_t2mi_pid*/, false /*must_exist*/);
	} else {
		assert(chdb::mux_common_ptr(mux)->tune_src != chdb::tune_src_t::TEMPLATE);
		chdb::update_mux(chdb_wtxn, mux, now, preserve, cb, true /*ignore_key*/,
										 false /*ignore_t2mi_pid*/, must_exist /*must_exist*/);
	}
	if (is_reader_mux /*|| (is_reader_mux && *mux_key_ptr(reader_mux) != *mux_key_ptr(mux))*/) {
		if (true || from_sdt || mux_common_ptr(reader_mux)->key_src != key_src_t::SDT_TUNED) {
#ifdef DEBUG_CHECK
			debug_check(chdb_wtxn, reader->stream_mux(), mux);
#endif
		reader->on_stream_mux_change(mux);
		}
	}
	return true;
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

std::tuple<bool, bool>
active_si_stream_t::sdt_process_service(db_txn& wtxn, const chdb::service_t& service, mux_data_t* p_mux_data,
																				bool donotsave, bool is_actual) {
	chdb::mux_key_t& mux_key = p_mux_data->mux_key;
	auto c = chdb::service::find_by_mux_sid(wtxn, mux_key, service.k.service_id);
	bool db_found{false};
	bool changed{false};
	if (c.is_valid()) {
		db_found = true;
		auto ch = c.current();

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

		// we do not count mux_desc changes, as they do not relate (ony) to SDT
		if (ch.mux_desc != p_mux_data->mux_desc) {
			ch.mux_desc = p_mux_data->mux_desc;
			assert((int)strlen(ch.mux_desc.c_str()) == ch.mux_desc.size());
			changed = true;
		}
		if(is_actual) {
			auto& actual_services = sdt_data.actual_services;
			actual_services.push_back(ch);
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
				put_record(wtxn, ch);
			}
		}
	} else {
		auto ch = service;
		ch.mtime = system_clock_t::to_time_t(now);
		ch.k.mux = mux_key;
		ch.mux_desc = p_mux_data->mux_desc;
		assert((int)strlen(ch.mux_desc.c_str()) == ch.mux_desc.size());
		if (!donotsave) {
			dtdebug("SAVING new service " << ch);
			put_record(wtxn, ch);
		}
		if(is_actual) {
			auto& actual_services = sdt_data.actual_services;
			actual_services.push_back(ch);
		}
	}
	return {db_found, changed};
}

dtdemux::reset_type_t active_si_stream_t::sdt_section_cb_(db_txn& wtxn, const sdt_services_t& services,
																													const subtable_info_t& info, mux_data_t* p_mux_data) {
	bool is_actual = services.is_actual;
	assert(p_mux_data);
	chdb::mux_key_t& mux_key = p_mux_data->mux_key;

	assert(mux_key.ts_id == services.ts_id);
	auto reader_mux = reader->stream_mux();
	auto& service_ids = p_mux_data->service_ids;
	if (p_mux_data->sdt[is_actual].subtable_info.version_number != info.version_number) {
		// record which services have been found
		service_ids.clear();
		sdt_data.actual_services.clear();
		if (p_mux_data)
			nit_data.reset_sdt_completion(scan_state, info, *p_mux_data);

		p_mux_data->sdt[is_actual].subtable_info = info;
	}
	bool donotsave = false;
	if (!services.is_actual) {
				auto reader_mux_key = this->stream_mux_key();
		if (reader_mux_key.sat_pos != mux_key.sat_pos) {
			dtdebug("SDT_OTHER: ignore services for other sat: " << mux_key);
			donotsave = true;
		}
	}
	int db_found{0};
	int db_changed{0};

	for (auto& service : services.services) {
		assert(mux_key.ts_id == service.k.mux.ts_id);

		if (service.name.size() == 0) {
			// dtdebug("Skipping service without name "<< service.k);
			continue;
		}
		service_ids.push_back(service.k.service_id);

		auto [db_found_, changed] =
			sdt_process_service(wtxn, service, p_mux_data, donotsave, is_actual);
		db_found += db_found_;
		db_changed += changed;

	}

	if (services.has_freesat_home_epg)
		p_mux_data->has_freesat_home_epg = true;
	if (services.has_opentv_epg)
		p_mux_data->has_opentv_epg = true;

	if (is_actual) {
		// update current_tp in case ts_id or network_id have changed
		auto tuned_key = this->stream_mux_key();
		bool mux_key_changed = mux_key != tuned_key;
		bool must_reset = sdt_actual_check_confirmation(mux_key_changed, db_found - db_changed, p_mux_data);
		if (must_reset) {
			wtxn.abort();
			service_ids.clear();
			sdt_data.reset();
			return dtdemux::reset_type_t::RESET;
		}
		tune_confirmation.sdt_actual_mux_key = mux_key;
	}

	bool done_now = nit_data.update_sdt_completion(scan_state, info, *p_mux_data);
	tune_confirmation.sdt_actual_received = done_now;
	if (done_now) {
		if (!donotsave) {
			process_removed_services(wtxn, mux_key, service_ids);
			dtdebugx("Notifying SDT ACTUAL");
			auto& aa = active_adapter();
			receiver.receiver_thread.notify_sdt_actual(sdt_data, aa.fe.get());
		}

		//Save statistics
		chdb::any_mux_t mux;
		auto reader_mux = reader->stream_mux();
		bool donotsave_stats{false};
		if (is_actual) {
			mux = reader_mux;

			if (mux_key_ptr(reader_mux)->ts_id == mux_key.ts_id)
				dterrorx("Unexpected: mux_key_ptr(reader_mux)->ts_id != mux_key.ts_id: %d %d\n", mux_key_ptr(reader_mux)->ts_id, mux_key.ts_id);
		} else {
			// we need the full mux, so we need to load it from the db
			// we only update if we found exactly 1 mux; otherwise we better wait for nit_actual/nit_other to tell us the
			// correct sat
			auto r = chdb::find_mux_by_key(wtxn, p_mux_data->mux_key);
			if(!r) {
				/*happens on 30.0W 12476H. Nit actual contains ts_id=47 and ts_id=49 for almost the same frequency
				 */
				donotsave_stats = true;
				dtdebug("found services for which mux is no longer in the database: mux=" <<  p_mux_data->mux_key);
			} else
				mux = *r;
		}

		if (!donotsave_stats) {
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

			if (changed) { //update statistics
				namespace m = chdb::update_mux_preserve_t;
				dtdebug("Update mux " << mux << " tuned=" << reader->stream_mux());
				auto preserve = is_actual
					? m::flags{m::ALL & ~(m::NUM_SERVICES | m::EPG_TYPES | m::MUX_KEY)}
					: m::ALL; //then only new records will be created, but nothing will be updated
				bool is_reader_mux = this->matches_reader_mux(mux);
				this->update_mux(wtxn, mux, now, is_reader_mux, true /*from_sdt*/, preserve /*preserve*/);
			}
		}
		if (is_actual) {
			auto cidx = scan_state_t::SDT_ACTUAL;
			scan_state.set_completed(cidx);
		}
	}
	if (donotsave) {
		wtxn.abort();
		service_ids.clear();
		sdt_data.actual_services.clear();
	} else {
		wtxn.commit();
	}
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

	dttime_init();

	auto stream_mux = reader->stream_mux();
	auto stream_mux_key = mux_key_ptr(stream_mux);

	auto epg_type = epg.epg_type;

	auto epg_source = epgdb::epg_source_t((epgdb::epg_type_t)(int)epg_type, info.table_id, info.version_number,
																				stream_mux_key->sat_pos, stream_mux_key->network_id, stream_mux_key->ts_id);

	if (epg.is_sky || epg.is_mhw2_title) {
		auto* service_key = bat_data.lookup_opentv_channel(epg.channel_id);
		if (!service_key) {
			bool done = bat_done();
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
		auto p_mux_key = lookup_nit_key(txn, epg.epg_service.network_id, epg.epg_service.ts_id);
		txn.abort();
		if (!p_mux_key) {
			bool done = network_done(epg.epg_service.network_id);
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}

		bool is_wrong_dvb_type = (dvb_type(p_mux_key->sat_pos) != dvb_type(stream_mux_key->sat_pos));
		if (is_wrong_dvb_type) {
			auto done = tune_confirmation.sat_by != confirmed_by_t::NONE;
			/*Hack: when the mux is for dvb-t and we are tuned to sat, assume that the
				mux is on the current sat. This will do the right thing for the French multistreams
				on 5.0W, but may  have unwanted consequences. We hope not...
			*/
			if (done)
				epg.epg_service.sat_pos = stream_mux_key->sat_pos;
			else {
				const auto* s = enum_to_str(epg_type);
				dtdebugx("Cannot enter EPG_%s records (%s), because mux with network_id=%d and ts_id=%d has different "
								 "dvb type and sat not yet confirmed (retrying)",
								 epg.is_actual ? "ACTUAL" : "OTHER", s, epg.epg_service.network_id, epg.epg_service.ts_id);
				return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
			}
		} else
			epg.epg_service.sat_pos = p_mux_key->sat_pos;
	}
	auto txn = epgdb_txn();
	for (auto& epg_record : epg.epg_records) {
		// assert(!epg.is_sky || p_mux_key->mux_key.network_id == epg_record.k.service.network_id);
		// assert(!epg.is_sky || p_mux_key->mux_key.ts_id == epg_record.k.service.ts_id);
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
	dttime(2000);
	txn.commit();
	dttime(2000);
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
	auto c = chdb::chgm_t::find_by_key(txn, chg_key, find_geq, chdb::chgm_t::partial_keys_t::chg);

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

dtdemux::reset_type_t active_si_stream_t::sdt_section_cb(const sdt_services_t& services,
																														const subtable_info_t& info)
{
	if(! pat_data.has_ts_id(services.ts_id))
		return pat_data.stable_pat() ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
	if(!is_embedded_si && !pat_data.stable_pat())
		return dtdemux::reset_type_t::RESET;
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

	if (services.is_actual) {
		dtdebugx("SDT_ACTUAL CONFIRMS sat=xx network_id=%d ts_id=%d", services.original_network_id, services.ts_id);

		if(nit_actual_done() && tune_confirmation.sat_by == confirmed_by_t::NONE)
			tune_confirmation.sat_by = confirmed_by_t::TIMEOUT;
		tune_confirmation.ts_id_by = confirmed_by_t::SDT;
		tune_confirmation.network_id_by = confirmed_by_t::SDT;
		sdt_data.actual_network_id = services.original_network_id;
		sdt_data.actual_ts_id = services.ts_id;
	}

	if (!info.timedout && services.original_network_id == sdt_data.actual_network_id)
		scan_state.set_active(scan_state_t::SDT_NETWORK);

	auto wtxn = chdb_txn();
	auto tuned_key = this->stream_mux_key();
	/*
		find out what we already know about network_id,m ts_id
		is mux known in database, and what is its extra_id?
		have we seen it in NIT_ACTUAL?
	 */
	auto* p_mux_data = services.is_actual ?
		add_reader_mux_from_sdt(wtxn, services.original_network_id, services.ts_id) :
		lookup_mux_from_sdt(wtxn, services.original_network_id, services.ts_id);
	if(!p_mux_data) {
		bool nit_done = (nit_actual_done());
		wtxn.abort();
		// if not nit_done: will reparse later; we could also store these records (would be faster)
		return nit_done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
	}

	auto ret = sdt_section_cb_(wtxn, services, info, p_mux_data);
	return ret;
}

dtdemux::reset_type_t active_si_stream_t::bat_section_cb(const bouquet_t& bouquet, const subtable_info_t& info) {
	dttime_init();
	if(!is_embedded_si && !pat_data.stable_pat())
		return dtdemux::reset_type_t::RESET;
	auto cidx = scan_state_t::BAT;
	if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
		dtdebugx("bouquet=%d requesting reset", bouquet.bouquet_id);
		return dtdemux::reset_type_t::RESET; // delay processing until sat_confirmed.
	}
	if (info.timedout) {
		scan_state.set_timedout(cidx);

		if (bat_all_bouquets_completed())
			scan_state.set_completed(cidx);
		return dtdemux::reset_type_t::NO_RESET;
	} else
		scan_state.set_active(cidx);

	auto stream_mux = reader->stream_mux();
	auto stream_mux_key = mux_key_ptr(stream_mux);
	auto txn = chdb_txn();
	auto bouquet_id = bouquet.is_mhw2 ? bouquet_id_movistar : bouquet.bouquet_id;
	chdb::chg_t chg(chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id, stream_mux_key->sat_pos), 0, bouquet.name,
									system_clock_t::to_time_t(now));
	if (chg.name.size() == 0)
		chg.name << "Bouquet " << (int)bouquet.bouquet_id;

	auto c = chdb::chg_t::find_by_key(
		txn, chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id, stream_mux_key->sat_pos), find_eq);
	bool chg_changed = true;
	dttime(1000);
	if (c.is_valid()) {
		auto tmp = c.current();
		chg_changed = (chg.name != tmp.name);
	}

	for (const auto& [channel_id, channel] : bouquet.channels) {
		if (channel_id == 0xff)
			continue;
		chdb::chgm_t chgm;
		//		channel.service_key.mux.sat_pos = stream_mux_key->sat_pos;
		/*we assume channel_ids are unique even when multiple bouquets exist in BAT
		 */
		auto p_mux_key = lookup_nit_key(txn, channel.service_key.mux.network_id, channel.service_key.mux.ts_id);
		if (!p_mux_key) {
			bool done = network_done(channel.service_key.mux.network_id);
			if (done)
				continue; // go for partial bouquet
			bat_data.reset_bouquet(bouquet.bouquet_id);
			txn.abort();
			// if not done: will reparse later; we could also store these records (would be faster)
			if (!done) {
				dtdebugx("bouquet=%d requesting reset", bouquet.bouquet_id);
			}
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
		chgm.k.chg = chg.k;
		chgm.service.mux = *p_mux_key;
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
			if (done)
				continue;
			bat_data.reset_bouquet(bouquet.bouquet_id);
			txn.abort();
			dtdebugx("bouquet=%d requesting reset", bouquet.bouquet_id);
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
		dtdebugx("BAT: bouquet=%d subtable version changed from %d to %d\n",
						 bouquet.bouquet_id,
						 bouquet_data.subtable_info.version_number,
						 info.version_number);
		bouquet_data.channel_ids.clear();
		bouquet_data.num_sections_processed = 0;
		bouquet_data.subtable_info = info;
	}

	for (const auto& [channel_id, channel] : bouquet.channels) {
		bouquet_data.channel_ids.push_back(channel_id);
	}

	assert(bouquet_data.num_sections_processed < bouquet_data.subtable_info.num_sections_present); //4.8E

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
	dttime(1000);
	txn.commit();
	dttime(1000);
	//@todo: can we check for completion?
	return dtdemux::reset_type_t::NO_RESET;
}

dtdemux::reset_type_t active_si_stream_t::eit_section_cb(epg_t& epg, const subtable_info_t& i) {
	if(!is_embedded_si && !pat_data.stable_pat())
		return dtdemux::reset_type_t::RESET;

	if (tune_confirmation.sat_by == confirmed_by_t::NONE)
		return dtdemux::reset_type_t::RESET; // delay processing until sat_confirmed.
	return eit_section_cb_(epg, i);
}

void active_si_stream_t::init_scanning(scan_target_t scan_target_) {
	dtdebug("setting scan_done=false");
	scan_done = false;
	scan_target = scan_target_ == scan_target_t::NONE ? scan_target_t::SCAN_FULL_AND_EPG : scan_target_;
	scan_state = scan_state_t();

	// add_table should be called in order of importance and urgency.

	// if nit.sat_id is not set, some tables will wait with parsing
	// until the correct nit_actual table is received.
	// We probably should deactivate this feature after a certain
	// timeout
}


active_si_stream_t::~active_si_stream_t() { deactivate(false /*tune_failed*/); }

/*
	mux is a mux identified as the one we are currently processsing si_data for
	However, the values in mux have been retrieved from SI data and/or database and may be incorect
	fix some obvious errors by comparing to what the frontend reports

	return false if the check was not possible
	otherwise mux will contain the updated values
 */
bool active_si_stream_t::update_reader_mux_parameters_from_frontend(chdb::any_mux_t& mux) {
	auto& aa = active_adapter();
	if(aa.tune_state == active_adapter_t::TUNE_FAILED)
		return false;
	auto monitor = aa.fe->monitor_thread;
	dttime_init();

	/*Obtain newwest signal info
	*/
	auto signal_info_ = aa.fe->get_last_signal_info(true /*wait*/);
	assert(signal_info_);
	auto & signal_info = *signal_info_;
	dttime(200);
	if (
#if 1
		true /*if lock is lost during init_si, we must proceed to avoid adding a template mux to the db.
					 A better approach would be to retry later
				 */
#else
		signal_info.lock_status.fe_status & FE_HAS_LOCK
#endif
		) {
		//c.tune_src = chdb::tune_src_t::DRIVER_NONE_TUNED;
		if(is_template(mux)) {
			mux_common_ptr(mux)->tune_src = chdb::tune_src_t::DRIVER;
		}
		*mux_key_ptr(signal_info.driver_mux) = *mux_key_ptr(mux);			 // overwrite key
		*mux_common_ptr(signal_info.driver_mux) = 	*mux_common_ptr(mux);

		auto& si_mux = mux;

		assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
					 chdb::mux_common_ptr(mux)->scan_id >0);

		visit_variant(signal_info.driver_mux,
									[&](chdb::dvbs_mux_t& mux) {
										auto* p = std::get_if<chdb::dvbs_mux_t>(&si_mux);
										assert(p);
										if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
											mux.frequency = p->frequency;
											mux.symbol_rate = p->symbol_rate;
										}

										/*override user entered "auto" data in signal_info.mux modulation data with si_data in case
											si_mux is later overwritten with signal_info.mux*/
										if(mux.rolloff == chdb::fe_rolloff_t::ROLLOFF_AUTO)
											mux.rolloff = p->rolloff;
										if(p->modulation == chdb::fe_modulation_t::QAM_AUTO) {//happens on 22.0E 4181V
										} else {
											mux.modulation = p->modulation;
										}
										if(p->delivery_system != mux.delivery_system) { //happens on 14.0W 11024H
											p->delivery_system = mux.delivery_system;
										}
										p->matype = mux.matype; /* set si_mux.matype from driver info (which is the only source for it)*/
									},
									[&](chdb::dvbc_mux_t& mux) {
										auto* p = std::get_if<chdb::dvbc_mux_t>(&si_mux);
										assert(p);
										if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
											mux.frequency = p->frequency;
											mux.symbol_rate = p->symbol_rate;
										}
										if(p->modulation != chdb::fe_modulation_t::QAM_AUTO) {
											mux.modulation = p->modulation;
										}

									},
									[&](chdb::dvbt_mux_t& mux) {
										auto* p = std::get_if<chdb::dvbt_mux_t>(&si_mux);
										assert(p);
										if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
											mux.frequency = p->frequency;
										}
										if(p->modulation != chdb::fe_modulation_t::QAM_AUTO) {
											mux.modulation = p->modulation;
										}
									});

		dtdebug("Update driver_mux=" << signal_info.driver_mux << " stream_mux=" << reader->stream_mux()
						<< " tune_src=" << (int)chdb::mux_common_ptr(si_mux)->tune_src);
		mux = signal_info.driver_mux;
		return true;
	}

	return (signal_info.lock_status.fe_status & FE_HAS_LOCK);
}


void active_si_stream_t::load_movistar_bouquet() {
	auto txn = chdb.rtxn();
	auto sat_pos = reader->get_sat_pos();
	auto chg = chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id_movistar, sat_pos);
	auto c = chdb::chgm_t::find_by_key(txn, chg, find_geq, chdb::chgm_t::partial_keys_t::chg);
	for (const auto& chgm : c.range()) {
		bat_data.opentv_service_keys.try_emplace(chgm.k.channel_id, chgm.service);
	}
	txn.abort();
}

void active_si_stream_t::load_skyuk_bouquet() {
	auto txn = chdb.rtxn();
	auto sat_pos = reader->get_sat_pos();
	auto chg = chdb::chg_key_t(chdb::group_type_t::BOUQUET, bouquet_id_sky_opentv, sat_pos);
	auto c = chdb::chgm_t::find_by_key(txn, chg, find_geq, chdb::chgm_t::partial_keys_t::chg);
	for (const auto& chgm : c.range()) {
		bat_data.opentv_service_keys.try_emplace(chgm.k.channel_id, chgm.service);
	}
	txn.abort();
}

reset_type_t active_si_stream_t::pmt_section_cb(const pmt_info_t& pmt, bool isnext) {
	if(isnext)
		return reset_type_t::NO_RESET;
	dtdebugx("pmt received for sid=%d: stopping stream", pmt.service_id);
	auto& p = pmt_data.by_service_id[pmt.service_id];
	p.parser.reset();
	if (!p.pmt_analysis_finished)
		pmt_data.num_pmts_received++;
	p.pmt_analysis_finished = true;
	p.pmt = pmt;
	pmt_data.saved =false; //we need to save again (new or updated pmt)

	for (const auto& desc : pmt.pid_descriptors) {
		bool is_t2mi = desc.t2mi_stream_id >= 0;
		auto sat_pos = this->stream_mux_key().sat_pos;
		if (pmt.pmt_pid == 256 && desc.stream_type ==  stream_type::stream_type_t::PES_PRIV
				&& desc.stream_pid == 4096 &&
				(
				std::abs(sat_pos - (int)4000) < 300  || //40.0 E
				std::abs(sat_pos - (int)-1400) < 300 || //14.0W
				std::abs(sat_pos - (int)2000) < 300) ){ //20.0E  3865L
			is_t2mi = true;
		}
		if (is_t2mi) {
			/*
				we discovered a t2mi stream and must ensure that its mux
				exists in the database.*/
			auto& aa = reader->active_adapter;
			chdb::any_mux_t mux = reader->stream_mux();
			mux_key_ptr(mux)->t2mi_pid = desc.stream_pid;
			assert(!chdb::is_template(mux));
			if(scan_in_progress) {
				/*
					It would be dangerous to just activate the si scan on the same subscription
					as the scanner will terminate the subscription when the master mux has been scanned
					Instead, we set the pending status on the t2mi mux so that it will be scanned later
				 */
				namespace m = chdb::update_mux_preserve_t;
				auto preserve = m::flags{ m::MUX_COMMON & ~ m::SCAN_STATUS};
				if(mux_common_ptr(mux)->scan_status == chdb::scan_status_t::ACTIVE) {
					mux_common_ptr(mux)->scan_status = chdb::scan_status_t::PENDING;
					preserve = m::flags(preserve & ~m::SCAN_STATUS);
				}
				auto wtxn = chdb_txn();
				namespace m = chdb::update_mux_preserve_t;
				this->update_mux(wtxn, mux, now, false /*is_reader_mux*/, false /*from_sdt*/, preserve);
				wtxn.commit();
			} else {
				/*as no scan is in progress,  this is a regular tune
					We launch si processing which may succeed or not, depending on how long we remain tuned
				*/
				aa.prepare_si(mux, true); //scan the t2mi mux (as opposed to the master mux)
			}
		}
	}

	if(pmts_can_be_saved()) {
		auto wtxn = chdb_txn();
		save_pmts(wtxn);
	}
	return reset_type_t::NO_RESET;
}

void active_si_stream_t::add_pmt(uint16_t service_id, uint16_t pmt_pid) {
	auto [it, inserted] = pmt_data.by_service_id.try_emplace(service_id, pat_service_t{});
	auto& p = it->second;
	if (inserted) {
		dtdebugx("Adding pmt for analysis: service=%d pmt_pid=%d", (int)service_id, (int)pmt_pid);

		auto pmt_section_cb = [this](dtdemux::pmt_parser_t*parser,
																 const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data) {
			remove_parser(parser);
			return this->pmt_section_cb(pmt, isnext);

		};
		add_parser<dtdemux::pmt_parser_t>(pmt_pid, service_id)->section_cb = pmt_section_cb;
		p.pmt_analysis_started = true;
	}
}

/*
	True if mux seems like the currently streamed mux (embedded mux for t2mi, or tuned mux)
	based on all data in an received NIT entry.

	There can be multiple records in the database matching the mux, but then
	at least one of them is the "reader" mux (=the mux from which the si table was reader"
 */
bool active_si_stream_t::matches_reader_mux(const chdb::any_mux_t& mux)
{

	/*
		t2mi pids start with the nid/tid from their parent mux. So tid may be incorrect
	*/
	auto* c = chdb::mux_common_ptr(mux);
	bool check_pat = !is_embedded_si || (c->tune_src == chdb::tune_src_t::DRIVER || c->tune_src == chdb::tune_src_t::NIT_TUNED);
	if ( check_pat && ! pat_data.has_ts_id( mux_key_ptr(mux)->ts_id))
		return false;
	/*sat, freq, pol match what is currently tuned;
		for t2mi, the frequency of the embedded mux matches that of the tuned one
	*/
	auto stream_mux = reader->stream_mux();
	return chdb::matches_physical_fuzzy(mux, stream_mux);  // true requires that stream_id or t2mi_pid also match
}


void active_si_stream_t::update_stream_ids_from_pat(db_txn& wtxn, chdb::any_mux_t& mux)
{
	auto it = pat_data.by_ts_id.begin();
	bool found = it != pat_data.by_ts_id.end();
	if(found) {
		auto ts_id = it->first;
		auto* mux_key = mux_key_ptr(mux);
		mux_key->ts_id = ts_id;
		mux_common_ptr(mux)->key_src = chdb::key_src_t::PAT_TUNED;
	}
	namespace m = chdb::update_mux_preserve_t;
	this->update_mux(wtxn, mux, now, true /*is_active_mux*/, false /*from_sdt*/,
									 found ? m::NONE : m::MUX_KEY /*preserve*/);
}

void active_si_stream_t::save_pmts(db_txn& wtxn)
{
	using namespace chdb;
	auto stream_mux = reader->stream_mux();
	auto* stream_mux_key = mux_key_ptr(stream_mux);
	auto mux_key = *stream_mux_key;
	assert(stream_mux_key->extra_id >0);
	assert(!chdb::is_template(stream_mux));
	ss::string<32> mux_desc;
	assert (stream_mux_key->sat_pos != sat_pos_none);
	chdb::to_str(mux_desc, stream_mux);
	int count{0};

	if (nit_data.by_network_id_ts_id.size()==1){
		for(auto &[key, val]: nit_data.by_network_id_ts_id) {
			mux_key.network_id = key.first;
			mux_key.ts_id = key.second;
		}
	}

	if(sdt_data.actual_network_id >=0 && sdt_data.actual_ts_id >= 0) {
		mux_key.network_id = sdt_data.actual_network_id;
		mux_key.ts_id = sdt_data.actual_ts_id;
		}

	for (auto& [service_id, pat_service]: pmt_data.by_service_id) {
		//pat entry for ts_id has priority
		for (auto& [ts_id, pat_table]:  pat_data.by_ts_id) {
			for(auto& e: pat_table.entries) {
				if(e.service_id == pat_service.pmt.service_id) {
					mux_key.ts_id = ts_id;
					break;
				}
			}
		}
		service_key_t service_key(mux_key,  pat_service.pmt.service_id);
		auto c = service_t::find_by_key(wtxn, service_key);
		auto service = c.is_valid() ? c.current() : service_t{};
		if(! c.is_valid()) {
			service.k = service_key;
			service.name.sprintf("Service %s:%d", mux_desc.c_str(), pat_service.pmt.service_id);
		}
		auto new_service = service;
		new_service.mux_desc = mux_desc;
		new_service.pmt_pid = pat_service.pmt.pmt_pid;
		new_service.video_pid = pat_service.pmt.video_pid;
		assert(	(int)pat_service.pmt.estimated_media_mode <=4);
		if (pat_service.pmt.estimated_media_mode != media_mode_t::UNKNOWN)
			new_service.media_mode = pat_service.pmt.estimated_media_mode;
		new_service.encrypted = pat_service.pmt.is_encrypted();
		if(new_service != service) {
			new_service.mtime = system_clock_t::to_time_t(now);
			dtdebug("Updating/saving service from pmt: " << service);
			count++;
			put_record(wtxn, new_service);
		}
	}
	dtdebugx("SAVED %d pmts\n", count);
}

/*
	for tuned_mux:  first lookup network_id,ts_id,extra_id in the database ig it is not exist
	add it initially as 0,0,unique_value but remember that this key is what is currently considered
	the tuned mux. This way, there is always an entry for the tuned mux in the database when si processing
	is running. This entry can be 0,0,unique_id in the worst case.


	for ALL nit lookups,  caller can check frequency and sat to find out if lookup is for tuned mux
	for sdt_actual lookup is also for tuned_mux for sdt_other it is not for tuned_mux. When the p_mux_data
	cache comes from nit it is assumed to be correct

	for sdt_actual lookups:
    we know lookup is for tuned mux; thus we always have a valid tuned_mux_key and we can safely enter
		services into the database

	for sdt_other lookups (which is never for the tuned mux!):
	  if we find a record in the p_mux_data cache we can use its sat_id, extra_id
		Otherwise, sdt can enter a temporary entry for network_id, ts_id, by finding out if there is a unique
		single mux on a close sat with the correct network_id, ts_id. If a unique entry exists, it is likely to be
		correct. There is a small possibility of confusion, e.g., when multiple muxes with network_id=1,ts_id=1
		are present. Tis confusion can lead to services associated with the wrong mux, but only as long
		as only one of these muxes is initially in the database. Once they are both in the database, the
		code will not consider these muxes without nit_actual received

	Caching must proceed as follows:
    sdt code must not change cache entry if one exists  and just accept its values

		nit_code must check if cache entry was entered by sdt or if it is for the current mux
		but the mux key is 0,0 or a value loaded from the database. If so if must check for
		a key change and possibly update muxes, services, channels,... in the database
		if nit_code finds no entry it needs to consult the database. This can possibly also lead
		to a mux_key change



 */
//matype 11391H -> changes from DVBS
/*@todo freesat is on all freesat transponders. Therefore we should remember
	when freesat was last scanned and not repeat the scan, irrespective of transponder
	This test should be based on successful completion

	@todo: The presence of freesat pids can be checked in the pmt of any freesat channel:
	this will contain MPEG-2 Private sections refering to the pids below and
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
	1.  for determining if tuned to current sat/mux verify is the correct one (up to a tolerance of sat_pos_tolerance)
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
