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
#include "recdb_extra.h"
#include "../chdb/chdb_extra.h"
#include "../epgdb/epgdb_extra.h"
#include <fmt/chrono.h>
#include "neumotime.h"

using namespace recdb;

static int overlapping(time_t s1, time_t e1, time_t s2, time_t e2) {
	assert(e1 >= s1);
	assert(e2 >= s2);
	time_t s = (s1 < s2) ? s2 : s1;
	time_t e = (e1 < e2) ? e1 : e2;
	if (e - s <= 0)
		return 0;
	if (e - s >= (e2 - s2) / 2 || e - s >= (e1 - s1) / 2)
		return 1; // if the programs overlap more than 50% of their duration

	return 0;
}

/*!
	returns best matching recording recording, taking into account differences
	in start time due to changed epg records; we require there to be overlap!
*/
std::optional<recdb::rec_t> recdb::rec::best_matching(db_txn& txn, const epgdb::epg_record_t& epg_,
																											bool anonymous) {

	const int tolerance = 60 * 60; // we allow a difference of 60 minutes
	auto epg = epg_;
	epg.k.start_time -= tolerance;
	epg.k.anonymous = anonymous;
	auto c = recdb::rec_t::find_by_key(txn, epg.k, find_type_t::find_geq,
																		 /*
																			 key_prefix: this is set to service and must NOT include start_time
																			*/
																		 rec_t::partial_keys_t::service);

	if (!c.is_valid())
		return {}; // no record found
	rec_t best_match{};
	auto best_delta = std::numeric_limits<int64_t>::max();
	for (const auto& rec : c.range()) {
		if (rec.epg.k.anonymous != anonymous)
			continue;
		if (rec.epg.rec_status != epgdb::rec_status_t::SCHEDULED &&
				rec.epg.rec_status != epgdb::rec_status_t::IN_PROGRESS)
			continue; //skip cancelled and finished recordings
		assert(rec.epg.k.service.service_id == epg.k.service.service_id); // sanity check

		if (rec.epg.k.start_time == epg_.k.start_time && rec.epg.k.event_id == epg_.k.event_id) {
			// we found a perfect match
			return rec;
		}
		if (!overlapping(rec.epg.k.start_time, rec.epg.end_time, epg_.k.start_time, epg_.end_time)) {
			if (rec.epg.k.start_time >= epg_.end_time)
				break;
			continue; // rec must be earlier than requested start
		}

		auto delta = std::abs((int64_t)rec.epg.k.start_time - (int64_t)epg.k.start_time);
		if (delta < best_delta ||
				(best_delta != std::numeric_limits<int64_t>::max() && rec.epg.k.event_id == epg.k.event_id &&
				 best_match.epg.k.event_id != epg.k.event_id)) { // prioritize records with the correct event_id
			best_delta = delta;
			best_match = rec;
		}
	}

	if (best_delta == std::numeric_limits<int64_t>::max())
		return {};
	return best_match;
}

/*
	create a file name for a recording
*/
void recdb::rec::make_filename(ss::string_& ret, const chdb::service_t& s, const epgdb::epg_record_t& epg) {
	ret.format("{} - {} - {:%F %H:%M}", epg.event_name, s.name, fmt::localtime(epg.k.start_time));
	for (auto& c : ret) {
		if (c == '/' || c == '\\' || iscntrl(c) || !c)
			c = ' ';
	}
}


int32_t recdb::make_unique_id(db_txn& txn, autorec_t& autorec)
{
	if(autorec.id >=0)
		return autorec.id;
	int32_t id = std::numeric_limits<int32_t>::max();
	while (id > 0) {
		auto c = recdb::autorec_t::find_by_key(txn, id, find_leq);
		if (c.is_valid()) {
			auto largest = c.current();
			if(largest.id + 1  < id) {
				autorec.id = largest.id + 1;
				return autorec.id;
			}
			/*at this stage the top range of ids is fully
				used. We move towards lower ids looking for a gap
			 */
			id = largest.id - 1;
			if(!c.prev()) {
				assert(0);
				break; //we failed
			}
		} else {
			autorec.id = 1; //initialisation
			return autorec.id;
		}
	}
	assert(0);
	return -1;
}



recdb::rec_t recdb::new_recording(db_txn& rec_wtxn, const chdb::service_t& service,
																					epgdb::epg_record_t& epgrec, int pre_record_time, int post_record_time) {
	auto stream_time_start = milliseconds_t(0);
	auto stream_time_end = stream_time_start;

	// TODO: times in start_play_time may have a different sign than stream_times (which can be both negative and
	// positive)
	using namespace recdb;
	time_t real_time_start = 0;
	// real_time end will determine when epg recording will be stopped
	time_t real_time_end = 0;
	subscription_id_t subscription_id = subscription_id_t{-1};
	using namespace recdb;
	using namespace recdb::rec;
	ss::string<256> filename;

	const auto rec_type = rec_type_t::RECORDING;
	assert(epgrec.rec_status != epgdb::rec_status_t::IN_PROGRESS);

	epgrec.rec_status = epgdb::rec_status_t::SCHEDULED;
	auto rec = rec_t(rec_type, -1 /*owner*/, (int)subscription_id, stream_time_start, stream_time_end,
									 real_time_start, real_time_end,
									 pre_record_time, post_record_time, filename, service, epgrec, {});
	put_record(rec_wtxn, rec);

	return rec;
}

/*
	make and insert a new recording into the global recording database
*/
recdb::rec_t recdb::new_recording(db_txn& rec_wtxn, db_txn& epg_wtxn,
																					const chdb::service_t& service, epgdb::epg_record_t& epgrec,
																	int pre_record_time, int post_record_time) {
	auto ret = new_recording(rec_wtxn, service, epgrec, pre_record_time, post_record_time);

	// update epgdb.mdb so that gui code can see the record
	auto c = epgdb::epg_record_t::find_by_key(epg_wtxn, epgrec.k);
	if (c.is_valid()) {
		assert(epgrec.k.anonymous == (epgrec.k.event_id == TEMPLATE_EVENT_ID));
		epgdb::update_record_at_cursor(c, epgrec);
	}
	return ret;
}



fmt::format_context::iterator
fmt::formatter<marker_key_t>::format(const marker_key_t& k, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{}",  k.time);
}

fmt::format_context::iterator
fmt::formatter<marker_t>::format(const marker_t& m, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "{} packets=[{:d} - {:d}]", m.k.time,
								 m.packetno_start, m.packetno_end);
}

fmt::format_context::iterator
fmt::formatter<file_t>::format(const file_t& f, format_context& ctx) const {
	return fmt::format_to(ctx.out(), "file {:d}; stream time: [{} - {}]"
								 "real time: [{:%F %H:%M} - {:%H:%M}"
								 " packets=[{:d} - {:d}]",
								 f.fileno, f.k.stream_time_start, f.stream_time_end,
								 fmt::localtime(f.real_time_start),  fmt::localtime(f.real_time_end),
								 f.stream_packetno_start, f.stream_packetno_end);
}

fmt::format_context::iterator
fmt::formatter<rec_fragment_t>::format(const rec_fragment_t& f, format_context& ctx) const {
	return fmt::format_to(ctx.out(),
												"stream time: [{} - {}]"
												"play time: [{} - {}]",
												f.play_time_start, f.play_time_end,
												f.stream_time_start, f.stream_time_end);
}

fmt::format_context::iterator
fmt::formatter<rec_t>::format(const rec_t& r, format_context& ctx) const {
	return fmt::format_to(ctx.out(),
												"{}\n        {}\n"
												"nstream time: [{} - {}]\n"
												"real time: [{:%F %H:%M} - {:%H:%M}]]\n",
												r.service, r.epg,
												r.stream_time_start, r.stream_time_end,
												fmt::localtime(r.real_time_start), fmt::localtime(r.real_time_end),
												r.filename);
}
