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
#include "util/dtassert.h"
#include "recdb_extra.h"
#include "../chdb/chdb_extra.h"
#include "../epgdb/epgdb_extra.h"
#include <fmt/chrono.h>
#include "xformat/ioformat.h"
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

std::ostream& recdb::operator<<(std::ostream& os, const marker_key_t& k) {
	os << k.time;
	return os;
}

std::ostream& recdb::operator<<(std::ostream& os, const marker_t& m) {
	os << m.k.time;
	stdex::printf(os, " packets=[%ld - %ld]", m.packetno_start, m.packetno_end);
	return os;
}

std::ostream& recdb::operator<<(std::ostream& os, const file_t& f) {
	stdex::printf(os, "file %d; stream time: [", f.fileno);
	os << f.k.stream_time_start << " - " << f.stream_time_end;
	os << "] real time: [";
	os << fmt::format("{:%F %H:%M}", fmt::localtime(system_clock::from_time_t(f.real_time_start)));
	os << " - ";
	os << fmt::format("{:%H:%M}", fmt::localtime(system_clock::from_time_t(f.real_time_end)));
	stdex::printf(os, " packets=[%ld - %ld]", f.stream_packetno_start, f.stream_packetno_end);
	return os;
}

std::ostream& recdb::operator<<(std::ostream& os, const rec_fragment_t& f) {
	os << "stream time: [" << f.play_time_start << " - " << f.play_time_end << "] play time: [" << f.stream_time_start
		 << " - " << f.stream_time_end << "]";
	return os;
}

std::ostream& recdb::operator<<(std::ostream& os, const rec_t& r) {
	os << r.service;
	os << "\n        ";
	os << r.epg
		 << "\nstream time: [" << r.stream_time_start << " - " << r.stream_time_end << "]\n  real time: ["
           << fmt::format("{:%F %H:%M}", fmt::localtime(system_clock::from_time_t(r.real_time_start))) << " - "
           << fmt::format("{:%H:%M}", fmt::localtime(system_clock::from_time_t(r.real_time_end)));
	stdex::printf(os, "]\n");
	os << "\n" << r.filename;
	return os;
}

void recdb::to_str(ss::string_& ret, const marker_key_t& k) { ret << k.time; }

void recdb::to_str(ss::string_& ret, const marker_t& m) { ret << m; }

void recdb::to_str(ss::string_& ret, const file_t& f) { ret << f; }

void recdb::to_str(ss::string_& ret, const rec_fragment_t& f) { ret << f; }

void recdb::to_str(ss::string_& ret, const rec_t& r) { ret << r; }

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
	ss::accu_t ss(ret);
	ss << epg.event_name << " - " << s.name << " - "
		 << fmt::format("{:%F %H:%M}", fmt::localtime(system_clock::from_time_t(epg.k.start_time)));
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
		}
	}
	assert(0);
	return -1;
}
