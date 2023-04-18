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

// classes to manage a single tuner
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
#include <values.h>
//#include <getopt.h>
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <set>
#include <stdarg.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <time.h>

#include "receiver.h"
#include "util/dtutil.h"

#include "active_adapter.h"
#include "active_service.h"
#include "neumo.h"
#include "filemapper.h"
#include "streamparser/packetstream.h"
#include "streamparser/si.h"

#include "date/date.h"
#include "date/iso_week.h"
#include "date/tz.h"
using namespace date;
using namespace date::clock_cast_detail;


active_service_t::active_service_t
(active_adapter_t& active_adapter_, const std::shared_ptr<stream_reader_t>& reader)
	: active_stream_t(active_adapter_.receiver, std::move(reader))
	, service_thread(*this)
	, mpm(this, system_clock_t::now())
	, periodic(60*30)
{
}



active_service_t::active_service_t(active_adapter_t& active_adapter, const chdb::service_t& current_service_,
																	 const std::shared_ptr<stream_reader_t>& reader)
	: active_stream_t(active_adapter.receiver, std::move(reader))
	, current_service(current_service_)
	, service_thread(*this)
	, mpm(this, system_clock_t::now())
	, periodic(60*30)
{
	dttime_init();
	update_epg(now); // get initial epg
	dttime(200);
}

ss::string<32> active_service_t::name() const { return current_service.name.c_str(); }

int active_service_t::open() {
	log4cxx::NDC(name());
	auto demux_fd = active_stream_t::open(PAT_PID, &service_thread.epx, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET);
	// TOOD: initially we read data as soon as it becomes available to speed up channel tuning
	// once the channel is up and running we will switch to polling
	return demux_fd;
}

void active_service_t::close() {
	log4cxx::NDC(name());
	mpm.close();
	active_stream_t::close();
}

int active_service_t::deactivate() {
	log4cxx::NDC(name());
	int ret = 0;
	dtdebugx("deactivate service");
	if (registered_scam) {
		auto& scam_thread = receiver.scam_thread;
		auto future = scam_thread.push_task(
				[this, &scam_thread]() { return cb(scam_thread).unregister_active_service(this, get_adapter_no()); });
		dtdebugx("deactivate stream");
		ret = active_stream_t::deactivate();
		future.wait(); // must be synchronous or problems will occur
		dtdebugx("scam_thread unregister_active_service done");
		registered_scam = false;
	} else
		ret = active_stream_t::deactivate();
	dtdebugx("deactivate service done");
	return ret;
}

void service_thread_t::cb_t::update_recording(recdb::rec_t& rec, const chdb::service_t& service,
																							const epgdb::epg_record_t& epgrec) {
	active_service.mpm.update_recording(rec, service, epgrec);
}

int service_thread_t::exit() {
	dtdebug("Starting to exit");
	active_service.deactivate();
	active_service.mpm.destroy();
	dtdebug("Ended exit");
	return -1;
}

std::optional<recdb::rec_t> service_thread_t::cb_t::start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec) {
	recdb::rec_t recnew = active_service.mpm.start_recording(subscription_id, rec);
	assert(recnew.epg.rec_status == epgdb::rec_status_t::IN_PROGRESS);
	auto* tuner_thread = &active_service.receiver.tuner_thread;

	tuner_thread->push_task([tuner_thread, recnew]() // recnew must be passed by value!
													{
														cb(*tuner_thread).update_recording(recnew);
														return 0;
													}); //.wait();
	return recnew;
}

int service_thread_t::cb_t::stop_recording(const recdb::rec_t& rec_in, mpm_copylist_t& copy_commands) {
	return active_service.mpm.stop_recording(rec_in, copy_commands);
}

void service_thread_t::cb_t::forget_recording(const recdb::rec_t& rec_in) {
	return active_service.mpm.forget_recording(rec_in);
}

/*!
	epg_record is either an updated epg record or an epg record which was not considered
	earlier

	We maintain all relevant epg records in a time range
 */
void active_service_t::on_epg_update(system_time_t now, const epgdb::epg_record_t& epg_record) {
	auto now_ = system_clock_t::to_time_t(now);
	assert(epg_record.k.service.service_id == current_service.k.service_id); // sanity check
	if (epg_record.end_time < system_clock_t::to_time_t(mpm.creation_time))
		return; // record is too old; do not update
	if (epg_record.k.start_time > now_ + 120 * 60)
		return; // record is too new; do not update
	auto txnrecepg = mpm.db->mpm_rec.recepgdb.wtxn();
	auto ret = epgdb::save_epg_record_if_better(txnrecepg, epg_record);
	if (ret) {
		if (epg_record.k.start_time <= now_ && now_ < epg_record.end_time) {
			auto mm = mpm.meta_marker.writeAccess();
			mm->update_epg(epg_record);
		}
	}
	txnrecepg.commit();
}

void active_service_t::save_pmt(system_time_t now_, const pmt_info_t& pmt_info) {
	auto now = system_clock_t::to_time_t(now_);
	using namespace recdb;
	const auto& marker = mpm.stream_parser.event_handler.last_saved_marker;

	current_streams = stream_descriptor_t(pmt_info.stream_packetno_end, now, marker.k.time, pmt_info.pmt_pid,
																				pmt_info.audio_languages(), pmt_info.subtitle_languages(), pmt_sec_data);
	auto txnidx = mpm.db->mpm_rec.idxdb.wtxn();
	put_record(txnidx, current_streams);
	txnidx.commit();
	{
		auto mm = mpm.meta_marker.writeAccess();
		mm->last_streams = current_streams;
	}
}

playback_info_t active_service_t::get_current_program_info() const {
	std::scoped_lock lck(mutex);
	playback_info_t ret;
	ret.service = current_service;

	auto mm = mpm.meta_marker.readAccess();
	ret.start_time = mm->livebuffer_start_time;
	ret.end_time = mm->livebuffer_end_time;
	ret.play_time = mm->livebuffer_end_time;
	ret.is_recording= false;
	return ret;
}

void service_thread_t::cb_t::on_epg_update(system_time_t now, const epgdb::epg_record_t& epg_record) {
	active_service.on_epg_update(now, epg_record);
}

/*
	called when a pmt has been fully processed in the service's data
	stream. This function is set as a callback in live_mpm.cc
 */
void active_service_t::update_pmt(const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data) {
	using namespace dtdemux;
	dtdebug(pmt);
	have_pmt = true;
	pmt_is_encrypted = false;

	if (pmt.service_id != current_service.k.service_id) {
		// This can happen according to the dvb specs
		dtdebugx("received pmt for wrong service_id: pid=%d service_id=%d!=%d", pmt.pmt_pid, pmt.service_id,
						 current_service.k.service_id);
		return;
	}
	bool is_new = current_pmt.pmt_pid == null_pid;
	bool ca_changed = is_new || pmt_ca_changed(current_pmt, pmt);
	bool service_changed = (pmt.pmt_pid != current_service.pmt_pid) || (pmt.video_pid != current_service.video_pid);
	if (service_changed) {
		std::scoped_lock lck(mutex);
		current_service.pmt_pid = pmt.pmt_pid;
		current_service.video_pid = pmt.video_pid;
	}
	if (!isnext) {
		auto& active_adapter = this->active_adapter();
		// pmt deliberately passed by value
		if (service_changed) {
			receiver.tuner_thread.push_task([this, &active_adapter, pmt, service = current_service] {
				auto& cb_ = cb(receiver.tuner_thread);
				cb_.on_pmt_update(active_adapter, pmt); //update epg types in dvbs_mux in database
				cb_.update_service(service); //update service record in database
				return 0;
			});
		} else {
			active_adapter.tuner_thread.push_task([this, &active_adapter, pmt] {
				cb(receiver.tuner_thread).on_pmt_update(active_adapter, pmt);
				return 0;
			});
		}
	}

	bool was_encrypted = registered_scam;
	bool is_encrypted = pmt.is_encrypted() || (mpm.stream_parser.num_encrypted_packets > 0);
	// we also report non-encrypted pmts, in case the current pmt is encrypted
	if (is_encrypted && (ca_changed || !was_encrypted)) {

		/*we send to scam thread also for non-encrypted streams,
			so that we can turn off encryption if stream stops being encrypted
		*/
		auto& scam_thread = receiver.scam_thread;
		// bool do_register = !registered_scam;
		registered_scam = true;
		auto adapter_no = get_adapter_no();
		scam_thread.push_task([this, adapter_no, &scam_thread, isnext, pmt]() { // pmt passed by value!
			return cb(scam_thread).update_pmt(this, adapter_no, pmt, isnext);
		}); // don't wait for result (async)
	} else if (was_encrypted && !is_encrypted) {
		auto& scam_thread = receiver.scam_thread;
		auto adapter_no = get_adapter_no();
		scam_thread.push_task([this, &scam_thread, adapter_no, pmt]() { // pmt passed by value
			return cb(scam_thread).unregister_active_service(this, adapter_no);
		}); // don't wait for result (async)
		registered_scam = false;
	}
	{
		std::scoped_lock lck(mutex);
		current_pmt = pmt;
		pmt_sec_data = sec_data;
	}

	if (isnext) {
		dtdebugx("Unhandled PMT NEXT: service=%d", pmt.service_id);
		return;
	}
	int old_size = open_pids.size(); //

	/*all the pids in open_pids were in use; we set their use count to zero
		but increment it again if the pid will still be in use

		New pids will be added at an index >= old_size;
	*/

	for (auto& x : open_pids) {
		assert(x.use_count > 0);
		x.use_count = 0; // indicates that this was already in use;
	}
	//	std::vector<uint16_t> pids_to_register;

	auto process = [this](uint16_t pid) {
		for (auto& x : open_pids) {
			if (x.pid == pid) {
				x.use_count++;
				return;
			}
		}
		open_pids.push_back(pid_with_use_count_t(pid));
	};

	process(pmt.pcr_pid);
	process(pmt.pmt_pid);
	process(PAT_PID);
	using namespace stream_type;
	for (const auto& pidinfo : pmt.pid_descriptors) {
		// dtdebug(pidinfo);
		if (is_video(pidinfo.stream_type) || is_audio(pidinfo) || pidinfo.has_subtitles())
			process(pidinfo.stream_pid);
		/*the following code will reuse any existing parser
			@todo: in case of a radio channel, we need to register an audio pid instead
			In that case audio_pid == video_pid
			*/
		if (pmt.pcr_pid == pidinfo.stream_pid) {
			if (is_video(pidinfo.stream_type))
				mpm.stream_parser.register_video_pids(pmt.service_id, pidinfo.stream_pid, pmt.pcr_pid, pidinfo.stream_type);
			else if (is_audio(pidinfo))
				mpm.stream_parser.register_audio_pids(pmt.service_id, pidinfo.stream_pid, pmt.pcr_pid, pidinfo.stream_type);
		}
	}

	std::vector<pid_with_use_count_t> old_open_pids;

	old_open_pids.reserve(open_pids.size());
	std::swap(old_open_pids, open_pids);

	// remove all pids no longer in use
	// also activate all new pids
	int i = 0;
	for (auto& x : old_open_pids) {
		if (x.use_count == 0) {
			// old pid no longer in use
			x.use_count = 1;				// ensure that that actual delete will occur
			open_pids.push_back(x); // temporarily add in order to remove it
			remove_pid(x.pid);
		} else if (i < old_size) {
			// old pid still in use
			open_pids.push_back(x);
		} else {
			// new pid which still needs to be added
			while (x.use_count-- > 0)
				add_pid(x.pid);
		}
		i++;
	}
	save_pmt(now, pmt);
}

void active_service_t::update_pmt_pid(int new_pmt_pid) {
	remove_pid(current_pmt_pid);
	add_pid(new_pmt_pid);
	this->current_pmt_pid = new_pmt_pid;
	/*
		we do not unregister video and audio streams yet, as some of them may remain unchanged.
		Any needed (un)registration will be handled by update_pmt
	 */
}


/*!
	Add newer epg records which are not yet present
 */
void active_service_t::update_epg_(db_txn& parent_txn, const system_time_t now, meta_marker_t* mm) {
	auto now_ = system_clock_t::to_time_t(now);
	auto dst_epg_txn = parent_txn.child_txn(mpm.db->mpm_rec.recepgdb);
	auto src_epg_txn = receiver.epgdb.rtxn();
	auto start = now;
	auto timeshift_duration = receiver.options.readAccess()->timeshift_duration;
	for (;;) {
		auto rec = epgdb::running_now(src_epg_txn, current_service.k, start);
		if (!rec)
			break; // no more records
		if (rec->k.start_time > system_clock_t::to_time_t(now + timeshift_duration))
			break;
		put_record(dst_epg_txn, *rec);
		if ((*rec).k.start_time <= now_ && now_ < (*rec).end_time && mm) {
			mm->update_epg(*rec);
			mm = nullptr;
		}
		start = system_clock_t::from_time_t(rec->end_time + 1); // so that we can find the next record
	}
	dst_epg_txn.commit();
}

void active_service_t::update_epg(system_time_t now, meta_marker_t* mm) {
	auto txn = mpm.db->mpm_rec.idxdb.wtxn();
	if (!mm) {
		auto w = mpm.meta_marker.writeAccess();
		update_epg_(txn, now, &*w); // get initial epg
	} else {
		update_epg_(txn, now, mm); // get initial epg
	}
	txn.commit();
}

/*!
	periodically called to remove old data in timeshift bufferl so that it does not grow larger than
	what user wants
*/
void active_service_t::housekeeping(system_time_t now) {
	auto parent_txn = mpm.db->mpm_rec.idxdb.wtxn();
	auto rec_txn = parent_txn.child_txn(mpm.db->mpm_rec.recdb);
	// Update stream_time_end and real_time end periodically
	mpm.update_recordings(rec_txn, now);
	rec_txn.commit();
	/*check if newer epg data hase arrived and
		transfer it into the local mpm database

		@todo: is it wise to run directory deletion from this thread?
	*/

	periodic.run([this, &parent_txn](system_time_t now) { mpm.delete_old_data(parent_txn, now); }, now);

	parent_txn.commit();
	/*@todo:
		1) global recording database must also be kept up todate.
		2) when recordings stop, receiver thread should know about this
		It may be more efficient to do part of the housekeeping in the receiver thread

		update_recordings can be run a second time on the global db

		stop_completed_recordings has side effects: it calls finalize recording.
		@todo: separate these side effects

		@todo: if we update the global db from the receiver thread (more efficient:
		only a single transaction) there could be border cases which cause races.

		The main dangerous cases are those where receiver and active_mpm have different
		views on which recordings are running (e.g., receiver first stops recording, but
		active_mpm has not taken action. Then receiver is asked to restart the same recording,
		but active_mpm sees it is already running).

		=> conslusion may be that start/stop recording should only be done from receiver thread?

		*/
}

int service_thread_t::run() {

	ss::string<128> ch_prefix;

	auto saved = active_service.shared_from_this();

	ch_prefix << "CH[" << active_service.get_adapter_no() << ":" << active_service.current_service.k.service_id << "]"
						<< active_service.current_service.name;
	char name[16];
	snprintf(name, 16, "%s", ch_prefix.c_str());
	name[15] = 0;

	set_name(name);
	logger = Logger::getLogger("service");

	log4cxx::NDC ndc(ch_prefix.c_str());

	timer_start(10); // fix recordings every few seconds
	if (active_service.open() < 0) {
		dterror("Could not open channel");
		return -1;
	}
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterror("error in poll");
			continue;
		}
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				log4cxx::NDC ndc("-CMD");
				/* an external request to execute a task, was received.
					 If the task is "exit", then run_tasks will return -1
				*/
				if (run_tasks(now) < 0) {
					dterror("Exiting");
					return 0;
				}
			} else if (is_timer_fd(evt)) {
				// time to do some housekeeping (check tuner)
				log4cxx::NDC ndc("-TIMER");
				now = system_clock_t::now();
				active_service.housekeeping(now);
			} else if (active_service.reader->on_epoll_event(evt->data.fd)) {
				// this must be a channel data event
				if (!(evt->events & EPOLLIN)) {
					dterror("Unexpected event: type=" << evt->events);
				}
				active_service.mpm.process_channel_data();
			} else {
				dtdebug("event from unknown fd\n");
				assert(0);
			}
		}
	}
	assert(0);
	return 0;
}

void active_service_t::restart_decryption(uint16_t ecm_pid, system_time_t t) {
	std::scoped_lock lck(mutex);
	dtdebugx("Restart decryption for pid %d", ecm_pid);
	if (current_pmt.is_ecm_pid(ecm_pid)) {
		/*set a flag indicating that decryption was interrupted,
			while locking a mutex
		*/
		mpm.dvbcsa.restart_decryption(t);
	}
}

void active_service_t::set_services_key(ca_slot_t& slot, int decryption_index) {
	auto slot_has_pid = [slot](uint16_t pid) -> bool {
		for (auto& pid_ : slot.pids)
			if (pid == pid_)
				return true;
		return false;
	};
	std::scoped_lock lck(mutex);
	bool found = false;
	/*
		as there can be multiple encrypted services on the mux,
		we attempt to detect the right service by checking for the presence
		of ca pids in the pmt. This will fail if we have a fake pmt
	 */
	for (auto desc : current_pmt.pid_descriptors) {
		if (slot_has_pid(desc.stream_pid)) {
			mpm.dvbcsa.add_key(slot, decryption_index, slot.last_key.receive_time);
			found = true;
			break; /*we assume that only a single key is used for the full service
							 If audio and video use a different scrambling key, the code
							 is not correct
						 */
		}
	}
	/*the following can theoretically install a key on the wrog service if multiple services
		are active on the same mux*/
	if (pmt_is_encrypted && !found) {
		mpm.dvbcsa.add_key(slot, decryption_index, slot.last_key.receive_time);
	}
}

void active_service_t::mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t t) {
	std::scoped_lock lck(mutex);
	if (current_pmt.is_ecm_pid(ecm_pid)) {
		/*set a flag indicating that decryption was interrupted,
			while locking a mutex
		*/
		mpm.dvbcsa.mark_ecm_sent(odd, t);
	}
}

recdb::live_service_t active_service_t::get_live_service() const {

	auto epg = mpm.meta_marker.readAccess()->get_current_epg();
	const char* p = mpm.dirname.c_str() + receiver.options.readAccess()->live_path.size();
	if (p[0] == '/')
		p++;
	assert(p - mpm.dirname.c_str() < mpm.dirname.size());
	return recdb::live_service_t(system_clock_t::to_time_t(mpm.creation_time), get_adapter_no(),
															 system_clock_t::to_time_t(now), get_current_service(), p, epg);
}

recdb::live_service_t active_service_t::get_live_service_key() const {

	const char* p = mpm.dirname.c_str() + receiver.options.readAccess()->live_path.size();
	if (p[0] == '/')
		p++;
	recdb::live_service_t ret{};
	ret.creation_time = system_clock_t::to_time_t(mpm.creation_time);
	ret.adapter_no = get_adapter_no();
	return ret;
}

std::unique_ptr<playback_mpm_t> active_service_t::make_client_mpm(subscription_id_t subscription_id) {
	auto ret = std::make_unique<playback_mpm_t>(mpm, current_service, current_streams, subscription_id);
	mpm.meta_marker.writeAccess()->register_playback_client(ret.get());
	return ret;
}

static inline pmt_info_t make_dummy_pmt(int service_id, int pmt_pid, int pcr_pid) {
	pmt_info_t ret;
	ret.service_id = service_id;
	ret.pmt_pid = pmt_pid;
	ret.pcr_pid = pcr_pid;
	ret.version_number = 1;
	ret.current_next = 1;
	uint8_t stream_type = 27;							// arbitrary
	ret.capmt_data.push_back((uint8_t)0); // special tag indicating that this is not a ca descriptor
	ret.capmt_data.push_back((uint8_t)3); // length
	ret.capmt_data.push_back((uint8_t)stream_type);
	ret.capmt_data.push_back((uint8_t)(pcr_pid >> 8));
	ret.capmt_data.push_back((uint8_t)(pcr_pid & 0xff));

	ret.capmt_data.push_back((uint8_t)0); // special tag indicating that this is not a ca descriptor
	ret.capmt_data.push_back((uint8_t)3); // length
	ret.capmt_data.push_back((uint8_t)stream_type);
	ret.capmt_data.push_back((uint8_t)(pmt_pid >> 8));
	ret.capmt_data.push_back((uint8_t)(pmt_pid & 0xff));

	return ret;
}

bool active_service_t::need_decryption() {
	if (pmt_is_encrypted) {
		bool ret = have_pat; /*we can only turn decryption on after having received a pmt and  having
													 registered video and audio streams. Otherwise the decryption code will
													 take a lot of time to fill its buffers due to posibly low data rate*/
		if (ret && !registered_scam) {
			// pmt claimed stream is not encrypted, but data tells us otherwise
			auto service_id = current_service.k.service_id;
			auto video_pid = current_service.video_pid;
			if ((video_pid & ~0x1fff) == 0) {
				auto pmt = make_dummy_pmt(service_id, current_pmt_pid, video_pid);
				/*we send to scam thread also for non-encrypted streams,
					so that we can turn off encryption if stream stops being encrypted
				*/
				auto& scam_thread = receiver.scam_thread;
				// bool do_register = !registered_scam;
				registered_scam = true;
				bool isnext{true};
				current_pmt.pid_descriptors.push_back(pid_info_t{video_pid, 2}); // stream_type=2 is fake
				auto adapter_no = get_adapter_no();
				scam_thread.push_task([this, &scam_thread, adapter_no, isnext, pmt]() { // pmt passed by value!
					return cb(scam_thread).update_pmt(this, adapter_no, pmt, isnext);
				}); // don't wait for result (async)
			}
		}
		return ret;
	} else {

		bool ret = have_pmt && /*we can only turn decryption on after having received a pmt and  having
														 registered video and audio streams. Otherwise the decryption code will
														 take a lot of time to fill its buffers due to posibly low data rate*/
							 (current_pmt.is_encrypted() || (mpm.stream_parser.num_encrypted_packets > 0));
		if (ret && !registered_scam) {
			/* pmt claimed stream is not encrypted, but data tells us otherwise
				 On rossia 1 to fail on 40E: 3992V sid=2020 causes errors like
				"older stream change not yet processed - skipping (viewing may fail)".
				The call below is needed ERT1 which reports in the pmt that its streams are no encrypted.
				whereas they are biss encrypted
			 */
			update_pmt(current_pmt, false, pmt_sec_data);
		}
		return ret;
	}
}
