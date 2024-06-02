/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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
#include "active_adapter.h"

#include <stdio.h>
#include <stdlib.h>
//#include <ctype.h>
#include <sys/time.h>
//#include <sys/poll.h>
#include <sys/epoll.h>
//#include <sys/stat.h>
#include "util/template_util.h"
#include "active_service.h"
#include "active_si_stream.h"
#include "receiver.h"
#include "util/neumovariant.h"
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <resolv.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <values.h>
#include "fmt/chrono.h"

using namespace chdb;

/** @brief Print the status
 * Print the status contained in festatus, this status says if the card is lock, sync etc.
 *
 * @param festatus the status to display
 */
void print_tuner_status(fe_status_t festatus) {
	dtdebugf("FE_STATUS:");
	if (festatus & FE_HAS_SIGNAL)
		dtdebugf("     FE_HAS_SIGNAL : found something above the noise level");
	if (festatus & FE_HAS_CARRIER)
		dtdebugf("     FE_HAS_CARRIER : found a DVB signal");
	if (festatus & FE_HAS_VITERBI)
		dtdebugf("     FE_HAS_VITERBI : FEC is stable");
	if (festatus & FE_HAS_SYNC)
		dtdebugf("     FE_HAS_SYNC : found sync bytes");
	if (festatus & FE_HAS_LOCK)
		dtdebugf("     FE_HAS_LOCK : everything's working...");
	if (festatus & FE_TIMEDOUT)
		dtdebugf("     FE_TIMEDOUT : no lock within the last about 2 seconds");
	if (festatus & FE_HAS_TIMING_LOCK)
		dtdebugf("     FE_REINIT : frontend has timing loop locked");
	dtdebugf("---");
}

ss::string<128> dump_caps(chdb::fe_caps_t caps) {
	ss::string<128> ret;
	for (int i = 0; i < 32; ++i) {
		auto mask = ((uint32_t)1) << i;
		if (mask & (uint32_t)caps) {
			ret.format("{} ", to_str((chdb::fe_caps_t)mask));
		}
	}
	return ret;
}

int tuner_thread_t::cb_t::on_pmt_update(active_adapter_t& active_adapter, const dtdemux::pmt_info_t& pmt) {
	auto mux = active_adapter.current_tp();

	bool changed = pmt.has_freesat_epg ? add_epg_type(mux, chdb::epg_type_t::FREESAT)
		: remove_epg_type(mux, chdb::epg_type_t::FREESAT);

	changed |=
		pmt.has_skyuk_epg ? add_epg_type(mux, chdb::epg_type_t::SKYUK) : remove_epg_type(mux, chdb::epg_type_t::SKYUK);

	if (changed) {
		dtdebugf("freesat/skyuk epg flag changed on mux {}", mux);
		active_adapter.set_current_tp(mux);
		auto txn = receiver.chdb.wtxn();
		namespace m = chdb::update_mux_preserve_t;
		chdb::update_mux(txn, mux, now, m::flags{m::ALL & ~m::EPG_TYPES}, /*false ignore_key,*/
										 false /*ignore_t2mi_pid*/, true /*must_exist*/);
		txn.commit();
		dtdebugf("committed");
	}
	return 0;
}

int tuner_thread_t::cb_t::update_service(const chdb::service_t& service) {
	auto txn = receiver.chdb.wtxn();
	put_record(txn, service);
	txn.commit();
	dtdebugf("committed");
	return 0;
}

int tuner_thread_t::cb_t::lnb_activate(subscription_id_t subscription_id, const subscribe_ret_t& sret,
																			 subscription_options_t tune_options) {
	dtdebugf("lnb activate subscription_id={:d}", (int) subscription_id);
	auto& aa = sret.aa;
	assert(aa.rf_path);
	assert(aa.lnb);
	return active_adapter.lnb_activate(*aa.rf_path, *aa.lnb, tune_options);
}

int tuner_thread_t::cb_t::lnb_spectrum_acquistion(subscription_id_t subscription_id, const subscribe_ret_t& sret,
																									subscription_options_t tune_options) {
	// check_thread();
	dtdebugf("lnb spectrum scan subscription_id={:d}", (int) subscription_id);
	assert((int) sret.sub_to_reuse  < 0  || sret.sub_to_reuse == sret.subscription_id);
	auto& aa = sret.aa;
	assert(aa.rf_path);
	assert(aa.lnb);
	return active_adapter.lnb_spectrum_scan(*aa.rf_path, *aa.lnb, tune_options);
}

/*
	Called from tune_mux when our own subscription is a normal tune, but mux is resubscribed, e.g.,
	to set the scan status. The newly subscribed mux could be for a not yet running embedded t2mi stream

	In this case, an embedded stream is added through prepare_si.
	if scan_id >0, prepare_si also adds the subscription to the list of subscriptions to notify
	when scanning finishes. In case scanning already finished, this notification is sent immediately

	Finally, add_si also sets the tune options:
	@todo: various tune requests may have conflicting tune options (such as propagate_scan)
 */

void tuner_thread_t::add_si(active_adapter_t& active_adapter,
																	const chdb::any_mux_t& mux, const subscription_options_t& tune_options ,
														subscription_id_t subscription_id) {
	// check_thread();
	dtdebugf("tune restart_si");
	active_adapter.prepare_si(mux, true /*start*/, subscription_id, true /*add_to_running_mux*/);
	active_adapter.fe->set_tune_options(tune_options);
}

int tuner_thread_t::tune(const subscribe_ret_t& sret, const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
												 const chdb::dvbs_mux_t& mux_, subscription_options_t tune_options) {
	// check_thread();
	chdb::dvbs_mux_t mux{mux_};
	/*
		The new scan status must be written to the database now.
		Otherwise there may be problems on muxes which fail to scan: their status would remain
		pending, and when parallel tuners are in use, the second tuner might decide to scan
			the mux again
	*/
	auto prefix = fmt::format("TUN{:d}-TUNE", active_adapter.get_adapter_no());
	log4cxx::NDC::pop();
	log4cxx::NDC ndc(prefix.c_str());

	dtdebugf("tune mux action lnb={} mux={}", lnb, mux);
	return active_adapter.tune(sret, rf_path, lnb, mux, tune_options);
}

template <typename _mux_t>
int tuner_thread_t::tune_dvbc_or_dvbt(const _mux_t& mux_, subscription_options_t tune_options,
												 subscription_id_t subscription_id) {
	_mux_t mux{mux_};

	assert( mux.c.scan_status != scan_status_t::ACTIVE);
	/*
		The new scan status must be written to the database now.
		Otherwise there may be problems on muxes which fail to scan: their status would remain
		pending, and whne parallel tuners are in use, the second tuner might decide to scan
		the mux again
		*/
	auto prefix = fmt::format("TUN{}-TUNE", active_adapter.get_adapter_no());
	log4cxx::NDC::pop();
	log4cxx::NDC ndc(prefix.c_str());

	dtdebugf("tune mux action");
	bool user_requested = true;
	auto ret = active_adapter.tune_dvbc_or_dvbt(mux, tune_options, user_requested, subscription_id);
	assert (ret>=0 || active_adapter.tune_state == active_adapter_t::TUNE_FAILED);
	return ret;
}

template int tuner_thread_t::cb_t::tune_dvbc_or_dvbt<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, subscription_options_t tune_options,
																													subscription_id_t subscription_id);

template int tuner_thread_t::cb_t::tune_dvbc_or_dvbt<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, subscription_options_t tune_options,
																													subscription_id_t subscription_id);

int tuner_thread_t::exit() {
	dtdebugf("tuner starting exit");
	this->active_adapter.destroy();
	return 0;
}

int tuner_thread_t::cb_t::stop_recording(const recdb::rec_t& rec,
																	 mpm_copylist_t& copy_commands)
{

	assert(rec.subscription_id >= 0);
	if (rec.subscription_id < 0)
		return -1;

	auto subscription_id = subscription_id_t{rec.subscription_id};
	auto active_service_p = active_adapter.active_service_for_subscription(subscription_id);
	assert(active_service_p);
	auto& as = *active_service_p;

	int ret{-1};
	as.service_thread //call by reference safe, because of subsequent .wait
		.push_task([&as, &copy_commands, &rec, &ret]() {
					ret = cb(as.service_thread).stop_recording(rec, copy_commands);
					cb(as.service_thread).forget_recording_in_livebuffer(rec);
					return 0;
		})
		.wait();

	return ret;
}


/*
	called whenever  a new or updated epg record is found to update information shown
	on live screen.
	Also sets recording status on epg_record
*/

void tuner_thread_t::on_epg_update(db_txn& epgdb_wtxn, system_time_t now,
																	 epgdb::epg_record_t& epg_record/*may be updated by setting epg_record.record
																																		to true or false*/)
{
	/*
		update the epg records in the live buffers (not in recdb) for all live
		services; also update the livebuffers
	*/
	/* todo:do we really need this? code is called for each epg record and
		 serves only to attach the current epg record with the live service.
	*/
	auto recdb_wtxn = recdbmgr.wtxn(); //transaction is only created when used
	auto recdb_rtxn = recdbmgr.rtxn();
	int32_t owner = getpid();
	auto c= recdb::live_service_t::find_by_key(recdb_rtxn, owner, find_type_t::find_geq,
																						 recdb::live_service_t::partial_keys_t::owner);
	auto now_ = system_clock_t::to_time_t(now);
	for(auto live_service: c.range()) {
		if (likely(epg_record.k.service != live_service.service.k))
			continue;
		if (likely(epg_record.k.start_time > now_ ||
							 epg_record.end_time <= now_))
			continue;
		put_record(recdb_wtxn, live_service);
	}
	on_epg_update_check_recordings(recdb_wtxn, epgdb_wtxn, epg_record);
	on_epg_update_check_autorecs(recdb_wtxn, epgdb_wtxn, epg_record);
	recdb_wtxn.commit();
	//recdbmgr.release_wtxn(); Not needed?
}

int tuner_thread_t::run() {
	thread_id = std::this_thread::get_id();
	auto adapter_no = active_adapter.get_adapter_no();
	/*TODO: The timer below is used to gather signal strength, cnr and ber.
		When used from a gui, it may be better to let the gui handle this asynchronously
	*/
	ss::string<64> thread_name;
	thread_name.format("tuner{:d}", adapter_no);
	set_name(thread_name.c_str());
	logger = Logger::getLogger("tuner"); // override default logger for this thread
	double period_sec = 1.0;
	timer_start(period_sec);

	/*
		Avoid destruction of active_adapter until this function stops running
	 */
	auto preserve = active_adapter.shared_from_this();
	now = system_clock_t::now();
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorf("error in poll: {}", strerror(errno));
			continue;
		}
		now = system_clock_t::now();
		for (auto evt = next_event(); evt; evt = next_event()) {
			if(is_wait_timer_fd(evt)) {
				active_adapter.fe->resume_task();
			} else if (is_event_fd(evt)) {
				ss::string<128> prefix;
				prefix.format("TUN-CMD");
				log4cxx::NDC ndc(prefix.c_str());
				// an external request was received
				// run_tasks returns -1 if we must exit
				if (run_tasks(now) < 0) {
					// detach();
					return 0;
				}
			} else if (is_timer_fd(evt)) {
			  {
					if (!active_adapter.fe) {
						dterror_nicef("Implementation error");
						continue;
					}
					dttime_init();
					ss::string<128> prefix;
					prefix.format("MON", active_adapter.get_adapter_no());
					log4cxx::NDC ndc(prefix.c_str());
					active_adapter.monitor();
					auto delay = dttime(-1);
					if (delay >= 500)
						dterrorf("monitor cycle took too long delay={:d}", delay);
					if(run_tasks(now)<0)
						return 0;
				}
				dttime_init();
				dttime(100);
				auto delay = dttime(-1);
				if (delay >= 500)
					dterrorf("clean cycle took too long delay={:d}", delay);
			} else {
				// this must be a si event
				{
					// active_adapter will return if fd is for other active_adapter
					// The following call returns true if fd was for this active_adapter
					ss::string<128> prefix;
					prefix.format("TUN{}-SI", active_adapter.get_adapter_no());
					log4cxx::NDC ndc(prefix.c_str());
					dttime_init();

					if (evt->events & EPOLLERR) {
						dterrorf("ERROR in epoll event for fd={:d}", evt->data.fd);
					}
					if (evt->events & EPOLLIN) {
						if (active_adapter.read_and_process_data_for_fd(evt)) {
							auto delay = dttime(300);
							if (delay >= 200)
								dterrorf("si cycle took too long delay={:d}", delay);
							break;
						}
					}
				}
				ss::string<128> prefix;
				prefix.format("TASK");
				log4cxx::NDC ndc(prefix.c_str());
				if(run_tasks(now)<0)
					return 0;
			}
		}
	}
	dtdebugf("Tuner thread ending done");
	return 0;
}

tuner_thread_t::tuner_thread_t(receiver_t& receiver_, active_adapter_t& aa)
	: task_queue_t(thread_group_t::tuner)
	, receiver(receiver_)
	, active_adapter(aa)
	, recdbmgr(receiver.recdb)
		 {
		 }

tuner_thread_t::~tuner_thread_t() {
}

/*
	returns
	int: status of command
	std::optional<int16_t> contains either the new usals position or sat_pos_none
	if the command rotated the rotor. sat_pos_none is returned in cas ethe new position is not known

 */
std::tuple<int, std::optional<int>>
tuner_thread_t::cb_t::positioner_cmd(subscription_id_t subscription_id, devdb::positioner_cmd_t cmd,
																		 int par) {
	using p_t = devdb::positioner_cmd_t;
	std::optional<int>  new_usals_pos;
	std::optional<bool> usals_pos_reliable;
	auto ret= (active_adapter.fe) ? active_adapter.fe->positioner_cmd(cmd, par) : -1;
	if(ret >=0) {
		switch(cmd) {
		case p_t::GOTO_REF:
		case p_t::RESET:
			new_usals_pos = 0;
			usals_pos_reliable = true;
			break;
		case p_t::DRIVE_EAST:
		case p_t::DRIVE_WEST:
		case p_t::HALT:
		case p_t::RECALCULATE_POSITIONS:
		case p_t::NUDGE_WEST:
		case p_t::NUDGE_EAST:
			new_usals_pos = sat_pos_none; //unknown
			usals_pos_reliable = false;
			break;
		case p_t::GOTO_NN:
		case p_t::GOTO_XX:
			new_usals_pos = par;
			usals_pos_reliable = true;
			break;
		case p_t::LIMITS_OFF:
		case p_t::LIMIT_EAST:
		case p_t::LIMIT_WEST:
		case p_t::STORE_NN:
		case p_t::LIMITS_ON:
			break;
		}
	}
	if(!new_usals_pos)
		return {ret, new_usals_pos};
	auto lnb = active_adapter.current_lnb();
	auto loc = receiver.options.readAccess()->usals_location;
	auto devdb_wtxn = receiver.devdb.wtxn();

	devdb::dish::schedule_move(devdb_wtxn, lnb, *new_usals_pos, lnb.cur_sat_pos, loc, true/*move_has_finished*/);

	devdb_wtxn.commit();
	return {ret, new_usals_pos};
}

int tuner_thread_t::cb_t::update_current_lnb(subscription_id_t subscription_id, const devdb::lnb_t& lnb) {
	active_adapter.update_current_lnb(lnb);
	return 0;
}

subscription_id_t
tuner_thread_t::subscribe_mux(const subscribe_ret_t& sret,
															const chdb::any_mux_t& mux,
															const subscription_options_t& tune_options) {
	/*In case of failure, release the resources associated with tjis subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	if(sret.failed && sret.was_subscribed) {
		release_all(sret.subscription_id);
		return subscription_id_t::NONE;
	}

	/*
		Perform all actions needed to tune to the proper mux, which we have been able to subscribe;
		In case of failure, release the resources assosciated with tjis subscription (active_adapter and
		active_service).
		This will also release any resources alreay in use (active_mux and active_service) if they are no
		longer needed.
	 */
	auto ret = this->tuner_thread_t::tune_mux(sret, mux, tune_options);
	if((int)ret < 0) {
		release_all(sret.subscription_id);
		return subscription_id_t::NONE;
	}

	return ret;
}

subscription_id_t
tuner_thread_t::cb_t::subscribe_mux(const subscribe_ret_t& sret,
															const chdb::any_mux_t& mux,
																		const subscription_options_t& tune_options) {
	return this->tuner_thread_t::subscribe_mux(sret, mux, tune_options);
}


subscription_id_t
tuner_thread_t::cb_t::subscribe_service_for_recording(const subscribe_ret_t& sret,
																				const chdb::any_mux_t& mux, recdb::rec_t& rec,
																											const subscription_options_t& tune_options) {
	/*In case of failure, release the resources assosciated with this subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	auto subscription_id = this->subscribe_mux(sret, mux, tune_options);
	assert(subscription_id == sret.subscription_id);
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback
	*/
	auto recnew = active_adapter.tune_service_for_recording(sret, mux, rec);

	assert(recnew);
	recnew->owner = getpid();
	recnew->subscription_id = (int) sret.subscription_id;
	return sret.subscription_id;
}

std::unique_ptr<playback_mpm_t>
tuner_thread_t::cb_t::subscribe_service_for_viewing(const subscribe_ret_t& sret,
																				const chdb::any_mux_t& mux, const chdb::service_t& service,
																				const subscription_options_t& tune_options) {
	/*In case of failure, release the resources assosciated with this subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	auto subscription_id = this->subscribe_mux(sret, mux, tune_options);

	assert(subscription_id == sret.subscription_id);
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback
	*/
	return active_adapter.tune_service_for_viewing(sret, mux, service);
}

devdb::stream_t tuner_thread_t::cb_t::add_stream(const subscribe_ret_t& sret,
																			 const chdb::any_mux_t& mux, const devdb::stream_t& stream,
																			 const subscription_options_t& tune_options) {
	/*In case of failure, release the resources assosciated with this subscription (active_adapter and
		active_service)
	*/
	assert(sret.subscription_id != subscription_id_t::NONE);
	auto subscription_id = this->subscribe_mux(sret, mux, tune_options);

	assert(subscription_id == sret.subscription_id);
	/*at this point
		1. we have tuned to the proper mux
		2. we no longer have a subscribed service of playback
	*/
	return active_adapter.add_stream(sret, stream, mux);
}

void tuner_thread_t::cb_t::remove_stream(subscription_id_t subscription_id) {
	active_adapter.remove_stream(subscription_id);
}

/*
	called by
     receiver_thread_t::subscribe_service_
		 receiver_thread_t::cb_t::tune_mux

	-subscribe new mux, and unsubscribe the old one, taking into account they may be the same;
	release active_adapters as needed as a side effect, in three steps
	 1. check if the new mux is the same as the one active on the subscription. In this case
	   restart si processing and/or retune
   2. else check if the the mux is already active on some other subscription. If so, increament use count
	    of that new frontend, and decrease it on the old frontend
   3. else reserve an fe  for the new mux. If so, increament use count
	    of that new frontend, and decrease it on the old frontend; new and old frontend can turn
			out to be the same, in which case use_count does not change
  Then, if the oild fe's use_count has dropped to 0, release the old active adapter

	-Before this call, any active service should be removed


 */
subscription_id_t
tuner_thread_t::tune_mux(const subscribe_ret_t& sret, const chdb::any_mux_t& mux,
												 const subscription_options_t& tune_options) {
	assert(sret.subscription_id != subscription_id_t::NONE);
	if(sret.failed && sret.was_subscribed) {
		release_all(sret.subscription_id);
		return subscription_id_t::NONE;
	}
	auto old_active_adapter = &active_adapter;
	/*If the requested mux happens to be already the active mux for this subscription, simply return,
		after restarting si processing or retuning
	*/
	if(!sret.aa.is_new_aa()) {
		dtdebugf("Reusing active_adapter {:p}: subscription_id={:d} adapter_no={:d}",
						 fmt::ptr(this), (int)sret.subscription_id,
						 (int)active_adapter.get_adapter_no());
		auto ret1 = active_adapter.remove_service(sret.subscription_id);
		dtdebugf("Called remove_service: service was {}removed", (ret1<0)? "NOT " : "");
	}

	if(sret.sub_to_reuse == sret.subscription_id)  {
		assert(old_active_adapter);
		dtdebugf("already subscribed to mux {}", mux);
		if(tune_options.subscription_type == devdb::subscription_type_t::TUNE) {
     //@todo: check the following call, this means we alreay have added si, but maybe with other tune options
			add_si(*old_active_adapter, mux, tune_options, sret.subscription_id);
		} else {
			/// during DX-ing and scanning retunes need to be forced
			old_active_adapter->request_retune(mux, tune_options, sret.subscription_id);
		}
		return sret.subscription_id;
	}

	/*We now know that the old unsubscribed mux is different from the new one
		or current_lnb != lnb
		We do not unsubscribe it yet, because that would cause the asssociated frontend
		to be released, which is not optimal if we decide to just reuse this frontend
		and tune it to a different mux
	*/
	if((int)sret.sub_to_reuse >=0)  {
		add_si(active_adapter, mux, tune_options, sret.subscription_id);
		dtdebugf("subscribe {}: reused activate_adapter from subscription_id={}",
						 mux, (int)sret.sub_to_reuse);
		return sret.subscription_id;
	}

	/*
		we must retune old_active_adapter or create a new active_adapter
	 */

	int ret{-1};
	auto& aa = sret.aa;
	dtdebugf("Active_adapter {:p}: subscription_id={:d} adapter_no={:d}",
					 fmt::ptr(this), (int) sret.subscription_id,
					 active_adapter.get_adapter_no());
	visit_variant(mux,
								[&](const chdb::dvbs_mux_t& mux) {
									assert(aa.rf_path);
									assert(aa.lnb);
									ret = this->tune(sret, *aa.rf_path, *aa.lnb, mux, tune_options);
								},
								[&](const chdb::dvbc_mux_t& mux) {
									ret = this->tune_dvbc_or_dvbt(mux, tune_options, sret.subscription_id);
								},
								[&](const chdb::dvbt_mux_t& mux) {
									ret = this->tune_dvbc_or_dvbt(mux, tune_options, sret.subscription_id);
								}
		);
	if (ret < 0)
		dterrorf("tune returned {:d}", ret);
	//Destructor of old_active_adapter can call deactivate at this point
	return sret.subscription_id;
}

void tuner_thread_t::release_all(subscription_id_t subscription_id) {
	if ((int)subscription_id < 0)
		return;
	active_adapter.reset();
}

void tuner_thread_t::on_epg_update_check_recordings(db_txn& recdb_wtxn,
																										db_txn& epg_wtxn, epgdb::epg_record_t& epg_record)
{
	using namespace recdb;
	auto recdb_rtxn = recdbmgr.rtxn();

	/*
		In recdb, find the recordings which match the new/changed epg record, and update them.
		Only ongoing or future recordings are returned by recdb::rec::best_matching

		In case of anonymous recordings, we may have an overlapping anonymous and non-anonymous
		recording. When updating an anonymous recording, we enter a new non-anonymous recording as well,
		but we should only do this once. Therefore, we first check for non-anonymous matches
		and if we find one, we do not create a new non-anonymous recording;
	*/
	for(int anonymous = 0; anonymous < 2; ++anonymous)
		if (auto rec_ = recdb::rec::best_matching(recdb_rtxn, epg_record, anonymous)) {

			auto& rec = *rec_;
			assert (anonymous == rec.epg.k.anonymous);
			assert (anonymous == (rec.epg.k.event_id == TEMPLATE_EVENT_ID));
			bool rec_key_changed = (epg_record.k != rec.epg.k); // can happen when start_time changed
			if (epg_record.rec_status != rec.epg.rec_status) {
				epg_record.rec_status = rec.epg.rec_status; //tag epg record as being scheduled for recording
				epgdb::update_epg_recording_status(epg_wtxn, epg_record);
			}

			if (anonymous) {
				/* we found a matching anonymous recording and we did not match a non-anonymous recording earlier.
					 In this case we need to create a new recording for the matching epg record
				*/
				auto r = receiver.options.readAccess();
				auto& options = *r;
				recdb::new_recording(recdb_wtxn, rec.service, epg_record, options.pre_record_time.count(),
											options.post_record_time.count());
				put_record(recdb_wtxn, rec); //2nd write to set the recording status
			} else {
				/* we found a matching non-anonymous recording. In this case we need to update the epg record
					 for the recording
				*/

				if (rec_key_changed)
					delete_record(recdb_wtxn, rec);
				assert(epg_record.rec_status== rec.epg.rec_status);
				rec.epg = epg_record;
				put_record(recdb_wtxn, rec);
			}
			return;
		}
	recdb_rtxn.commit();
}

void tuner_thread_t::on_epg_update_check_autorecs(db_txn& recdb_wtxn,
																									db_txn& epg_wtxn, epgdb::epg_record_t& epg_record)
{

	using namespace recdb;
	using namespace std::chrono;
	auto recdb_rtxn = recdbmgr.rtxn();
	auto service_key = epg_record.k.service;

	//find all autorecs related to this service
	auto c =  autorec_t::find_by_service(recdb_rtxn, service_key,
																			 find_type_t::find_geq, autorec_t::partial_keys_t::service,
																			 autorec_t::partial_keys_t::service);


	if(!c.is_valid())
		return;
	auto start_time = system_clock::from_time_t(epg_record.k.start_time);
	auto tp = zoned_time(current_zone(), floor<std::chrono::seconds>(start_time));

	auto const info = tp.get_time_zone()->get_info(start_time);
	start_time += info.offset;

	auto dp = std::chrono::floor<std::chrono::days>(start_time);
	hh_mm_ss t{std::chrono::floor<std::chrono::seconds>(start_time-dp)};

	int start_seconds = t.minutes().count()*60;
	int duration = epg_record.end_time - epg_record.k.start_time;

	for(auto autorec: c.range()) {
		if(start_seconds < autorec.starts_after || start_seconds > autorec.starts_before)
			continue; //no match
		if(duration < autorec.min_duration || duration > autorec.max_duration)
			continue; //no match
		if(autorec.event_name_contains.size() >0 &&
			 strcasestr(epg_record.event_name.c_str(), autorec.event_name_contains.c_str()) == nullptr)
			continue;
		if(autorec.story_contains.size() >0 &&
			 strcasestr(epg_record.story.c_str(), autorec.story_contains.c_str()) == nullptr)
			continue;

		if (epg_record.rec_status == epgdb::rec_status_t::NONE) {
			epg_record.rec_status = epgdb::rec_status_t::SCHEDULED; //tag epg record as being scheduled for recording
			epgdb::update_epg_recording_status(epg_wtxn, epg_record);
		}
		auto cr =  rec_t::find_by_key(recdb_rtxn, epg_record.k);
		if(cr.is_valid())
			continue; //recording already created
		auto r = receiver.options.readAccess();
		auto& options = *r;
		auto chdb_rtxn = receiver.chdb.rtxn();
		auto cs = chdb::service_t::find_by_key(chdb_rtxn, epg_record.k.service.mux,
																					 epg_record.k.service.service_id);
		if(cs.is_valid()) {
			auto service = cs.current();
			recdb::new_recording(recdb_wtxn, service, epg_record, options.pre_record_time.count(),
										options.post_record_time.count());
		}
		chdb_rtxn.abort();
	}
	c.destroy();
	recdb_rtxn.abort();
}

void tuner_thread_t::add_live_buffer(const recdb::live_service_t& live_service) {
	using namespace recdb;
	auto wtxn = recdbmgr.wtxn();
	auto c = live_service_t::find_by_key(wtxn, live_service.owner, live_service.subscription_id, find_type_t::find_eq);
	if(c.is_valid()) {
		auto old = c.current();
		dtdebugf("updating live_service: last_use_time from={} new={}",
						 fmt::localtime(old.last_use_time), fmt::localtime(live_service.last_use_time));
	} else {
		dtdebugf("new live_service: last_use_time={}", fmt::localtime(live_service.last_use_time));
	}
	c.destroy();
	put_record(wtxn, live_service);
	wtxn.commit();
}

/*
	indicate that livebuffer is now not in use any more; it will be removed after an expiration period
 */
void tuner_thread_t::remove_live_buffer(subscription_id_t subscription_id) {
	using namespace recdb;
	int pid = getpid();
	auto txn = recdbmgr.wtxn();
	auto c = live_service_t::find_by_key(txn, pid, (int)subscription_id, find_type_t::find_eq);
	if(!c.is_valid()) {
		dterrorf("Live buffer no longer exists.");
		return;
	}
	auto live_service = c.current();
	dtdebugf("setting live buffer last_use_time={}", fmt::localtime(system_clock_t::to_time_t(now)));
	live_service.last_use_time = system_clock_t::to_time_t(now);
	c.destroy();
	txn.commit();
	recdbmgr.flush_wtxn();
}

void tuner_thread_t::update_dbfe(const devdb::fe_t& updated_dbfe) {
	active_adapter.fe->update_dbfe(updated_dbfe);
}
