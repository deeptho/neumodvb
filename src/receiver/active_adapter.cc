/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include "neumodemux.h"
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

//needed to implement active_adapter_t::si_streams
static inline bool operator<(const si_key_t& a, const si_key_t& b) {
	return
		(a.stream_id == b.stream_id) ? (a.t2mi_pid < b.t2mi_pid)
		: (a.stream_id < b.stream_id);
}


using namespace chdb;

/** @brief for a retune on next monitor cycle
*/
void active_adapter_t::force_retune() {
		fe->request_retune(tuner_thread, false/*user_requested*/);
		tune_state = TUNE_REQUESTED;
}

/** @brief Check the status of the card
		Returns must_tune, must_restart_si, must_stop_si, is_not_ts (boolean)
*/
std::tuple<bool, bool, bool, bool> active_adapter_t::check_status() {
	using namespace devdb;

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
	bool tune_failed = status.fem_state == fem_state_t::FAILED;
	lock_state.tune_lock_result = status.tune_lock_result();
	lock_state.locked_normal = is_locked;
	lock_state.locked_minimal = lock_state.tune_lock_result >= chdb::lock_result_t::CAR;
	lock_state.temp_tune_failure = temp_tune_failure;
	lock_state.is_not_ts = is_not_ts;
	lock_state.is_dvb = is_dvb;
	if(tune_failed)
		tune_state = TUNE_FAILED;
	else if(temp_tune_failure)
		tune_state = TUNE_FAILED_TEMP;
	switch (tune_state) {
	case TUNE_INIT: {
		assert(0);
		must_tune = true;
		isi_processing_done = false;
		dtdebugf("Initial tuning");
	} break;
	case TUNE_REQUESTED:
		if (status.fem_state == fem_state_t::SEC_POWERED_UP ||
				status.fem_state == fem_state_t::MONITORING) {
			tune_state = WAITING_FOR_LOCK;
			tune_start_time = system_clock_t::now();
		}
		break;
	case LOCK_TIMEDOUT:
	case WAITING_FOR_LOCK: {
		if (lock_lost && is_locked) {
			dtdebugf("Lock was lost; restarting si");
			relocked_now = true;
			isi_processing_done = false;
		} else if (is_locked) {
			dtdebugf("First lock detected");
			tune_state = LOCKED;
			relocked_now = true;
			isi_processing_done = false;
		} else { // not locked
			if ((now - tune_start_time) >= tune_timeout && fe) {
				auto retune_mode = fe->ts.readAccess()->tune_options.retune_mode;
				if (retune_mode == retune_mode_t::AUTO) {
					dtdebugf("Timed out while waiting for lock; retuning for {}", tuned_mux());
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
				dtdebugf("Lock was lost");
				/*lock ws lost according to the driver, but our si processing is still running. This is to be distinghuished
					from after retune, in which case we are in the WAITING_FOR_LOCK state, which would need starting si from scratch.
					TODO: Instead, we should signal to our caller a new flag "stop_si", which is incompatible with must_tune and
					with relocked_now, and which signifies: do not retune, but close si, and restart it. "stop_si" is not suitable
					as it allso calls "on_scan_mux_end". During scanning, we also prefer not to restart si processing, as we could
					end up which an andlessly repeating loop.
				 */
			} else {
				dtdebugf("tuner no longer locked; retuning mux={}", tuned_mux());
				must_tune = true;
				isi_processing_done = false;
			}
		} else {
		}
		break;
	}
	return {must_tune, relocked_now, tune_failed, is_not_ts};
}

int active_adapter_t::lnb_activate(const subscribe_ret_t& sret, subscription_options_t tune_options) {
	assert(si_streams.size() == 0);
	auto& aa = sret.aa;
	return this->fe->request_positioner_control(tuner_thread, *aa.updated_new_dbfe, *aa.rf_path, *aa.lnb, tune_options);
}

/*
	called after tuning or when lock is lost
 */
void active_adapter_t::reset()
{
	tune_state = TUNE_INIT;
	processed_isis.reset();
	isi_processing_done = false;
	lock_state = {};
	usals_timer = {};
	for (auto& [pid, si] : si_streams) {
		si.reset();
	}
	si_is_on = false;
}


std::optional<chdb::any_mux_t> active_adapter_t::mux_for_key(const chdb::mux_key_t& mux_key) const {
	for (auto& [pid, si] : si_streams) {
		auto& key = *chdb::mux_key_ptr(si.dbmux);
		if (key == mux_key) {
			return si.dbmux;
		}

		for(auto& [subscription_id, mux] : si.subscriptions) {
			auto& key = *chdb::mux_key_ptr(mux);
			if (key == mux_key) {
				return si.dbmux;
			}
		}
	}
	return {};
};

std::map <si_key_t, active_si_stream_t>::iterator active_adapter_t::si_ptr_for_subscription(subscription_id_t subscription_id) {
	auto ret = this->si_streams.end();
	for (auto it = this->si_streams.begin(); it != this->si_streams.end(); ++it) {
		auto& si = it->second;
		if (si.subscription_exists(subscription_id)) {
			ret = it;
		}
	}
	return ret;
};


void active_adapter_t::on_pmt_update(const chdb::any_mux_t& mux) {
	auto& mux_key = *chdb::mux_key_ptr(mux);
	auto it = si_streams.find({mux_key.stream_id, mux_key.t2mi_pid});
	assert (it != si_streams.end());
	auto &si = it->second;
	si.dbmux = mux;
}

subscription_id_t active_adapter_t::tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
																						 subscription_options_t tune_options) {
	this->fe->update_dbfe(*sret.aa.updated_new_dbfe);
#ifndef NDEBUG
	auto sub_to_reuse_removed =	sret.sub_to_reuse!=subscription_id_t::NONE &&  !this->subscription_exists(sret.sub_to_reuse);
	assert(!sub_to_reuse_removed);
#endif

	assert(tune_options.subscription_type == devdb::subscription_type_t::SUBSCRIBE ||
				 tune_options.subscription_type == devdb::subscription_type_t::MUX_SCAN ||
				 //The following occurs when called from positioner_dialog to tune on a specific rf_path
				 tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL);

	auto [error, must_full_tune, must_restart_tune]  = this->add_si_subscription(mux,  tune_options, sret);
	if(error) {
		dterrorf("Stream failed to start: mux={}", mux);
		return subscription_id_t::NONE;
	}

	assert(!must_full_tune || !	must_restart_tune);

	assert(this->subscription_exists(sret.subscription_id));
	if(!fe) {
		dterrorf("Tune failed fe=null: mux={}", mux);
		assert(false);
		return subscription_id_t::NONE;
	}

#ifndef NDEBUG
	assert(is_template(mux) == (chdb::mux_key_ptr(mux)->mux_id ==0));
#else
	if(is_template(mux))
		chdb::mux_key_ptr(mux)->mux_id =0;
#endif

	//LNB_CONTROL forces a tune, even when one would otherwise be refused
#ifndef NDEBUG
	if(tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL) {
		assert(must_full_tune || must_restart_tune);
	}
#endif
	if(must_full_tune) {
		this->tune_options = tune_options;
		fe->set_tune_options(tune_options);

		this->reset();
		auto ret1 = this->remove_stream(sret.subscription_id); //remove any streaming to the outside world
		if(ret1)
			dtdebugf("removed stream");

		ret1 = this->remove_service(sret.subscription_id);
		if(ret1)
			dtdebugf("removed service");
		assert(this->main_si);
		visit_variant(mux,
									[this, &sret, &tune_options](const chdb::dvbs_mux_t& m) {
										assert(sret.aa.lnb);
										auto& aa = sret.aa;
										fe->request_tune(tuner_thread, *aa.rf_path, *aa.lnb, m, tune_options);
										},
									[this, &tune_options](const chdb::dvbc_mux_t& m) {
										fe->request_tune(m, tune_options);
									},
									[this, &tune_options](const chdb::dvbt_mux_t& m) {
										fe->request_tune(m, tune_options);
									});

		tune_state = TUNE_REQUESTED;
	}
	if(must_restart_tune) {
		this->reset();
		this->force_retune();
	}
	dtdebugf("Subscribed: subscription_id={:d}", (int) sret.subscription_id);
	return sret.subscription_id;
}

int active_adapter_t::remove_all_services() {
	std::vector<task_queue_t::future_t> futures;
	{
		auto& m = *subscribed_active_services.readAccess();
		for(auto& [subscription_id, aa] : m) {
			auto& service_thread = aa->service_thread;
			if(!service_thread.must_exit())
				futures.push_back(service_thread.stop_running(false/*wait*/));
		}
	}
	wait_for_all(futures, true /*clear all errors*/);
	auto& m = *subscribed_active_services.writeAccess();
	m.clear();
	return 0;
}

int active_adapter_t::remove_service(subscription_id_t subscription_id) {
	auto fn = [&](subscription_id_t subscription_id) ->auto
	{
		auto &m = *this->subscribed_active_services.writeAccess();
		auto [it, found] = find_in_map(m, subscription_id);
		auto as = it->second;
		auto num = m.size();
		int count{0}; //number of other subscriptions using the active_service
		if(found) {
			m.erase(it);
			assert (num >= 1);
			num--;
			for(auto& [subscription_id_, as_]: m) {
				assert(subscription_id_ != subscription_id);
				count += (as_.get() == as.get());
			}
		}
		return std::tuple{as, found, num, count};
	};
	auto [as, found,  num_active_services, other_subscriptions_count] = fn(subscription_id);
	if (!found) {
		//dterrorf("Request to deactivate non active service: subscription_id={:d}", (int)subscription_id);
		return -1;
	}
	auto& active_service = *as;
	auto service = active_service.get_current_service();
	if(other_subscriptions_count >= 1)  { //other subscriptions still use the service (e.g. a recording)
		dtdebugf("Not ending service for subscription {}: service={} num={} count={}", (int) subscription_id, service,
						 num_active_services, other_subscriptions_count);
	} else {
		dtdebugf("Ending service for subscription {}: service={} num={} count={}", (int) subscription_id, service,
						 num_active_services, other_subscriptions_count);
		tuner_thread.remove_live_buffer(service);
		auto& service_thread = active_service.service_thread;
		service_thread.stop_running(true/*wait*/);
		as.reset(); //cause active_service_t to be destroyed
	}
	return num_active_services;
}

int active_adapter_t::add_service(subscription_id_t subscription_id, active_service_t& active_service) {
	const auto& service = active_service.get_current_service();
	{
		auto& m = *subscribed_active_services.writeAccess();
		// pmt_pid is set to null_pid until we read it from PAT table
		m[subscription_id] = active_service.shared_from_this();
	}
	return 0;
}

void active_adapter_t::on_stable_pat() {
	auto e = usals_timer.end();
	if(!e)
		return;
	auto [end_time, usals_pos_start, usals_pos_end] = *e;
 	if(usals_pos_end == usals_pos_start)
		return;
	auto ret =
		fe->get_positioner_move_stats(usals_pos_start, usals_pos_end, end_time);
	if(!ret) {
		dtdebugf("positioner did not move");
		return;
	}
	auto [ old_angle, new_angle, move_time_ms, speed, pol ] = *ret;
	if (std::abs(new_angle - old_angle) <10)
		return;

	dtdebugf("positioner moved from {:d} to {:d} in {:d}ms = {:f} degree/s pol={}",
					 old_angle, new_angle, move_time_ms, speed, to_str(pol));
	fmt::print("positioner moved from {:d} to {:d} in {:d}ms = {:f} degree/s pol={}\n",
						 old_angle, new_angle, move_time_ms, speed, to_str(pol));


}

void active_adapter_t::on_first_pat(bool restart) {
	usals_timer.stamp(restart);
}


//called periodically from tuner thread
void active_adapter_t::monitor() {
	assert(fe);
	auto tune_mode = fe->ts.readAccess()->tune_mode;
	if(tune_mode != devdb::tune_mode_t::NORMAL && tune_mode != devdb::tune_mode_t::BLIND) {
		//dtdebugf("adapter {:d} NO MONITOR: tune_mode={:d}", get_adapter_no(), (int) tune_mode);
		return;
	}
	if(tune_state == TUNE_INIT) {
		//dtdebugf("adapter {:d} NO MONITOR: tune_mode={:d}", get_adapter_no(), (int) tune_mode);
		return;
	}

	bool must_retune{false};
	bool relocked_now{false};
	bool tune_failed{false};
	bool is_not_ts{false};
	dttime_init();
	if (this->main_si && this->main_si->abort_on_wrong_sat()) {
		dtdebugf("Attempting retune (wrong sat detected)");
		must_retune = true;
	} else {
		std::tie(must_retune, relocked_now, tune_failed, is_not_ts) = check_status();
	}
	dttime(200);
	assert(!(relocked_now && must_retune)); // if must_retune si will be inited anyway
	if(relocked_now) {
		last_new_matype_time = steady_clock_t::now();
		auto signal_info_ = fe->get_last_signal_info(false /*wait*/);
		assert(lock_state.locked_normal); //implied by relocked_now==true
		this->on_lock(*signal_info_, is_not_ts);
		if (!is_not_ts ) {
			/* we first start to lock, or we relock after lock was lost*/

			/*
				The stream is locked and was not locked before. The stream is a transport stream.
				start si processing
			*/
			init_si(signal_info_->driver_mux, signal_info_->driver_data_reliable);
			return;
		}
	}

	if(tune_state == tune_state_t::LOCKED || tune_state == tune_state_t::LOCK_TIMEDOUT) {
		if(this->main_si)
			check_isi_processing();
	}
	if (must_retune) {
		dtdebugf("Calling si.close");
		assert(this->main_si || this->si_streams.size()==0);
		if(this->main_si)
			this->main_si->reset(); //calls on_scan_mux_end
		fe->request_retune(tuner_thread, false/*user_requested*/);
		tune_state = TUNE_REQUESTED;
		dttime(200);
		return;
	}

	if((is_not_ts && isi_processing_done) || tune_failed ||  tune_state == tune_state_t::LOCK_TIMEDOUT
		 || (tune_state == active_adapter_t::LOCKED && ! is_not_ts && !lock_state.is_dvb)
		) {
		this->end_si();
		return;
	}


	/*usually scan_report will be called by process_si_data, but on bad muxes data may not
		be present. scan_report runs with a min frequency of 1 call per 2 seconds
	*/
	for (auto& [pid, si] : si_streams) {
		si.scan_report(this->si_is_on);
	}
}

int active_adapter_t::lnb_spectrum_scan(const subscribe_ret_t& sret, subscription_options_t tune_options) {
	assert(si_streams.size() == 0);
	assert(subscribed_active_services.readAccess()->size() == 0);
	assert(streamers.size() == 0);
	auto& aa = sret.aa;
	assert(aa.rf_path);
	assert(aa.lnb);
	fe->request_lnb_spectrum_scan(tuner_thread, *aa.updated_new_dbfe, *aa.rf_path, *aa.lnb, tune_options);
	return 0;
}

active_adapter_t::active_adapter_t(receiver_t& receiver_, std::shared_ptr<dvb_frontend_t>& fe_)
	:
	receiver(receiver_)
	, fe(fe_)
	, tuner_thread(receiver_, *this)
{
}

void active_adapter_t::destroy() {
#ifndef NDEBUG
	cb(tuner_thread); //test being called from correct thread
	assert(!tuner_thread.has_exited());
#endif
	dtdebugf("~active_adapter_t: {:p}. Adapter {:d} frontend {:d} destroyed",
					 fmt::ptr(this), get_adapter_no(), frontend_no());
	this->deactivate();
	dtdebugf("calling deactivate adapter {:d} done", this->get_adapter_no());
}


active_adapter_t::~active_adapter_t() {
	dtdebugf("~active_adapter_t: {:p}. Adapter {:d} frontend {:d} destroyed",
					 fmt::ptr(this), get_adapter_no(), frontend_no());
}

int active_adapter_t::open_demux(int mode) const {
	ss::string<PATH_MAX> demux_fname;
	const int demux_no = 0; // are there any adapters on wwich demux_no!=0? If so how to associate them with frontends?
	demux_fname.format("{:s}{:d}/demux{:d}", DVB_DEV_PATH, get_adapter_no(), demux_no);

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
	dtdebugf("adapter {:d} Updating LNB LOF offset: nit_f={} driver_f={} offset={}", get_adapter_no(),
					 nit_frequency, uncorrected_driver_freq, lof_offset);
	set_lnb_lof_offset(devdb_wtxn, lnb, (int)band, nit_frequency, lof_offset);
	devdb_wtxn.commit();

}

/*
	called before active_adapter is destroyed
 */
int active_adapter_t::deactivate() {
	remove_all_services();
	this->end_si();
	this->si_streams.clear();
	this->main_si = nullptr;
	stream_filters.writeAccess()->clear();
	auto* fe = this->fe.get();
	auto fefd = fe->ts.readAccess()->fefd;
	dtdebugf("Release fe_fd={:d}", fefd);
	fe->release_fe();
	tune_state = TUNE_INIT;
	dtdebugf("Release fe_fd={:d} DONE", fefd);
	return 0;
}

devdb::usals_location_t active_adapter_t::get_usals_location() {
	auto r = receiver.options.readAccess();
	return r->usals_location;
}


/*
	Called when user edits lnb parameters.
	Todo: What if the parameters cause a change in dish?
	returns true if usals_pos has changed
 */
bool active_adapter_t::update_current_lnb_from_gui(const devdb::lnb_t& lnb) {
	auto w = fe->ts.writeAccess();
	bool usals_pos_changed = (lnb.on_positioner && lnb.usals_pos != w->reserved_lnb.usals_pos);
	w->reserved_lnb = lnb;
	assert(w->dbfe.rf_inputs.size()>0);
	return usals_pos_changed;
};

//only called from active_si_stream.h
void active_adapter_t::on_stream_mux_change(const chdb::any_mux_t& mux) {
	if(!si_is_on) {
		dtdebugf("Skipping on_stream_mux_change for mux={}", mux);
		return;
	}
	auto old_mux = tuned_mux();
	auto& old_mux_key = *chdb::mux_key_ptr(old_mux);
	auto& mux_key = *chdb::mux_key_ptr(mux);

	if(old_mux_key.stream_id == (int)ANY_STREAM_ID_FILTER)
		old_mux_key.stream_id = chdb::mux_key_ptr(mux)->stream_id;

	bool is_tuned_mux = (old_mux_key.stream_id == mux_key.stream_id && mux_key.t2mi_pid == -1);

	if(mux_key != old_mux_key &&old_mux_key.mux_id>0) {
		dtdebugf("Mux key changed from {} to {}", old_mux_key, *chdb::mux_key_ptr(mux));
	}
	if(is_tuned_mux)
		fe->update_tuned_mux_nit(mux);
}

void active_adapter_t::update_received_si_mux(const std::optional<chdb::any_mux_t>& mux,
																							bool is_bad) {
	fe->update_received_si_mux(mux, is_bad);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_dvb_stream_reader
(const chdb::any_mux_t& mux, ssize_t dmx_buffer_size) {
	return std::make_shared<dvb_stream_reader_t>(*this, mux, dmx_buffer_size);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_embedded_stream_reader(
	const chdb::any_mux_t& embedded_mux, ssize_t dmx_buffer_size) {
	auto sf = stream_filters.writeAccess();
	auto& mux_key = *chdb::mux_key_ptr(embedded_mux);
	auto [it, found] = find_in_map(*sf, mux_key.t2mi_pid);
	std::shared_ptr<stream_filter_t> substream;
	if (found) {
		substream = it->second;
	} else {
		auto embedding_type = chdb::get_embedding_type(embedded_mux);
		switch(embedding_type) {
		case chdb::embedding_type_t::NONE:
			assert(false);
			break;
		case chdb::embedding_type_t::T2MI:
			substream = std::make_shared<t2mi_stream_filter_t>(*this, embedded_mux, &tuner_thread.epx);
			break;
		case chdb::embedding_type_t::TS: {
				auto mux_key = *chdb::mux_key_ptr(embedded_mux);
				assert(mux_key.sat_pos != sat_pos_none);
				auto service_id = mux_key.t2mi_pid; //todo: rename this
				mux_key.t2mi_pid = -1 ; //convert to parent mux
				auto txn = receiver.chdb.rtxn();
				auto ec = chdb::service_t::find_by_key(txn, mux_key, service_id);
				if(!ec.is_valid()) {
					user_errorf("Could not find embedding service for embedded mux {}", embedded_mux);
					return nullptr;
				}
				auto service = ec.current();
				txn.abort();

				substream = std::make_shared<ts_in_ts_stream_filter_t>(*this, embedded_mux, service, &tuner_thread.epx);

		}
			break;
		}

		(*sf)[mux_key.t2mi_pid] = substream;
	}
	return std::make_shared<embedded_stream_reader_t>(*this, embedded_mux, substream);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_stream_reader
(const chdb::any_mux_t& mux, ssize_t dmx_buffer_size) {
	auto embedding_type = chdb::get_embedding_type(mux);
	auto use_embedded_reader =
		((embedding_type == chdb::embedding_type_t::T2MI) && !this->fe->ts.readAccess()->dbfe.supports.t2mi) ||
		embedding_type ==  chdb::embedding_type_t::TS;
	if(use_embedded_reader)
		return make_embedded_stream_reader(mux, dmx_buffer_size);
	else
		return this->make_dvb_stream_reader(mux, dmx_buffer_size);
}


active_si_stream_t* active_adapter_t::add_si_stream(const chdb::any_mux_t& mux) {
	auto& mux_key = *chdb::mux_key_ptr(mux);

	si_key_t k{mux_key.stream_id, mux_key.t2mi_pid};
	auto [it, found] = find_in_map(this->si_streams, k);
	if (found) {
		return &it->second;
	}
	auto reader = make_stream_reader(mux);
	if(!reader) {
		return nullptr;
	}
	auto is_main = si_streams.size()==0;
	auto [it1, inserted] =
		si_streams.try_emplace({mux_key.stream_id, mux_key.t2mi_pid}, receiver, std::move(reader), mux, is_main);
	assert(inserted);
	if(!this->main_si)
		this->main_si = &it1->second;
#ifndef NDEBUG
	else {
		auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&main_si->dbmux);
		for(auto& [subscription_id, m]: it1->second.subscriptions) {
			auto* tst = std::get_if<chdb::dvbs_mux_t>(&m);
			assert(!dvbs_mux || dvbs_mux->pol == tst->pol);
		}
	}
#endif
	return  &it1->second;
}


bool active_adapter_t::read_and_process_data_for_fd(const epoll_event* evt) {
	debug_xxx = true;
	for (auto& [mux_key, si] : this->si_streams) {
		if (si.read_and_process_data_for_fd(evt)) {
			return true;
		}
	}
	return false;
}

void active_adapter_t::on_lock(const signal_info_t& signal_info, bool is_not_ts) {
	/*When the frontend tunes, it asks for a specific stream_id>=0, or for stream_id=-1.
		When tuning succeeds, the requested isi will be returned if it exists.

		In some drivers, if stream_id==-1 and a multistream exists, the driver will return
		a randomly selected stream and the resulting stream_id may be >=0 and therefore different
		from the requested one. Similarly, when the user requests for a single stream during tune,
		a multistream may be returned. This happens on stid135

		In addition, for newer drivers, the demux can be requested to return a specific isi,t2mi_pid combination.
		In that case, only a transport stream for this specific combination will be returned, and isi is always
		what was requested.  Also, for t2mi, the drivers internally de-embed the desired transport stream

		As a special case, the stid135 drivers allow a bbframes mode, which can be activatated on a multistream only.
		In that case it is possible to receive transport streams for different isi then the one requested while tuning.
		The received isi is the one selected while configuring the demux.


		on_lock handles the case where the user accepts an arbitrary stream (ANY_STREAM_ID_FILTER)

		The other case where the driver was asked to tune stream_id=-1, but found a multistream (driver_mux.stream_id>=0)
		is not considered here and will not tune

		@todo: adjust drivers to distinghuish between stream_id=-1 meaning "no multi stream" and
		"pick any stream". Only stid135 makes this distinction and it creates additional confuson
		between stream_id also encodes teh (default) pls_code and pls_mode.

	*/

	/*first replace any si_streams with ANY_STREAM_ID_FILTER (wildcard) ISIs
		by entries with a specific ISI that is now selected*/
	auto any_node = this->si_streams.extract(si_key_t({(int)ANY_STREAM_ID_FILTER, -1}));
	auto any_stream_registered = !any_node.empty();
	auto& driver_mux_key = *chdb::mux_key_ptr(signal_info.driver_mux);
	if(any_stream_registered) {
		/*user wants any stream for an arbitrary isi. We pick the one that was selected
			during tuning
		 */
		si_key_t si_key{driver_mux_key.stream_id, -1 /* t2mi_pid*/};
		auto stream_registered= this->si_streams.contains(si_key);
		if(stream_registered) {
			/*There are two entries in si_streams, one for ANY_STREAM_ID_FILTER, and one
				for the specific ISI we have just selected. We remove the entry for  ANY_STREAM_ID_FILTER
				and keep the other one, while transferring subscriptions
			*/
			auto& any_si =  any_node.mapped();
			auto& si =  this->si_streams.at(si_key);
			si.scan_target =  std::max(si.scan_target, any_si.scan_target);
			si.is_main |= any_si.is_main;
			si.subscriptions.merge(any_si.subscriptions);
			for(const auto& ss: any_si.scan_state.scans_in_progress) {
				auto& [scan_id, subscription_id] = ss;
				if(!si.scan_state.scans_in_progress.contains({scan_id, subscription_id})) {
					si.scan_state.scans_in_progress.push_back({scan_id, subscription_id});
				}
			}
		} else {
			/*There is no entry yet for the just selected ISI. We replace the  (ANY_STREAM_ID_FILTER,-1) entry in si_streams
				for the specific one
				for the specific ISI we have just selected. We remove the entry for  ANY_STREAM_ID_FILTER
				and keep the other one, while transferring subscriptions
			*/

			//update the key
			any_node.key() = si_key;
			this->si_streams.insert(std::move(any_node));

			//update wildcard stream_id internal data in the si_streams entry
			auto& dbmux = this->si_streams.at(si_key).dbmux;
			auto& mux_key = *chdb::mux_key_ptr(dbmux);
			mux_key.stream_id = si_key.stream_id;
			assert(mux_key.t2mi_pid <0);

			//update wildcard stream_id in the internal data of the suscriptions
			auto & si = any_node.mapped();
			for (auto& [subscription_id, mux]: si.subscriptions) {
				auto& mux_key = *chdb::mux_key_ptr(mux);
				mux_key.stream_id = si_key.stream_id;
			}
		}
	}
	return;
}

void active_adapter_t::init_si(const chdb::any_mux_t& driver_mux, bool driver_data_reliable) {
	si_is_on = true;
	auto& driver_mux_key = *chdb::mux_key_ptr(driver_mux);
	si_key_t si_key{driver_mux_key.stream_id, driver_mux_key.t2mi_pid};
	auto stream_registered= this->si_streams.contains(si_key);
	if(!stream_registered) {
		dtdebugf("Driver mux without a registered stream isi={} t2mi_pid ={}", driver_mux_key.stream_id, driver_mux_key.t2mi_pid);
		return;
	}
	for (auto& [si_key, si] : this->si_streams) {
		si.init(driver_mux, driver_data_reliable);
	}
}

/*
	Create an active_si_stream and add it to an internal list. The active_si_stream will remain
	dormant until the tuner has actually been locked.

	Returns (must_tune, new_mux)
	 must_tune: frontend must be called to tune the same or a different frequency
	 new_mux: tuning parameters need to be changed as well
	if sret had a mux subscribed and the new mux differs from the old
	one.

	Notes and caveats:
	-when multiple subscribers share the same active activa adapters, the first one perform the tuning
	-it is possible that initial tuning causes the first subscriber to unsubscrbe before the second
	 one calls add_si_subscription (e.g., a mux scan which has found all required information exceptionally quickly).
	 In that case the next subscrier calling add_si_subscription must tune

	-all subscriptions sharing an active_adapter may have different, but compatible tune parameters (e.g., slightly different
	 frequency, symbol rate, different ISI when bbframes is on, different t2mi_pid) or tune options (e.g., retuning or not
	 when mux does not lock). It may even be the case that some of these parameters result in locking the mux and others don't.

	 Such differences and problems occur during the first phase of scanning, when parameters are still not fully known, but guessed.
	 Therefore the scanning code, operates in two phases
	  1. make guesses based on earlier scans; these often will end up sharing a mux, with the risk that wrong parameters lead to
		   not locking; often this will work anyway, teh correct parameters are found, and all candidates are merged in the database
		2. when this does not work, then a non-shared peak scan for each spectral peak

	possible special cases depending on:
   -was the calling subscriber already subscribed? If yes, then it is subscribing to a "compatible" mux (different ISI, T2MI_PID),
	  or subscribing to the exact same mux (e.g., changing service on the same mux)
	 -is tuning required or not? tuning is required for the first subscriber, or when tuning to a mux on a different frequency.
	  In that case the calling subscriber is the only remaining subscriber
	 -is forced retuning required? This is the case if the subscription is privilaged (positioner_dialog)

	 Returns:  tuple of two bools, only one of which can be true
	   -must_full_tune: a full tune must be performed (changed tune parameters)
		 -must_restart_tune: a retune is required with the previous tune parameters.

 */
std::tuple<bool, bool, bool> active_adapter_t::add_si_subscription(
	chdb::any_mux_t mux, const subscription_options_t& tune_options, const subscribe_ret_t& sret)
{
	bool error{false};
	dtdebugf("mux to subscribe={}", mux);
	//auto& mux_key = *mux_key_ptr(mux);
	auto it = si_ptr_for_subscription(sret.subscription_id);
	auto* p_si = (it == this->si_streams.end()) ? (active_si_stream_t*) nullptr : &it->second;
	auto is_existing_subscription = !!p_si;

	bool is_only_subscriber = (this->si_streams.size() == 0)  || //first tune
		((this->si_streams.size() == 1) && p_si && (p_si->subscriptions.size() == 1)); //single remaining subscriber


		/*check for compatibility between the currently tuned mux
		and the newly subscribed one.  needs_full_tune  is true in case of
		a change of frequency, or a change in ISI when bbframes are not in use.
		*/

	bool is_same_si = false;
	bool is_same_physical_mux = false;
	if(p_si && is_only_subscriber) {
		auto main_key = *chdb::mux_key_ptr(p_si->dbmux);
		auto mux_ =  mux; //need to take a copy as we are going to modify stream_id
		auto& key = *chdb::mux_key_ptr(mux_);
		is_same_si = main_key.stream_id == key.stream_id &&  main_key.t2mi_pid == key.t2mi_pid;
		if(sret.tune_pars.use_bbframes)
			key.stream_id = main_key.stream_id;
		key.t2mi_pid = main_key.t2mi_pid;
		is_same_physical_mux = key == main_key &&
			key.mux_id>0; //key.mux_id==0 is a peak; for real muxes the assumption is that same mux_id implies same tuning pars
		bool is_tuned_freq = matches_physical_fuzzy(mux_, p_si->dbmux, false /*check_sat_pos*/); //correct pol, stream_id, t2mid_pid, frequency; sat_pos may be off
		assert(is_tuned_freq || !is_same_physical_mux);
		if(is_tuned_freq)
			is_same_physical_mux = true;
	}

	bool must_full_tune = (is_only_subscriber ||
												 tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL)
		&&  (!is_same_physical_mux || tune_state != tune_state_t::LOCKED);
	if(!sret.aa.lnb && std::get_if<chdb::dvbs_mux_t>(&mux))
		must_full_tune = false;

	bool must_restart_tune = !must_full_tune && tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL;

	assert(!must_full_tune || is_only_subscriber ||
				 tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL);

	if(is_existing_subscription) {
		if(is_same_si && is_same_physical_mux) {
			/*no si processing restarting is needed
				a special case is when the mux is the same as before*/
			p_si->remove_si_subscription(sret.subscription_id);
			p_si->add_si_subscription(mux, tune_options.scan_target, sret.subscription_id);
		} else {
			dtdebugf("Replacing si_stream for subscription_id={} old_mux={} new_mux={}",  (int)sret.subscription_id, p_si->dbmux, mux);
			//assert(!is_only_subscriber || must_full_tune);
			bool no_more_subscriptions = this->remove_si_subscription(*sret.aa.updated_new_dbfe, sret.subscription_id);
			assert(!must_full_tune || no_more_subscriptions);

			p_si = this->add_si_stream(mux);
			p_si->add_si_subscription(mux, tune_options.scan_target, sret.subscription_id);
			if(si_is_on) {
				auto signal_info_ = this->get_last_signal_info(false/*wait*/);
				p_si->init(signal_info_->driver_mux, signal_info_->driver_data_reliable);
			}
		}
	} else { //!is_existing_subscription
#ifndef NDEBUG
		if(is_only_subscriber) {
			assert(!si_is_on);
			assert(must_full_tune);
		} else { //!is_only_subscriber
			assert(!must_full_tune || tune_options.subscription_type == devdb::subscription_type_t::LNB_CONTROL);
		}
#endif
		p_si = this->add_si_stream(mux);
		if (! p_si) {
			error = true;
			return { error, must_full_tune, must_restart_tune};
		}
		p_si->add_si_subscription(mux, tune_options.scan_target, sret.subscription_id);
	}
	return{error, must_full_tune, must_restart_tune};
}

/*
	returns stop_running
 */
bool active_adapter_t::unregister_subscription(const devdb::fe_t& updated_dbfe, subscription_id_t subscription_id)
{
	this->fe->update_dbfe(updated_dbfe);
	bool no_more_streams = (this->remove_stream(subscription_id) <=0);
	bool no_more_services = (this->remove_service(subscription_id) <=0);
	bool no_more_subscriptions = this->remove_si_subscription(updated_dbfe, subscription_id);
	bool stop_running = updated_dbfe.sub.subs.size() ==0 ||  (no_more_subscriptions && no_more_streams && no_more_services);
	return stop_running;
}

/*
	returns true if there are no remaining subscriptions
 */
bool active_adapter_t::remove_si_subscription(const devdb::fe_t& updated_dbfe, subscription_id_t subscription_id)
{

	auto it = si_ptr_for_subscription(subscription_id);
	if(it == this->si_streams.end()) {
		dterrorf("cannot find subscription to remove: subscription_id={}", (int) subscription_id);
		return true;
	}
	auto& si = it->second;
	bool no_longer_subscribed = si.remove_si_subscription(subscription_id);
	if(no_longer_subscribed) {
		//
		dtdebugf("active_adapter is no longer subscribed");
		si.end_si();
		si.reset();
		if (this->main_si == &si) {
			this->main_si = nullptr;
		}
		this->si_streams.erase(it);
	}

	dtdebugf("{} active_si_streams remaining",  this->si_streams.size());
	if(!this->main_si && si_streams.size()>0) {
		auto& si = si_streams.begin()->second;
		dtdebugf("transferring is_main_status to {}",  si.dbmux);
		si.is_main = true;
	}

	this->fe->update_dbfe(updated_dbfe);
	return  this->si_streams.size() == 0;
}

/*
	Finalize all si processing, and notify scanners
 */
void active_adapter_t::end_si() {
	for (auto& [si_key, si] : this->si_streams) {
		si.end_si();
	}
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
	if(this->main_si) {
		auto mux = this->main_si->dbmux;
		if(!is_template(mux))
			check_for_unlockable_streams();
		if(!main_si->si_processing_started) {
			dtdebugf("calling this->tuned_si->finalize_scan");
			this->main_si->finalize_scan();
			this->main_si->check_scan_mux_end();
		}
	}
}

void active_adapter_t::add_mux_for_scanning_(db_txn& wtxn, chdb::any_mux_t mux, time_t scan_start_time)
{
	bool propagate_scan = this->main_si->reader->tune_options().propagate_scan;
	auto* c = mux_common_ptr(mux);
	const auto& c1 = this->main_si->get_initial_mux_common();

	bool is_scanning = c1.scan_status == scan_status_t::PENDING;

	auto* mux_key = mux_key_ptr(mux);

	auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
	int matype = dvbs_mux ? dvbs_mux->matype: -1;
	auto is_dvb = !dvbs_mux || (((matype >> 6) & 0x3) == 0x3);

#if 0
	is_dvb=true; //assert(false); //fix this!
#endif
	auto& scan_id = c1.scan_id;
	assert(!scanner_t::is_scanning(scan_id) || scanner_t::is_our_scan(scan_id));
	c->scan_time =0;
	c->scan_result = chdb::scan_result_t::NOTS;
	c->scan_lock_result = lock_state.tune_lock_result;
	c->epg_scan_completeness =0;
	//c->scan_duration: set per stream below
	c->epg_scan = false;
	c->scan_status = scan_status_t::IDLE;
	c->scan_id = {};
	c->num_services =0;
	c->network_id = 0; //unknown
	c->ts_id = 0; //unknown
	c->nit_network_id = 0; //unknown
	c->nit_ts_id = 0; //unknown
	c->tune_src = tune_src_t::DRIVER;
	c->key_src = key_src_t::NONE;
	//c->mtime // auto changed
	//c->epg_types // from database

	auto ctemplate = *c; //make copy
	namespace m = chdb::update_mux_preserve_t;

	auto update_scan_status = [&](chdb::mux_common_t* pmergedc, chdb::mux_key_t* pmergedk,
																const chdb::mux_common_t* pdbc, const chdb::mux_key_t* pdbk) {
		bool is_active = pdbc && pdbc->scan_status == scan_status_t::ACTIVE;
		if( is_active) {
			return false;
		}
		if(is_dvb) {
			if(!pdbc) { //there is no mux for this stream yet; create one
				c->scan_status = is_scanning ? scan_status_t::PENDING : scan_status_t::IDLE;
				c->scan_id = is_scanning ? scan_id : chdb::scan_id_t{};
			} else {
				if(!propagate_scan || pdbc->mtime >= scan_start_time || pdbc->scan_status == scan_status_t::ACTIVE)
					return false; //leave scanning to future subscription, or scanning was already done
				pmergedc->scan_status = scan_status_t::PENDING;
				pmergedc->scan_id = is_scanning ? scan_id : chdb::scan_id_t{};
				*c = *pmergedc;
			}
		} else { //not dvb
			*c = ctemplate;
#if 0
			c->scan_time = system_clock_t::to_time_t(now);
			c->scan_duration = std::chrono::duration_cast<std::chrono::seconds>(system_clock_t::now()
																																					- tune_start_time).count();
#endif
		}
		return true;
	};

	assert(chdb::mux_key_ptr(mux)->t2mi_pid == -1);
	chdb::update_mux(wtxn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS}, update_scan_status, /*true ignore_key,*/ false /*ignore_t2mi_pid*/,
		false /*must_exist*/);
	assert (mux_key->mux_id > 0);
}

void active_adapter_t::check_for_new_streams()
{
	auto signal_info_ = fe->get_last_signal_info(false /*wait*/);
	if(!signal_info_)
		return;
	assert(this->main_si);
	if(!this->main_si)
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
	//auto* mux_key = mux_key_ptr(signal_info.driver_mux);
	auto* c = mux_common_ptr(signal_info.driver_mux);
	auto* k = mux_key_ptr(signal_info.driver_mux);
	*c = this->main_si->get_initial_mux_common();
	k->t2mi_pid = -1;
	auto& scan_id = c->scan_id;
	assert(!scanner_t::is_scanning(scan_id) || scanner_t::is_our_scan(scan_id));
	int tuned_stream_id = mux_key_ptr(signal_info.driver_mux)->stream_id;
	//bool is_scanning = scanner_t::is_scanning(scan_id);
	auto scan_start_time = receiver.scan_start_time();
	auto is_template = chdb::is_template(this->main_si->dbmux);
	for(auto ma: signal_info.matype_list) {
		auto stream_id = ma & 0xff;
		if(this->processed_isis.test(stream_id)) //already processed
			continue;
		if(stream_id == tuned_stream_id && !is_template)
			continue;
		last_new_matype_time = signal_info.last_new_matype_time;
		//we have found a new stream_id
		this->processed_isis.set(stream_id);
		auto matype = ma >> 8;

		/*c->mux_id should be the same for all streams; the mux_key of the streams will
			differ because of a differetn stream_id
		*/

		/* note: s m->modulation, m->fec cannot be found from matype. Assume they are the same;
			 m->rolloff could be found*/
		visit_variant(
			signal_info.driver_mux,
			[stream_id, matype](chdb::dvbs_mux_t& m) { m.k.stream_id = stream_id; m.matype = matype;},
			[stream_id](chdb::dvbc_mux_t& m) {m.k.stream_id = stream_id;},
			[stream_id](chdb::dvbt_mux_t& m) {m.k.stream_id = stream_id;});

		auto& wtxn = get_txn();
		this->add_mux_for_scanning_(wtxn, signal_info.driver_mux, scan_start_time);
	}
	if(txn) {
		txn->commit();
		dtdebugf("committed");
	}
}

void active_adapter_t::check_for_unlockable_streams()
{
	if(!(tune_state == tune_state_t::LOCK_TIMEDOUT || tune_state == tune_state_t::TUNE_FAILED))
		return;
	assert(this->main_si);
	if(!this->main_si)
		return;
	auto mux = tuned_mux();
	auto* c = chdb::mux_common_ptr(mux);
	*c = this->main_si->get_initial_mux_common();
	c->scan_result = (tune_state == tune_state_t::TUNE_FAILED) ? scan_result_t::BAD : scan_result_t::NOLOCK;
	auto chdb_wtxn = receiver.chdb.wtxn();
	chdb::clear_all_streams_pending_status(chdb_wtxn, now, mux);
	chdb_wtxn.commit();
	dtdebugf("checked for unlockable stream: tune_state={}", (int)tune_state);
}

void active_adapter_t::check_for_non_existing_streams()
{
	assert (tune_state == tune_state_t::LOCKED);
	auto mux = tuned_mux();
	auto* c = chdb::mux_common_ptr(mux);
	assert(processed_isis.count()==0);
	c->scan_result=scan_result_t::BAD;
	auto chdb_wtxn = receiver.chdb.wtxn();
	chdb::clear_all_streams_pending_status(chdb_wtxn, now, mux);
	chdb_wtxn.commit();
	dtdebugf("checked for non-existing streams");
}



std::shared_ptr<active_service_t>
active_adapter_t::tune_service_in_use(const subscribe_ret_t& sret,
																			const chdb::service_t& service) {
	if((int)sret.sub_to_reuse <0)
		return nullptr;
	auto [it, found] = find_in_safe_map(this->subscribed_active_services, sret.sub_to_reuse);
	if(!found)
		return nullptr;
	auto& active_servicep = it->second;
	if (active_servicep->get_current_service().k != service.k)
		return nullptr;

	/* The service is already subscribed
		 Unsubscribe_ our old mux and service (if any) by overwriting it with the found active_service
	*/
	{
		auto& m = *subscribed_active_services.writeAccess();
		m[sret.subscription_id] = active_servicep;
	}
	dtdebugf("[{}] sub={}: reusing existing service", service, (int) sret.subscription_id);
	return active_servicep;
}

std::shared_ptr<active_service_t>
active_adapter_t::tune_service(const subscribe_ret_t& sret,
															 const chdb::any_mux_t& mux,
															 const chdb::service_t& service,
															 const subscription_options_t& tune_options) {
	assert(sret.subscription_id != subscription_id_t::NONE);

	dtdebugf("Subscribe {} sub={}: tune start", service,(int) sret.subscription_id);
	this->remove_service(sret.subscription_id);

	/*
		If another subscription is tuned to the wanted service, we do not have to tune.
		We only place a reservation so that the service will remain tuned
	*/
	auto active_service_ptr = tune_service_in_use(sret, service);
	if (active_service_ptr.get())
		return active_service_ptr;
	// now create a new active_service, subscribe the related service and send instructions to start it
	auto subscription_id = this->tune_mux(sret, mux, tune_options);
	assert(subscription_id == sret.subscription_id);

	auto prefix =fmt::format("CH[{:d}:{}]", this->get_adapter_no(), service);
	log4cxx::NDC::push(prefix.c_str());
	auto reader = make_stream_reader(mux, -1);

	auto live_service = tuner_thread.add_live_buffer(service);
	active_service_ptr = std::make_shared<active_service_t>(*this, service, live_service, std::move(reader));
	active_service_ptr->add_pat_and_pmt_parsers();
	log4cxx::NDC::pop();
	// remember that this service is now in use (for future planning and for later unsubscription)

	add_service(sret.subscription_id, *active_service_ptr);
	active_service_ptr->service_thread.start_running();
	return active_service_ptr;
}

/*
	create a stream for a full mux
 */
devdb::stream_t active_adapter_t::add_stream
(const subscribe_ret_t& sret, const devdb::stream_t& stream, const chdb::any_mux_t& mux) {
	auto fd = active_adapter_t::open_demux(O_RDONLY);
	uint16_t pid= 0x2000; //full transport stream
	dtdebugf("Adding pid={}", pid);
	int dmx_buffer_size = 32 * 1024 * 1024;
	if(ioctl(fd, DMX_SET_BUFFER_SIZE, dmx_buffer_size)) {
		dterrorf("DMX_SET_BUFFER_SIZE failed: {}", strerror(errno));
	}

	auto bbframes_on = this->fe->ts.readAccess()->dbfe.sub.bbframes_on;
	auto mux_key = *chdb::mux_key_ptr(mux);
	if(dmx_set_mux(fd, mux_key, pid, bbframes_on) < 0)
		return {};

	if(ioctl (fd, DMX_START)<0) {
		dterrorf("DMX_START FAILED: {}", strerror(errno));
	}

	auto s = std::make_shared<streamer_t>(fd, stream);
	s->start();
	streamers[sret.subscription_id] = s;
	return s->stream;
}

int active_adapter_t::remove_stream(subscription_id_t subscription_id) {
	auto [it, found] = find_in_map(streamers, subscription_id);
	if(!found)
		return -1;
	assert(found);
	auto& streamer = *it->second;
	streamer.stop();
	streamers.erase(it);
	return streamers.size();
}


std::shared_ptr<active_service_t>
active_adapter_t::active_service_for_subscription(subscription_id_t subscription_id) {
	auto [it, found] = find_in_safe_map(this->subscribed_active_services, subscription_id);
	return found ? it->second : std::shared_ptr<active_service_t>{};
}

void active_adapter_t::on_message(active_service_t* active_service, const ss::string_& message) {
	ss::vector<subscription_id_t> subscription_ids;
	{
		auto& m = *subscribed_active_services.readAccess();
		for (auto& [subscription_id, as] : m) {
			if (as.get() == active_service) {
				subscription_ids.push_back(subscription_id);
			}
		}
	}
	receiver.on_message(message, subscription_ids);
}

std::optional<std::tuple<steady_time_t, int16_t, int16_t>> usals_timer_t::end() {
	if(!started) {
		return {};
	}
	started = false;
	auto end = stamped ? first_pat_time : steady_clock_t::now();
	return std::tuple<steady_time_t, int16_t, int16_t>{end, usals_pos_start, usals_pos_end};
}

/*
running mux subscription is released
lnb_activate calls request_positioner_control, which erases frontend reserved_mux (bad)
then calls si.read_and_process_data_for_fd which goes wrong because of the erased mux

instead active_adapter_t::reset should be called (perhaps only if no tone needs to be set: what happens with small
interruptions of tone?

lnb_activate should only be called if no mux is running
if a mux is running some permission bits in tune_pars in fe should be changed
perhaps retuning should be forced?

 */
