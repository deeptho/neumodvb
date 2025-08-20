/*
 * Neumo dvb (C) 2019-2025 deeptho@gmail.com
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
#include <type_traits>
#include <values.h>
//#include <getopt.h>
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include "mpm.h"
#include "neumodmx.h"
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
#include "streamfilter.h"
#include "streamparser/pes.h"
#include "util/dtutil.h"

#include "active_adapter.h"
#include "active_service.h"
#include "neumo.h"
#include "filemapper.h"
#include "streamparser/packetstream.h"
#include "streamparser/si.h"

active_service_t::active_service_t(
	active_adapter_t& active_adapter, const chdb::service_t& service_,
	const std::shared_ptr<stream_reader_t>& reader)
	: active_stream_t(active_adapter.receiver, std::move(reader))
	, current_service(service_)
	, stream_buffer(std::make_unique<active_mpm_t>( this))
	, service_thread(*this) {
}

active_service_t::active_service_t(active_adapter_t& active_adapter,
																	 ts_in_ts_stream_filter_t* output_filter,
																	 const chdb::service_t& service_,
																	 const std::shared_ptr<stream_reader_t>& reader)
	: active_stream_t(active_adapter.receiver, std::move(reader))
	, current_service(service_)
	, stream_buffer(std::make_unique<active_ts_t>( this, output_filter))
	, service_thread(*this)
{
}

ss::string<32> active_service_t::name() const { return current_service.name.c_str(); }

int active_service_t::open() {
	log4cxx::NDC(name());
	auto demux_fd = active_stream_t::open(PAT_PID, &service_thread.epx, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET);
	// TOOD: initially we read data as soon as it becomes available to speed up channel tuning
	// once the channel is up and running we will switch to polling
	if(demux_fd< 0)
		return demux_fd;
	this->last_payload_data = system_clock_t::now();
	this->last_decrypted_data = this->last_payload_data;
	this->active_adapter().on_message(this, "");
	dtdebugf("Opening");
	return demux_fd;
}

int active_service_t::deactivate() {
	log4cxx::NDC(name());
	int ret = 0;
	dtdebugf("deactivate service");
	this->stream_buffer->close();
	if (registered_scam) {
		auto& scam_thread = receiver.scam_thread;
		auto future = scam_thread.push_task(
				[this, &scam_thread]() { return cb(scam_thread).unregister_active_service(this, get_adapter_no()); });
		dtdebugf("deactivate stream");
		ret = active_stream_t::deactivate();
		future.wait(); // must be synchronous or problems will occur
		dtdebugf("scam_thread unregister_active_service done");
		registered_scam = false;
	} else
		ret = active_stream_t::deactivate();
	dtdebugf("deactivate service done");
	return ret;
}

int service_thread_t::exit() {
	dtdebugf("Starting to exit");
	active_service.deactivate();
	dtdebugf("Ended exit");
	return -1;
}

std::optional<recdb::rec_t> service_thread_t::cb_t::start_recording(
	subscription_id_t subscription_id, const recdb::rec_t& rec) {
	recdb::rec_t recnew = active_service.mpm()->start_recording(subscription_id, rec);
	assert(recnew.epg.rec_status == epgdb::rec_status_t::IN_PROGRESS);
	return recnew;
}

int service_thread_t::cb_t::stop_recording(const recdb::rec_t& rec_in, mpm_copylist_t& copy_commands) {
	return active_service.mpm()->stop_recording(rec_in, copy_commands);
}

void service_thread_t::cb_t::forget_recording_in_livebuffer(const recdb::rec_t& rec_in) {
	return active_service.mpm()->forget_recording_in_livebuffer(rec_in);
}

static inline bool is_abertis(const chdb::service_t& service, const pmt_info_t& pmt, const dtdemux::pid_info_t & pid_info)
{
	return
		(service.k.mux.sat_pos -(int) -3000) < 300 &&
		strncmp(pmt.provider_name.c_str(), "HSA", 2)==0 &&
		pid_info.stream_type == stream_type::stream_type_t::MPE_FEC;
}

void active_service_t::update_aa_pmt_(const dtdemux::pmt_info_t& pmt, bool isnext, bool service_changed) {
	auto mux_key = current_service.k.mux;
	if (!isnext) {
		auto& active_adapter = this->active_adapter();
		auto active_adapter_p = active_adapter.shared_from_this();
		// pmt deliberately passed by value
		if (service_changed) {
			active_adapter.tuner_thread.push_task([active_adapter_p, pmt, mux_key, service = current_service] {
				auto& cb_ = cb(active_adapter_p->tuner_thread);
				cb_.on_pmt_update(*active_adapter_p, mux_key, pmt); //update epg types in dvbs_mux in database
				cb_.update_service(service); //update service record in database
				return 0;
			});
		} else {
			active_adapter.tuner_thread.push_task([active_adapter_p, pmt, mux_key] {
				cb(active_adapter_p->tuner_thread).on_pmt_update(*active_adapter_p, mux_key, pmt);
				return 0;
			});
		}
	}
}

void active_service_t::update_scam_pmt_(const dtdemux::pmt_info_t& pmt, bool isnext,
																				bool service_changed, bool ca_changed) {

	bool was_encrypted = registered_scam;
	bool is_encrypted = pmt.is_encrypted() || this->stream_buffer->has_encrypted_packets();
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
}

void active_service_t::update_pmt(const dtdemux::pmt_info_t& pmt, bool isnext,
																	const ss::bytebuffer_& sec_data) {
	//assert(!this->is_ts_in_ts());
	using namespace dtdemux;
#if 0
	dtdebugf("{}", pmt);
#endif
	have_pmt = true;
	pmt_is_encrypted = false;

	if (pmt.service_id != current_service.k.service_id) {
		// This can happen according to the dvb specs
		dtdebugf("received pmt for wrong service_id: pid={:d} service_id={:d}!={:d}", pmt.pmt_pid, pmt.service_id,
						 current_service.k.service_id);
		return;
	}
	bool is_new = current_pmt.pmt_pid == null_pid;
	bool ca_changed = is_new || pmt_ca_changed(current_pmt, pmt);
	bool service_changed = (pmt.pmt_pid != current_service.pmt_pid) || (pmt.video_pid != current_service.video_pid);
	auto mux_key = current_service.k.mux;
	{
		std::scoped_lock lck(mutex);
		if (service_changed) {
			if(current_service.media_mode != pmt.estimated_media_mode) {
				if (pmt.estimated_media_mode  == chdb::media_mode_t::DATA  &&
						current_service.media_mode == chdb::media_mode_t::T2MI) {
					//we prefer media_mode from pmt
					current_service.media_mode = pmt.estimated_media_mode;
					current_service.media_mode_from_pmt = true;
				}
			}
			current_service.service_type = pmt.service_type;
			current_service.pmt_pid = pmt.pmt_pid;
			current_service.video_pid = pmt.video_pid;
		}
		current_pmt = pmt;
		pmt_sec_data = sec_data;
	}
	update_aa_pmt_(pmt, isnext, service_changed);
	update_scam_pmt_(pmt, isnext, service_changed, ca_changed);

	if (isnext) {
		dtdebugf("Unhandled PMT NEXT: service={:d}", pmt.service_id);
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
		if(pid == 0x1fff)
			return;
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
		// dtdebugf(pidinfo);
		if (is_video(pidinfo.stream_type) || is_audio(pidinfo) || pidinfo.has_subtitles())
			process(pidinfo.stream_pid);
		else if(is_abertis(current_service, pmt, pidinfo)) {
			dtdebugf("Starting Abertis service");
			process(pidinfo.stream_pid);
			this->stream_buffer->register_parser_pid(pmt.service_id, pidinfo);
			continue;
		}

		/*the following code will parse either video or audio streams to extract timeing info.
			The choice between either is based on pcr_pid.
			@todo: in case of a radio channel, we need to register an audio pid instead
			In that case audio_pid == video_pid
			*/
		if (pmt.pcr_pid == pidinfo.stream_pid) {
			this->stream_buffer->register_parser_pid(pmt.service_id, pidinfo);
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
	stream_buffer->save_pmt(now, pmt, sec_data);
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

int service_thread_t::run() {

	ss::string<128> ch_prefix;
	ch_prefix.format("CH[{}:{}] {}", (int)active_service.get_adapter_no(),
									 (int)active_service.current_service.k.service_id,
									 (const char*)active_service.current_service.name.c_str());

	char name[16];
	snprintf(name, 16, "%s", ch_prefix.c_str());
	name[15] = 0;

	set_name(name);
	logger = Logger::getLogger("service");

	log4cxx::NDC ndc(ch_prefix.c_str());

	timer_start(10); // fix recordings every few seconds
	if (active_service.open() < 0) {
		dterrorf("Could not open channel");
		return -1;
	}
	for (;;) {
		auto n = epoll_wait(2000);
		if (n < 0) {
			dterrorf("error in poll");
			continue;
		}
		for (auto evt = next_event(); evt; evt = next_event()) {
			if (is_event_fd(evt)) {
				log4cxx::NDC ndc("-CMD");
				/* an external request to execute a task, was received.
					 If the task is "exit", then run_tasks will return -1
				*/
				if (run_tasks(now) < 0) {
					dterrorf("Exiting");
					return 0;
				}
			} else if (is_timer_fd(evt)) {
				// time to do some housekeeping (check tuner)
				log4cxx::NDC ndc("-TIMER");
				now = system_clock_t::now();
				active_service.housekeeping(now);
			} else if (active_service.reader->on_epoll_event(evt)) {
				// this must be a channel data event
				if (!(evt->events & EPOLLIN)) {
					dterrorf("Unexpected event: type={}", evt->events);
				}
				active_service.process_service_data();
			} else {
				dtdebugf("event from unknown fd\n");
				assert(0);
			}
		}
	}
	assert(0);
	return 0;
}

void active_service_t::restart_decryption(uint16_t ecm_pid, system_time_t t) {
	std::scoped_lock lck(mutex);
	dtdebugf("Restart decryption for pid {:d}", ecm_pid);
	if (current_pmt.is_ecm_pid(ecm_pid)) {
		/*set a flag indicating that decryption was interrupted,
			while locking a mutex
		*/
		reader->dvbcsa.restart_decryption(t);
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
			reader->dvbcsa.add_key(slot, decryption_index, slot.last_key.receive_time);
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
		reader->dvbcsa.add_key(slot, decryption_index, slot.last_key.receive_time);
	}
}

void active_service_t::mark_ecm_sent(bool odd, uint16_t ecm_pid, system_time_t t) {
	std::scoped_lock lck(mutex);
	if (current_pmt.is_ecm_pid(ecm_pid)) {
		/*set a flag indicating that decryption was interrupted,
			while locking a mutex
		*/
		reader->dvbcsa.mark_ecm_sent(odd, t);
	}
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
			(current_pmt.is_encrypted() || (this->stream_buffer->stream_parser.num_encrypted_packets > 0));
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

void active_service_t::destroy() {
#ifndef NDEBUG
#endif
	assert(service_thread.has_exited());
}

active_service_t::~active_service_t() {
#ifndef NDEBUG
	assert(service_thread.has_exited());
#endif
}

std::optional<recdb::rec_t>
active_service_t::start_recording(subscription_id_t subscription_id, const recdb::rec_t& rec_in)
{
	std::vector<task_queue_t::future_t> futures;
	std::optional<recdb::rec_t> rec;
	auto& as = this->service_thread;
	assert((int) subscription_id == rec_in.subscription_id);
	futures.push_back(as.push_task(
											// subscription_id is stored in the recording itself
											//Pass by reference is safe because we call wait_for_all
											[&as, &rec, &rec_in, subscription_id]() {
												rec = cb(as).start_recording(subscription_id, rec_in);
												return 0;
											}));

	bool error = wait_for_all(futures);
	if (error) {
		dterrorf("Unhandled error in unsubscribe");
	}

	if((int)subscription_id < 0 && receiver.global_subscriber) {
		ss::string<256> msg;
		msg.format("Could not start recording: {}\n{}\n{}", rec_in.epg.event_name, rec_in.service.name, get_error());
		receiver.global_subscriber->notify_message(msg);
	}
	/*wait_for_futures is needed because active_adapters/channels may be removed from reserved_services and subscribed_aas
		This could cause these structures to be destroyed while still in use by by stream/active_adapter threads

		See
		https://stackoverflow.com/questions/50799719/reference-to-local-binding-declared-in-enclosing-function?noredirect=1&lq=1
	*/

	return rec;
}

void active_service_t::service_status_message(stream_status_t status) {
	ss::string<128> msg;
	auto s = this->get_current_service();
	if(!this->have_pmt) {
		msg.format("Service \"{}\" not currently active", s.name);
		stream_buffer->set_stream_status(stream_status_t::INACTIVE);
	} else {
		switch(status) {
		case stream_status_t::ERROR:
			msg.format("Service \"{}\": error", s.name);
			break;
		case stream_status_t::ENCRYPTED:
			msg.format("Service \"{}\": cannot decrypt", s.name);
			break;
		case stream_status_t::NODATA:
			msg.format("Service \"{}\": no data", s.name);
			break;
		default:
			break;
		}
		stream_buffer->set_stream_status(status);
	}
	if(msg.size()>0) {
		auto& active_adapter = this->active_adapter();
		active_adapter.on_message(this, msg);
	}
}

void active_service_t::process_service_data() {
	now = system_clock_t::now();
	auto start = steady_clock_t::now();
	for (;;) {
		auto s = steady_clock_t::now();
		auto delta = s - start;
		if (delta > 500ms) {
			dtdebugf("SKIPPING EARLY\n");
			break;
		}
		if(now- this->last_payload_data > 4000ms) {
			if (is_encrypted && (now - last_decrypted_data) > 4000ms) {
				service_status_message(stream_status_t::ENCRYPTED);
				this->last_decrypted_data = now + 1s;
			}
			else
				service_status_message(stream_status_t::NODATA);
			this->last_payload_data = now + 1s;
		}
		uint8_t* buffer = NULL;
		ssize_t remaining_space = this->stream_buffer->get_write_buffer(buffer);
		// TODO: ensure parser can cope with changing mmap region

		if (remaining_space < 1024) {
			/*
				grow the file and move the mmaped region
				TODO: to support rewind and such, either we need to map full files
				or come up with some system of mapping multiple chunks. In the latter case
				moving an mmapped region is not optimal. The readv function call can help to
				read data into multiple chunks
			*/
			this->stream_buffer->advance();
			remaining_space = this->stream_buffer->get_write_buffer(buffer);
		}
		/*
			read as much data as possible.
			TODO: could it be more efficient to simply use timed reads? I.e., we wait as long
			as allowed (e.g., 100ms) and then read large chunks of data for one stream. This
			may be more efficient for filesystem access.
		*/
		int toread = std::min(remaining_space, (long)ts_packet_t::size * 1024);
		ssize_t ret = this->reader->read_into(buffer, toread - (toread % dtdemux::ts_packet_t::size),
																					&this->open_pids);
		if (ret < 0) {
			if (errno == EINTR) {
				// dtdebugf("Interrupt received (ignoring)");
				continue;
			}
			if (errno == EOVERFLOW) {
				dtdebug_nicef("OVERFLOW");
				continue;
			}
			if (errno == EAGAIN) {
				break; // no more data
			} else {
				dterrorf("error while reading: {}", strerror(errno));
				break;
			}
		}
		assert(ret >= 0);
		if (ret == 0) {
			return;
		}

		if (ret % ts_packet_t::size != 0) {
			dterrorf("ret={:d} ret%%188={:d}", ret, ret % ts_packet_t::size);
		}
		/*decrypt as many bytes as possible.
			In case stream is not encrypted, we just move the decrypt pointer.
			The decryptiomn process simply overwrites the encrypted data.
			TODO: improved handling of decrypt failures from oscam. When decryption fails,
			decrypt pointer should not be advanced and decryption should be retried later.
			This means we have to store the decrypt pointer on file, or even implement
			a better system where decrypted and not yet encrypted ranges may be mixed in the file.
			Decryption could then proceed at some later time. This also allows nonlive decryption.
		*/
		assert(ret >= 0);
		this->stream_buffer->advance_write_pointer(ret);
		auto* pmt_parser = this->pmt_parser.get();
		this->pmt_is_encrypted = (pmt_parser && pmt_parser->num_encrypted_packets > 0);
		this->is_encrypted = this->need_decryption();
		assert(!is_encrypted || this->stream_buffer->num_bytes_decrypted == this->reader->dvbcsa.num_bytes_decrypted);

		bool low_data_rate = this->pmt_is_encrypted;
		int num_bytes_to_decrypt = this->stream_buffer->bytes_to_decrypt(buffer);


		auto num_bytes_decrypted_now =
			(is_encrypted) ? this->reader->decrypt_channel_data(buffer, num_bytes_to_decrypt, low_data_rate)
			: num_bytes_to_decrypt;
		if (!is_encrypted)
			this->reader->dvbcsa.num_bytes_decrypted += num_bytes_decrypted_now;

		auto new_payload_data = this->stream_buffer->process_service_data(num_bytes_decrypted_now);

		if (this->stream_buffer->num_bytes_read % dtdemux::ts_packet_t::size != 0) {
			dtdebugf("Read partial packet: num_bytes_read={:d} num_bytes_read%%188={:d}", this->stream_buffer->num_bytes_read,
							 this->stream_buffer->num_bytes_read % dtdemux::ts_packet_t::size);
		}
		if(new_payload_data)
			last_payload_data = now;
		if(is_encrypted && num_bytes_decrypted_now>0)
			last_decrypted_data = now;
		assert(this->stream_buffer->num_bytes_decrypted == this->reader->dvbcsa.num_bytes_decrypted);
		this->stream_buffer->num_bytes_read += ret;
	}
}

void active_ts_t::close() {
	stream_parser.exit();
	// TODO: check that parser is complete destroyed
	this->active_service = nullptr;
	dtdebugf("active_ts closed");
}

void active_ts_t::data_cb(uint8_t* buffer, int num_bytes) {
	//printf("received  %d bytes\n", num_bytes);
	output_filter->read_data(buffer, num_bytes);
}

bool active_ts_t::process_service_data(int num_bytes_decrypted_now)
{
	/*
		For an encrypted channel, note that the code below will not parse unencrypted data such
		as PMT and PAT while problems with video/audio scrambling exist and as a result num_bytes_decrypted_now==0.
		However, video and audio streams are not present until after the first pmt is successfully read. So we should be safe
		@todo: we could make discarding data more clever by only skipping encrypted packets
	*/
	//set location where stream_parser will start parsing data
	this->set_buffer(num_bytes_decrypted_now);

	if(num_bytes_decrypted_now) {
		assert(num_bytes_decrypted_now % ts_packet_t::size == 0);

		dttime_init();
		this->stream_parser.parse();
		dttime(500);
		this->advance_decrypt_pointer(num_bytes_decrypted_now);

		this->num_bytes_decrypted += num_bytes_decrypted_now;
	}

	return true;
}

void active_ts_t::register_parser_pid(int service_id, const dtdemux::pid_info_t& pidinfo)
{
	this->stream_parser.register_embedded_ts_pid(pidinfo.stream_pid,  service_id,
																							 [this ] (uint8_t*buffer, int size) mutable {
																								 this->data_cb(buffer, size);
																							 });
}


void active_service_t::add_pat_and_pmt_parsers() {
	auto& stream_parser = stream_buffer->stream_parser;
	this->pat_parser = stream_parser.register_pat_pid();
	this->pat_parser->section_cb = [this](const pat_services_t& pat_services, const subtable_info_t& i) {
		bool found{false};
		assert(!i.timedout);
		auto& stream_parser = this->stream_buffer->stream_parser;
		for (const auto& e : pat_services.entries) {
			if (e.service_id == this->current_service.k.service_id) {
				this->have_pat = true;
				dtdebugf("PAT START PMT=0x{:x}", e.pmt_pid);
				this->update_pmt_pid(e.pmt_pid);
				this->pmt_parser = stream_parser.register_pmt_pid(e.pmt_pid, e.service_id);
				this->pmt_parser->section_cb =
					[this](pmt_parser_t* parser, const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data) {
						if(pmt.service_id == this->current_service.k.service_id) {
							//on 30.0W 12398, multiple services share the same pmt_pid. We need the correct one
							this->update_pmt(pmt, isnext, sec_data);
						}
						return dtdemux::reset_type_t::NO_RESET;
					};
				found = true;
				break;
			}
		}
		if(!found) {
			auto s = this->get_current_service();
			dtdebugf("Service \"{}\" not present in pat", s);
			//the following copes with services that may not be in PAT nor in SDT
			this->pmt_parser = stream_parser.register_pmt_pid(current_service.pmt_pid, current_service.k.service_id);
			this->pmt_parser->section_cb =
				[this](pmt_parser_t* parser, const pmt_info_t& pmt, bool isnext, const ss::bytebuffer_& sec_data) {
					if(pmt.service_id == this->current_service.k.service_id) {
						//on 30.0W 12398, multiple services share the same pmt_pid. We need the correct one
						this->update_pmt(pmt, isnext, sec_data);
					}
					return dtdemux::reset_type_t::NO_RESET;
				};
		}
		return dtdemux::reset_type_t::NO_RESET;
	};
}
