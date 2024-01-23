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

#pragma once
#include "neumodb/epgdb/epgdb_db.h"
#include "neumodb/chdb/chdb_extra.h"
#include "fmt/core.h"

namespace epgdb {
	typedef screen_t<epgdb::epg_record_t> epg_screen_base_t;
	struct epg_screen_t : public epg_screen_base_t {

		inline bool update_between(db_txn& txn, time_t start_time, time_t end_time) {
			auto match_fn = [&start_time, &end_time](const epgdb::epg_record_t& epg) {
				return epg.k.start_time < end_time && epg.end_time >= start_time;
			};
			return update_if_matches(txn, match_fn);
		}
		using epg_screen_base_t::epg_screen_base_t; //inherit constructors

	};

	inline bool is_same(const epg_record_t &a, const epg_record_t &b) {
		if (!(a.k == b.k))
			return false;
		if (!(a.content_codes == b.content_codes)) //@todo: content_codes must be compared after ordering
			return false;
		if (!(a.parental_rating == b.parental_rating))
			return false;
		/*
		if (!(a.source == b.source))
			return false;
		*/
		if (!(a.end_time == b.end_time))
			return false;
		if (!(a.event_name == b.event_name))
			return false;
		if (!(a.story == b.story))
			return false;
		/*
		if (!(a.mtime == b.mtime))
			return false;
		*/
		return true;

	}

		//TODO: see //emphasis off: see en_300468v010701p.pdf p. 78 data cleaning

	//bool save_epg_record_if_better(db_txn& txnepg, epgdb::epg_record_t& record);

	std::optional<epgdb::epg_record_t> best_matching(db_txn& txnepg, const epgdb::epg_key_t& k, time_t end_time,
																									 int tolerance=30*60);

	std::optional<epgdb::epg_record_t> running_now(db_txn& txnepg, const chdb::service_key_t& k, time_t now);

	inline std::optional<epgdb::epg_record_t> running_now(db_txn& txnepg, const chdb::service_key_t& k,
																												system_time_t now_) {
		auto now = system_clock_t::to_time_t(now_);
		return running_now(txnepg, k, now);
	}
	std::unique_ptr<epg_screen_t>
	chepg_screen(db_txn& txnepg,
							 std::shared_ptr<neumodb_t> tmpdb, uint32_t sort_order,
							 const chdb::service_key_t& service_key,
							 time_t start_time,
#ifdef USE_END_TIME
							 time_t end_time =0,
#endif
							 const ss::vector_<field_matcher_t>* field_matchers_ =nullptr,
							 const epgdb::epg_record_t* match_data_ = nullptr,
							 const ss::vector_<field_matcher_t>* field_matchers2_ =nullptr,
							 const epgdb::epg_record_t* match_data2_ = nullptr
		);

	void clean(db_txn& txnepg, system_time_t start_time);

	std::optional<chdb::service_t> service_for_epg_record(db_txn &txn,  const epgdb::epg_record_t& epg_record);

	bool save_epg_record_if_better_update_input(db_txn& txnepg,
																							epgdb::epg_record_t& record /* no const because we update story and
																																						 such in the input variable record*/);
	bool save_epg_record_if_better(db_txn& txnepg, const epgdb::epg_record_t& record);

	bool update_epg_recording_status(db_txn& epgdb_wtxn, const epgdb::epg_record_t& epgrec);

	class gridepg_screen_t {
		struct entry_t {
			chdb::service_key_t service_key;
			std::unique_ptr<epg_screen_t> epg_screen;
			entry_t(const chdb::service_key_t& service_key) : service_key(service_key)
				{}
		};

		std::vector<entry_t> entries;
		system_time_t start_time;
#ifdef USE_END_TIME
		system_time_t end_time; //limits start_time of records if larger than start_time
#endif
		uint32_t epg_sort_order;
		std::shared_ptr<neumodb_t> tmpdb; //database used for temporary epg records

	public:
#ifdef USE_END_TIME
		gridepg_screen_t(system_time_t start_time, system_time_t end_time, int num_services, uint32_t epg_sort_order)
			: start_time(start_time)
			, end_time(end_time)
			, epg_sort_order(epg_sort_order) {
			entries.reserve(num_services);
		}

		gridepg_screen_t(time_t start_time, time_t end_time, int num_services, uint32_t epg_sort_order)
			: gridepg_screen_t(system_clock_t::from_time_t(start_time),
												 system_clock_t::from_time_t(end_time), num_services, epg_sort_order)
			{}

#else
		gridepg_screen_t(system_time_t start_time, int num_services, uint32_t epg_sort_order)
			: start_time(start_time)
			, epg_sort_order(epg_sort_order) {
			entries.reserve(num_services);
		}

		gridepg_screen_t(time_t start_time, int num_services, uint32_t epg_sort_order)
			: gridepg_screen_t(system_clock_t::from_time_t(start_time),
												 num_services, epg_sort_order)
			{}
#endif


		void remove_service(const chdb::service_key_t& service_key);

		epg_screen_t* epg_screen_for_service(const chdb::service_key_t& service_key);

		epg_screen_t* add_service(db_txn& txnepg, const chdb::service_key_t& service_key);


	};

};

#define declfmt(t)																											\
	template <> struct fmt::formatter<t> {																\
	inline constexpr format_parse_context::iterator parse(format_parse_context& ctx) { \
		return ctx.begin();																									\
	}																																			\
																																				\
	format_context::iterator format(const t&, format_context& ctx) const ;\
}


declfmt(epgdb::epg_source_t);
declfmt(epgdb::epg_key_t);
declfmt(epgdb::epg_record_t);
#undef declfmt
