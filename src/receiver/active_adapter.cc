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

#include <ctype.h>
#include <fcntl.h>
#include <resolv.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>
#include <values.h>
#include "active_adapter.h"
#include "active_service.h"
#include "receiver.h"
#include "streamfilter.h"
#include "util/neumovariant.h"
#include "util/template_util.h"
#include <algorithm>
#include <errno.h>
#include <iomanip>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
using namespace chdb;

#define FREQ_MULT 1000

#define CBAND_LOF 5150000

/** @brief Check the status of the card
		Returns must_tune, must_restart_si, must_stop_si, is_not_ts (boolean)
*/
std::tuple<bool, bool, bool, bool> active_adapter_t::check_status() {
	if (!fe)
		return {};
	auto status = fe->get_lock_status();
	bool is_locked = status.is_locked();
	bool temp_tune_failure = status.has_soft_tune_failure();
	bool is_not_ts = status.is_not_ts();
	bool is_dvb = status.is_dvb();
	int lock_lost = status.lock_lost;
	bool must_tune = false;
	bool relocked_now = false;
	bool tune_failed = false;
	lock_state.tune_lock_result = status.tune_lock_result();
	lock_state.locked_normal = is_locked;
	lock_state.locked_minimal = lock_state.tune_lock_result >= chdb::lock_result_t::CAR;
	lock_state.temp_tune_failure = temp_tune_failure;
	lock_state.is_not_ts = is_not_ts;
	lock_state.is_dvb = is_dvb;
	if(temp_tune_failure)
		tune_state = TUNE_FAILED_TEMP;
	switch (tune_state) {
	case TUNE_INIT: {
		assert(0);
		must_tune = true;
		isi_processing_done = false;
		dtdebugx("Initial tuning");
	} break;
	case LOCK_TIMEDOUT:
	case WAITING_FOR_LOCK: {
		if (lock_lost && is_locked) {
			dtdebugx("Lock was lost; restarting si");
			relocked_now = true;
			isi_processing_done = false;
		} else if (is_locked) {
			dtdebugx("First lock detected");
			tune_state = LOCKED;
			relocked_now = true;
			isi_processing_done = false;
		} else { // not locked
			if ((now - tune_start_time) >= tune_timeout && fe) {
				auto retune_mode = fe->ts.readAccess()->tune_options.retune_mode;
				if (retune_mode == retune_mode_t::AUTO) {
					dtdebug("Timed out while waiting for lock; retuning for " << current_tp());
					must_tune = true;
				}
				tune_state = LOCK_TIMEDOUT;
			}
		}
	} break;
	case TUNE_FAILED:
	case TUNE_FAILED_TEMP:
		tune_failed = true;
		break;
	case LOCKED:
		if (lock_lost) {
			if (is_locked) {
				dtdebugx("Lock was lost; restarting si");
				relocked_now = true;
				isi_processing_done = false;
			} else {
				dtdebug("tuner no longer locked; retuning for " << current_tp());
				must_tune = true;
				isi_processing_done = false;
			}
		} else {
		}
		break;
	}
	return {must_tune, relocked_now, tune_failed, is_not_ts};
}

int active_adapter_t::lnb_activate(const devdb::rf_path_t& rf_path,
																	 const devdb::lnb_t& lnb, tune_options_t tune_options) {
#if 0
	scan_mux_end_reported = false;
#endif
	this->fe->start_fe_and_lnb(rf_path, lnb);

	switch (tune_options.tune_mode) {
	case tune_mode_t::SPECTRUM:
		return lnb_spectrum_scan(rf_path, lnb, tune_options);
	default:
		assert(0);
		//fall through
	case tune_mode_t::POSITIONER_CONTROL: {
		auto [ret, new_usals_sat_pos] = fe->diseqc(true /*skip_positioner*/);
		if(new_usals_sat_pos != sat_pos_none)
			lnb_update_usals_pos(new_usals_sat_pos, sat_pos_none);

		if(ret<0) {
			dterrorx("diseqc failed: err=%d", ret);
			return ret;
		}
	}
		break;
	}
	return 0;
}

int active_adapter_t::tune(const devdb::rf_path_t& rf_path,
													 const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, tune_options_t tune_options,
													 bool user_requested, const devdb::resource_subscription_counts_t& use_counts) {
	if(!fe) {
		dterror("Tune failed fe=null: mux=" << mux);
		return -1;
	}
	if(user_requested) {
		tune_state = TUNE_INIT;
	}
#if 0
	scan_mux_end_reported = false;
#endif
	assert(tune_state != TUNE_FAILED);
	auto [ret, new_usals_sat_pos] = fe->tune(rf_path, lnb, mux, tune_options, user_requested, use_counts);
	if(ret>=0 && new_usals_sat_pos != sat_pos_none)
		lnb_update_usals_pos(new_usals_sat_pos, mux.k.sat_pos);
	auto tune_mode = fe->ts.readAccess()->tune_options.tune_mode;
	tune_start_time = system_clock_t::now();
	tune_state = ret<0 ? TUNE_FAILED: WAITING_FOR_LOCK;
	dtdebugx("tune: done ret=%d tun_mode=%d\n", ret, (int) tune_mode);
	return ret;
}

template<>
int active_adapter_t::retune<chdb::dvbs_mux_t>() {
	auto mux = std::get<chdb::dvbs_mux_t>(current_tp());
	bool user_requested = false;
	devdb::lnb_t lnb;
	devdb::resource_subscription_counts_t use_counts;
	{
		auto devdb_rtxn = receiver.devdb.rtxn();
		auto rf_path = current_rf_path();
		auto rf_coupler_id = current_rf_coupler_id();
		use_counts = devdb::fe::subscription_counts(devdb_rtxn, rf_path, rf_coupler_id,
																								nullptr /*fe_key_to_release*/);
		devdb_rtxn.abort();
	}
	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	return tune(current_rf_path(), current_lnb(), mux, tune_options, user_requested, use_counts);
}

template <typename mux_t> inline int active_adapter_t::retune() {
	auto mux = std::get<mux_t>(current_tp());
	bool user_requested = false;

	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	return tune(mux, tune_options, user_requested);
}

int active_adapter_t::restart_tune(const chdb::any_mux_t& mux) {
	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	visit_variant(
		mux,
		[this, &tune_options](const dvbs_mux_t& mux) {
			bool user_requested = true;
			devdb::resource_subscription_counts_t use_counts;
			{
				auto devdb_rtxn = receiver.devdb.rtxn();
				auto rf_coupler_id = current_rf_coupler_id();
				use_counts = devdb::fe::subscription_counts(devdb_rtxn, current_rf_path(), rf_coupler_id,
																										nullptr /*fe_key_to_release*/);
				devdb_rtxn.abort();
			}
			tune(current_rf_path(), current_lnb(), mux, tune_options, user_requested, use_counts);
		},
		[this, &tune_options](const dvbc_mux_t& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		},
		[this, &tune_options](const dvbt_mux_t& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		});
	return 0;
}



template<typename mux_t>
int active_adapter_t::tune(const mux_t& mux, tune_options_t tune_options, bool user_requested) {
	if(!fe)
		return -1;
	if(user_requested) {
		tune_state = TUNE_INIT;
	}
#if 0
	scan_mux_end_reported = false;
#endif
	auto ret = fe->tune(mux, tune_options, user_requested);
	tune_start_time = system_clock_t::now();
	tune_state = ret<0 ? TUNE_FAILED: WAITING_FOR_LOCK;
	return 0;
}


int active_adapter_t::remove_all_services(rec_manager_t& recmgr) {
	// special type of loop because monitors map will be erased and iterators are invalidated
	for (auto it = active_services.begin(); it != active_services.end();) {
		auto& [pmt_pid, active_servicep] = (it++)->second; // it is incremented because it will be invalidated
		recmgr.remove_live_buffer(*active_servicep);
		remove_service(*active_servicep);
	}
	assert(active_services.size() == 0);
	return 0;
}

int active_adapter_t::remove_service(active_service_t& channel) {
	auto service_id = channel.get_current_service().k.service_id;
	auto it = active_services.find(service_id);
	if (it == active_services.end()) {
		dterrorx("Request to deactivate non active channel: service_id=%d", service_id);
		return -1;
	}

	auto& [pmt_pid_, active_servicep] = it->second;
	assert(active_servicep.get());
	auto& active_service = *active_servicep;
	dtdebugx("calling active_service.service_thread.deactivate");
	active_service.service_thread.stop_running(true);

	active_services.erase(it);
	return 0;
}

int active_adapter_t::add_service(active_service_t& active_service) {
	active_service.service_thread.start_running();
	const auto ch = active_service.get_current_service();
	dtdebug("TUNE ADD_CHANNEL STARTED " << ch.name);
	{
		// pmt_pid is set to null_pid until we read it from PAT table
		active_services[ch.k.service_id] = std::make_tuple(null_pid, active_service.shared_from_this());
	}
	return 0;

	dtdebug("TUNE ADD_CHANNEL ENDED " << ch.name);
}

void active_adapter_t::on_stable_pat() {
	usals_timer.end();
}

void active_adapter_t::on_first_pat() {
	usals_timer.stamp();
}


//called periodically from tuner thread
void active_adapter_t::monitor() {
	assert(fe);
	auto tune_mode = fe->ts.readAccess()->tune_options.tune_mode;
	if(tune_mode != tune_mode_t::NORMAL && tune_mode != tune_mode_t::BLIND) {
		dtdebugx("adapter %d NO MONITOR: tune_mode=%d", get_adapter_no(), (int) tune_mode);
		return;
	}
	if(tune_state == TUNE_INIT) {
		dtdebugx("adapter %d NO MONITOR: tune_mode=%d", get_adapter_no(), (int) tune_mode);
		return;
	}
	//dtdebugx("adapter %d MONITOR: tune_mode=%d", get_adapter_no(), (int) tune_mode);

	bool must_retune{false};
	bool relocked_now{false};
	bool tune_failed{false};
	bool is_not_ts{false};
	dttime_init();
	if (si.abort_on_wrong_sat()) {
		dtdebug("Attempting retune (wrong sat detected)");
		must_retune = true;
	} else {
		std::tie(must_retune, relocked_now, tune_failed, is_not_ts) = check_status();
	}
	dttime(200);
	assert(!(relocked_now && must_retune)); // if must_retune si will be inited anyway
	if(relocked_now) {
		last_new_matype_time = steady_clock_t::now();
		if (!is_not_ts ) {
			/* we first start to lock, or we relock after lock was lost*/

			assert(lock_state.locked_normal); //implied by relocked_now==true
			/*
				The stream is locked and was not locked before. The stream is a transport stream.
				start si processing
			*/
			auto t = fe->ts.readAccess()->tune_options.scan_target;
			ss::string<128> prefix;
			prefix << "TUN" << this->get_adapter_no();
			log4cxx::NDC ndc(prefix.c_str());
			init_si(t);
			return;
		} else {
			if(!si.fix_tune_mux_template()) {
				//lock was lost
				si.reset_si(false/*close_streams*/);
				return;
			}
		}
	}

	if(tune_state == tune_state_t::LOCKED || tune_state == tune_state_t::LOCK_TIMEDOUT) {
		check_isi_processing();
	}
	if (must_retune) {
		dtdebug("Calling si.reset with force_finalize=true\n");
		si.reset_si(false /*close_streams*/);
		visit_variant(
			current_tp(),
			[this](dvbs_mux_t&& mux) { retune<dvbs_mux_t>();},
			[this](dvbc_mux_t&& mux) { retune<dvbc_mux_t>(); },
			[this](dvbt_mux_t&& mux) { retune<dvbt_mux_t>(); });
		dttime(200);
		return;
	}

	if((is_not_ts && isi_processing_done) || tune_failed ||  tune_state == tune_state_t::LOCK_TIMEDOUT
		 || (tune_state == active_adapter_t::LOCKED && ! is_not_ts && !lock_state.is_dvb)
		) {
		end_si();
		return;
	}


	/*usually scan_report will be called by process_si_data, but on bad muxes data may not
		be present. scan_report runs with a min frequency of 1 call per 2 seconds
	*/
	si.scan_report();
	dttime(200);
	for (auto& [pid, si] : embedded_si_streams) {
		si.scan_report();
	}
}


int active_adapter_t::lnb_spectrum_scan(const devdb::rf_path_t& rf_path,
																				const devdb::lnb_t& lnb, tune_options_t tune_options) {

	set_current_tp({});
	auto [ret, new_usals_sat_pos] = fe->lnb_spectrum_scan(rf_path, lnb, tune_options);
	if(new_usals_sat_pos != sat_pos_none)
		lnb_update_usals_pos(new_usals_sat_pos, sat_pos_none);

	dtdebug("spectrum: diseqc done");
	return ret;
}

active_adapter_t::active_adapter_t(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_)

	: receiver(receiver_)
	, fe(fe_)
	, tuner_thread(receiver_.tuner_thread)
	,	si(receiver, std::make_unique<dvb_stream_reader_t>(*this, -1), false)
{
}


active_adapter_t::~active_adapter_t() {
	dtdebugx("~active_adapter_t: %p. Adapter %d frontend %d destroyed", this, get_adapter_no(), frontend_no());
}

int active_adapter_t::open_demux(int mode) const {
	ss::string<PATH_MAX> demux_fname;
	const int demux_no = 0; // are there any adapters on wwich demux_no!=0? If so how to associate them with frontends?
	demux_fname.sprintf("%s%d/demux%d", DVB_DEV_PATH, get_adapter_no(), demux_no);

	int fd = ::open(demux_fname.c_str(), mode);
	return fd;
}


static void set_lnb_lof_offset(db_txn& devdb_wtxn, devdb::lnb_t& lnb, int band, int nit_frequency, int lof_offset) {

	auto c = devdb::lnb_t::find_by_key(devdb_wtxn, lnb.k);
		/*note that we do not update the full lnb (e.g., when called from positioner_dialog user may want
			to not save some changes.
			So instead, we copy most of the lnb from the database
		*/
	if(c.is_valid())
		lnb = c.current();

	if (lnb.lof_offsets.size() <= band + 1) {
		// make sure both entries exist
		for (int i = lnb.lof_offsets.size(); i < band + 1; ++i)
			lnb.lof_offsets.push_back(0);
	}

	lnb.lof_offsets[band] = lof_offset;

	if (std::abs(lnb.lof_offsets[band]) > 5000)
		lnb.lof_offsets[band] = 0;

	put_record(devdb_wtxn, lnb);
}

/*
	Estimate the lnb's local offset frequency, by comparing driver data with NIT data, while allowing
	some errors in the NIT data.

	We keep a list of up to 19 muxes tuned (on any sat or polarision) and the differences between driver
	and nit frequency on those muxes. The median of the offsets is taken as the lof offset.
	When the list grows to 19, it is reduced to 17 muxes by removing the most extreme measured
	frequency differences.

	This approach will return the correct lof offset as soon as more muxes with correct nit frequency
	than with incorrect nit frequencies have been tuned.

	The estimates are also gradually updated over time.

 */
void active_adapter_t::update_lof(devdb::lnb_t& lnb, int16_t sat_pos, chdb::fe_polarisation_t pol,
																	int nit_frequency, int uncorrected_driver_freq) {
	using namespace devdb;
	auto band = devdb::lnb::band_for_freq(lnb, nit_frequency);

	auto devdb_wtxn = receiver.devdb.wtxn();

	tuned_frequency_offsets_key_t k{lnb.k, band};
	auto c = devdb::tuned_frequency_offsets_t::find_by_key(devdb_wtxn, k);
	auto offsets_record = c.is_valid() ? c.current() : tuned_frequency_offsets_t{k, {}, {}};
	auto& offsets = offsets_record.frequency_offsets;
	tuned_frequency_offset_t offset{sat_pos, (uint32_t) nit_frequency, pol, uncorrected_driver_freq - nit_frequency};
	bool found{false};

	for(auto& o: offsets) {
		if ((int)o.nit_frequency == nit_frequency) {
			float learning_rate = 0.5;
			o.frequency_offset += learning_rate * (uncorrected_driver_freq - nit_frequency - o.frequency_offset);
			found = true;
			break;
		}
	}

	if(!found)
		offsets.push_back(offset);

	auto cmp = [](const auto &a, const auto  &b) {
		return a.frequency_offset < b.frequency_offset;
	};

	if(offsets.size()>=19) {
		//remove lowest
		auto m = offsets.begin();
		std::nth_element(offsets.begin(), m,  offsets.end(), cmp);
		offsets.erase(0);

		//remove largest
		m= offsets.end()-1;
		std::nth_element(offsets.begin(), m,  offsets.end(), cmp);
		offsets.erase(offsets.size()-1);
	}

	//compute median
	auto n = offsets.size()/2;
	auto m = offsets.begin() +n ;
		std::nth_element(offsets.begin(), m,  offsets.end(), cmp);
	auto lof_offset = m->frequency_offset;

	put_record(devdb_wtxn, offsets_record);

	set_lnb_lof_offset(devdb_wtxn, lnb, (int)band, nit_frequency, lof_offset);
	devdb_wtxn.commit();

}

/*
	called before active_adapter is destroyed
 */
int active_adapter_t::deactivate() {
	reset_si();
	active_services.clear();
	auto* fe = this->fe.get();
	auto fefd = fe->ts.readAccess()->fefd;
	dtdebugx("Release fe_fd=%d", fefd);
	fe->release_fe();
	tune_state = TUNE_INIT;
	return 0;
}

devdb::usals_location_t active_adapter_t::get_usals_location() {
	auto r = receiver.options.readAccess();
	return r->usals_location;
}

/*
	set a new usals_pos, for tuning to sat at sat_pos (which may be left unspecified as sat_pos_none)
 */
void active_adapter_t::lnb_update_usals_pos(int16_t usals_pos, int16_t sat_pos) {
	auto loc = this->get_usals_location();
	auto devdb_wtxn = receiver.devdb.wtxn();
	auto lnb = this->fe->ts.readAccess()->reserved_lnb;
	int ret = devdb::dish::update_usals_pos(devdb_wtxn, lnb, usals_pos, loc, sat_pos);
	if( ret<0 )
		devdb_wtxn.abort();
	else
		devdb_wtxn.commit();

	auto w = this->fe->ts.writeAccess();
	w->reserved_lnb = lnb;
	if (usals_pos != lnb.usals_pos) {
		// measure how long it takes to move positioner
		usals_timer.start(lnb.usals_pos, usals_pos);
		w->reserved_lnb.usals_pos = usals_pos;
		assert(w->dbfe.rf_inputs.size()>0);
	}
}

/*
	Called when user edits lnb parameters.
	Todo: What if the parameters cause a change in dish, usals_pos...?
 */
void active_adapter_t::update_current_lnb(const devdb::lnb_t& lnb) {
	fe->ts.writeAccess()->reserved_lnb = lnb;
	assert(fe->ts.readAccess()->dbfe.rf_inputs.size()>0);
};

//only called from active_si_stream.h
void active_adapter_t::on_tuned_mux_change(const chdb::any_mux_t& mux) {
	fe->update_tuned_mux_nit(mux);
}

void active_adapter_t::update_received_si_mux(const std::optional<chdb::any_mux_t>& mux,
																							bool is_bad) {
	fe->update_received_si_mux(mux, is_bad);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_dvb_stream_reader(ssize_t dmx_buffer_size) {
	return std::make_shared<dvb_stream_reader_t>(*this, dmx_buffer_size);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_embedded_stream_reader(
	const chdb::any_mux_t& embedded_mux, ssize_t dmx_buffer_size) {
	auto sf = stream_filters.writeAccess();
	auto* mux_key = chdb::mux_key_ptr(embedded_mux);
	auto [it, found] = find_in_map(*sf, mux_key->t2mi_pid);
	std::shared_ptr<stream_filter_t> substream;
	if (found) {
		substream = it->second;
	} else {
		substream = std::make_shared<stream_filter_t>(*this, embedded_mux, &tuner_thread.epx);
		(*sf)[mux_key->t2mi_pid] = substream;
	}
	return std::make_shared<embedded_stream_reader_t>(*this, substream);
}

/*
	start si processing for an embedded stream on a mux;
	Note that the substream's si processing cannot be removed until the active_adapter
	is deactived or tuned to a new mux. This is a limitation of the current code.

	A better approach would be to introduce subscriptions for each substream; even then,
	we face the complication that the si processing code can start embedded stream processing
	itself when it detects t2mi_pids during pmt processing. In that case, some "internal"
	subscription should be added as well

	Returns true if an embedded stream was added and false if stream was already started
 */

bool active_adapter_t::add_embedded_si_stream(const chdb::any_mux_t& embedded_mux, bool start) {
	auto* mux_key = chdb::mux_key_ptr(embedded_mux);
	auto [it, found] = find_in_map(embedded_si_streams, mux_key->t2mi_pid);
	if (found) {
		dtdebugx("Ignoring request to add the same si stream twice");
		return false;
	}
	auto reader = make_embedded_stream_reader(embedded_mux);
	const bool is_embedded_si{true};
	auto [it1, inserted] =
		embedded_si_streams.try_emplace((uint16_t)mux_key->t2mi_pid, receiver, std::move(reader), is_embedded_si);
	assert(inserted);
	if (start) {
		auto scan_target = fe->ts.readAccess()->tune_options.scan_target;
		it1->second.init(scan_target);
	}
	return true;
}

bool active_adapter_t::read_and_process_data_for_fd(const epoll_event* evt) {
	if (si.read_and_process_data_for_fd(evt)) {
		return true;
	}
	for (auto& [pid, si] : embedded_si_streams) {
		if (si.read_and_process_data_for_fd(evt)) {
			return true;
		}
	}
	return false;
}

void active_adapter_t::init_si(scan_target_t scan_target) {
	/*@When we are called on a t2mi mux, there could be confusion between the t2mi mux (with t2mi_pid set)
		and the one without. To avoid this, we find the non-t2mi_pid version first
	*/
	bool failed = ! si.init(scan_target);
	if(failed)
		return;
	for (auto& [pid, si_] : embedded_si_streams) {
		failed = ! si_.init(si.scan_target);
		if(failed)
			return;
	}
	return;
}




/*
	set scan_status when starting to tune the mux
	Return a mux record with the updated status
	Also update the database, but only in case the input mux is not
	a template; there are two cases to consider:
    1. mux.k.mux_id = 0 and is_template(mux). This means: tuning by specifying (approx) frequency,
		  which may not yet be in the database. We do not enter a record in the database
			and will wait until we are sure the mux can actually be tuned (not handled in this function)

			note that tune_src =  tune_src_t::TEMPLATE in thisd case, and is not changed

		2. mux.k.mux_id > 0 and !is_template(mux). This means: user wants to tune specific mux, whose
		  tuning data is already in the database. In this case, the input mux should be the data
			stored in the database, and there is no need to reread it
	In case mux.k.t2mi_pid >= 0, also set up the stream filter to be able to process embedded data

	callers:
	start=true when called from active_si_stream_t::pmt_section_cb
	start=true when called from receiver_thread_t::suscribe_mux (via restart_si)
	start=true when called from receiver_thread_t::subscription_mux_in_use

	start=false when called from tuner_thread_t::cb_t::tune
	start=false when called from tuner_thread_t::request_retune


	 */


chdb::any_mux_t active_adapter_t::prepare_si(chdb::any_mux_t mux, bool start,
																						 subscription_id_t subscription_id, uint32_t scan_id,
																						 bool add_to_running_mux) {
	namespace m = chdb::update_mux_preserve_t;
	dtdebug("prepare_si: mux=" << mux);
	/*
		add an embedded si stream and set the current_mux to to the encapsulating mux
	 */
	auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
	if (dvbs_mux && dvbs_mux->k.t2mi_pid >= 0) {
		dtdebug("mux " << *dvbs_mux << " is an embedded stream");
		auto master_mux = *dvbs_mux;
		if(add_embedded_si_stream(mux, start)) {
			auto mux_key = master_mux.k;
			master_mux.k.t2mi_pid = -1;
			if(!add_to_running_mux)
				set_current_tp(master_mux);
		}
		if(scan_id > 0) {
			assert((int) subscription_id >=0 );
			auto& si = embedded_si_streams.at(dvbs_mux->k.t2mi_pid);
			si.activate_scan(mux, subscription_id, scan_id);
		}
		return master_mux;
	} else {
		if(!add_to_running_mux)
			set_current_tp(mux);
		if(scan_id > 0) {
			assert((int) subscription_id >=0 );
			si.activate_scan(mux, subscription_id, scan_id);
		}
		return mux;
	}
}

/*
	Finalize all si processing, and notify scanners
 */
void active_adapter_t::end_si() {
#if 0 //in case tuning fails, adapter may not be open
	if(!is_open())
		return;
#endif
	for (auto& [pid, si_] : embedded_si_streams) {
		si_.end();
	}
	si.end();
}

/*
	Stop all si processing, and close down the resulting streams.
	also notifies scanners
 */
void active_adapter_t::reset_si() {
	if(!is_open()) {
		dtdebug("skipping; not open");
		return;
	}
	if (tune_state != tune_state_t::TUNE_INIT) {
		dtdebugx("resetting si_processing_done=%d\n", si.si_processing_done);
		for (auto& [pid, si_] : embedded_si_streams) {
			si_.reset_si(true /*close stream*/);
		}
		si.reset_si(true /*close streams*/);
		dtdebugx("resetting now: si_processing_done=%d\n", si.si_processing_done);
	} else {
		dtdebugx("skipping tune_state=%d", (int) tune_state);
	}
	embedded_si_streams.clear();
	stream_filters.writeAccess()->clear();
}

//called from tuner thread
void active_adapter_t::update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation) {
	/*we split the code in two parts to avoid deadlock between receiver and tuner thread
		that is caused by locking devdb and fe->ts simulataneously
	*/
	devdb::lnb_t lnb;
	int16_t sat_pos{sat_pos_none};
	uint32_t uncorrected_driver_freq;
	uint32_t nit_frequency;
	chdb::fe_polarisation_t pol;

	bool need_lof_offset_update{false};
	{
		auto w = fe->ts.writeAccess();
		if(!w->tune_confirmation.nit_actual_received && tune_confirmation.nit_actual_received
			 && w->received_si_mux && ! w->received_si_mux_is_bad)  {
			auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(& (*w->received_si_mux));
			if(dvbs_mux) {
				lnb = w->reserved_lnb;
				need_lof_offset_update = true;
				sat_pos = dvbs_mux->k.sat_pos;
				pol = dvbs_mux->pol;
				nit_frequency = dvbs_mux->frequency;
				uncorrected_driver_freq = w->last_signal_info->uncorrected_driver_freq;
			}
		}
		w->tune_confirmation = tune_confirmation;
	}
	if(need_lof_offset_update) {
		dtdebugx("adapter %d Updating LNB LOF offset", get_adapter_no());
		update_lof(lnb, sat_pos, pol, nit_frequency, uncorrected_driver_freq);
		fe->ts.writeAccess()->reserved_lnb = lnb;
	}
}


void active_adapter_t::check_isi_processing()
{
	if(isi_processing_done)
		return;
	const std::chrono::seconds timeout{10}; //seconds
	bool tune_failed = (tune_state == tune_state_t::LOCK_TIMEDOUT ||  tune_state == tune_state_t::TUNE_FAILED);
	if(!tune_failed && tune_state != tune_state_t::TUNE_FAILED_TEMP)
		check_for_new_streams();

	bool isi_stable = steady_clock_t::now() - last_new_matype_time >= timeout;
	bool isi_ready{processed_isis.count() >=255 || isi_stable || tune_failed};

	if(!isi_ready) {
		return;
	}

	isi_processing_done = true; //ensure that we run once
	if(!this->fe)
		return;

	if (!tune_failed) {
		if(tune_state == tune_state_t::LOCKED)
			if(processed_isis.count()==0) {
				check_for_non_existing_streams();
				return;
			}
	}

	/*processing after tune failed or timed out
		In this case it is impossible that SI data was processed
	*/

	auto mux = si.reader->stream_mux();
	if(!is_template(mux))
		check_for_unlockable_streams();
	si.finalize_scan();
	si.check_scan_mux_end();
}

void active_adapter_t::check_for_new_streams()
{
	auto signal_info_ = fe->get_last_signal_info(false /*wait*/);
	if(!signal_info_)
		return;
	auto& signal_info = *signal_info_;
	if(signal_info.last_new_matype_time == last_new_matype_time)
		return;
	last_new_matype_time = signal_info.last_new_matype_time;

	std::optional<db_txn> txn;
	auto get_txn = 	[this, &txn] () -> db_txn& {
		if(!txn)
			txn.emplace(receiver.chdb.wtxn());
		return *txn;
	};
	auto* mux_key = mux_key_ptr(signal_info.driver_mux);
	auto* c = mux_common_ptr(signal_info.driver_mux);
	auto scan_id = c->scan_id;
	int tuned_stream_id = mux_key_ptr(signal_info.driver_mux)->stream_id;

	auto tuned_mux = current_tp();
	bool is_scanning = mux_common_ptr(tuned_mux)->scan_status == scan_status_t::ACTIVE;
#ifndef NDEBUG
	int last_mux_id = mux_key->mux_id;
#endif
	*c = chdb::mux_common_t{};
	c->scan_result = chdb::scan_result_t::NOTS;
	c->scan_lock_result = lock_state.tune_lock_result;
	//c->scan_lock_result : unchanged
	//c->scan_duration: set per stream below
	//c->epg_scan // from database
	c->scan_status = scan_status_t::IDLE;
	c->scan_id = 0;
	//c->num_services // from database
	c->network_id = 0; //unknown
	c->ts_id = 0; //unknown
	c->nit_network_id = 0; //unknown
	c->nit_ts_id = 0; //unknown
	c->tune_src = tune_src_t::DRIVER;
	c->key_src = key_src_t::NONE;
	//c->mtime // auto changed
	//c->epg_types // from database

	auto ctemplate = *c; //make copy
	bool propagate_scan = si.reader->tune_options().propagate_scan;
	auto scan_start_time = receiver.scan_start_time();

	for(auto ma: signal_info.matype_list) {
		auto stream_id = ma & 0xff;
		if(this->processed_isis.test(stream_id))
			continue;
		if(stream_id == tuned_stream_id)
			continue;
		last_new_matype_time = signal_info.last_new_matype_time;
		//we have found a new stream_id
		this->processed_isis.set(stream_id);
		auto matype = ma >> 8;
		auto is_dvb = ((ma >> 14) & 0x3) == 0x3;

		/*c->mux_id should be the same for all streams
		 */

		/* note: s m->modulation, m->fec cannot be found from matype. Assume they are the same;
			 m->rolloff could be found*/
		visit_variant(
			signal_info.driver_mux,
			[stream_id, matype](chdb::dvbs_mux_t& m) { m.k.stream_id= stream_id; m.matype=matype;},
			[stream_id](chdb::dvbc_mux_t& m) {m.k.stream_id= stream_id;},
			[stream_id](chdb::dvbt_mux_t& m) {m.k.stream_id= stream_id;});

		namespace m = chdb::update_mux_preserve_t;

		auto update_scan_status = [&](const chdb::mux_common_t* pdbc, const chdb::mux_key_t* pdbk) {
			bool is_active = pdbc && pdbc->scan_status == scan_status_t::ACTIVE;
			if( is_active) {
				return false;
			}
			if(is_dvb) {
				if(!pdbc) { //there is no mux for this stream yet; create one
					c->scan_status = is_scanning ? scan_status_t::PENDING : scan_status_t::IDLE;
					c->scan_id = is_scanning ? scan_id : 0;
				} else {
					if(!propagate_scan || pdbc->mtime >= scan_start_time || pdbc->scan_status == scan_status_t::ACTIVE)
						return false; //leave scanning to future subscription, or scanning was already done
					*c = *pdbc;
					c->scan_status = scan_status_t::PENDING;
					c->scan_id = is_scanning ? scan_id : 0;
				}
			} else { //not dvb
				*c = ctemplate;
				c->scan_time = system_clock_t::to_time_t(now);
				c->scan_duration = std::chrono::duration_cast<std::chrono::seconds>(system_clock_t::now()
																																					- tune_start_time).count();
			}
			return true;
		};

		//The following inserts a mux for each discovered multistream, but only if none exists yet
		auto& wtxn = get_txn();
		assert(chdb::mux_key_ptr(signal_info.driver_mux)->t2mi_pid == -1);
		chdb::update_mux(wtxn, signal_info.driver_mux, now, m::flags{m::ALL & ~m::SCAN_STATUS &
				~m::SCAN_DATA}, update_scan_status, /*true ignore_key,*/ false /*ignore_t2mi_pid*/,
			false /*must_exist*/);
		assert (mux_key->mux_id > 0);
		assert(last_mux_id == 0 || mux_key->mux_id == last_mux_id);
#ifndef NDEBUG
		last_mux_id = mux_key->mux_id;
#endif
	}
	if(txn) {
		txn->commit();
		dtdebug("committed");
	}
}

void active_adapter_t::check_for_unlockable_streams()
{
	if(!(tune_state == tune_state_t::LOCK_TIMEDOUT || tune_state == tune_state_t::TUNE_FAILED))
		return;
	auto mux = current_tp();
	auto* c = chdb::mux_common_ptr(mux);
	c->scan_result = (tune_state == tune_state_t::TUNE_FAILED) ? scan_result_t::BAD : scan_result_t::NOLOCK;
	auto chdb_wtxn = receiver.chdb.wtxn();
	chdb::clear_all_streams_pending_status(chdb_wtxn, now, mux);
	chdb_wtxn.commit();
	dtdebug("committed");
}

void active_adapter_t::check_for_non_existing_streams()
{
	assert (tune_state == tune_state_t::LOCKED);
	auto mux = current_tp();
	auto* c = chdb::mux_common_ptr(mux);
	assert(processed_isis.count()==0);
	c->scan_result=scan_result_t::BAD;
	auto chdb_wtxn = receiver.chdb.wtxn();
	chdb::clear_all_streams_pending_status(chdb_wtxn, now, mux);
	chdb_wtxn.commit();
	dtdebug("committed");
}

//instantiations
template
int active_adapter_t::tune<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, tune_options_t
																							tune_options, bool user_requested);

template
int active_adapter_t::tune<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, tune_options_t
																							tune_options, bool user_requested);
