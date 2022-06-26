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

#include <errno.h>
#include <libconfig.h++>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <map>

#include "active_adapter.h"
#include "adapter.h"
#include "signal_info.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/neumovariant.h"
#include "spectrum_algo.h"

static inline constexpr int make_code(int pls_mode, int pls_code, int timeout = 0) {
	return (timeout & 0xff) | ((pls_code & 0x3FFFF) << 8) | (((pls_mode)&0x3) << 26);
}

void cmdseq_t::add_clear() {
	int cmd = DTV_CLEAR;
	assert(cmdseq.num < props.size() - 1);
	memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
	cmdseq.props[cmdseq.num].cmd = cmd;
	cmdseq.num++;
};

void cmdseq_t::add(int cmd, const dtv_fe_constellation& constellation) {
	assert(cmdseq.num < props.size() - 1);
	memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
	cmdseq.props[cmdseq.num].cmd = cmd;
	cmdseq.props[cmdseq.num].u.constellation = constellation;
	cmdseq.num++;
};

void cmdseq_t::add_pls_codes(int cmd) {
	assert(cmdseq.num < props.size() - 1);
	auto* tvp = &cmdseq.props[cmdseq.num];
	memset(tvp, 0, sizeof(cmdseq.props[cmdseq.num]));
	tvp->cmd = cmd;
	assert(pls_codes.size() > 0);
	tvp->u.pls_search_codes.num_codes = pls_codes.size();
	tvp->u.pls_search_codes.codes = &pls_codes[0];
	cmdseq.num++;
};

void cmdseq_t::add_pls_range(int cmd, const pls_search_range_t& pls_search_range) {
	assert(cmdseq.num < props.size() - 1);
	auto* tvp = &cmdseq.props[cmdseq.num];
	memset(tvp, 0, sizeof(cmdseq.props[cmdseq.num]));
	tvp->cmd = cmd;
	auto pls_start = make_code((int)pls_search_range.pls_mode, pls_search_range.start, pls_search_range.timeoutms);
	auto pls_end = make_code((int)pls_search_range.pls_mode, pls_search_range.end, pls_search_range.timeoutms);
	memcpy(&tvp->u.buffer.data[0 * sizeof(uint32_t)], &pls_start, sizeof(pls_start));
	memcpy(&tvp->u.buffer.data[1 * sizeof(uint32_t)], &pls_end, sizeof(pls_end));
	tvp->u.buffer.len = 2 * sizeof(uint32_t);
	cmdseq.num++;
};

int cmdseq_t::tune(int fefd, int heartbeat_interval) {
	add(DTV_TUNE, 0);
	if (heartbeat_interval > 0)
		add(DTV_HEARTBEAT, heartbeat_interval);
	if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
		dterror("FE_SET_PROPERTY failed: " << strerror(errno));
		return -1;
	}
	return 0;
}

int cmdseq_t::scan(int fefd, bool init) {
	add(DTV_SCAN, init);
	if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
		dterrorx("FE_SET_PROPERTY failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int cmdseq_t::spectrum(int fefd, dtv_fe_spectrum_method method) {
	add(DTV_SPECTRUM, method);
	if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
		dterrorx("FE_SET_PROPERTY failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int dvb_frontend_t::open_device(thread_safe_t& t, bool rw, bool allow_failure) {
	if (t.fefd >= 0)
		return 0; // already open

	try {
		api_type = get_api_type();

	} catch(...) {
		return -1;
	}

	ss::string<PATH_MAX> frontend_fname;
	frontend_fname.sprintf("/dev/dvb/adapter%d/frontend%d", adapter->adapter_no, frontend_no);
	int rw_flag = rw ? O_RDWR : O_RDONLY;
	t.fefd = open(frontend_fname.c_str(), rw_flag | O_NONBLOCK | O_CLOEXEC);
	if (t.fefd < 0) {
		user_errorx("Error opening /dev/dvb/adapter%d/frontend%d in %s mode: %s", (int)adapter->adapter_no,
								(int)frontend_no, rw ? "read-write" : "readonly", strerror(errno));
		return -1;
	}

	return 0;
}

dvb_frontend_t::~dvb_frontend_t() {
	auto t = ts.writeAccess();
	if(t->fefd>=0) {
		api_type = api_type_t::UNDEFINED;
		dtdebugx("closing fefd=%d", t->fefd);
		while (::close(t->fefd) != 0) {
			if (errno != EINTR)
				dterrorx("Error closing /dev/dvb/adapter%d/frontend%d: %s", (int)adapter->adapter_no, (int)frontend_no,
								 strerror(errno));
		}
		t->fefd = -1;
	}
}

void dvb_frontend_t::close_device(thread_safe_t& t) {
	if (t.fefd < 0)
		return;
	api_type = api_type_t::UNDEFINED;
	dtdebugx("closing fefd=%d", t.fefd);
	while (::close(t.fefd) != 0) {
		if (errno != EINTR)
			dterrorx("Error closing /dev/dvb/adapter%d/frontend%d: %s", (int)adapter->adapter_no, (int)frontend_no,
							 strerror(errno));
	}
	t.fefd = -1;
}

/*
	return -1 on error, 1 on change, 0 on no change
*/
static int get_frontend_names_dvapi(const adapter_no_t adapter_no, dvb_frontend_t::thread_safe_t& t) {
	struct dvb_frontend_info fe_info {}; // front_end_info
	if (ioctl(t.fefd, FE_GET_INFO, &fe_info) < 0) {
		dterrorx("FE_GET_FRONTEND_INFO FAILED: %s", strerror(errno));
		return -1;
	}
	t.dbfe.adapter_name.clear();
	t.dbfe.card_name.clear();
	t.dbfe.card_name.sprintf(fe_info.name, strlen(fe_info.name));
	t.dbfe.adapter_name.sprintf("adapter%d", (int)adapter_no);
	// todo: res should also take into account changes below
	// todo: add caps
	t.dbfe.frequency_min = fe_info.frequency_min;
	t.dbfe.frequency_max = fe_info.frequency_max;
	t.dbfe.symbol_rate_min = fe_info.symbol_rate_min;
	t.dbfe.symbol_rate_max = fe_info.symbol_rate_max;
	t.dbfe.supports.multistream = fe_info.caps & FE_CAN_MULTISTREAM;
	t.dbfe.supports.iq = false;
	t.dbfe.supports.blindscan = false;
	t.dbfe.supports.spectrum = false;
	return 0;
}

//	return -1 on error, 1 on change, 0 on no change
static int get_frontend_names(dvb_frontend_t::thread_safe_t& t, int adapter_no) {
	struct dvb_frontend_extended_info fe_info {}; // front_end_info

	if (ioctl(t.fefd, FE_GET_EXTENDED_INFO, &fe_info) < 0) {
		dterrorx("FE_GET_FRONTEND_INFO FAILED: %s", strerror(errno));
		return -1;
	}

	t.dbfe.adapter_name.clear();
	t.dbfe.card_name.clear();
	auto* card_name = fe_info.card_name[0] == 0 ? fe_info.name : fe_info.card_name;

	t.dbfe.card_name.sprintf(card_name, strlen(card_name));

	if (fe_info.adapter_name[0] == 0) {
		// old style
		ss::string<256> adapter_name;
		adapter_name.sprintf("%s #%d", fe_info.name, adapter_no);
		t.dbfe.adapter_name.sprintf(adapter_name.c_str(), strlen(fe_info.adapter_name));

		// fake but unique. Each adapter is consdered to be on a separate card
		ss::string<256> card_address;
		ss::string_& adapter_address = card_address;
		card_address.sprintf("adapter%d", adapter_no);

		t.dbfe.card_address.sprintf(card_address.c_str(), strlen(card_address.c_str()));

		t.dbfe.adapter_address.sprintf(adapter_address.c_str(), strlen(adapter_address.c_str()));

	} else {
		t.dbfe.adapter_name.sprintf(fe_info.adapter_name, strlen(fe_info.adapter_name));
		t.dbfe.card_address.sprintf(fe_info.card_address, strlen(fe_info.card_address));

		t.dbfe.adapter_address.sprintf(fe_info.adapter_address, strlen(fe_info.adapter_address));
	}

	// todo: add caps
	t.dbfe.frequency_min = fe_info.frequency_min;
	t.dbfe.frequency_max = fe_info.frequency_max;
	t.dbfe.symbol_rate_min = fe_info.symbol_rate_min;
	t.dbfe.symbol_rate_max = fe_info.symbol_rate_max;
	t.dbfe.supports.multistream = fe_info.caps & FE_CAN_MULTISTREAM;
	t.dbfe.supports.blindscan = fe_info.extended_caps & FE_CAN_BLINDSEARCH;
	t.dbfe.supports.spectrum = fe_info.extended_caps & FE_CAN_SPECTRUMSCAN;
	t.dbfe.supports.iq = fe_info.extended_caps & FE_CAN_IQ;
	return 0;
}

int dvb_frontend_t::get_frontend_info(const adapter_no_t adapter_no, const frontend_no_t frontend_no,
																			dvb_frontend_t::thread_safe_t& t) {

	int ret = get_frontend_names(t, (int)adapter_no);
	if (ret < 0)
		ret = get_frontend_names_dvapi(adapter_no, t);
	if (ret < 0)
		return ret;
	struct dtv_property properties[16];
	memset(properties, 0, sizeof(properties));
	unsigned int i = 0;
	properties[i++].cmd = DTV_ENUM_DELSYS;
	properties[i++].cmd = DTV_DELIVERY_SYSTEM;
	struct dtv_properties props = {.num = i, .props = properties};

	if ((ioctl(t.fefd, FE_GET_PROPERTY, &props)) == -1) {
		dterror("FE_GET_PROPERTY failed: " << strerror(errno));
		return -1;
	}

	auto& supported_delsys = properties[0].u.buffer.data;
	int num_delsys = properties[0].u.buffer.len;

	t.dbfe.delsys.resize(num_delsys);
	for (int i = 0; i < num_delsys; ++i) {
		auto delsys = (chdb::fe_delsys_t)supported_delsys[i];
		// auto fe_type = chdb::delsys_to_type (delsys);
		auto* s = enum_to_str(delsys);
		dtdebug("delsys[" << i << "]=" << s);
		t.dbfe.delsys[i] = delsys;
	}

	return 0;
}

std::shared_ptr<dvb_frontend_t> dvb_frontend_t::make(dvb_adapter_t* adapter, frontend_no_t frontend_no) {
	auto fe = std::make_shared<dvb_frontend_t>(adapter, frontend_no);

	auto t = fe->ts.writeAccess();
	// first try writeable access, then readonly
	if (fe->open_device(*t, true, true) < 0) {
		t->can_be_used = false;
		adapter->can_be_used = false;
		t->info_valid = (fe->open_device(*t, false, true) == 0);
		dtdebugx("/dev/dvb/adapter%d/frontend%d currently not useable", (int)fe->adapter->adapter_no, (int)fe->frontend_no);
	} else {
		t->info_valid = true;
		t->can_be_used = true;
		adapter->can_be_used = true;
		fe->get_frontend_info(adapter->adapter_no, frontend_no, *t);
		fe->close_device(*t);
	}
	return fe;
}

//returns the tuned frequency, compensated for lnb offset
static int get_dvbs_mux_info(chdb::dvbs_mux_t& mux, struct dtv_properties& cmdseq, const chdb::lnb_t& lnb, int& i,
														 int band, chdb::fe_polarisation_t pol) {

	mux.delivery_system = (chdb::fe_delsys_dvbs_t)cmdseq.props[i++].u.data;
	int voltage = cmdseq.props[i++].u.data;
	bool tone_on = cmdseq.props[i++].u.data == SEC_TONE_ON;
	if (tone_on != (band == 1)) {
		// dtdebugx("driver does not return proper tone setting");
		tone_on = band;
	}
	int freq = cmdseq.props[i++].u.data;

	mux.frequency = chdb::lnb::freq_for_driver_freq(lnb, freq, tone_on); // always in kHz
	mux.pol =  chdb::lnb::pol_for_voltage(lnb, voltage);


	if (mux.pol != pol) {
		// dtdebugx("driver does not return proper voltage setting");
		mux.pol = pol;
	}

	mux.symbol_rate = cmdseq.props[i++].u.data; // in Hz

	mux.modulation = (chdb::fe_modulation_t)cmdseq.props[i++].u.data;
	mux.fec = (chdb::fe_code_rate_t)cmdseq.props[i++].u.data;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.props[i++].u.data;
	mux.rolloff = (chdb::fe_rolloff_t)cmdseq.props[i++].u.data;
	mux.pilot = (chdb::fe_pilot_t)cmdseq.props[i++].u.data;
	auto stream_id_prop = cmdseq.props[i++].u.data;
	mux.stream_id = (stream_id_prop & 0xff) == 0xff ? -1 : (stream_id_prop & 0xff);
	mux.pls_mode = chdb::fe_pls_mode_t((stream_id_prop >> 26) & 0x3);
	mux.pls_code = (stream_id_prop >> 8) & 0x3FFFF;

	i += 6; // skip dvbt
	return mux.frequency;
}

//returns the tuned frequency
static int get_dvbc_mux_info(chdb::dvbc_mux_t& mux, dtv_properties& cmdseq, int& i) {

	mux.delivery_system = (chdb::fe_delsys_dvbc_t)cmdseq.props[i++].u.data;
	i++; // int voltage
	i++; // int tone

	int freq = cmdseq.props[i++].u.data;

	mux.frequency = freq / 1000; // freq in Hz; mux.frequency in kHz

	mux.symbol_rate = cmdseq.props[i++].u.data; // in Hz

	mux.modulation = (chdb::fe_modulation_t)cmdseq.props[i++].u.data;
	mux.fec_inner = (chdb::fe_code_rate_t)cmdseq.props[i++].u.data;
	mux.fec_outer = chdb::fe_code_rate_t::FEC_AUTO;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.props[i++].u.data;
	i++; // mux.rolloff
	i++; // mux.pilot

	mux.stream_id = cmdseq.props[i++].u.data;
	// int dtv_scrambling_sequence_index_prop = cmdseq.props[i++].u.data;

	i += 6; // skip dvbt
	return mux.frequency;
}

//returns the tuned frequency
static int get_dvbt_mux_info(chdb::dvbt_mux_t& mux, dtv_properties& cmdseq, int& i) {

	mux.delivery_system = (chdb::fe_delsys_dvbt_t)cmdseq.props[i++].u.data;
	i++; // int voltage
	i++; // int tone

	int freq = cmdseq.props[i++].u.data;

	mux.frequency = freq; // in Hz

	i++; // mux.symbol_rate

	mux.modulation = (chdb::fe_modulation_t)cmdseq.props[i++].u.data;
	i++; // mux.fec_inner;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.props[i++].u.data;
	i++; // mux.rolloff
	i++; // mux.pilot

	mux.stream_id = cmdseq.props[i++].u.data;
	// int dtv_scrambling_sequence_index_prop = cmdseq.props[i++].u.data;

	i++; // mux.fec_outer

	mux.bandwidth = (chdb::fe_bandwidth_t)cmdseq.props[i++].u.data;
	mux.transmission_mode = (chdb::fe_transmit_mode_t)cmdseq.props[i++].u.data;
	mux.HP_code_rate = (chdb::fe_code_rate_t)cmdseq.props[i++].u.data;
	mux.LP_code_rate = (chdb::fe_code_rate_t)cmdseq.props[i++].u.data;
	mux.hierarchy = (chdb::fe_hierarchy_t)cmdseq.props[i++].u.data;
	mux.guard_interval = (chdb::fe_guard_interval_t)cmdseq.props[i++].u.data;
	return mux.frequency;
}

void dvb_frontend_t::get_mux_info(chdb::signal_info_t& ret, struct dtv_properties& cmdseq, api_type_t api, int& i) {
	using namespace chdb;
	const auto& r = this->adapter->reservation.readAccess();
	const auto* dvbs_mux = std::get_if<dvbs_mux_t>(&r->reserved_mux);
	ret.tune_confirmation = r->tune_confirmation;
	ret.si_mux = r->reserved_mux;

	ret.mux = r->reserved_mux; //ensures that we return proper any_mux_t type for dvbc and dvbt
	ret.stat.k.mux = *mux_key_ptr(r->reserved_mux);
	if (ret.tune_confirmation.si_done) {
		dtdebugx("reporting si_done=true");
	}
	*mux_key_ptr(ret.mux) = ret.stat.k.mux;
	const auto& lnb = r->reserved_lnb;
	int band = 0;
	chdb::fe_polarisation_t pol{chdb::fe_polarisation_t::UNKNOWN};
	if (dvbs_mux) {
		ret.stat.k.frequency = dvbs_mux->frequency;
		ret.stat.k.pol = dvbs_mux->pol;
		ret.stat.k.lnb = lnb.k;
		ret.stat.k.pol = dvbs_mux->pol;
		auto [band_, pol_, freq] = chdb::lnb::band_voltage_freq_for_mux(lnb, *dvbs_mux);
		band = band_;
		pol = dvbs_mux->pol;
		if(band < r->reserved_lnb.lof_offsets.size())
			ret.lnb_lof_offset = r->reserved_lnb.lof_offsets[band];
		else
			ret.lnb_lof_offset.reset();
	}

	//the following must be called even when not lockes, to consume the results of all DTV_... commands
	visit_variant(
		ret.mux,
		[&cmdseq, &i, &lnb, &ret, band, pol](chdb::dvbs_mux_t& mux) {
			ret.stat.k.frequency = get_dvbs_mux_info(mux, cmdseq, lnb, i, band, pol);
				ret.stat.symbol_rate = mux.symbol_rate;
		},
		[&cmdseq, &i, &ret](chdb::dvbc_mux_t& mux) {
			ret.stat.k.frequency = get_dvbc_mux_info(mux, cmdseq, i); },
		[&cmdseq, &i, &ret](chdb::dvbt_mux_t& mux) {
			ret.stat.k.frequency = get_dvbt_mux_info(mux, cmdseq, i); });

	/*at this point ret.mux and ret.stat.frequency contain the frequency as reported from the tuner itself,
		but after compensation for the currently known lnb offset

		dvbs_mux->frequency is the frequency which we were asked to tune
			*/
	tuned_frequency = ret.stat.k.frequency;
	if (api == api_type_t::NEUMO) {
		ret.matype = cmdseq.props[i++].u.data;
		auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&ret.mux);

		if (dvbs_mux) {
			if(dvbs_mux->delivery_system == fe_delsys_dvbs_t::SYS_DVBS) {
				dvbs_mux->matype = 256;
				ret.matype =  256; //means dvbs
				dvbs_mux->stream_id = -1;
			} else {
				dvbs_mux->matype = ret.matype;
#if 0 //seems to go wrong on 25.5W 11174V: multistream
				bool is_mis = !(ret.matype & (1 << 5));
				if (!is_mis)
					dvbs_mux->stream_id = -1;
#endif
			}
		}
		assert(cmdseq.props[i].u.buffer.len == 32);
		uint32_t* isi_bitset =
			(uint32_t*)cmdseq.props[i++].u.buffer.data; // TODO: we can only return 32 out of 256 entries...
		for (int i = 0; i < 256; ++i) {
			int j = i / 32;
			uint32_t mask = ((uint32_t)1) << (i % 32);
			if (isi_bitset[j] & mask) {
				ret.isi_list.push_back(i);
			}
		}
	} else {
#if 0
		ret.stat.ber = 0;
		ret.stat.snr = 0;
#endif
	}
}

/*
	lnb is needed to identify dish on which signal is captured
	mux_ is needed to translate tuner frequency to real frequency
*/
void dvb_frontend_t::get_signal_info(chdb::signal_info_t& ret, bool get_constellation) {
	ret.lock_status = ts.readAccess()->lock_status.fe_status;
	using namespace chdb;
	// bool is_sat = true;
	ret.stat.k.time = system_clock_t::to_time_t(now);
	struct dtv_property p[] = {
		{.cmd = DTV_STAT_SIGNAL_STRENGTH},
		{.cmd = DTV_STAT_CNR},

		{.cmd = DTV_STAT_PRE_ERROR_BIT_COUNT},
		{.cmd = DTV_STAT_PRE_TOTAL_BIT_COUNT},
#if 0
		{.cmd = DTV_STAT_POST_ERROR_BIT_COUNT	},
		{.cmd = DTV_STAT_POST_TOTAL_BIT_COUNT	},
		{.cmd = DTV_STAT_ERROR_BLOCK_COUNT	},
		{.cmd = DTV_STAT_TOTAL_BLOCK_COUNT	},
#endif

		{.cmd = DTV_DELIVERY_SYSTEM}, // 0 DVB-S, 9 DVB-S2
		{.cmd = DTV_VOLTAGE},					// 0 - 13V Vertical, 1 - 18V Horizontal, 2 - Voltage OFF
		{.cmd = DTV_TONE},
		{.cmd = DTV_FREQUENCY},
		{.cmd = DTV_SYMBOL_RATE},

		{.cmd = DTV_MODULATION}, // 5 - QPSK, 6 - 8PSK
		{.cmd = DTV_INNER_FEC},
		{.cmd = DTV_INVERSION},
		{.cmd = DTV_ROLLOFF},
		{.cmd = DTV_PILOT}, // 0 - ON, 1 - OFF
		{.cmd = DTV_STREAM_ID},

		//	{ .cmd = DTV_SCRAMBLING_SEQUENCE_INDEX },
		{.cmd = DTV_BANDWIDTH_HZ},			// DVB-T
		{.cmd = DTV_TRANSMISSION_MODE}, // DVB-
		{.cmd = DTV_CODE_RATE_HP},			// DVB-T
		{.cmd = DTV_CODE_RATE_LP},			// DVB-T
		{.cmd = DTV_HIERARCHY},					// DVB-T
		{.cmd = DTV_GUARD_INTERVAL},		// DVB-T
		// The following are only supported by neumo version of dvbapi
		{.cmd = DTV_MATYPE},
		{.cmd = DTV_ISI_LIST},
		{.cmd = DTV_CONSTELLATION},
	};

	struct dtv_properties cmdseq = {.num = sizeof(p) / sizeof(p[0]), .props = p};
	if (api_type != api_type_t::NEUMO)
		cmdseq.num -= 3;
	else if (!get_constellation) {
		cmdseq.num -= 1;
	} else {
		auto& cs = cmdseq.props[cmdseq.num - 1].u.constellation;
		ret.constellation_samples.resize(num_constellation_samples);
		cs.num_samples = num_constellation_samples;
		cs.samples = &ret.constellation_samples[0];
	}

	auto fefd = ts.readAccess()->fefd;
	if (ioctl(fefd, FE_GET_PROPERTY, &cmdseq) < 0) {
		dterrorx("ioctl failed: %s\n", strerror(errno));
		// @todo: handle EINTR
		return;
	}
	if (get_constellation && api_type == api_type_t::NEUMO) {
		auto& cs = cmdseq.props[cmdseq.num - 1].u.constellation;
		assert(cs.num_samples >= 0);
		assert((int)cs.num_samples <= ret.constellation_samples.size());
		assert(cs.num_samples <= 4096);
		ret.constellation_samples.resize_no_init(cs.num_samples); // we may have retrieved fewer samples than we asked
	}

	int i = 0;
	statdb::signal_stat_entry_t& last_stat = ret.last_stat();
	auto& signal_strength_stats = cmdseq.props[i++].u.st;
	for (int i = 0; i < signal_strength_stats.len; ++i) {
		auto& signal_strength = signal_strength_stats.stat[i];
		if (signal_strength.scale != FE_SCALE_DECIBEL) {
			dterrorx("Unexpected: stats[%d] is not in dB", i); // this actually happens sometimes =>driver bug
			continue;
		}
		last_stat.signal_strength =
			signal_strength.scale == FE_SCALE_DECIBEL
			? signal_strength.svalue
			: (signed)(1000 * (signal_strength.uvalue - 1000)); // make an attempt at scaling to dB (aribitrary)
		if (false && signal_strength_stats.len > 1)
			dtdebugx("Extra statistics ignored (%d available)", signal_strength_stats.len);
		break;
	}

	auto& snr_stats = cmdseq.props[i++].u.st;

	if (snr_stats.len > 0) {
		auto& snr = snr_stats.stat[0];
		last_stat.snr = (snr.scale == FE_SCALE_DECIBEL)
			? snr.svalue :
			last_stat.snr = (snr.scale == FE_SCALE_RELATIVE)
			? (int32_t)(1000 * (snr.uvalue - 1000)) // make an attempt at scaling to dB (aribitrary)
			: -1e6;
		if (false && snr_stats.len > 1)
			dtdebugx("Extra statistics ignored (%d available)", snr_stats.len);
	}

	uint64_t ber_enum = cmdseq.props[i++].u.st.stat[0].uvalue;
	uint64_t ber_denom = cmdseq.props[i++].u.st.stat[0].uvalue;
	if(ber_denom >0) {
		last_stat.ber = ber_enum / (double)ber_denom;
	} else {
		last_stat.ber = ber_enum; //hack
	}

	get_mux_info(ret, cmdseq, api_type, i);
	if (get_constellation) {
		if (ret.lock_status & FE_HAS_LOCK) {
			if (api_type == api_type_t::NEUMO)
				i++; // for constellation samples
			assert(i == (int)cmdseq.num);
		} else {
			//ret.constellation_samples.resize(0);
		}
	}
}

int dvb_frontend_t::clear() {
	auto fefd = ts.readAccess()->fefd;
	if ((ioctl(fefd, DTV_STOP)) == -1) {
		dtdebug("DTV STOP failed: " << strerror(errno));

		struct dtv_property pclear[] = {
			{
				.cmd = DTV_CLEAR,
			}, // RESET frontend's cached data

		};
		struct dtv_properties cmdclear = {.num = 1, .props = pclear};

		auto fefd = ts.readAccess()->fefd;
		if ((ioctl(fefd, FE_SET_PROPERTY, &cmdclear)) == -1) {
			dterror("FE_SET_PROPERTY clear failed: " << strerror(errno));
			// set_interrupted(ERROR_TUNE<<8);
			return -1;
		}
	}

	return 0;
}

/*
	returns two fields indicating the current lock status,
	and the lock history: 0= never locked, 1 = currently not locked, 2 = currently locked
	but we lost lock at least once since the  ast call
*/
dvb_frontend_t::lock_status_t dvb_frontend_t::get_lock_status() {

	auto& t = *ts.writeAccess();
	auto ret = t.lock_status;
	t.lock_status.lock_lost = false;
	return ret;
}

void dvb_frontend_t::set_lock_status(fe_status_t fe_status) {
	bool locked_now = fe_status & FE_HAS_LOCK;
	auto& t = *ts.writeAccess();
	// if(fe_status & FE_TIMEDOUT)
	bool locked_before = t.lock_status.fe_status & FE_HAS_LOCK;
	if (locked_before && !locked_now)
		t.lock_status.lock_lost = true;
	t.lock_status.fe_status = fe_status;
}

static int sat_pos_to_angle(int angle, int my_longitude, int my_latitude) {
	double g8, g9, g10;
	double g14, g15, g16, g17, g18, g19, g20, g21, g22, g23, g24, g25;
	double g26, g27, g28, g29, g30, g31, g32, g33, g34, g35, g36, g37;
	int posi_angle = 0;
	int tmp;
	char z_conversion[10] = {0x00, 0x02, 0x03, 0x05, 0x06, 0x08, 0x0a, 0x0b, 0x0d, 0x0e};

	g8 = ((double)angle) / 10.;
	g9 = ((double)my_longitude) / 10.;
	g10 = ((double)my_latitude) / 10.;

	g14 = (fabs(g8) > 180.) ? 1000. : g8;
	g15 = (fabs(g9) > 180.) ? 1000. : g9;
	g16 = (fabs(g10) > 90.) ? 1000. : g10;

	if (g14 > 999. || g15 > 999. || g16 > 999.) {
		g17 = 0.;
		g18 = 0.;
		g19 = 0.;
	} else {
		g17 = g14;
		g18 = g15;
		g19 = g16;
	}

	g20 = fabs(g17 - g18);
	g21 = g20 > 180. ? -(360. - g20) : g20;
	if (fabs(g21) > 80.) {
		g22 = 0;
	} else
		g22 = g21;

	g23 = 6378. * sin(M_PI * g19 / 180.);
	g24 = sqrt(40678884. - g23 * g23);
	g25 = g24 * sin(M_PI * g22 / 180.);
	g26 = sqrt(g23 * g23 + g25 * g25);
	g27 = sqrt(40678884. - g26 * g26);
	g28 = 42164.2 - g27;
	g29 = sqrt(g28 * g28 + g26 * g26);
	g30 = sqrt((42164.2 - g24) * (42164.2 - g24) + g23 * g23);
	g31 = sqrt(3555639523. - 3555639523. * cos(M_PI * g22 / 180.));
	g32 = acos((g29 * g29 + g30 * g30 - g31 * g31) / (2 * g29 * g30));
	g33 = g32 * 180. / M_PI;
	if (fabs(g33) > 80.) {
		g34 = 0.;
	} else
		g34 = g33;

	g35 = ((g17 < g18 && g19 > 0.) || (g17 > g18 && g19 < 0.)) ? g34 : -g34;
	g36 = (g17 < -89.9 && g18 > 89.9) ? -g35 : g35;
	g37 = (g17 > 89.9 && g18 < -89.9) ? -g35 : g36;

	if (g37 > 0.) {
		tmp = (int)((g37 + 0.05) * 10); //+0.05 means: round up
		posi_angle |= 0xD000;
	} else {
		tmp = (int)((g37 - 0.05) * 10); //-0.05 means: round down
		posi_angle |= 0xE000;
		tmp = -tmp;
	}

	posi_angle |= (tmp / 10) << 4;
	posi_angle |= z_conversion[tmp % 10]; // computes decimal fraction of angle in 1/16 of a degree
	return posi_angle;
}

/** @brief generate and diseqc message for a committed or uncommitted switch
 * specification is available from http://www.eutelsat.com/
 * @param extra: extra bits to set polarisation and band; not sure if this does anything useful
 */
int dvb_frontend_t::send_diseqc_message(char switch_type, unsigned char port, unsigned char extra, bool repeated) {
	struct dvb_diseqc_master_cmd cmd {};
	// Framing byte : Command from master, no reply required, first transmission : 0xe0
	cmd.msg[0] = repeated ? 0xe1 : 0xe0;
	// Address byte:
	// 0x10: Any LNB, switcher or SMATV
	// 0x30: any positioner
	// 0x31: azimuth positioner
	cmd.msg[1] = 0x10; // Address byte : Any LNB, switcher or SMATV
	// Command byte : Write to port group 1 (Uncommited switches)
	// Command byte : Write to port group 0 (Committed switches) 0x38
	if (switch_type == 'U')
		cmd.msg[2] = 0x39;
	else if (switch_type == 'C')
		cmd.msg[2] = 0x38;
	else if (switch_type == 'X') {
		cmd.msg[2] = 0x6B; // positioner goto
		return 0;
	}
	/* param: high nibble: reset bits, low nibble set bits,
	 * bits are: option, position, polarisation, band */
	cmd.msg[3] = 0xf0 | (port & 0x0f) | extra;

	//
	cmd.msg[4] = 0x00;
	cmd.msg[5] = 0x00;
	cmd.msg_len = 4;
	ss::string<64> s;
	s.sprintf("Diseqc message[%d]: ", cmd.msg_len);
	s.sprintf("%02x %02x %02x %02x %02x %02x", cmd.msg[0], cmd.msg[1], cmd.msg[2], cmd.msg[3], cmd.msg[4], cmd.msg[5]);
	dtdebug(s.c_str());

	int err;
	auto fefd = ts.readAccess()->fefd;
	if ((err = ioctl(fefd, FE_DISEQC_SEND_MASTER_CMD, &cmd))) {
		dterror("problem sending the DiseqC message\n");
		return -1;
	}
	return 0;
}

int dvb_frontend_t::send_positioner_message(chdb::positioner_cmd_t command, int32_t par, bool repeated) {
	using namespace chdb;
	struct dvb_diseqc_master_cmd cmd {};
	// Framing byte : Command from master, no reply required, first transmission : 0xe0
	cmd.msg[0] = repeated ? 0xe1 : 0xe0;
	// Address byte:
	// 0x10: Any LNB, switcher or SMATV
	// 0x30: any positioner
	// 0x31: azimuth positioner
	cmd.msg[1] = 0x31;
	// set some defaults
	cmd.msg[2] = (uint8_t)command;
	cmd.msg_len = 4;
	switch (command) {
	case positioner_cmd_t::RECALCULATE_POSITIONS:
		assert(0);
		break;
	case positioner_cmd_t::LIMIT_EAST:
	case positioner_cmd_t::LIMIT_WEST:
	case positioner_cmd_t::RESET:
	case positioner_cmd_t::HALT:
	case positioner_cmd_t::LIMITS_OFF:
		cmd.msg_len = 3;
		break;
	case positioner_cmd_t::NUDGE_EAST:
	case positioner_cmd_t::NUDGE_WEST:
		par = 0; //fall through
	case positioner_cmd_t::DRIVE_EAST:
	case positioner_cmd_t::DRIVE_WEST:
		cmd.msg[1] = 0x31;
		if (par > 0)
			par = -par; // step mode
		cmd.msg[3] = par;
		break;
	case positioner_cmd_t::STORE_NN:
	case positioner_cmd_t::GOTO_NN:
		cmd.msg[3] = par;
		break;
	case positioner_cmd_t::GOTO_REF:
		cmd.msg[2] = (uint8_t)positioner_cmd_t::GOTO_NN;
		cmd.msg[3] = 0;
		cmd.msg_len = 4;
		break;
	case positioner_cmd_t::LIMITS_ON:
		cmd.msg[2] = (uint8_t)positioner_cmd_t::STORE_NN;
		cmd.msg[3] = 0;
		cmd.msg_len = 4;
		assert (cmd.msg[3]==0);
		break;
	case positioner_cmd_t::GOTO_XX: {
		auto loc = adapter->get_usals_location();

		auto angle = sat_pos_to_angle(par / 10, loc.usals_longitude / 10, loc.usals_lattitude / 10);

		cmd.msg[3] = (angle >> 8) & 0xff;
		cmd.msg[4] = angle & 0xff;
		cmd.msg_len = 5;
	} break;
	}
	ss::string<64> s;
	s.sprintf("Diseqc message[%d]: ", cmd.msg_len);
	s.sprintf("%02x %02x %02x %02x %02x %02x", cmd.msg[0], cmd.msg[1], cmd.msg[2], cmd.msg[3], cmd.msg[4], cmd.msg[5]);
	dtdebug(s.c_str());

	int err;
	auto fefd = ts.readAccess()->fefd;
	if ((err = ioctl(fefd, FE_DISEQC_SEND_MASTER_CMD, &cmd))) {
		dterror("problem sending the DiseqC message\n");
		return -1;
	}
	return 0;
}


void dvb_frontend_t::set_lnb_lof_offset(const chdb::dvbs_mux_t& dvbs_mux, chdb::lnb_t& lnb) {

	auto [band, voltage_, freq_] = chdb::lnb::band_voltage_freq_for_mux(lnb, dvbs_mux);
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

void dvb_frontend_t::update_tuned_mux_nit(const chdb::any_mux_t& mux) {
	auto w = adapter->reservation.writeAccess();
	w->reserved_mux = mux;
}

void dvb_frontend_t::update_tuned_mux_tune_confirmation(const tune_confirmation_t& tune_confirmation) {
	auto w = adapter->reservation.writeAccess();
	if(!w->tune_confirmation.nit_actual_ok && tune_confirmation.nit_actual_ok) {
		const auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&w->reserved_mux);
		if(dvbs_mux) {
			using namespace chdb;
			//ss::vector<int32_t, 2> lof_offsets;
			auto& lnb = w->reserved_lnb;
			set_lnb_lof_offset(*dvbs_mux, lnb);
			auto lof_offsets = lnb.lof_offsets;
			auto m = get_monitor_thread();
			if(m) {
				auto& tuner_thread = m->receiver.tuner_thread;
				int fefd = ts.readAccess()->fefd;
				if(dvbs_mux->c.tune_src == tune_src_t::NIT_ACTUAL_TUNED) {
					dtdebug("Updating LNB LOF offset");
					tuner_thread.push_task([&tuner_thread, fefd, lof_offsets = std::move(lof_offsets)]() {
						cb(tuner_thread).on_lnb_lof_offset_update(fefd, lof_offsets);
						return 0;
					});
				}
			}
		}
	}
	w->tune_confirmation = tune_confirmation;
}

template <typename mux_t> bool dvb_frontend_t::is_tuned_to(const mux_t& mux, const chdb::lnb_t* required_lnb) const {
	if (!adapter)
		return false;
	return adapter->reservation.readAccess()->is_tuned_to(mux, required_lnb);
}

api_type_t dvb_frontend_t::get_api_type() {
	static std::mutex m;
	static api_type_t cached = api_type_t::UNDEFINED;
	/*
		Note: multiple threads could simultaneously TRY to initialize the value, but only
		one will succeed.
	*/
	if (cached == api_type_t::UNDEFINED) {
		std::scoped_lock lck(m);
		if (cached != api_type_t::UNDEFINED) {
			// this could happen  if another thread beat us to it
			return cached;
		}
		using namespace libconfig;
		Config cfg;
		try {
			cfg.readFile("/sys/module/dvb_core/info/version");
		} catch (const FileIOException& fioex) {
			cached = api_type_t::DVBAPI; // default
			return cached;
		} catch (const ParseException& pex) {
			cached = api_type_t::DVBAPI; // default
			return cached;
		}

		try {
			std::string type = cfg.lookup("type");
			if (strcmp(type.c_str(), "neumo") != 0) {
				cached = api_type_t::DVBAPI; // default
				return cached;
			}
		} catch (const SettingNotFoundException& nfex) {
			cached = api_type_t::DVBAPI; // default
			return cached;
		}

		try {
			std::string version = cfg.lookup("version");
			dtdebugx("Neumo dvbapi detected; version=%s", version.c_str());
			cached = api_type_t::NEUMO; // default
			return cached;

		} catch (const SettingNotFoundException& nfex) {
			cached = api_type_t::DVBAPI; // default
			return cached;
		}
	}
	return cached;
}

inline void spectrum_scan_t::resize(int num_freq, int num_peaks) {
	assert(num_freq <= max_num_freq);
	assert(num_peaks <= max_num_peaks);
	freq.resize_no_init(num_freq);
	rf_level.resize_no_init(num_freq);
	peaks.resize_no_init(num_peaks);
}

inline void spectrum_scan_t::adjust_frequencies(const chdb::lnb_t& lnb, int high_band) {
	for (auto& f : freq) {
		f = chdb::lnb::freq_for_driver_freq(lnb, f, high_band);
	}
	for (auto& p : peaks) {
		p.freq = chdb::lnb::freq_for_driver_freq(lnb, p.freq, high_band);
	}
}

std::optional<statdb::spectrum_t> dvb_frontend_t::get_spectrum(const ss::string_& spectrum_path) {
	this->num_constellation_samples = 0;
	auto ret = std::make_unique<spectrum_scan_t>();
	auto& scan = *ret;
	struct dtv_property p[] = {
		{.cmd = DTV_SPECTRUM}, // 0 DVB-S, 9 DVB-S2
		//		{ .cmd = DTV_BANDWIDTH_HZ },    // Not used for DVB-S
	};
	struct dtv_properties cmdseq = {.num = sizeof(p) / sizeof(p[0]), .props = p};

	int fefd = ts.readAccess()->fefd;

	decltype(cmdseq.props[0].u.spectrum) spectrum{};
	spectrum.num_freq = scan.max_num_freq;
	spectrum.freq = &scan.freq[0];
	spectrum.rf_level = &scan.rf_level[0];
	spectrum.candidates = &scan.peaks[0];
	spectrum.num_candidates = scan.max_num_peaks;
	cmdseq.props[0].u.spectrum = spectrum;

	if (ioctl(fefd, FE_GET_PROPERTY, &cmdseq) < 0) {
		dterrorx("ioctl failed: %s\n", strerror(errno));
		assert(0); // todo: handle EINTR
		return {};
	}
	spectrum = cmdseq.props[0].u.spectrum;

	if (spectrum.num_freq <= 0) {
		dterror("kernel returned spectrum with 0 samples\n");
		return {};
	}
	scan.resize(spectrum.num_freq, spectrum.num_candidates);
	if(spectrum.num_candidates == 0) {
		find_tps(scan.peaks, scan.rf_level, scan.freq);
	}

	bool append_now = false;
	bool incomplete = false;
	int min_freq_to_save{0};

	{
		const auto& r = this->adapter->reservation.readAccess();
		const auto& ts = this->ts.readAccess();
		auto& lnb = r->reserved_lnb;
		auto& options = ts->spectrum_scan_options;
		scan.start_time = options.start_time;
		scan.sat_pos = options.sat_pos;
		scan.lnb_key = lnb.k;
		auto [start_freq, mid_freq, end_freq] = chdb::lnb::band_frequencies(lnb, options.band_pol.band);
		scan.band_pol = options.band_pol;
		scan.start_freq = options.start_freq;
		scan.end_freq = options.end_freq;
		scan.resolution = options.resolution;
		scan.spectrum_method = options.spectrum_method;
		scan.adjust_frequencies(lnb, scan.band_pol.band == chdb::fe_band_t::HIGH);
		scan.usals_pos = lnb.usals_pos;
		auto* network = chdb::lnb::get_network(lnb, scan.sat_pos);
		scan.lof_offsets = lnb.lof_offsets;
		assert(!network || network->sat_pos == scan.sat_pos);
		append_now = options.append;
		// we will need to call start_lnb_spectrum again later to retrieve (part of) the high band)
		incomplete =
			(options.band_pol.band == chdb::fe_band_t::LOW && scan.end_freq > mid_freq && mid_freq < options.end_freq) ||
			(options.scan_both_polarisations &&
			 (options.band_pol.pol == chdb::fe_polarisation_t::H ||
				options.band_pol.pol == chdb::fe_polarisation_t::L));
	}
	{
		auto ts = this->ts.writeAccess();
		//frequency at which scan should resume (avoid frequency jumping down when starting high band)
		min_freq_to_save = ts->last_saved_freq;
		ts->last_saved_freq = scan.freq.size() ==0 ? 0 : scan.freq[scan.freq.size()-1];
	}


	if (incomplete) {
		auto options = this->ts.readAccess()->spectrum_scan_options; // make a copy
		auto lnb = this->adapter->reservation.readAccess()->reserved_lnb;
		if (options.band_pol.band == chdb::fe_band_t::HIGH) {
			// switch to the next polarisation
			assert(options.band_pol.pol == chdb::fe_polarisation_t::H ||
						 options.band_pol.pol == chdb::fe_polarisation_t::L);
			options.band_pol.band = chdb::fe_band_t::LOW;
			options.band_pol.pol = options.band_pol.pol == chdb::fe_polarisation_t::H ?
				chdb::fe_polarisation_t::V : chdb::fe_polarisation_t::R;
			options.append = false;
			dtdebugx("Continuing spectrum scan with pol=V band=low");
		} else {
			// switch to next band
			options.band_pol.band = chdb::fe_band_t::HIGH;
			options.append = true;
			dtdebugx("Continuing spectrum scan with pol=%s band=high",
							 enum_to_str(options.band_pol.pol));
		}
		start_lnb_spectrum_scan(lnb, options);
	}
	auto result = statdb::save_spectrum_scan(spectrum_path, scan, append_now, min_freq_to_save);
	result->is_complete = !incomplete;
	return result;
}

void cmdseq_t::init_pls_codes() {
	pls_codes = {
		// In use on 5.0W
		make_code(0, 16416),
		make_code(0, 8),
		make_code(1, 121212),
		make_code(1, 262140),
		make_code(1, 50416),
		// In use on 30.0W
		make_code(0, 185747),
		make_code(0, 133460),
		make_code(1, 174526),
		// In use on 33E
		make_code(0, 218997)
	};
}

int dvb_frontend_t::tune(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, const tune_options_t& tune_options) {
	auto blindscan = tune_options.use_blind_tune || mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_AUTO;
	//||(mux.symbol_rate < 1000000 &&  ts.readAccess()->dbfe.supports.blindscan);
	int num_constellation_samples = tune_options.constellation_options.num_samples;
	tuned_frequency = mux.frequency;

	cmdseq_t cmdseq;
	this->num_constellation_samples = num_constellation_samples;
	// auto lo_frequency = get_lo_frequency(mux.frequency);
	auto [band, voltage, frequency] = chdb::lnb::band_voltage_freq_for_mux(lnb, mux);
	int pol_is_v = 1 - voltage;
	cmdseq.add_clear();
	if (blindscan) {
		assert (api_type == api_type_t::NEUMO);
		cmdseq.add(DTV_ALGORITHM, ALGORITHM_BLIND);
		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)SYS_AUTO);
		if (mux.symbol_rate > 0)
			cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); //
		cmdseq.add(DTV_FREQUENCY, frequency);						// For satellite delivery systems, it is measured in kHz.

		// not needed?
		cmdseq.add(DTV_VOLTAGE, 1 - pol_is_v);
		cmdseq.init_pls_codes();
		if (mux.stream_id >= 0)
			cmdseq.pls_codes.push_back(make_code((int)mux.pls_mode, (int)mux.pls_code));
		cmdseq.add_pls_codes(DTV_PLS_SEARCH_LIST);
		cmdseq.add(DTV_STREAM_ID, mux.stream_id);
		cmdseq.add(DTV_SEARCH_RANGE, std::min(mux.symbol_rate*2, (unsigned int)4000000));
	} else {
		if (((int)mux.delivery_system != SYS_DVBS) && ((int)mux.delivery_system != SYS_DVBS2)) {
			dterror("illegal delivery system: " << chdb::to_str(mux.delivery_system));
			return -1;
		}

		cmdseq.add(DTV_ALGORITHM, ALGORITHM_COLD);

		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);
		cmdseq.add(DTV_MODULATION, (int)mux.modulation);

		// cmdseq.add(DTV_VOLTAGE,  1 - pol_is_v);
		cmdseq.add(DTV_FREQUENCY, frequency);					// For satellite delivery systems, it is measured in kHz.
		cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); // Must be in Symbols/second
		cmdseq.add(DTV_INNER_FEC, (int)mux.fec);
		cmdseq.add(DTV_INVERSION, INVERSION_AUTO);
		cmdseq.add(DTV_ROLLOFF, (int) mux.rolloff);

		cmdseq.add(DTV_PILOT, PILOT_AUTO);
#if 0
		auto stream_id =
			(mux.stream_id < 0 ? -1 : (make_code((int)mux.pls_mode, (int)mux.pls_code)) | (mux.stream_id & 0xff));
#else
		auto stream_id = make_code((int)mux.pls_mode, (int)mux.pls_code) | (mux.stream_id & 0xff);
#endif
		cmdseq.add(DTV_STREAM_ID, stream_id);
		cmdseq.add(DTV_SEARCH_RANGE, mux.symbol_rate);
	}
	dtv_fe_constellation constellation;
	if (tune_options.pls_search_range.start < tune_options.pls_search_range.end) {
		cmdseq.add_pls_range(DTV_PLS_SEARCH_RANGE, tune_options.pls_search_range);
	}

	auto& t = *ts.writeAccess();
	if( t.dbfe.supports.iq && num_constellation_samples > 0) {
		constellation.num_samples = num_constellation_samples;
		constellation.samples = nullptr;
		constellation.method = CONSTELLATION_METHOD_DEFAULT;
		constellation.constel_select = 1;
		cmdseq.add(DTV_CONSTELLATION, constellation);
	} else {
		constellation.num_samples = 0;
	}
	auto fefd = t.fefd;
	t.use_blind_tune = blindscan;
	t.tune_mode = tune_options.tune_mode;
	int heartbeat_interval = (api_type == api_type_t::NEUMO) ? 1000 : 0;
	auto ret = cmdseq.tune(fefd, heartbeat_interval);
	dtdebugx("tune_it returning ret=%d", ret);
	return ret;
}

int dvb_frontend_t::tune(const chdb::dvbc_mux_t& mux, const tune_options_t& tune_options) {
	this->num_constellation_samples = 0;
	cmdseq_t cmdseq;
	// any system
	tuned_frequency = mux.frequency;

	cmdseq.add(DTV_FREQUENCY, mux.frequency * 1000); // For DVB-C, it is measured in Hz.
	cmdseq.add(DTV_INVERSION, mux.inversion);
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);
	// dvbc
	cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); // in Symbols/second
	cmdseq.add(DTV_MODULATION, mux.modulation);
	cmdseq.add(DTV_INNER_FEC, mux.fec_inner);
	if (mux.stream_id >= 0)
		cmdseq.add(DTV_STREAM_ID, mux.stream_id);

	auto& t = *ts.writeAccess();
	auto fefd = t.fefd;
	t.tune_mode = tune_options.tune_mode;
	t.tune_mode = tune_mode_t::NORMAL;
	int heartbeat_interval = 0;
	return cmdseq.tune(fefd, heartbeat_interval);
}

int dvb_frontend_t::tune(const chdb::dvbt_mux_t& mux, const tune_options_t& tune_options) {
	this->num_constellation_samples = 0;
	cmdseq_t cmdseq;

	/** @todo here check the capabilities of the card*/
	int dvbt_bandwidth = 0;
	switch ((int)mux.bandwidth) {
	case BANDWIDTH_8_MHZ:
		dvbt_bandwidth = 8000000;
		break;
	case BANDWIDTH_7_MHZ:
		dvbt_bandwidth = 7000000;
		break;
	case BANDWIDTH_6_MHZ:
		dvbt_bandwidth = 6000000;
		break;
	case BANDWIDTH_AUTO:
	default:
		dvbt_bandwidth = 0;
		break;
	}

	if (((int)mux.delivery_system != SYS_DVBT) || ((int)mux.delivery_system != SYS_DVBT2)) {
		dterror("Unsupported delivery system");
		return -1;
	}
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);
	tuned_frequency = mux.frequency;

	cmdseq.add(DTV_FREQUENCY, mux.frequency * 1000); // For DVB-T, it is measured in Hz.
	cmdseq.add(DTV_BANDWIDTH_HZ, dvbt_bandwidth);
	cmdseq.add(DTV_CODE_RATE_HP, mux.HP_code_rate);
	cmdseq.add(DTV_CODE_RATE_LP, mux.LP_code_rate);
	cmdseq.add(DTV_MODULATION, mux.modulation);
	cmdseq.add(DTV_GUARD_INTERVAL, mux.guard_interval);
	cmdseq.add(DTV_TRANSMISSION_MODE, mux.transmission_mode);
	cmdseq.add(DTV_HIERARCHY, mux.hierarchy);

	if (mux.stream_id >= 0)
		cmdseq.add(DTV_STREAM_ID, mux.stream_id);

	auto& t = *ts.writeAccess();
	auto fefd = t.fefd;
	t.use_blind_tune = tune_options.use_blind_tune;
	t.tune_mode = tune_options.tune_mode;
	int heartbeat_interval = 0;
	return cmdseq.tune(fefd, heartbeat_interval);
}

int dvb_frontend_t::start_lnb_spectrum_scan(const chdb::lnb_t& lnb, spectrum_scan_options_t options) {
	this->num_constellation_samples = 0;
	using namespace chdb;
	auto lnb_voltage = (fe_sec_voltage_t) chdb::lnb::voltage_for_pol(lnb, options.band_pol.pol);

	fe_sec_tone_mode_t tone = (options.band_pol.band == fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;

	//dttime_init();
	if (this->clear() < 0) /*this call takes 500ms for the tas2101, probably because the driver's tuning loop \
													 is slow to react*/
		return -1;
	dtdebug("tune: clear done");

	// request spectrum scan
	cmdseq_t cmdseq;
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)SYS_DVBS);
	auto [start_freq, mid_freq, end_freq] = chdb::lnb::band_frequencies(lnb, options.band_pol.band);

	start_freq = std::max(options.start_freq, start_freq);
	end_freq = std::min(options.end_freq, end_freq);
	switch (options.band_pol.band) {
	case chdb::fe_band_t::LOW:
		end_freq = std::min(options.end_freq, mid_freq);
		break;
	case chdb::fe_band_t::HIGH:
		start_freq = std::max(options.start_freq, mid_freq);
		break;
	default:
		assert(0);
	}
	assert(start_freq <= end_freq);

	dtdebug("Spectrum acquisition on lnb  " << lnb << " diseqc: lnb_id=" << lnb << " " << lnb.tune_string << " range=["
					<< start_freq << ", " << end_freq << "]");

	start_freq = chdb::lnb::driver_freq_for_freq(lnb, start_freq);
	end_freq = chdb::lnb::driver_freq_for_freq(lnb, end_freq - 1) + 1;
	if (start_freq > end_freq)
		std::swap(start_freq, end_freq); //e.g., this occurs for C-band (spectrum inversion)
	cmdseq.add(DTV_SCAN_START_FREQUENCY, start_freq);
	cmdseq.add(DTV_SCAN_END_FREQUENCY, end_freq);

	cmdseq.add(DTV_SCAN_RESOLUTION, options.resolution); // in kHz
	cmdseq.add(DTV_SCAN_FFT_SIZE, options.fft_size);		 // in kHz
	cmdseq.add(DTV_SYMBOL_RATE, 2000 * 1000);						 // controls tuner bandwidth (in Hz)

	auto& t = *ts.writeAccess();
	auto fefd = t.fefd;

	if ((ioctl(fefd, FE_SET_VOLTAGE, lnb_voltage))) {
		dterror("problem Setting the Voltage\n");
		return -1;
	}

	if (ioctl(fefd, FE_SET_TONE, tone) < 0) {
		dterror("problem Setting the Tone back\n");
		return -1;
	}

	while (1) {
		struct dvb_frontend_event event {};
		if (ioctl(fefd, FE_GET_EVENT, &event) < 0)
			break;
	}

	auto ret = cmdseq.spectrum(fefd, options.spectrum_method);

	t.tune_mode = tune_mode_t::SPECTRUM;
	t.spectrum_scan_options = options;
	dtdebugx("tune: spectrum acquisition started ret=%d", ret);
	//dttime(100);
	return ret;
}


template bool dvb_frontend_t::is_tuned_to(const chdb::dvbs_mux_t& mux, const chdb::lnb_t* required_lnb) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::dvbc_mux_t& mux, const chdb::lnb_t* required_lnb) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::dvbt_mux_t& mux, const chdb::lnb_t* required_lnb) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::any_mux_t& mux, const chdb::lnb_t* required_lnb) const;
