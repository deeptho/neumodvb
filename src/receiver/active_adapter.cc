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

#if 0
/** @brief Wait msec miliseconds
 */
static inline void msleep(uint32_t msec) {
	struct timespec req = {msec / 1000, 1000000 * (msec % 1000)};
	while (nanosleep(&req, &req))
		;
}
#endif

#define FREQ_MULT 1000

#define CBAND_LOF 5150000

/** @brief Check the status of the card
		Returns must_tune, must_restart_si (boolean)
*/
std::tuple<bool, bool> active_adapter_t::check_status() {
	if (!fe)
		return {};
	auto status = fe->get_lock_status();
	bool is_locked = status.fe_status & FE_HAS_LOCK;
	int lock_lost = status.lock_lost;
	bool must_tune = false;
	bool must_reinit_si = false;

	si.scan_state.locked = is_locked;
	switch (tune_state) {
	case TUNE_INIT: {
		assert(0);
		must_tune = true;
		dtdebugx("Initial tuning");
	} break;
	case WAITING_FOR_LOCK: {
		if (lock_lost && is_locked) {
			dtdebugx("Lock was lost; restarting si");
			must_reinit_si = true;
		} else if (is_locked) {
			dtdebugx("First lock detected");
			tune_state = LOCKED;
			must_reinit_si = true;
		} else { // not locked
			if ((system_clock_t::now() - tune_start_time) >= tune_timeout) {
				if (fe && fe->ts.readAccess()->tune_options.retune_mode == retune_mode_t::AUTO) {
					dtdebugx("Timed out while waiting for lock; retuning");
					must_tune = true;
				}
			}
		}
	} break;
	case LOCKED:
		if (lock_lost) {
			if (is_locked) {
				dtdebugx("Lock was lost; restarting si");
				must_reinit_si = true;
			} else {
				dtdebug("tuner no longer locked; retuning");
				must_tune = true;
			}
		} else {
		}
		break;
	}
	return {must_tune, must_reinit_si};
}

int active_adapter_t::lnb_activate(const devdb::lnb_t& lnb, tune_options_t tune_options) {
	this->fe->start_fe_and_lnb(lnb);
	switch (tune_options.tune_mode) {
	case tune_mode_t::SPECTRUM:
		return lnb_spectrum_scan(lnb, tune_options);
	case tune_mode_t::SCAN_BLIND:
		return lnb_blind_scan(lnb, tune_options);
	default:
		assert(0);
		//fall through
	case tune_mode_t::POSITIONER_CONTROL: {
		auto [ret, new_usals_sat_pos] = fe->diseqc(true /*skip_positioner*/);
		if(new_usals_sat_pos != sat_pos_none)
			lnb_update_usals_pos(new_usals_sat_pos);

		if(ret<0) {
			dterrorx("diseqc failed: err=%d", ret);
			return ret;
		}
	}
		break;
	}
	return 0;
}

int active_adapter_t::tune(const devdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, tune_options_t tune_options,
													 bool user_requested, const devdb::resource_subscription_counts_t& use_counts) {
	if(!fe)
		return -1;
	auto [ret, new_usals_sat_pos] = fe->tune(lnb, mux, tune_options, user_requested, use_counts);
	if(ret>=0 && new_usals_sat_pos != sat_pos_none)
		lnb_update_usals_pos(new_usals_sat_pos);

	tune_start_time = system_clock_t::now();
	tune_state = WAITING_FOR_LOCK;

	si.deactivate();
	dtdebugx("tune: done ret=%d\n", ret);
	return ret;
}

template<>
int active_adapter_t::retune<chdb::dvbs_mux_t>() {
	auto mux = std::get<chdb::dvbs_mux_t>(current_tp());
	bool user_requested = false;
	const bool is_retune{true};
	devdb::lnb_t lnb;
	devdb::resource_subscription_counts_t use_counts;
	{
		auto devdb_rtxn = receiver.devdb.rtxn();
		auto lnb_key = current_lnb().k;
		use_counts = devdb::fe::subscription_counts(devdb_rtxn, lnb_key);
		devdb_rtxn.abort();
	}
	si.reset(is_retune);
	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	return tune(current_lnb(), mux, tune_options, user_requested, use_counts);
}

template <typename mux_t> inline int active_adapter_t::retune() {
	auto mux = std::get<mux_t>(current_tp());
	bool user_requested = false;
	const bool is_retune{true};
	si.reset(is_retune);

	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	return tune(mux, tune_options, user_requested);
}

int active_adapter_t::restart_tune() {
	//TODO: needless read here, followed by write within tune()
	auto tune_options = fe->ts.readAccess()->tune_options;
	visit_variant(
		current_tp(),
		[this, &tune_options](dvbs_mux_t&& mux) {
			bool user_requested = true;
			devdb::resource_subscription_counts_t use_counts;
			{
				auto devdb_rtxn = receiver.devdb.rtxn();
				use_counts = devdb::fe::subscription_counts(devdb_rtxn, current_lnb().k);
				devdb_rtxn.abort();
			}
			tune(current_lnb(), mux, tune_options, user_requested, use_counts);
		},
		[this, &tune_options](dvbc_mux_t&& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		},
		[this, &tune_options](dvbt_mux_t&& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		});
	return 0;
}



template<typename mux_t>
int active_adapter_t::tune(const mux_t& mux, tune_options_t tune_options, bool user_requested) {
	if(!fe)
		return -1;
	auto ret = fe->tune(mux, tune_options, user_requested);

	tune_state = WAITING_FOR_LOCK;

	ret = fe->tune_(mux, tune_options);
	if (ret < 0)
		return ret;
	tune_start_time = system_clock_t::now();
	si.deactivate();
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
	if (fe && fe->ts.readAccess()->tune_options.tune_mode != tune_mode_t::NORMAL)
		return;
	bool must_retune{false};
	bool must_reinit_si{false};
	assert(!(must_reinit_si && must_retune)); // if must_retune si will be inited anyway

	if (si.abort_on_wrong_sat()) {
		dtdebug("Attempting retune (wrong sat detected)");
		must_retune = true;
	} else {
		std::tie(must_retune, must_reinit_si) = check_status();
	}

	if (must_retune) {
		visit_variant(
			current_tp(),
			[this](dvbs_mux_t&& mux) { retune<dvbs_mux_t>();},
			[this](dvbc_mux_t&& mux) { retune<dvbc_mux_t>(); },
			[this](dvbt_mux_t&& mux) { retune<dvbt_mux_t>(); });
	} else if (must_reinit_si) {
		auto t = fe->ts.readAccess()->tune_options.scan_target;
		init_si(t);
	} else {
		/*usually scan_report will be called by process_si_data, but on bad muxes data may not
			be present. scan_report runs with a max frequency of 1 call per 2 seconds
		*/
		si.scan_report();

	}

}

int active_adapter_t::lnb_blind_scan(const devdb::lnb_t& lnb, tune_options_t tune_options) {
	return 0;
}

int active_adapter_t::lnb_spectrum_scan(const devdb::lnb_t& lnb, tune_options_t tune_options) {
	//dttime_init();
	// needs to be at very start!
#if 0
	bool need_diseqc = true;
#endif
	set_current_tp({});

	auto band = tune_options.spectrum_scan_options.band_pol.band;
	auto pol = tune_options.spectrum_scan_options.band_pol.pol;
	auto voltage = devdb::lnb::voltage_for_pol(lnb, pol) ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;

	auto [ret, new_usals_sat_pos ] =
		fe->do_lnb_and_diseqc(band, voltage);
	if(new_usals_sat_pos != sat_pos_none)
		lnb_update_usals_pos(new_usals_sat_pos);

	dtdebug("spectrum: diseqc done");
	fe->stop();
	if (fe->stop() < 0) /* Force the driver to go into idle mode immediately, so
													 that the fe_monitor_thread_t will also return immediately
												*/
		return -1;
	ret = fe->start_lnb_spectrum_scan(lnb, tune_options);

	fe->start();

	//dttime(100);
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
	dtdebugx("Adapter %d frontend %d destroyed\n", get_adapter_no(), frontend_no());
}

int active_adapter_t::open_demux(int mode) const {
	ss::string<PATH_MAX> demux_fname;
	const int demux_no = 0; // are there any adapters on wwich demux_no!=0? If so how to associate them with frontends?
	demux_fname.sprintf("%s%d/demux%d", DVB_DEV_PATH, get_adapter_no(), demux_no);

	int fd = ::open(demux_fname.c_str(), mode);
	return fd;
}

void active_adapter_t::update_lof(fe_state_t& ts, const ss::vector<int32_t, 2>& lof_offsets) {
	auto& lnb = ts.reserved_lnb;
	lnb.lof_offsets = lof_offsets;
	auto devdb_wtxn = receiver.devdb.wtxn();
	auto c = devdb::lnb_t::find_by_key(devdb_wtxn, lnb.k);
	if (c.is_valid()) {
		/*note that we do not update the full mux (e.g., when called from positioner_dialog user may want
			to not save some changes.
			So instead, we set only the lof_offsets
		*/
		auto lnb = c.current();
		lnb.lof_offsets = lof_offsets;
		put_record(devdb_wtxn, lnb);
		devdb_wtxn.commit();
	}
}

int active_adapter_t::deactivate() {
	tune_state = TUNE_INIT;
	end_si();
	active_services.clear();

	auto* fe = this->fe.get();
	auto fefd = fe->ts.readAccess()->fefd;
	dtdebugx("Release fe_fd=%d", fefd);
	fe->release_fe();
	return 0;
}

void active_adapter_t::lnb_update_usals_pos(int16_t usals_pos) {
	int dish_id = this->fe->ts.readAccess()->reserved_lnb.k.dish_id;
	auto devdb_wtxn = receiver.devdb.wtxn();
	int ret = devdb::dish::update_usals_pos(devdb_wtxn, dish_id, usals_pos);
	if( ret<0 )
		devdb_wtxn.abort();
	else
		devdb_wtxn.commit();

	auto w = this->fe->ts.writeAccess();
	if (usals_pos != w->reserved_lnb.usals_pos) {
		// measure how long it takes to move positioner
		usals_timer.start(w->reserved_lnb.usals_pos, usals_pos);
		w->reserved_lnb.usals_pos = usals_pos;
	}
}

/*
	Called when user edits lnb parameters.
	Todo: What if the parameters cause a change in dish, usals_pos...?
 */
void active_adapter_t::update_current_lnb(const devdb::lnb_t& lnb) {
	fe->ts.writeAccess()->reserved_lnb = lnb;
};

//only called from active_si_stream.h true, true and true, false and false, false,
void active_adapter_t::on_tuned_mux_change(const chdb::any_mux_t& mux) {
	fe->update_tuned_mux_nit(mux);
}

void active_adapter_t::update_bad_received_si_mux(const std::optional<chdb::any_mux_t>& mux) {
	fe->update_bad_received_si_mux(mux);
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

void active_adapter_t::add_embedded_si_stream(const chdb::any_mux_t& embedded_mux, bool start) {
	auto* mux_key = chdb::mux_key_ptr(embedded_mux);
	auto [it, found] = find_in_map(embedded_si_streams, mux_key->t2mi_pid);
	if (found) {
		dtdebugx("Ignoring request to add the same si stream twice");
		return;
	}
	auto reader = make_embedded_stream_reader(embedded_mux);
	const bool is_embedded_si{true};
	auto [it1, inserted] =
		embedded_si_streams.try_emplace((uint16_t)mux_key->t2mi_pid, receiver, std::move(reader), is_embedded_si);
	assert(inserted);
	if (start)
		it1->second.init(fe->ts.readAccess()->tune_options.scan_target);
}

bool active_adapter_t::read_and_process_data_for_fd(int fd) {
	if (si.read_and_process_data_for_fd(fd))
		return true;
	for (auto& [pid, si] : embedded_si_streams) {
		if (si.read_and_process_data_for_fd(fd)) {
			return true;
		}
	}
	return false;
}

void active_adapter_t::init_si(scan_target_t scan_target) {
	/*@When we are called on a t2mi mux, there could be confusion between the t2mi mux (with t2mi_pid set)
		and the one without. To avoid this, we find the non-t2mi_pid version first
	*/
	si.init(scan_target);
	for (auto& [pid, si_] : embedded_si_streams) {
		si_.init(si.scan_target);
	}
}


void active_adapter_t::prepare_si(chdb::any_mux_t mux, bool start) {
	namespace m = chdb::update_mux_preserve_t;
	auto* muxc = mux_common_ptr(mux);
	bool must_activate{false};
	if(muxc->scan_status == scan_status_t::PENDING || muxc->scan_status == scan_status_t::ACTIVE)  {
		muxc->scan_status = scan_status_t::ACTIVE;
		must_activate = true;
	}
	auto chdb_txn = must_activate ? receiver.chdb.wtxn() : receiver.chdb.rtxn();

	auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&mux);
	if (dvbs_mux && dvbs_mux->k.t2mi_pid > 0) {
		auto master_mux = *dvbs_mux;
		auto mux_key = master_mux.k;
		dtdebug("mux " << *dvbs_mux << " is an embedded stream");
		master_mux.k.t2mi_pid = 0;
		/*TODO: this will not detect almost duplicates in frequency (which should not be present anyway) or
			handle small differences in sat_pos*/
		auto c = chdb::find_by_mux_physical(chdb_txn, master_mux);
		if (c.is_valid()) {
			assert(mux_key_ptr(c.current())->sat_pos != sat_pos_none);
			master_mux = c.current();
		}
		if(must_activate)
			update_mux(chdb_txn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS});
		chdb_txn.commit();
		set_current_tp(master_mux);
		add_embedded_si_stream(mux, start);
	} else {
		//update_mux(txn, mux, now, m::flags{m::ALL & ~m::SCAN_STATUS});
		chdb_txn.commit();
		set_current_tp(mux);
	}
}

void active_adapter_t::end_si() {
	for (auto& [pid, si_] : embedded_si_streams) {
		si_.deactivate();
	}
	si.deactivate();
	embedded_si_streams.clear();
	stream_filters.writeAccess()->clear();
}


static void set_lnb_lof_offset(const chdb::dvbs_mux_t& dvbs_mux, devdb::lnb_t& lnb, int tuned_frequency) {

	auto [band, voltage_, freq_] = devdb::lnb::band_voltage_freq_for_mux(lnb, dvbs_mux);
	/*
		extra_lof_offset is the currently observed offset, after having corrected LOF by
		the last known value of lnb.lof_offsets[band]
	*/

	if (lnb.lof_offsets.size() <= band + 1) {
		// make sure both entries exist
		for (int i = lnb.lof_offsets.size(); i < band + 1; ++i)
			lnb.lof_offsets.push_back(0);
	}
	auto old = lnb.lof_offsets[band];
	float learning_rate = 0.2;
	int delta = (int)tuned_frequency - (int)dvbs_mux.frequency; //additional correction  needed
	lnb.lof_offsets[band] += learning_rate * delta;

	dtdebugx("Updated LOF: %d => %d", old, lnb.lof_offsets[band]);
	if (std::abs(lnb.lof_offsets[band]) > 5000)
		lnb.lof_offsets[band] = 0;
}


//called from tuner thread
void active_adapter_t::update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation) {
	auto w = fe->ts.writeAccess();
	if(!w->tune_confirmation.nit_actual_received && tune_confirmation.nit_actual_received) {
		const auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&w->reserved_mux);
		if(dvbs_mux) {
			using namespace chdb;
			//ss::vector<int32_t, 2> lof_offsets;
			auto& lnb = w->reserved_lnb;
			set_lnb_lof_offset(*dvbs_mux, lnb, w->tuned_frequency);
			auto lof_offsets = lnb.lof_offsets;
			if(dvbs_mux->c.tune_src == tune_src_t::NIT_ACTUAL_TUNED) {
				dtdebug("Updating LNB LOF offset");
				update_lof(*w, lof_offsets);
			}
		}
	}
	w->tune_confirmation = tune_confirmation;
}



//instantiations
template
int active_adapter_t::tune<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, tune_options_t
																							tune_options, bool user_requested);

template
int active_adapter_t::tune<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, tune_options_t
																							tune_options, bool user_requested);
