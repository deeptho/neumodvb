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
	dtdebug("setting si_processing_done=false (init)");
}


/*
	change a mux with scan_status_t::PENDING to scan_status_t::ACTIVE
	when tuning is requested using the proper scan_id
 */
void active_si_stream_t::activate_scan(chdb::any_mux_t& mux,
																		 subscription_id_t subscription_id, uint32_t scan_id) {
	if(scan_id == 0)
		return;
	dtdebug("activate_scan mux=" << mux);
	using namespace chdb;
	scan_state.scans_in_progress.push_back({scan_id, subscription_id});
	namespace m = chdb::update_mux_preserve_t;
	auto* muxc = mux_common_ptr(mux);

	if(muxc->scan_status == scan_status_t::ACTIVE) {
     //some other subscription is already scanning
	} else {

		bool must_save{false};

		if(muxc->scan_status == scan_status_t::PENDING ||
			 muxc->scan_status == scan_status_t::RETRY)  {
			dtdebug("SET ACTIVE " << mux << " must_save=" << must_save);
			if(si_processing_done) {
				muxc->scan_status = scan_status_t::IDLE;
				muxc->scan_id=0;
			} else {
				muxc->scan_status = scan_status_t::ACTIVE;
				assert (muxc->scan_id > 0);
			}
			must_save = !is_template(mux);
		}

		if(must_save) {
			auto& k = *mux_key_ptr(mux);
			assert(k.mux_id > 0 && ! is_template(mux));
			auto chdb_wtxn = receiver.chdb.wtxn();
			chdb::update_mux(chdb_wtxn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS},
											 false /*ignore_t2mi_pid*/, true /*must_exist*/);
			chdb_wtxn.commit();
			dtdebug("committed write");
		}
		reader->on_stream_mux_change(mux);
	}
	if(si_processing_done)
		check_scan_mux_end(); //notify scanner if scan already completed (because of other scanner)
}

/*
	update scan_result, scan_state and related parameters when
	a scan is considered final, i.e., when
	 -tuning has failed permanently due to bad tuning parameters
	 -tuning has failed temporarily due to insufficient driver resources
	 -Tuner locked briefly but lock was lost
	 -Tuner did not lock at all and then timed out
	 -we detected a non-dvb stream
	 -we detected a dvb stream and all si tables were received
	 -we detected a dvb stream and not all si tables were received before a timeout
	Note that finalizing a scan does not mean that si processing is stopped, but instead
	that the scanner is informed that sufficient data has been gathered and that the scanner
	can now stop the subscription

	is_no_ts=true: we detected a non-transport stream
	is_no_ts=false: we did not lock, or found a dvb stream (with or without data)
 */
void active_si_stream_t::finalize_scan()
{
	si_processing_done = true;
	// now update the mux's scan state
	ss::string<32> s;
	auto mux = reader->stream_mux();
	bool nosave{is_template(mux)};
	to_str(s, mux);
	auto tune_state = active_adapter().tune_state;
	auto& lock_state = active_adapter().lock_state;
	dtdebug("setting si_processing_done=true mux=" << mux);
	auto* c = chdb::mux_common_ptr(mux);
	c->scan_lock_result = lock_state.tune_lock_result;

	using tune_state_t = active_adapter_t::tune_state_t;
	switch(tune_state) {
	case tune_state_t::LOCKED: {
		c->scan_status = chdb::scan_status_t::IDLE;
		bool no_data = reader->no_data();
		c->scan_result = lock_state.is_not_ts ? chdb::scan_result_t::NOTS :
			no_data ?  chdb::scan_result_t::NODATA :
			scan_state.scan_completed() ? chdb::scan_result_t::OK : chdb::scan_result_t::PARTIAL;
		if(no_data) {
			tune_confirmation.sat_by = confirmed_by_t::FAKE;
			c->ts_id = 0;
			c->network_id = 0;
			c->nit_ts_id = 0;
			c->nit_network_id = 0;
			c->num_services = 0;
		}
		nosave = false;
	}
		break;

	case tune_state_t::TUNE_INIT:
		c->scan_status = chdb::scan_status_t::IDLE;
		c->scan_result = chdb::scan_result_t::NOLOCK; //mux could not be locked
	case tune_state_t::WAITING_FOR_LOCK:
		//we do not know if mux can be locked
		c->scan_status = chdb::scan_status_t::IDLE;
		c->scan_result = chdb::scan_result_t::NOLOCK; //mux could not be locked
		break;
	case tune_state_t::LOCK_TIMEDOUT:
		c->scan_status = chdb::scan_status_t::IDLE;
		c->scan_result = chdb::scan_result_t::NOLOCK; //mux could not be locked
		break;
	case tune_state_t::TUNE_FAILED:
		c->scan_status = chdb::scan_status_t::IDLE;
		c->scan_result = chdb::scan_result_t::BAD; //mux has untunable parameters
		break;
	case tune_state_t::TUNE_FAILED_TEMP:
		c->scan_status = chdb::scan_status_t::RETRY;
		c->scan_result = chdb::scan_result_t::TEMPFAIL;
		nosave = true;
		break;
	}

	c->scan_duration = scan_state.scan_duration();
	c->scan_time = system_clock_t::to_time_t(now);
	if(c->scan_duration == 0) { //needed in case tuning fails
		c->scan_duration =
			std::chrono::duration_cast<std::chrono::seconds>(now - active_adapter().tune_start_time).count();
	}

	tune_confirmation.si_done = true;
	reader->update_stream_mux_tune_confirmation(tune_confirmation);

	dttime_init();
	reader->on_stream_mux_change(mux); //needed to ensure that mux_common changes are not overwritten in save_pmts
	dttime(200);

	if(nosave)
		return;

	auto wtxn = chdb.wtxn();
	dttime(200);

	if(lock_state.is_dvb  &&
		 (c->key_src == chdb::key_src_t::NONE || (nit_actual_notpresent() && sdt_actual_notpresent())))
		update_stream_ids_from_pat(wtxn, mux);
	else {
		namespace m = chdb::update_mux_preserve_t;

		this->update_mux(wtxn, mux, now, true /*is_reader_mux*/, true /*is_tuned_freq*/,
										 false /*from_sdt*/, m::MUX_KEY /*preserve*/);
	}

	if(lock_state.is_dvb && (nit_actual_done() || nit_actual_notpresent())) {
		if(pmts_can_be_saved())
			save_pmts(wtxn);
	}
	wtxn.commit();

}


void active_si_stream_t::check_scan_mux_end()
{
	if(this->scan_state.scans_in_progress.size()==0)
		return; //nothing to report

	if(!active_adapter().fe)
		return;

	auto dbfe = active_adapter().fe->dbfe();
	auto mux = reader->stream_mux();
	auto& receiver_thread = active_adapter().receiver.receiver_thread;

	dtdebug("calling on_scan_mux_end dbfe=" << dbfe << " mux=" <<mux << " scan_result="<<
					mux_common_ptr(mux)->scan_result);
	assert(mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE);
  //note: scans_in_progress is copied (no call by reference)
	receiver_thread.push_task([&receiver_thread, dbfe, mux,  scans_in_progress=this->scan_state.scans_in_progress]() {
		for(auto& e: scans_in_progress) {
			auto [scan_id, subscription_id ] = e;
			cb(receiver_thread).on_scan_mux_end(dbfe, mux, scan_id, subscription_id);
		}
		return 0;
	});

	this->scan_state.scans_in_progress.clear();
}

void active_si_stream_t::end()
{
	finalize_scan();
	check_scan_mux_end();
}

void active_si_stream_t::reset_si(bool close_streams) {
#if 0
	//wrong for a non-ts
	if(!is_open())
		return;
#else
#endif
	if(!si_processing_done) {
		dtdebug("calling finalize_scan\n");
		finalize_scan();
		dtdebugx("calling scan si_processing_done=%d\n", si_processing_done);
		check_scan_mux_end();
		si_processing_done = false;
	}
	si_processing_done = false;
	::active_si_data_t::reset(); //reset variables to initial state
	if (close_streams) {
		if(is_open()) {
			log4cxx::NDC(name());
			dtdebug("deactivate si stream reader_mux=" << reader->stream_mux());
			// active_stream_t::close();
			stream_parser.exit(); // remove all fibers
			parsers.clear();			// remove all parser data (parsers will be reregistered)
		}
		::active_stream_t::deactivate(); //remove all pids, and close streams
	/* TODO:  check that this also closes any open pid.
		 There should not be any, unless a stream changes from ts to non-ts
		 but it is better to be produnt
	*/

	}
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
mux_data_t* active_si_stream_t::add_mux(db_txn& wtxn, chdb::any_mux_t& mux, bool is_actual,
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
				 chdb::mux_common_ptr(mux)->scan_id > 0);
	if(is_active_mux) {
		auto tmp = *mux_common;
		*mux_common = *mux_common_ptr(stream_mux); //copy scan_id and such
		if(from_sdt) {
			mux_common->network_id = tmp.network_id;
			mux_common->ts_id = tmp.ts_id;
		} else {
			mux_common->nit_network_id = tmp.nit_network_id;
			mux_common->nit_ts_id = tmp.nit_ts_id;
		}
		mux_key->t2mi_pid = reader->embedded_stream_pid();
	}
	if(from_sdt) {
		assert(is_actual);
		assert(is_active_mux);
#if 0
		//set in add_mux
		mux_common->key_src = key_src_t::SDT_TUNED;
#endif
		mux_common->tune_src = is_actual ? tune_src_t::DRIVER : tune_src_t::UNKNOWN;
	} else { //from nit_actual
		mux_common->key_src = is_active_mux ? key_src_t::NIT_TUNED :
			is_actual ? key_src_t::NIT_ACTUAL : key_src_t::NIT_OTHER;
		mux_common->tune_src =  is_tuned_freq ? tune_src_t::NIT_TUNED
			: is_actual ?  tune_src_t::NIT_ACTUAL : tune_src_t::NIT_OTHER;
	}

	update_mux_preserve_t::flags preserve = m::flags(m::ALL);
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
		preserve = from_sdt ? m::flags((m::MUX_COMMON /* & ~ m::SCAN_STATUS*/ & ~ m::SDT_SI_DATA)| m::TUNE_DATA | m::MUX_KEY)
				: m::flags( (m::MUX_COMMON & ~ m::NIT_SI_DATA /*& ~m::SCAN_STATUS*/) | m::MUX_KEY);
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
		preserve = m::flags( preserve | m::MUX_KEY);
	}

	if(!this->update_mux(wtxn, mux, now, is_active_mux /*is_reader_mux*/, is_tuned_freq, from_sdt, preserve))
		return nullptr; //something went wrong, e.g., on wrong sat

	assert (mux_key->sat_pos != sat_pos_none);

	if(is_active_mux) {
		auto& c  = *mux_common_ptr(mux);
		if(!(c.tune_src == tune_src_t::NIT_TUNED ||
					 c.tune_src == tune_src_t::NIT_ACTUAL || c.tune_src == tune_src_t::NIT_OTHER ||
				 (from_sdt&& c.tune_src == tune_src_t::DRIVER))) {
			dterrorx("Incorrect tune_src=%d\n", (int) c.tune_src);
			c.tune_src = tune_src_t::DRIVER;
		}
		if(!((c.key_src == chdb::key_src_t::NONE) ||
					 (c.key_src == chdb::key_src_t::SDT_TUNED) ||
				 (!from_sdt  && c.key_src == key_src_t::NIT_TUNED))) {
			dterrorx("Incorrect key_src=%d\n", (int) c.key_src);
			c.key_src = chdb::key_src_t::NONE;
		}
	}

	/*in case from_sdt==false, the update_mux call above may have ipdated network_id and ts_id
		based on what was found in the database. This is important in case sdt and nit disagree.
		The map below always indexes by the data from sdt, if it is known (in the database), otherwise
		from nit.
	 */
	auto [it, inserted] =
		nit_data.by_network_id_ts_id.try_emplace(std::make_pair(mux_common->network_id, mux_common->ts_id),
																						 mux_data_t{mux});
	auto* p_mux_data = & it->second;
	if(from_sdt && ! p_mux_data->have_sdt) {
		p_mux_data->have_sdt = true;
		dtdebug("Updated fropm sdt: " << mux);
	}
	if(!from_sdt && ! p_mux_data->have_nit) {
		p_mux_data->have_nit = true;
		dtdebug("Updated fropm nit: " << mux);
	}
	p_mux_data->is_active_mux = is_active_mux;
	p_mux_data->is_tuned_freq = is_tuned_freq;
	p_mux_data->mux = mux;

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

	auto& n = nit_data.get_original_network(mux_common->network_id);
	n.add_mux(mux_common_ptr(p_mux_data->mux)->ts_id, false /*from_sdt*/);
	return p_mux_data;
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
	mux_common->network_id = network_id;
	mux_common->nit_network_id = network_id;
	mux_common->ts_id = ts_id;
	mux_common->nit_ts_id = ts_id;
	mux_key->t2mi_pid = reader->embedded_stream_pid();

	if (no_data) {
		auto* mux_common = chdb::mux_common_ptr(mux);
		mux_common->scan_result = chdb::scan_result_t::NODATA;
		mux_common->scan_lock_result = chdb::lock_result_t::NOLOCK;
		mux_common->scan_duration = scan_state.scan_duration();
		// assert(scan_state.scan_duration()>=0);
		mux_common->num_services = 0;
		mux_common->scan_time = system_clock_t::to_time_t(now);
		preserve = m::flags{ preserve & ~ m::MUX_COMMON};
	}

	this->update_mux(wtxn, mux, now, true /*is_reader_mux*/, true /*is_tuned_freq*/,
									 from_sdt,  m::flags{ m::MUX_COMMON /*& ~m::SCAN_STATUS*/ } /*preserve*/);
	//assert(mux_key->ts_id == ts_id);
	if (!from_sdt && !is_embedded_si) {
		reader->on_stream_mux_change(mux);
	}

	if (expected_sat_pos != sat_pos_none) {
		// we overwrite any existing mux - if we are called, this means any existing mux must be wrong
		auto [it, inserted] = nit_data.by_network_id_ts_id.insert_or_assign(std::make_pair(network_id, ts_id),
																																				mux_data_t{mux});
		auto* p_mux_data = & it->second;
		p_mux_data->is_active_mux = true;
		p_mux_data->is_tuned_freq = true;
		p_mux_data->mux = mux;

		if(from_sdt && ! p_mux_data->have_sdt) {
			p_mux_data->have_sdt = true;
			dtdebug("Updated fropm sdt: " << mux);
		}
		if(!from_sdt && ! p_mux_data->have_nit) {
			p_mux_data->have_nit = true;
			dtdebug("Updated fropm nit: " << mux);
		}
		return p_mux_data;
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
mux_data_t*
active_si_stream_t::lookup_mux_data_from_sdt(db_txn& txn, uint16_t network_id, uint16_t ts_id) {
	auto [it, found] = find_in_map(nit_data.by_network_id_ts_id, std::make_pair(network_id, ts_id));
	if (found)
		return &it->second; // nit has been received and mux is already known in cache, this is authorative

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
	auto ret = chdb::get_by_nid_tid_sat_unique(txn, network_id, ts_id, stream_mux_key.sat_pos);

	if (ret.unique == chdb::get_by_nid_tid_unique_ret_t::UNIQUE_ON_SAT) {
		auto [it, inserted] =
			nit_data.by_network_id_ts_id.try_emplace(std::make_pair(
																								 network_id, ts_id), mux_data_t{ret.mux});
		auto* p_mux_data = & it->second;
		assert (inserted);
		return p_mux_data;
	}
	return nullptr;
}

/*
	code called from sdt when sdt_actual is received
 */
mux_data_t* active_si_stream_t::add_reader_mux_from_sdt(db_txn& wtxn, uint16_t network_id, uint16_t ts_id) {
	namespace m = chdb::update_mux_preserve_t;
	auto mux = this->reader->stream_mux();
	auto* mux_common = mux_common_ptr(mux);
	mux_common->network_id = network_id;
	mux_common->ts_id = ts_id;
	return add_mux(wtxn, mux, true /*is_actual*/, true /*is_active_mux*/, true /*is_tuned_freq*/, true /*from_sdt*/);
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
bool active_si_stream_t::read_and_process_data_for_fd(const epoll_event* evt) {
	if (!reader->on_epoll_event(evt))
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

bool active_si_stream_t::fix_tune_mux_template() {
	using namespace chdb;
	namespace m = chdb::update_mux_preserve_t;
	auto stream_mux = reader->stream_mux();
	auto& c = *mux_common_ptr(stream_mux);
	bool is_template = c.tune_src == chdb::tune_src_t::TEMPLATE;
	bool is_active = c.scan_status == scan_status_t::ACTIVE;
	auto lock_state = active_adapter().lock_state;
	if(!lock_state.locked_minimal)
		return false;
	auto old_stream_id = mux_key_ptr(stream_mux)->stream_id;
	auto [locked, stream_id_changed] = update_reader_mux_parameters_from_frontend(stream_mux);

	if (is_template) {
		dtdebug("Fixing stream_mux template status: " << stream_mux);
		c.scan_time = time_t(0);
		c.num_services = 0;
		c.nit_network_id = 0;
		c.nit_ts_id = 0;
		c.key_src = key_src_t::NONE;
		c.mtime = time_t{0};
		c.epg_types = {};
		assert(c.tune_src != chdb::tune_src_t::TEMPLATE || !locked);
		if(!locked) {
			dterrorx("lock lost\n");
			return false;
		}
		//at this stage we must be locked (si processing only starts after lock)
	}
	if(is_active ||is_template) { /*we  need to set the active status, or save the mux if it
																 was not yet in the database (is_template) because it can be
																locked now*/
		auto wtxn = receiver.chdb.wtxn();
		c.scan_lock_result = lock_state.tune_lock_result;
		chdb::update_mux(wtxn, stream_mux, now,  m::flags{ (m::MUX_COMMON|m::MUX_KEY)/* & ~m::SCAN_STATUS*/},
										 /*true  ignore_key,*/ false /*ignore_t2mi_pid*/, false /*must_exist*/);

		if(stream_id_changed) {
			mux_key_ptr(stream_mux)->stream_id = old_stream_id;
			chdb::delete_record(wtxn, stream_mux);
		}

		wtxn.commit();
	}
	assert( c.tune_src != chdb::tune_src_t::TEMPLATE);
	reader->on_stream_mux_change(stream_mux);
	return true;
}

/*
	called when stream is first locked and is a transport stream; returns true on success
 */
bool active_si_stream_t::init(scan_target_t scan_target_) {
	log4cxx::NDC(name());
	if (is_open())
		reset_si(true /*close_streams*/);
	init_scanning(scan_target_);
	active_stream_t::open(dtdemux::ts_stream_t::PAT_PID, &active_adapter().tuner_thread.epx,
												EPOLLIN | EPOLLERR | EPOLLHUP);
	if(!fix_tune_mux_template()) {
		//lock was lost
		reset_si(false/*close_streams*/);
		return false;
	}
	auto stream_mux = reader->stream_mux();
	auto* mux_common = chdb::mux_common_ptr(stream_mux);
	assert( mux_common->tune_src != chdb::tune_src_t::TEMPLATE);
	scan_in_progress = (mux_common->scan_status == chdb::scan_status_t::ACTIVE);
	assert(!scan_in_progress || mux_common->scan_id>0);
	dtdebug("si_processing_done= " << (int) si_processing_done << " " << stream_mux);
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
	return true;
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

	if (si_processing_done)
		return;

	bool no_data = reader->no_data();
	auto done = scan_state.scan_done();

	if (no_data || done) {
		finalize_scan();
		check_scan_mux_end();
	}
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
			if (s.service_id != 0x0 /*skip pat*/)
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
			reader->update_received_si_mux(std::optional<chdb::any_mux_t>{}, false /*is_bad*/);
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
		bool is_active_mux = this->matches_reader_mux(mux, false /*from_sdt*/) && network.is_actual;
		/* update database: tune_src, mux_key, tuning parameters;
			 perform overall database changes when mux_key changes
			 check and fix modulation parameters by consulting driver
			 insert/update mux in nit_data.by_network_id_ts_id
			 insert mux in nit_data.get_original_network
			 add sat entry if none present yet
			 updates reader->current_mux
		 */
		bool bad_si_mux = is_tuned_freq && ! is_active_mux; //not tsid in pat or tsid differs from the one in reader_mux
		bad_si_mux |= (network.is_actual && !ts_id_in_pat(mux_common_ptr(mux)->nit_ts_id));
		if(is_active_mux)
			reader->update_received_si_mux(mux, bad_si_mux); //store the bad mux

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
			assert(chdb::mux_key_ptr(stream_mux)->mux_id > 0 ||
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
			if(pat_data.has_ts_id(tmp.c.ts_id)) {
					//happens on 26.0E: 12034H and 14.0W: 11623V, 14.0W: 11638H
				dtdebug("Fixing zero frequency: " << mux);
				tmp = *std::get_if<chdb::dvbs_mux_t>(&tuned_mux);
				tmp.c.ts_id = dvbs_mux->c.ts_id;
				tmp.c.network_id = dvbs_mux->c.network_id;
				tmp.c.nit_ts_id = dvbs_mux->c.nit_ts_id;
				tmp.c.nit_network_id = dvbs_mux->c.nit_network_id;
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
		if(dvbs_mux->c.nit_network_id==65 && dvbs_mux->c.nit_ts_id == 65 && dvbs_mux->pol ==  chdb::fe_polarisation_t::L
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
	auto& mux_key = *mux_key_ptr(p_mux_data->mux);
	auto& mux_common = *mux_common_ptr(p_mux_data->mux);
	if (tune_confirmation.sat_by == confirmed_by_t::NONE) {
		// figure out if we can confirm sat
		if (!mux_key_changed && db_correct > 2) {
			dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: found=%d existing services",
							 mux_key.sat_pos, mux_common.network_id, mux_common.ts_id, db_correct);
			tune_confirmation.sat_by = confirmed_by_t::SDT;
			tune_confirmation.on_wrong_sat = false;

			if (tune_confirmation.ts_id_by == confirmed_by_t::NONE)
				tune_confirmation.ts_id_by = confirmed_by_t::SDT;
			if (tune_confirmation.network_id_by == confirmed_by_t::NONE)
				tune_confirmation.network_id_by = confirmed_by_t::SDT;
		} else if (!mux_key_changed && nit_actual_done()) {
			dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: NIT_ACTUAL is done and will not confirm",
							 mux_key.sat_pos, mux_common.network_id, mux_common.ts_id);
			return false;
		} else {
			tune_confirmation.sat_by = confirmed_by_t::NONE;
			tune_confirmation.ts_id_by = confirmed_by_t::NONE;
		}
	} else if (tune_confirmation.network_id_by == confirmed_by_t::NONE ||
						 tune_confirmation.ts_id_by == confirmed_by_t::NONE) {
		// this happens when nit_actual has seen muxes, but not the tuned mux yet
		dtdebugx("SDT_ACTUAL CONFIRMS sat=%d network_id=%d ts_id=%d: ",
						 mux_key.sat_pos, mux_common.network_id, mux_common.ts_id);
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
	auto* mux_common = mux_common_ptr(mux);
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
		dtdebugx("NIT CONFIRMS sat=%d network_id=%d ts_id=%d", mux_key->sat_pos,
						 mux_common->nit_network_id, mux_common->nit_ts_id);
		if (tune_confirmation.sat_by == confirmed_by_t::NONE)
			tune_confirmation.sat_by = confirmed_by_t::NIT;

		if (tune_confirmation.ts_id_by == confirmed_by_t::NONE ||
				tune_confirmation.network_id_by == confirmed_by_t::NONE) {
			tune_confirmation.ts_id_by = confirmed_by_t::NIT;
			tune_confirmation.network_id_by = confirmed_by_t::NIT;
			sdt_data.actual_network_id = mux_common->nit_network_id;
			sdt_data.actual_ts_id = mux_common->nit_ts_id;
			sdt_data.mux_key = *mux_key;
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
	mux: typically an SI mux. Usually its key cannot be relied upon, bvecause mux.k.mux_id == 0
	is_reader_mux: when true, we know for sure that we are processing si data retrieved for this mux (this is
	either the tuned mux or an embedded t2mi mux).

	from_sdt:
	 *if false, then the code looks for a mux with matching frequency, prefering
	  one with also a matching key. If no matching key is found, but another mux with matching frequency but differing
	  key, then the old key is replaced with the new one.
		It also propagates scan states

	 *if true, then the code looks for a mux with matching frequency if is_reader_mux == true,
	  requiring a matching key. If no matching key
    is found, then a new mux is inserted. No scan satets are propagates

	returns true if mux was updated
 */
bool active_si_stream_t::update_mux(
	db_txn& chdb_wtxn, chdb::any_mux_t& mux, system_time_t now,
	bool is_reader_mux, bool is_tuned_freq, bool from_sdt, chdb::update_mux_preserve_t::flags preserve) {
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

	if((is_tuned_freq || is_reader_mux) && ! is_embedded_si) {
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
		if(active_adapter().lock_state.locked_minimal) {
			auto old_stream_id = mux_key_ptr(mux)->stream_id;
			auto [locked, stream_id_changed] = update_reader_mux_parameters_from_frontend(mux);
			auto failed = !locked;
			if(failed) {
				dtdebug("update_reader_mux_parameters_from_frontend failed (lock lost?)\n");
				chdb::mux_common_ptr(mux)->scan_result = chdb::scan_result_t::NOLOCK;
			}
			if(stream_id_changed) {
				auto new_stream_id = mux_key_ptr(mux)->stream_id;
				mux_key_ptr(mux)->stream_id = old_stream_id;
				chdb::delete_record(chdb_wtxn, mux);
				mux_key_ptr(mux)->stream_id = new_stream_id;
			}

		}
		else {
			assert(!is_template(mux));
		}

		assert(chdb::mux_common_ptr(mux)->tune_src != chdb::tune_src_t::TEMPLATE);
		assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
					 chdb::mux_common_ptr(mux)->scan_id >0);

	}

	auto cb = [&](chdb::mux_common_t* pdbc, const chdb::mux_key_t* pdbk) {
		/*lookup always was done by frequency. Either nothing was found in the database (pdbk and pdbc
			both nullptr) or some database mux was found (pdbk and pdbc different from nullptr)

			It is possible that *pdbk does not match the the key of the mux being inserted:
			for is_reader_mux==false, "mux" may be populated from the stream and then mux.k.mux_id==0
			for is_reader_mux==true, mux.mux_id should already match what is in the database, because
			fix_tune_mux_template() has been called.
		 */
		auto* pc = mux_common_ptr(mux);
		auto tmp = *chdb::mux_key_ptr(mux);
		if(pdbk) {
			assert (*pdbk == tmp || tmp.mux_id == 0);

			assert(tmp.mux_id == pdbk->mux_id);
			if(is_reader_mux && pdbc && (
					 pdbc->key_src == chdb::key_src_t::SDT_TUNED ||
					 pdbc->key_src == chdb::key_src_t::PAT_TUNED
					 )) {
				pc->key_src = pdbc->key_src;
			} else if (*pdbk != tmp) {
				assert(!is_reader_mux); //because fix_tune_mux_template() has been called
				/*
				 */

				*mux_key_ptr(mux) = *pdbk;
				assert(preserve & update_mux_preserve_t::MUX_KEY);
			}
		} //end of if(pdbk)

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
		chdb::update_mux(chdb_wtxn, mux, now,  preserve, /*true ignore_key,*/
										 false /*ignore_t2mi_pid*/, false /*must_exist*/);
	} else {
		assert(!chdb::is_template(mux));
		assert(mux_key_ptr(mux)->mux_id  > 0 || !from_sdt);
		chdb::update_mux(chdb_wtxn, mux, now, preserve, cb, /*true ignore_key,*/
										 false /*ignore_t2mi_pid*/, false /*must_exist*/);
	}
	bool saving_reader_mux = (*mux_key_ptr(mux) == *mux_key_ptr(reader_mux));
	if (is_reader_mux || (is_tuned_freq && saving_reader_mux)) {
#ifdef DEBUG_CHECK
		debug_check(chdb_wtxn, reader->stream_mux(), mux);
#endif
		reader->on_stream_mux_change(mux);
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
	auto& mux_key = *mux_key_ptr(mux_data.mux);
	auto& mux_common = *mux_common_ptr(mux_data.mux);

	if (done_now || empty) {
		auto& n = get_original_network(mux_common.network_id);
		n.add_mux(mux_common.ts_id, true);
		// n.sdt_num_muxes_present += empty;
		if (done_now) {
			dtdebugx("set_sdt_completed: ts_id=%d is_actual=%d vers=%d pid=%d proc=%d pres=%d", mux_common.ts_id,
							 info.is_actual, info.version_number, info.pid, sdt.num_sections_processed,
							 sdt.subtable_info.num_sections_present);
			n.set_sdt_completed(mux_common.ts_id);
		}
		if (n.sdt_num_muxes_completed > n.sdt_num_muxes_present)
			dterrorx("assertion failed: num_muxes_completed=%d; n.sdt_num_muxes_present=%d", n.sdt_num_muxes_completed,
							 n.sdt_num_muxes_present);
		assert(n.sdt_num_muxes_completed <= n.sdt_num_muxes_present);
		if (done_now) {
			dtdebug("SDT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " subtable completed: " << mux_key << " "
							<< "network_id=" << mux_common.network_id << "ts_id=" << mux_common.ts_id
							<< mux_data.service_ids.size() << " services; muxes now completed on network: "
							<< n.sdt_num_muxes_completed << "/" << n.sdt_num_muxes_present);
			if (info.is_actual) {
				scan_state.set_completed(scan_state_t::SDT_ACTUAL);
			}
			if (!info.is_actual) {
				if (n.sdt_num_muxes_completed == n.sdt_num_muxes_present) {
					dtdebug("Network completed: " << mux_common.network_id);
				}
			}
		} else if (empty) {
			dtdebug("SDT_" << (info.is_actual ? "ACTUAL" : "OTHER") << " subtable reset: " << mux_key << " "
							<< "network_id=" << mux_common.network_id << "ts_id=" << mux_common.ts_id
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
	chdb::mux_key_t& mux_key = *mux_key_ptr(p_mux_data->mux);
	auto c = chdb::service::find_by_mux_key_sid(wtxn, mux_key, service.k.service_id);
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
		// note: we do not check pmt_pid, as this can only be found in PAT (so we preserve)
		changed |= (ch.expired || ch.media_mode != service.media_mode || ch.service_type != service.service_type ||
								ch.encrypted != service.encrypted);
		std::visit([&](auto&mux) {
			auto pol = get_member(mux, pol, chdb::fe_polarisation_t::NONE);
			changed |= (ch.frequency != mux.frequency || ch.pol != pol);
			ch.frequency = mux.frequency;
			ch.pol = pol;
		}, p_mux_data->mux);

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
			if (!donotsave) {
				dtdebug("SAVING changed service " << ch);
				put_record(wtxn, ch);
			}
		}
	} else { //no mux yet
		auto ch = service;
		ch.mtime = system_clock_t::to_time_t(now);
		ch.k.mux = mux_key;
		std::visit([&](auto&mux) {
			auto pol = get_member(mux, pol, chdb::fe_polarisation_t::NONE);
			ch.frequency = mux.frequency;
			ch.pol = pol;
		}, p_mux_data->mux);

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
	auto& mux_key = *mux_key_ptr(p_mux_data->mux);
	auto& mux_common = *mux_common_ptr(p_mux_data->mux);

	assert(mux_common.ts_id == services.ts_id);
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
		assert(mux_common.ts_id == service.k.ts_id);

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
	}

	bool done_now = nit_data.update_sdt_completion(scan_state, info, *p_mux_data);
	tune_confirmation.sdt_actual_received = done_now;
	if (done_now) {
		if (!donotsave) {
			process_removed_services(wtxn, p_mux_data->mux, service_ids);
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
		} else {
			mux = p_mux_data->mux;
		}

		if (!donotsave_stats) {
			auto& common = *chdb::mux_common_ptr(mux);
			bool changed = (common.num_services != service_ids.size());
			common.num_services = service_ids.size();

			auto has_movistar = (mux_key.sat_pos == 1920 && mux_common.network_id == 1 && mux_common.ts_id == 1058);
			auto has_viasat_baltic = (mux_key.sat_pos == 500 && mux_common.network_id == 86 && mux_common.ts_id == 27);

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
				bool is_reader_mux = this->matches_reader_mux(mux, true /*from_sdt*/);
				this->update_mux(wtxn, mux, now, is_reader_mux, is_actual /*is_tuned_freq*/,
												 true /*from_sdt*/, preserve /*preserve*/);
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
	auto stream_mux_c = mux_common_ptr(stream_mux);

	auto epg_type = epg.epg_type;

	auto epg_source = epgdb::epg_source_t((epgdb::epg_type_t)(int)epg_type, info.table_id, info.version_number,
																				stream_mux_key->sat_pos, stream_mux_c->network_id, stream_mux_c->ts_id);

	if (epg.is_sky || epg.is_mhw2_title) {
		auto* service_key = bat_data.lookup_opentv_channel(epg.channel_id);
		if (!service_key) {
			bool done = bat_done();
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
		epg.epg_service.sat_pos = service_key->mux.sat_pos;
		epg.epg_service.network_id = service_key->network_id;
		epg.epg_service.ts_id = service_key->ts_id;
		epg.epg_service.service_id = service_key->service_id;
	} else if (epg.is_mhw2_summary) {
		// service will be looked up later
	} else {
		auto txn = chdb.rtxn();

		auto* p_mux_data = lookup_mux_data_from_sdt(txn, epg.epg_service.network_id, epg.epg_service.ts_id);
		txn.abort();
		if (!p_mux_data) {
			bool done = network_done(epg.epg_service.network_id);
			return done ? dtdemux::reset_type_t::NO_RESET : dtdemux::reset_type_t::RESET;
		}
		auto* p_mux_key = mux_key_ptr(p_mux_data->mux);
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

void active_si_stream_t::process_removed_services(db_txn& txn, chdb::any_mux_t& mux,
																									ss::vector_<uint16_t>& service_ids) {
	auto& mux_key = *mux_key_ptr(mux);
	auto c = chdb::service::find_by_mux_key(txn, mux_key);

	for (const auto& service : c.range()) {
#if 0
		if (service.k.mux != mux_key)
			break; // TODO: currently there is no way to iterate over services on a single mux
#else
		assert(service.k.mux == mux_key);
#endif
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
#if 0
		if (channel.k.chg != chg_key)
			break; // TODO: currently there is no way to iterate over services on a single mux
#else
		assert(channel.k.chg == chg_key);
#endif
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
		auto reader_mux = reader->stream_mux();
		sdt_data.mux_key = *chdb::mux_key_ptr(reader_mux);
	}

	if (!info.timedout && services.original_network_id == sdt_data.actual_network_id)
		scan_state.set_active(scan_state_t::SDT_NETWORK);

	auto wtxn = chdb_txn();
	/*
		find out what we already know about network_id,m ts_id
		is mux known in database, and what is its extra_id?
		have we seen it in NIT_ACTUAL?
	 */
	auto* p_mux_data = services.is_actual ?
		add_reader_mux_from_sdt(wtxn, services.original_network_id, services.ts_id) :
		lookup_mux_data_from_sdt(wtxn, services.original_network_id, services.ts_id);
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
		auto* p_mux_data = lookup_mux_data_from_sdt(txn, channel.service_key.network_id, channel.service_key.ts_id);

		if (!p_mux_data) {
			bool done = network_done(channel.service_key.network_id);
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
		auto* p_mux_key = mux_key_ptr(p_mux_data->mux);
		auto* mux_common = mux_common_ptr(p_mux_data->mux);
		chgm.service.mux = *p_mux_key;
		chgm.service.network_id = mux_common->network_id;
		chgm.service.ts_id = mux_common->ts_id;
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

		auto c1 = chdb::service::find_by_mux_key_sid(txn, chgm.service.mux, chgm.service.service_id);
		if (!c1.is_valid()) {
			bool done = mux_sdt_done(chgm.service.network_id, chgm.service.ts_id);
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
	dtdebug("setting si_processing_done=false");
	si_processing_done = false;
	scan_target = scan_target_ == scan_target_t::NONE ? scan_target_t::SCAN_FULL_AND_EPG : scan_target_;
	scan_state.reset();

	// add_table should be called in order of importance and urgency.

	// if nit.sat_id is not set, some tables will wait with parsing
	// until the correct nit_actual table is received.
	// We probably should deactivate this feature after a certain
	// timeout
}


active_si_stream_t::~active_si_stream_t() {
#if 0
	/*the following can cause a raise when receiver_thread attempts to call remove_active
		in tuner thread: when creating the task, the active_adapter shared_ptr is copied into the lambda,
		but then discivered to have been resleased. receiver_thread then calls ~active_adapter, which
		calls ~active_si_stream, but "reset" should only be called from tuner_thread

		reset(true /*close_streams*/);
#endif
}

/*
	mux is the one we are currently processsing si_data for
	However, the values in mux have been retrieved from SI data and/or database and may be incorect
	fix some obvious errors by comparing to what the frontend reports

	returns:
    locked: driver was locked and updated data is valid
		stream_id changed: driver returned stream_id different from the requested one
 */
std::tuple<bool, bool> active_si_stream_t::update_reader_mux_parameters_from_frontend(chdb::any_mux_t& mux) {
	auto& aa = active_adapter();
	assert(aa.lock_state.locked_minimal);
	if(aa.tune_state == active_adapter_t::TUNE_FAILED)
		return {false, false};
	auto monitor = aa.fe->monitor_thread;
	dttime_init();
	bool stream_id_changed{false};

	/*Obtain newest signal info
	 */
	auto signal_info_ = aa.fe->get_last_signal_info(true /*wait*/);
	assert(signal_info_);
	auto & signal_info = *signal_info_;
	dttime(200);

	auto& driver_stream_id = mux_key_ptr(signal_info.driver_mux)->stream_id;
	auto& mux_stream_id = mux_key_ptr(mux)->stream_id;
	if(driver_stream_id != mux_stream_id) {
		stream_id_changed = true;
		if(mux_stream_id >= 0 || !is_template(mux)) {
			dterror("Driver returned different stream id than requested: driver="
							 << signal_info.driver_mux << " mux=" << mux);
		}
		//assert(is_template(mux));
		mux_stream_id = driver_stream_id;
	}
	//assert(* mux_key_ptr(signal_info.driver_mux) == * mux_key_ptr(mux));
	if(is_template(mux)) {
		mux_common_ptr(mux)->tune_src = chdb::tune_src_t::DRIVER;
	}

	assert(mux_key_ptr(signal_info.driver_mux)->mux_id == 0 || mux_key_ptr(mux)->mux_id ==0 ||
				 mux_key_ptr(signal_info.driver_mux)->mux_id == mux_key_ptr(mux)->mux_id);


	*mux_common_ptr(signal_info.driver_mux) = 	*mux_common_ptr(mux); //overfride things like ts_id

	auto& si_mux = mux;

	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);
	bool driver_data_reliable = signal_info.driver_data_reliable;
	visit_variant(signal_info.driver_mux,
								[&](chdb::dvbs_mux_t& mux) {
									auto* p = std::get_if<chdb::dvbs_mux_t>(&si_mux);
									mux.k.t2mi_pid = p->k.t2mi_pid;
									assert(p);
									bool use_driver{false};
									if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
										//copy frequency and symbol_rate from si_mux
										if(!driver_data_reliable || (std::abs((int)mux.frequency - (int) p->frequency) < 50))
											mux.frequency = p->frequency;
										else
											use_driver = true;
										if(!driver_data_reliable || (std::abs((int)mux.symbol_rate - (int) p->symbol_rate) < 10000))
											mux.symbol_rate = p->symbol_rate;
										else
											use_driver = true;
										if(use_driver)
											mux.c.tune_src = chdb::tune_src_t::DRIVER;
									}

									/*override user entered "auto" data in signal_info.mux modulation data,
										e.g. because blindscan is not well supported */
									if(!driver_data_reliable || mux.rolloff == chdb::fe_rolloff_t::ROLLOFF_AUTO)
										mux.rolloff = p->rolloff;
									if(!driver_data_reliable || mux.modulation == chdb::fe_modulation_t::QAM_AUTO)
										mux.modulation = p->modulation;
									if(driver_data_reliable)
										p->matype = mux.matype; /* set si_mux.matype from driver info (which is the only source for it)*/
								},
								[&](chdb::dvbc_mux_t& mux) {
									auto* p = std::get_if<chdb::dvbc_mux_t>(&si_mux);
									assert(p);
									if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
										if(!driver_data_reliable || (std::abs((int)mux.frequency - (int) p->frequency) < 50))
											mux.frequency = p->frequency;
										if(!driver_data_reliable || (std::abs((int)mux.symbol_rate - (int) p->symbol_rate) < 10))
											mux.symbol_rate = p->symbol_rate;
									}
									if(!driver_data_reliable || mux.modulation == chdb::fe_modulation_t::QAM_AUTO) {
										mux.modulation = p->modulation;
									}

								},
								[&](chdb::dvbt_mux_t& mux) {
									auto* p = std::get_if<chdb::dvbt_mux_t>(&si_mux);
									assert(p);
									if(p->c.tune_src == chdb::tune_src_t::NIT_TUNED) {
										if(!driver_data_reliable || (std::abs((int)mux.frequency - (int) p->frequency) < 50))
											mux.frequency = p->frequency;
									}
									if(!driver_data_reliable || mux.modulation == chdb::fe_modulation_t::QAM_AUTO) {
										mux.modulation = p->modulation;
									}
								});

	dtdebug("Update driver_mux=" << signal_info.driver_mux << " stream_mux=" << reader->stream_mux()
					<< " tune_src=" << (int)chdb::mux_common_ptr(si_mux)->tune_src);
	mux = signal_info.driver_mux;

	return {(signal_info.lock_status.fe_status & FE_HAS_LOCK), stream_id_changed};
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
		if (!is_t2mi && pmt.pmt_pid == 256 && desc.stream_type ==  stream_type::stream_type_t::PES_PRIV
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
				namespace m = chdb::update_mux_preserve_t;
				auto preserve = m::flags{ m::MUX_COMMON & ~ m::SCAN_STATUS};
				if(mux_common_ptr(mux)->scan_status == chdb::scan_status_t::ACTIVE) {
					mux_common_ptr(mux)->scan_status = chdb::scan_status_t::PENDING;
					preserve = m::flags(preserve | m::SCAN_STATUS); //avoid changing ACTIVE to pending
				}
				auto wtxn = chdb_txn();
				namespace m = chdb::update_mux_preserve_t;
				this->update_mux(wtxn, mux, now, false /*is_reader_mux*/, true /*is_tuned_freq*/,
												 false /*from_sdt*/, preserve);
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
bool active_si_stream_t::matches_reader_mux(const chdb::any_mux_t& mux, bool from_sdt)
{
	/*
		t2mi pids start with the nid/tid from their parent mux. So tid may be incorrect
	*/
	auto* c = chdb::mux_common_ptr(mux);
	bool check_pat = !is_embedded_si || (c->tune_src == chdb::tune_src_t::DRIVER || c->tune_src == chdb::tune_src_t::NIT_TUNED);
	if ( check_pat && ! pat_data.has_ts_id(from_sdt ? c->ts_id : c->nit_ts_id))
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
		auto* mux_common = mux_common_ptr(mux);
		mux_common->ts_id = ts_id;
		mux_common->key_src = chdb::key_src_t::PAT_TUNED;
	}
	namespace m = chdb::update_mux_preserve_t;
	this->update_mux(wtxn, mux, now, true /*is_active_mux*/, true /*is_tuned_freq*/,
									 false /*from_sdt*/, found ? m::NONE : m::MUX_KEY /*preserve*/);
}

void active_si_stream_t::save_pmts(db_txn& wtxn)
{
	using namespace chdb;
	auto stream_mux = reader->stream_mux();
	auto* stream_mux_key = mux_key_ptr(stream_mux);
	auto& mux_common = *mux_common_ptr(stream_mux);
	auto mux_key = *stream_mux_key;
	assert(mux_key.mux_id >0);

	assert(!chdb::is_template(stream_mux));
	ss::string<32> mux_desc;
	assert (stream_mux_key->sat_pos != sat_pos_none);
	int count{0};
	chdb::to_str(mux_desc, stream_mux);

	if (nit_data.by_network_id_ts_id.size()==1){
		for(auto &[key, val]: nit_data.by_network_id_ts_id) {
			mux_common.network_id = key.first;
			mux_common.ts_id = key.second;
		}
	}

	if(sdt_data.actual_network_id >=0 && sdt_data.actual_ts_id >= 0) {
		mux_common.network_id = sdt_data.actual_network_id;
		mux_common.ts_id = sdt_data.actual_ts_id;
		}

	for (auto& [service_id, pat_service]: pmt_data.by_service_id) {
		//pat entry for ts_id has priority
		for (auto& [ts_id, pat_table]:  pat_data.by_ts_id) {
			for(auto& e: pat_table.entries) {
				if(e.service_id == pat_service.pmt.service_id) {
					mux_common.ts_id = ts_id;
					break;
				}
			}
		}
		service_key_t service_key(mux_key, mux_common.network_id, mux_common.ts_id, pat_service.pmt.service_id);
		auto c = service_t::find_by_key(wtxn, mux_key, service_key.service_id);
		auto service = c.is_valid() ? c.current() : service_t{};
		if(! c.is_valid()) {
			service.k = service_key;
			service.name.sprintf("Service %s:%d", mux_desc.c_str(), pat_service.pmt.service_id);
		}
		auto new_service = service;
		std::visit([&](auto&mux) {
			auto pol = get_member(mux, pol, chdb::fe_polarisation_t::NONE);
			new_service.frequency = mux.frequency;
			new_service.pol = pol;
		}, stream_mux);

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
