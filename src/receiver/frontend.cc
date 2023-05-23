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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <map>

#include "receiver.h"
#include "signal_info.h"
#include "util/dtassert.h"
#include "util/logger.h"
#include "util/neumovariant.h"
#include "spectrum_algo.h"
#include "devmanager.h"
#include "util/template_util.h"

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

void cmdseq_t::add(int cmd, const dtv_matype_list& matype_list) {
	assert(cmdseq.num < props.size() - 1);
	memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
	cmdseq.props[cmdseq.num].cmd = cmd;
	cmdseq.props[cmdseq.num].u.matype_list = matype_list;
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

int cmdseq_t::get_properties(int fefd) {
	if ((ioctl(fefd, FE_GET_PROPERTY, &cmdseq)) == -1) {
		user_error("Error setting frontend property: " << strerror(errno));
		return -1;
	}
	return 0;
}

int cmdseq_t::tune(int fefd, int heartbeat_interval) {
	add(DTV_TUNE, 0);
	if (heartbeat_interval > 0)
		add(DTV_HEARTBEAT, heartbeat_interval);
	if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
		user_error("Error setting frontend property: " << strerror(errno));
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

int dvb_frontend_t::open_device(fe_state_t& t, bool rw, bool allow_failure) {
	if (t.fefd >= 0)
		return 0; // already open

	ss::string<PATH_MAX> frontend_fname;
	frontend_fname.sprintf("/dev/dvb/adapter%d/frontend%d", adapter_no, frontend_no);
	int rw_flag = rw ? O_RDWR : O_RDONLY;
	t.fefd = open(frontend_fname.c_str(), rw_flag | O_NONBLOCK | O_CLOEXEC);
	if (t.fefd < 0) {
		user_errorx("Error opening /dev/dvb/adapter%d/frontend%d in %s mode: %s", (int)adapter_no,
								(int)frontend_no, rw ? "read-write" : "readonly", strerror(errno));
		return -1;
	}

	return 0;
}

dvb_frontend_t::~dvb_frontend_t() {
	auto w = ts.writeAccess();
	if(w->fefd>=0) {
		dtdebugx("closing fefd=%d", w->fefd);
		while (::close(w->fefd) != 0) {
			if (errno != EINTR)
				dterrorx("Error closing /dev/dvb/adapter%d/frontend%d: %s", (int)adapter_no, (int)frontend_no,
								 strerror(errno));
		}
		w->fefd = -1;
	}
}

void dvb_frontend_t::close_device(fe_state_t& t) {
	if (t.fefd < 0)
		return;
	dtdebugx("closing fefd=%d\n", t.fefd);
	while (::close(t.fefd) != 0) {
		if (errno != EINTR)
			dterrorx("Error closing /dev/dvb/adapter%d/frontend%d: %s", (int)adapter_no, (int)frontend_no,
							 strerror(errno));
	}
	t.fefd = -1;
}

/*
	return -1 on error, 1 on change, 0 on no change
*/
static int get_frontend_names_dvapi(const adapter_no_t adapter_no, fe_state_t& t) {
	struct dvb_frontend_info fe_info {}; // front_end_info
	if (ioctl(t.fefd, FE_GET_INFO, &fe_info) < 0) {
		dterrorx("FE_GET_FRONTEND_INFO FAILED: %s", strerror(errno));
		return -1;
	}
	t.dbfe.k.adapter_mac_address = -1;
	t.dbfe.card_mac_address = -1;

	t.dbfe.card_name.clear();
	t.dbfe.card_short_name.clear();
	t.dbfe.adapter_name.clear();
	t.dbfe.card_address.clear();

	t.dbfe.card_name.sprintf(fe_info.name, strlen(fe_info.name));
	t.dbfe.card_short_name.sprintf(fe_info.name, strlen(fe_info.name));
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
	t.dbfe.supports.spectrum_sweep = false;
	t.dbfe.supports.spectrum_fft = false;
	return 0;
}

//	return -1 on error, 1 on change, 0 on no change
static int get_frontend_names(fe_state_t& t, int adapter_no, int api_version) {
	struct dvb_frontend_extended_info fe_info {}; // front_end_info

	if (ioctl(t.fefd, FE_GET_EXTENDED_INFO, &fe_info) < 0) {
		dterrorx("FE_GET_FRONTEND_INFO FAILED: %s", strerror(errno));
		return -1;
	}
	t.dbfe.card_name.clear();
	t.dbfe.card_short_name.clear();
	t.dbfe.adapter_name.clear();
	t.dbfe.card_address.clear();

	auto* card_name = fe_info.card_name;
	auto* card_short_name = fe_info.card_short_name;
	if(strlen(card_short_name)==0)
		card_short_name = card_name;
	if(api_version < 1.3) { //hack
		fe_info.supports_neumo = false; /*in older api, several fields were not present in  dvb_frontend_extended_info
																			but were part of an fields "name" instead; this hack is a temporary
																			solution until the API stabilizes
																		*/
	}
	t.dbfe.supports_neumo = 	fe_info.supports_neumo;
#if 0
	t.dbfe.rf_in = (fe_info.supports_neumo && fe_info.default_rf_input >= 0) ?
		fe_info.default_rf_input : adapter_no;
#endif
	t.dbfe.card_name.sprintf(card_name, strlen(card_name));
	t.dbfe.card_short_name.sprintf(card_short_name, strlen(card_short_name));
	for(int i=0; i < fe_info.num_rf_inputs; ++i) {
		t.dbfe.rf_inputs.push_back(fe_info.rf_inputs[i]);
	}
	if (fe_info.adapter_name[0] == 0) {
		// old style, for cards not supported by neumoDVB
		ss::string<256> adapter_name;
		adapter_name.sprintf("A%d %s", adapter_no, fe_info.card_short_name);
		t.dbfe.adapter_name.sprintf(adapter_name.c_str(), strlen(fe_info.adapter_name));
	} else
		t.dbfe.adapter_name.sprintf(fe_info.adapter_name);

	if (fe_info.card_address[0] == 0) {
		// fake but unique. Each adapter is consdered to be on a separate card
		ss::string<256> card_address;
		card_address.sprintf("adapter%d", adapter_no);
	} else {
		t.dbfe.card_address.sprintf(fe_info.card_address, strlen(fe_info.card_address));
	}

	// todo: add caps
	t.dbfe.card_mac_address = fe_info.card_mac_address;
	t.dbfe.k.adapter_mac_address = fe_info.adapter_mac_address;
	t.dbfe.frequency_min = fe_info.frequency_min;
	t.dbfe.frequency_max = fe_info.frequency_max;
	t.dbfe.symbol_rate_min = fe_info.symbol_rate_min;
	t.dbfe.symbol_rate_max = fe_info.symbol_rate_max;
	t.dbfe.supports.multistream = fe_info.caps & FE_CAN_MULTISTREAM;
	t.dbfe.supports.blindscan = fe_info.extended_caps & FE_CAN_BLINDSEARCH;
	t.dbfe.supports.spectrum_sweep = fe_info.extended_caps & FE_CAN_SPECTRUM_SWEEP;
	t.dbfe.supports.spectrum_fft = fe_info.extended_caps & FE_CAN_SPECTRUM_FFT;
	t.dbfe.supports.iq = fe_info.extended_caps & FE_CAN_IQ;
	return 0;
}

static int get_frontend_info(const adapter_no_t adapter_no, const frontend_no_t frontend_no, int api_version,
														 fe_state_t& t) {

	int ret = get_frontend_names(t, (int)adapter_no, api_version);
	if (ret < 0)
		ret = get_frontend_names_dvapi(adapter_no, t);
	if (ret < 0)
		return ret;
	if(t.dbfe.card_mac_address <0 || 	t.dbfe.card_mac_address == 0xffffffffffff) {
		t.dbfe.card_mac_address = 0x2L | ((uint64_t)(int(frontend_no) | (int(adapter_no) << 8)) <<32);
		dtdebugx("No mac address; faking one: 0x%lx\n", t.dbfe.card_mac_address);
	}
	if(t.dbfe.k.adapter_mac_address < 0 || t.dbfe.k.adapter_mac_address == 0xffffffffffff) {
		t.dbfe.k.adapter_mac_address = 0x2L | ((uint64_t)(int(frontend_no) | (int(adapter_no) << 8)) <<32);
		dtdebugx("No mac address; faking one: 0x%lx\n", t.dbfe.k.adapter_mac_address);
	}
	if(t.dbfe.rf_inputs.size() ==0) {
		//non blindscan drivers
		t.dbfe.rf_inputs.push_back(0);
	}

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

std::shared_ptr<dvb_frontend_t> dvb_frontend_t::make(adaptermgr_t* adaptermgr,
																										 adapter_no_t adapter_no, frontend_no_t frontend_no,
																										 api_type_t api_type, int api_version) {
	auto fe = std::make_shared<dvb_frontend_t>(adaptermgr, adapter_no, frontend_no, api_type, api_version);

	auto w = fe->ts.writeAccess();
	// first try writeable access, then readonly
	if (fe->open_device(*w, true, true) < 0) {
		w->dbfe.can_be_used = false;
		w->info_valid = (fe->open_device(*w, false, true) == 0);
		dtdebugx("/dev/dvb/adapter%d/frontend%d currently not useable", (int)fe->adapter_no, (int)fe->frontend_no);
	} else {
		w->info_valid = true;
		w->dbfe.can_be_used = true;
		get_frontend_info(adapter_no, frontend_no, api_version, *w);

		if(int64_t(fe->adapter_mac_address) <=0)
			fe->adapter_mac_address = adapter_mac_address_t(w->dbfe.k.adapter_mac_address);
		if(int64_t(fe->card_mac_address) <=0)
			fe->card_mac_address = card_mac_address_t(w->dbfe.card_mac_address);
		fe->close_device(*w);
	}
	return fe;
}

//returns the tuned frequency, compensated for lnb offset
static int get_dvbs_mux_info(chdb::dvbs_mux_t& mux, const cmdseq_t& cmdseq, const devdb::lnb_t& lnb,
														 int band, chdb::fe_polarisation_t pol) {

	mux.delivery_system = (chdb::fe_delsys_dvbs_t)cmdseq.get(DTV_DELIVERY_SYSTEM)->u.data;
	bool tone_on = band; //we cannot rely on DTV_TONE from driver, because it may not have been set on slave connections
	int freq = cmdseq.get(DTV_FREQUENCY)->u.data;

	mux.frequency = devdb::lnb::freq_for_driver_freq(lnb, freq, tone_on); // always in kHz
	mux.pol = pol; //we cannot rely on DTV_VOLTAGE from driver, because it may not have been set on slave connections

	mux.symbol_rate = cmdseq.get(DTV_SYMBOL_RATE)->u.data; // in Hz

	mux.modulation = (chdb::fe_modulation_t)cmdseq.get(DTV_MODULATION)->u.data;
	mux.fec = (chdb::fe_code_rate_t)cmdseq.get(DTV_INNER_FEC)->u.data;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.get(DTV_INVERSION)->u.data;
	mux.rolloff = (chdb::fe_rolloff_t)cmdseq.get(DTV_ROLLOFF)->u.data;
	mux.pilot = (chdb::fe_pilot_t)cmdseq.get(DTV_PILOT)->u.data;
	auto stream_id_prop = cmdseq.get(DTV_STREAM_ID)->u.data;
	if(stream_id_prop ==0xffffffff) {
		mux.k.stream_id = -1;
		mux.pls_mode = chdb::fe_pls_mode_t::ROOT;
		mux.pls_code = 1;
	} else {
		mux.k.stream_id = (stream_id_prop & 0xff) == 0xff ? -1 : (stream_id_prop & 0xff);
		mux.pls_mode = chdb::fe_pls_mode_t((stream_id_prop >> 26) & 0x3);
		mux.pls_code = (stream_id_prop >> 8) & 0x3FFFF;
	}
	return mux.frequency;
}

//returns the tuned frequency
static int get_dvbc_mux_info(chdb::dvbc_mux_t& mux, const cmdseq_t& cmdseq) {

	mux.delivery_system = (chdb::fe_delsys_dvbc_t)cmdseq.get(DTV_DELIVERY_SYSTEM)->u.data;

	int freq = cmdseq.get(DTV_FREQUENCY)->u.data;
	mux.frequency = freq / 1000; // freq in Hz; mux.frequency in kHz

	mux.symbol_rate = cmdseq.get(DTV_SYMBOL_RATE)->u.data; // in Hz

	mux.modulation = (chdb::fe_modulation_t)cmdseq.get(DTV_MODULATION)->u.data;
	mux.fec_inner = (chdb::fe_code_rate_t)cmdseq.get(DTV_INNER_FEC)->u.data;
	mux.fec_outer = chdb::fe_code_rate_t::FEC_AUTO;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.get(DTV_INVERSION)->u.data;
	mux.k.stream_id = cmdseq.get(DTV_STREAM_ID)->u.data;
	// int dtv_scrambling_sequence_index_prop = cmdseq.get()->u.data;
	return mux.frequency;
}

//returns the tuned frequency
static int get_dvbt_mux_info(chdb::dvbt_mux_t& mux, const cmdseq_t& cmdseq) {

	mux.delivery_system = (chdb::fe_delsys_dvbt_t)cmdseq.get(DTV_DELIVERY_SYSTEM)->u.data;

	int freq = cmdseq.get(DTV_FREQUENCY)->u.data;
	mux.frequency = freq /1000; //freq in Hz; mux.frequency in kHz

	mux.modulation = (chdb::fe_modulation_t)cmdseq.get(DTV_MODULATION)->u.data;
	mux.inversion = (chdb::fe_spectral_inversion_t)cmdseq.get(DTV_INVERSION)->u.data;
	mux.k.stream_id = cmdseq.get(DTV_STREAM_ID)->u.data;
	mux.bandwidth = (chdb::fe_bandwidth_t)cmdseq.get(DTV_BANDWIDTH_HZ)->u.data;
	mux.transmission_mode = (chdb::fe_transmit_mode_t)cmdseq.get(DTV_TRANSMISSION_MODE)->u.data;
	mux.HP_code_rate = (chdb::fe_code_rate_t)cmdseq.get(DTV_CODE_RATE_HP)->u.data;
	mux.LP_code_rate = (chdb::fe_code_rate_t)cmdseq.get(DTV_CODE_RATE_LP)->u.data;
	mux.hierarchy = (chdb::fe_hierarchy_t)cmdseq.get(DTV_HIERARCHY)->u.data;
	mux.guard_interval = (chdb::fe_guard_interval_t)cmdseq.get(DTV_GUARD_INTERVAL)->u.data;
	return mux.frequency;
}

int dvb_frontend_t::get_mux_info(signal_info_t& ret, const cmdseq_t& cmdseq, api_type_t api) {
	int matype{-1};
	using namespace chdb;
	const auto r = this->ts.readAccess();
	const auto* dvbs_mux = std::get_if<dvbs_mux_t>(&r->reserved_mux);
	ret.tune_confirmation = r->tune_confirmation;
	ret.received_si_mux = r->received_si_mux;
	ret.received_si_mux_is_bad = r->received_si_mux_is_bad;
	ret.driver_mux = r->reserved_mux; //ensures that we return proper any_mux_t type for dvbc and dvbt
	ret.stat.k.sat_pos = mux_key_ptr(r->reserved_mux)->sat_pos;
	if (ret.tune_confirmation.si_done) {
		dtdebugx("reporting si_done=true");
	}
	*mux_key_ptr(ret.driver_mux) = *mux_key_ptr(r->reserved_mux);
	const auto& lnb = r->reserved_lnb;
	const auto& rf_path = r->reserved_rf_path;
	int band = 0;
	chdb::fe_polarisation_t pol{chdb::fe_polarisation_t::NONE};
	if (dvbs_mux) {
		ret.stat.k.frequency = dvbs_mux->frequency;
		ret.stat.k.pol = dvbs_mux->pol;
		ret.stat.k.rf_path = rf_path;
		ret.stat.k.pol = dvbs_mux->pol;
		auto [band_, pol_, freq] = devdb::lnb::band_voltage_freq_for_mux(lnb, *dvbs_mux);
		band = band_;
		pol = dvbs_mux->pol;
		if(band < r->reserved_lnb.lof_offsets.size())
			ret.lnb_lof_offset = r->reserved_lnb.lof_offsets[band];
		else
			ret.lnb_lof_offset.reset();
	}

	bool tone_on = band;  //we cannot rely on DTV_TONE from driver, because it may not have been set on slave connections

	auto freq = cmdseq.get(DTV_FREQUENCY)->u.data;
	ret.uncorrected_driver_freq =  devdb::lnb::uncorrected_freq_for_driver_freq(lnb, freq, tone_on);

	//the following must be called even when not locked, to consume the results of all DTV_... commands
	visit_variant(
		ret.driver_mux,
		[&cmdseq,  &lnb, &ret, band, pol](chdb::dvbs_mux_t& mux) {
			ret.stat.k.frequency = get_dvbs_mux_info(mux, cmdseq, lnb, band, pol);
			ret.stat.symbol_rate = mux.symbol_rate;
		},
		[&cmdseq, &ret](chdb::dvbc_mux_t& mux) {
			ret.stat.k.frequency = get_dvbc_mux_info(mux, cmdseq); },
		[&cmdseq, &ret](chdb::dvbt_mux_t& mux) {
			ret.stat.k.frequency = get_dvbt_mux_info(mux, cmdseq); });

	/*at this point ret.mux and ret.stat.frequency contain the frequency as reported from the tuner itself,
		but after compensation for the currently known lnb offset

		dvbs_mux->frequency is the frequency which we were asked to tune
			*/
	if (api == api_type_t::NEUMO) {
		ret.driver_data_reliable =true;
		matype = cmdseq.get(DTV_MATYPE)->u.data;
		auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&ret.driver_mux);
		if (dvbs_mux) {
			if(dvbs_mux->delivery_system == fe_delsys_dvbs_t::SYS_DVBS) {
				dvbs_mux->matype = 256;
				matype =  256; //means dvbs
				dvbs_mux->k.stream_id = -1;
			} else {
				dvbs_mux->matype = matype;
#if 0 //seems to go wrong on 25.5W 11174V: multistream
				bool is_mis = !(matype & (1 << 5));
				if (!is_mis)
					dvbs_mux->stream_id = -1;
#endif
			}
		} else {
			//currently no dvbt/dvbc mux correctly sets matype
			matype = -2;
		}
		auto* p = cmdseq.get(DTV_CONSTELLATION);
		if(p) {
			auto& cs = p->u.constellation;
			assert(cs.num_samples >= 0);
			assert((int)cs.num_samples <= ret.constellation_samples.size());
			assert(cs.num_samples <= 4096);
			ret.constellation_samples.resize_no_init(cs.num_samples); // we may have retrieved fewer samples than we asked
		}

		if(api_type == api_type_t::NEUMO && api_version >= 1200) {
			ret.locktime_ms = cmdseq.get(DTV_LOCKTIME)->u.data;
			ret.bitrate = cmdseq.get(DTV_BITRATE)->u.data;
			auto* isi_bitset = (uint32_t*)cmdseq.get(DTV_ISI_LIST)->u.buffer.data;
			for (int i = 0; i < 256; ++i) {
				int j = i / 32;
				uint32_t mask = ((uint32_t)1) << (i % 32);
				if (isi_bitset[j] & mask) {
					ret.isi_list.push_back(i);
				}
			}
			auto& matype_list = cmdseq.get(DTV_MATYPE_LIST)->u.matype_list;
			ret.matype_list.resize_no_init(matype_list.num_entries);
		} else {
			auto* isi_bitset = (uint32_t*)cmdseq.get(DTV_ISI_LIST)->u.buffer.data;
			ret.matype_list.clear();
			for (int i = 0; i < 256; ++i) {
				int j = i / 32;
				uint32_t mask = ((uint32_t)1) << (i % 32);
				if (isi_bitset[j] & mask) {
					ret.isi_list.push_back(i);
					//fake matype for earlier versions of neumo dvbapi
					ret.matype_list.push_back(i | (matype <<8));
				}
			}
		}
	}
	return matype;
}

/*
	lnb is needed to identify dish on which signal is captured
	mux_ is needed to translate tuner frequency to real frequency
*/
int dvb_frontend_t::request_signal_info(cmdseq_t& cmdseq, signal_info_t& ret, bool get_constellation) {
	cmdseq.add(DTV_STAT_SIGNAL_STRENGTH);
	cmdseq.add(DTV_STAT_CNR);

	cmdseq.add(DTV_STAT_PRE_ERROR_BIT_COUNT);
	cmdseq.add(DTV_STAT_PRE_TOTAL_BIT_COUNT);
#if 0
	cmdseq.add(DTV_STAT_POST_ERROR_BIT_COUNT	);
	cmdseq.add(DTV_STAT_POST_TOTAL_BIT_COUNT	);
	cmdseq.add(DTV_STAT_ERROR_BLOCK_COUNT	);
	cmdseq.add(DTV_STAT_TOTAL_BLOCK_COUNT	);
#endif

	cmdseq.add(DTV_DELIVERY_SYSTEM); // 0 DVB-S, 9 DVB-S2
	cmdseq.add(DTV_VOLTAGE);					// 0 - 13V Vertical, 1 - 18V Horizontal, 2 - Voltage OFF
	cmdseq.add(DTV_TONE);
	cmdseq.add(DTV_FREQUENCY);
	if(current_delsys_type != chdb::delsys_type_t::DVB_T)
		cmdseq.add(DTV_SYMBOL_RATE);

	cmdseq.add(DTV_MODULATION); // 5 - QPSK, 6 - 8PSK
	cmdseq.add(DTV_INNER_FEC);
	cmdseq.add(DTV_INVERSION);
	cmdseq.add(DTV_ROLLOFF);
	cmdseq.add(DTV_PILOT); // 0 - ON, 1 - OFF
	cmdseq.add(DTV_STREAM_ID);
	if(current_delsys_type == chdb::delsys_type_t::DVB_T) {
		//	{ .cmd = DTV_SCRAMBLING_SEQUENCE_INDEX },
		cmdseq.add(DTV_BANDWIDTH_HZ);			// DVB-T
		cmdseq.add(DTV_TRANSMISSION_MODE); // DVB-
		cmdseq.add(DTV_CODE_RATE_HP);			// DVB-T
		cmdseq.add(DTV_CODE_RATE_LP);			// DVB-T
		cmdseq.add(DTV_HIERARCHY);					// DVB-T
		cmdseq.add(DTV_GUARD_INTERVAL);		// DVB-T
	}

	if (api_type == api_type_t::NEUMO) {
		// The following are only supported by neumo version 1.1 and later of dvbapi
		cmdseq.add(DTV_MATYPE);
		if (get_constellation) {
				auto r = ts.readAccess();
				if(r->dbfe.supports.iq && num_constellation_samples > 0) {
					ret.constellation_samples.resize(num_constellation_samples);
					dtv_fe_constellation cs;
					cs.num_samples = num_constellation_samples;
					cs.samples = &ret.constellation_samples[0];
					cs.method = CONSTELLATION_METHOD_DEFAULT;
					cs.constel_select = 1;
					cmdseq.add(DTV_CONSTELLATION, cs);
				} else {
					ret.constellation_samples.clear();
				}
		}
		if(api_version >= 1200) {
			// The following are only supported by neumo version 1.2 and later of dvbapi
			cmdseq.add(DTV_LOCKTIME);
			cmdseq.add(DTV_BITRATE);
			cmdseq.add(DTV_ISI_LIST); //TODO: phase out
			dtv_matype_list matype_list;
			matype_list.num_entries = ret.matype_list.capacity();
			assert(matype_list.num_entries==256);
			matype_list.matypes = ret.matype_list.buffer();
			cmdseq.add(DTV_MATYPE_LIST, matype_list);
		} else {
			cmdseq.add(DTV_ISI_LIST);
		}
		if(api_version >=1500) {
			cmdseq.add(DTV_RF_INPUT);
		}
	}
	auto fefd = ts.readAccess()->fefd;
	return cmdseq.get_properties(fefd);
}

signal_info_t dvb_frontend_t::get_signal_info(bool get_constellation) {
	signal_info_t ret{this, ts.readAccess()->dbfe.k};
	ret.tune_count = ts.readAccess()->tune_count;
	using namespace chdb;
	// bool is_sat = true;
	ret.stat.k.time = system_clock_t::to_time_t(now);

	cmdseq_t cmdseq;
	if(request_signal_info(cmdseq, ret, get_constellation) < 0) {
		ret.lock_status = ts.readAccess()->lock_status; //needs to be done after retrieving signal_info
		return ret;
	}

	statdb::signal_stat_entry_t& last_stat = ret.last_stat();
	auto& signal_strength_stats = cmdseq.get(DTV_STAT_SIGNAL_STRENGTH)->u.st;

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

	auto& snr_stats = cmdseq.get(DTV_STAT_CNR)->u.st;

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

	uint64_t ber_enum = cmdseq.get(DTV_STAT_PRE_ERROR_BIT_COUNT)->u.st.stat[0].uvalue;
	uint64_t ber_denom = cmdseq.get(DTV_STAT_PRE_TOTAL_BIT_COUNT)->u.st.stat[0].uvalue;
	if(ber_denom >0) {
		last_stat.ber = ber_enum / (double)ber_denom;
	} else {
		last_stat.ber = ber_enum; //hack
	}

	auto matype = get_mux_info(ret, cmdseq, api_type);
	{
		auto r = ts.readAccess();
		if(r->last_signal_info) {
			auto new_isi_found = ret.isi_list != r->last_signal_info->isi_list;
			ret.last_new_matype_time = new_isi_found ?  steady_clock_t::now() :
				r->last_signal_info->last_new_matype_time;
		} else {
			ret.last_new_matype_time = steady_clock_t::now();
		}
	}
	ret.lock_status = ts.readAccess()->lock_status; //needs to be done after retrieving signal_info
	ret.lock_status.matype = matype; //not yet set in ts.readAccess()->lock_status; will be set there below

	auto* p = cmdseq.get(DTV_CONSTELLATION);
	if (p) {
		auto& cs = p->u.constellation;
		assert(cs.num_samples >= 0);
		assert((int)cs.num_samples <= ret.constellation_samples.size());
		assert(cs.num_samples <= 4096);
		ret.constellation_samples.resize_no_init(cs.num_samples); // we may have retrieved fewer samples than we asked
	}

	if (api_type == api_type_t::NEUMO && api_version >= 1200) {
		uint64_t lock_time = cmdseq.get(DTV_LOCKTIME)->u.data;
		uint64_t bitrate = cmdseq.get(DTV_BITRATE)->u.data;
		ret.stat.locktime_ms = (ret.lock_status.fe_status & FE_HAS_LOCK) ? lock_time : 0;
		ret.bitrate = bitrate;
#if 0
		auto matype_list = cmdseq.get(DTV_MATYPE_LIST)->u.matype_list;
		ret.matype_list.clear();
		for(int i = 0; i < (int) matype_list.num_entries; ++i)
			ret.matype_list.push_back(matype_list.matypes[i]);
#endif
	}

	ret.lock_status.matype = matype;
	{
		auto w = ts.writeAccess();
		w->lock_status.matype = matype;
		w->last_signal_info = ret;
	}
	ts_cv.notify_all();
	return ret;
}

/* Force the driver to go into idle mode immediately, so
	 that the fe_monitor_thread_t will also return immediately
*/
int dvb_frontend_t::stop() {

	/*
		First, prevent frontend_monitor from making future calls
		to the dvb_frontend, because

		1. such FE_GET_PROPERTY calls lock the internal frontend driver mutex,
		which can slow down the FE_SET_PROPERTY calls we wish to make. However, typically FE_GET_PROPERTY do not
		contain time consuming code.

		2. To avoid sending signal_info_t messages to the GUI for the previously tuned mux, while
		a new mux is being tuned.

	 */
	auto m = monitor_thread;
	task_queue_t::future_t f;
	if (m.get()) {
		f = m->push_task( [m] () {
			cb(*m).pause();
			return 0;
		});
	}

	if(ts.readAccess()->dbfe.supports_neumo) {
		/*
			ask driver to abort quickly any tune or monitoring command in progress.
			This is needed in case a long-lasting tune, spectrum acquisition.... is still running
		*/
		auto fefd = ts.readAccess()->fefd;
		struct dtv_algo_ctrl algo_ctrl;
		algo_ctrl.cmd = DTV_STOP;
		if ((ioctl(fefd, FE_ALGO_CTRL, &algo_ctrl)) == -1) {
			dtdebug("ALGO_CTRL: DTV STOP failed: " << strerror(errno));
		}

		/* In case monitor_thread has called ioctl. This ioctl will no longer be blocked by a tune in
			 progress and thus will return fast. monitor_thread should therefore quickly execute the pause()
		 task
		*/
		if(f.valid())
			f.wait();
		/* pause task has been run now
		 */
	}
	return 0;
}

int dvb_frontend_t::start() {
	auto m = monitor_thread;
	task_queue_t::future_t f;
	if (m.get()) {
		//need tp push by value as we don't wait for call to complete
		f = m->push_task( [m] () {
			cb(*m).unpause();
			return 0;
		});
	}
	return 0;
}



/*
	returns two fields indicating the current lock status,
	and the lock history: 0= never locked, 1 = currently not locked, 2 = currently locked
	but we lost lock at least once since the  ast call
*/
fe_lock_status_t dvb_frontend_t::get_lock_status() {
	auto w = ts.writeAccess();
	auto& ret = w->lock_status;
	bool is_locked = ret.fe_status & FE_HAS_LOCK;
	if (!is_locked) {
		this->sec_status.set_tune_status(false); // ensures that diseqc will be sent again
	} else
		this->sec_status.set_tune_status(true); // ensures that diseqc will be sent again
	w->lock_status.lock_lost = false;
	return ret;
}

void dvb_frontend_t::set_lock_status(fe_status_t fe_status) {
	if(api_type == api_type_t::DVBAPI) {
		if (fe_status & (FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK))
			fe_status = (fe_status_t) (fe_status | FE_HAS_TIMING_LOCK); //not present in dvbapi
	} else {
		if (fe_status & FE_OUT_OF_RESOURCES)
			fe_status = (fe_status_t) (fe_status | FE_HAS_TIMING_LOCK); //handle older neumodrivers
	}

	bool locked_now = fe_status & FE_HAS_LOCK;
	auto w = ts.writeAccess();

	bool locked_before = w->lock_status.fe_status & FE_HAS_LOCK;
	if(!locked_now)
		w->lock_status.matype = -1;
	if (locked_before && !locked_now)
		w->lock_status.lock_lost = true;
	w->lock_status.fe_status = fe_status;
}

void dvb_frontend_t::clear_lock_status() {
	auto w = ts.writeAccess();
	w->lock_status = {};
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
		dterror("problem sending the DiseqC message");
		return -1;
	}
	return 0;
}

int dvb_frontend_t::send_positioner_message(devdb::positioner_cmd_t command, int32_t par, bool repeated) {
	using namespace devdb;
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
		auto loc = this->get_usals_location();

		auto angle = devdb::lnb::sat_pos_to_usals_par(par / 10, loc.usals_longitude / 10, loc.usals_latitude / 10);

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
		dterror("problem sending the DiseqC message");
		return -1;
	}
	return 0;
}



void dvb_frontend_t::update_tuned_mux_nit(const chdb::any_mux_t& mux) {
	assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);

	auto w = this->ts.writeAccess();
	w->reserved_mux = mux;
}

void dvb_frontend_t::update_received_si_mux(const std::optional<chdb::any_mux_t>& mux, bool is_bad) {
	auto w = this->ts.writeAccess();
	w->received_si_mux = mux;
	w->received_si_mux_is_bad = is_bad;
}


template <typename mux_t> bool dvb_frontend_t::is_tuned_to(
	const mux_t& mux, const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid) const {
	return this->ts.readAccess()->is_tuned_to(mux, required_rf_path, ignore_t2mi_pid);
}



inline void spectrum_scan_t::resize(int num_freq, int num_peaks) {
	assert(num_freq <= max_num_freq);
	assert(num_peaks <= max_num_peaks);
	freq.resize_no_init(num_freq);
	rf_level.resize_no_init(num_freq);
	peaks.resize_no_init(num_peaks);
}

inline void spectrum_scan_t::adjust_frequencies(const devdb::lnb_t& lnb, int high_band) {
	for (auto& f : freq) {
		f = devdb::lnb::freq_for_driver_freq(lnb, f, high_band);
	}
	for (auto& p : peaks) {
		p.freq = devdb::lnb::freq_for_driver_freq(lnb, p.freq, high_band);
	}
}

std::optional<statdb::spectrum_t> dvb_frontend_t::get_spectrum(const ss::string_& spectrum_path) {
	this->num_constellation_samples = 0;
	this->clear_lock_status();
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
		dterrorx("ioctl failed: %s", strerror(errno));
		assert(0); // todo: handle EINTR
		return {};
	}
	spectrum = cmdseq.props[0].u.spectrum;

	if (spectrum.num_freq <= 0) {
		dterror("kernel returned spectrum with 0 samples");
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
		const auto r = this->ts.readAccess();
		auto& lnb = r->reserved_lnb;
		auto& rf_path = r->reserved_rf_path;
		auto& options = r->tune_options.spectrum_scan_options;
		scan.start_time = options.start_time;
		scan.sat_pos = options.sat_pos;
		scan.rf_path = rf_path;
		auto [start_freq, mid_freq, end_freq, lof_low, lof_high, inverted_spectrum] =
			devdb::lnb::band_frequencies(lnb, options.band_pol.band);
		scan.band_pol = options.band_pol;
		scan.start_freq = options.start_freq;
		scan.end_freq = options.end_freq;
		scan.resolution = options.resolution;
		scan.spectrum_method = options.spectrum_method;
		scan.adjust_frequencies(lnb, scan.band_pol.band == devdb::fe_band_t::HIGH);
		scan.usals_pos = lnb.usals_pos;
		scan.adapter_no =  (int)this->adapter_no;
		scan.rf_path =  rf_path;
		auto* network = devdb::lnb::get_network(lnb, scan.sat_pos);
		scan.lof_offsets = lnb.lof_offsets;
		assert(!network || network->sat_pos == scan.sat_pos);
		append_now = options.append;
		// we will need to call start_lnb_spectrum again later to retrieve (part of) the high band)
		incomplete =
			(options.band_pol.band == devdb::fe_band_t::LOW && scan.end_freq > mid_freq && mid_freq < options.end_freq) ||
			(options.scan_both_polarisations &&
			 (options.band_pol.pol == chdb::fe_polarisation_t::H ||
				options.band_pol.pol == chdb::fe_polarisation_t::L));
	}
	{
		auto w = this->ts.writeAccess();
		//frequency at which scan should resume (avoid frequency jumping down when starting high band)
		min_freq_to_save = w->last_saved_freq;
		w->last_saved_freq = scan.freq.size() ==0 ? 0 : scan.freq[scan.freq.size()-1];
	}


	if (incomplete) {
		auto tune_options = this->ts.readAccess()->tune_options; // make a copy
		auto& options  = tune_options.spectrum_scan_options;
		auto lnb = this->ts.readAccess()->reserved_lnb;
		auto rf_path = this->ts.readAccess()->reserved_rf_path;
		if (options.band_pol.band == devdb::fe_band_t::HIGH) {
			// switch to the next polarisation
			assert(options.band_pol.pol == chdb::fe_polarisation_t::H ||
						 options.band_pol.pol == chdb::fe_polarisation_t::L);
			options.band_pol.band = devdb::fe_band_t::LOW;
			options.band_pol.pol = options.band_pol.pol == chdb::fe_polarisation_t::H ?
				chdb::fe_polarisation_t::V : chdb::fe_polarisation_t::R;
			options.append = false;
			dtdebugx("Continuing spectrum scan with pol=V band=low");
		} else {
			// switch to next band
			options.band_pol.band = devdb::fe_band_t::HIGH;
			options.append = true;
			dtdebugx("Continuing spectrum scan with pol=%s band=high",
							 enum_to_str(options.band_pol.pol));
		}
		start_lnb_spectrum_scan(rf_path, lnb, tune_options);
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

int dvb_frontend_t::tune_(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
													const chdb::dvbs_mux_t& mux, const tune_options_t& tune_options) {
	// Clear old tune_mux_confirmation info
	this->clear_lock_status();
	this->reset_tuned_mux_tune_confirmation();
	if(api_type != api_type_t::NEUMO && mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_AUTO) {
		user_error("Standard dvb api drivers do not support SYS_AUTO; Install neumo drivers or select "
							 "SYS_DVBSS, SYS_DVBS...");
		return -1;
	}

	auto blindscan = tune_options.use_blind_tune || (
		api_type == api_type_t::NEUMO && mux.delivery_system == chdb::fe_delsys_dvbs_t::SYS_AUTO);

	auto ret = -1;
	dtdebug("Tuning adapter " << (int) adapter_no << " rf_in=" << (int) rf_path.rf_input
					<< " DVB-S to " << mux << (blindscan ? " BLIND" : ""));

	current_delsys_type = chdb::delsys_type_t::DVB_S;
	int num_constellation_samples = tune_options.constellation_options.num_samples;

	cmdseq_t cmdseq;
	this->num_constellation_samples = num_constellation_samples;
	auto [band, voltage, frequency] = devdb::lnb::band_voltage_freq_for_mux(lnb, mux);
	cmdseq.add_clear();
	if (blindscan) {
		assert (api_type == api_type_t::NEUMO);
		cmdseq.add(DTV_ALGORITHM, ALGORITHM_BLIND);
		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)SYS_AUTO);
		if (mux.symbol_rate > 0)
			cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); //
		cmdseq.add(DTV_FREQUENCY, frequency);						// For satellite delivery systems, it is measured in kHz.
#if 0
		// not needed and is dangerous when multiple frontends attached to same tuner
		cmdseq.add(DTV_VOLTAGE, 1 - pol_is_v);
#endif
		cmdseq.init_pls_codes();
		if (mux.k.stream_id >= 0)
			cmdseq.add_pls_code(make_code((int)mux.pls_mode, (int)mux.pls_code));
		cmdseq.add_pls_codes(DTV_PLS_SEARCH_LIST);
		cmdseq.add(DTV_STREAM_ID, mux.k.stream_id);
		if(mux.symbol_rate >= 2000000)
			cmdseq.add(DTV_SEARCH_RANGE, std::max(mux.symbol_rate, (unsigned int)4000000));
		else
			cmdseq.add(DTV_SEARCH_RANGE, mux.symbol_rate);
	} else {
		if (((int)mux.delivery_system != SYS_DVBS) && ((int)mux.delivery_system != SYS_DVBS2)) {
			dterror("illegal delivery system: " << chdb::to_str(mux.delivery_system));
			return -1;
		}

		if (api_type == api_type_t::NEUMO && ts.readAccess()->dbfe.supports.blindscan)
			cmdseq.add(DTV_ALGORITHM, ALGORITHM_COLD);

		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);
		cmdseq.add(DTV_MODULATION, (int)mux.modulation);
#if 0
// not needed and is dangerous when multiple frontends attached to same tuner
		cmdseq.add(DTV_VOLTAGE,  1 - pol_is_v);
#endif
		cmdseq.add(DTV_FREQUENCY, frequency);					// For satellite delivery systems, it is measured in kHz.
		cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); // Must be in Symbols/second
		cmdseq.add(DTV_INNER_FEC, (int)mux.fec);
		cmdseq.add(DTV_INVERSION, INVERSION_AUTO);
		cmdseq.add(DTV_ROLLOFF, (int) mux.rolloff);

		cmdseq.add(DTV_PILOT, PILOT_AUTO);
#if 1
		auto stream_id =
			(mux.k.stream_id < 0 ? -1 : (make_code((int)mux.pls_mode, (int)mux.pls_code)) | (mux.k.stream_id & 0xff));
#else
		auto stream_id = make_code((int)mux.pls_mode, (int)mux.pls_code) | (mux.k.stream_id & 0xff);
#endif
		cmdseq.add(DTV_STREAM_ID, stream_id);
		if (api_type == api_type_t::NEUMO && ts.readAccess()->dbfe.supports.blindscan)
			cmdseq.add(DTV_SEARCH_RANGE, mux.symbol_rate);
	}
	dtv_fe_constellation constellation;
	if (tune_options.pls_search_range.start < tune_options.pls_search_range.end) {
		cmdseq.add_pls_range(DTV_PLS_SEARCH_RANGE, tune_options.pls_search_range);
	}

	auto w = ts.writeAccess();
	w->tune_count++;
	if( w->dbfe.supports.iq && num_constellation_samples > 0) {
		constellation.num_samples = num_constellation_samples;
		constellation.samples = nullptr;
		constellation.method = CONSTELLATION_METHOD_DEFAULT;
		constellation.constel_select = 1;
		cmdseq.add(DTV_CONSTELLATION, constellation);
	} else {
		constellation.num_samples = 0;
	}
	auto fefd = w->fefd;
	w->use_blind_tune = blindscan;
	dtdebugx("change tune mode on adapter %d from %d to %d", (int)adapter_no,
					 (int) w->tune_mode, (int) tune_options.tune_mode);
	w->tune_mode = tune_options.tune_mode;
	int heartbeat_interval = (api_type == api_type_t::NEUMO) ? 1000 : 0;
	ret = cmdseq.tune(fefd, heartbeat_interval);
	dtdebugx("tune returning ret=%d", ret);
	return ret;
}

/*
	returns: ret = return status
	         new_usals_sat_pos
 */
std::tuple<int, int>
dvb_frontend_t::lnb_spectrum_scan(const devdb::rf_path_t& rf_path,
																	const devdb::lnb_t& lnb, tune_options_t tune_options) {
	this->start_fe_and_lnb(rf_path, lnb);

	auto band = tune_options.spectrum_scan_options.band_pol.band;
	auto pol = tune_options.spectrum_scan_options.band_pol.pol;
	auto voltage = devdb::lnb::voltage_for_pol(lnb, pol) ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;

	if(api_version >=1500) {
		auto fefd = ts.readAccess()->fefd;
		assert(ts.readAccess()->dbfe.rf_inputs.contains(rf_path.rf_input));
		if ((ioctl(fefd, FE_SET_RF_INPUT, (int32_t) rf_path.rf_input))) {
			dtdebugx("problem Setting rf_input: %s", strerror(errno));
			return {-1, sat_pos_none};
		}
	}
	auto [ret, new_usals_sat_pos ] =
		this->do_lnb_and_diseqc(band, voltage);

	dtdebug("spectrum: diseqc done");
	this->stop();
	if (this->stop() < 0) /* Force the driver to go into idle mode immediately, so
													 that the fe_monitor_thread_t will also return immediately
												*/
		return {-1, new_usals_sat_pos};
	ret = this->start_lnb_spectrum_scan(rf_path, lnb, tune_options);

	this->start();

	//dttime(100);
	return {ret, new_usals_sat_pos};
}

std::tuple<int, int>
dvb_frontend_t::tune(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
										 const chdb::dvbs_mux_t& mux, const tune_options_t& tune_options,
										 bool user_requested, const devdb::resource_subscription_counts_t& use_counts) {
	{
		auto w =  this->ts.writeAccess();
		w->tune_options = tune_options;
	}
	dttime_init();
	auto muxname = chdb::to_str(mux);
	auto *conn = connection_for_rf_path(lnb, rf_path);
	if(!conn)
		return {-1, sat_pos_none};
	dtdebug("Tuning to DVBS mux " << muxname.c_str() << " diseqc: lnb_id=" << lnb << " " << conn->tune_string);

	auto [need_diseqc, need_lnb] = this->need_diseqc_or_lnb(rf_path, lnb, mux, use_counts);

	if (user_requested) {
		this->start_fe_lnb_and_mux(rf_path, lnb, mux);
	} else
		this->sec_status.retune_count++;

	//abort the current operation of the frontend making it go to IDLE mode
	dtdebug("calling stop");
	if (this->stop() < 0)  /* Force the driver to go into idle mode immediately, so
																	that the fe_monitor_thread_t will also return immediately*/
		return {-1, sat_pos_none};

	dttime(300);

	const auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&this->ts.readAccess()->reserved_mux);
	assert(dvbs_mux);
	int ret;
	int new_usals_sat_pos{sat_pos_none};
	auto band = devdb::lnb::band_for_mux(lnb, *dvbs_mux);
	if(api_type == api_type_t::NEUMO && api_version >=1500) {
		auto fefd = ts.readAccess()->fefd;
		assert(ts.readAccess()->dbfe.rf_inputs.contains(rf_path.rf_input));
		if ((ioctl(fefd, FE_SET_RF_INPUT, (int32_t) rf_path.rf_input))) {
			dtdebugx("problem Setting rf_input: %s", strerror(errno));
			return {-1, new_usals_sat_pos};
		}
	}
	if (need_diseqc) {
		std::tie(ret, new_usals_sat_pos) =
			this->do_lnb_and_diseqc(band, (fe_sec_voltage_t)devdb::lnb::voltage_for_pol(lnb, dvbs_mux->pol));
		dtdebug("tune: do_lnb_and_diseqc done");
	} else if(need_lnb) {
		this->do_lnb(band, (fe_sec_voltage_t)devdb::lnb::voltage_for_pol(lnb, dvbs_mux->pol));
		dtdebug("tune: do_lnb done");
	}

	dttime(300);
	ret = this->tune_(rf_path, lnb, *dvbs_mux, tune_options);
	if (ret < 0)
		return {ret, new_usals_sat_pos};
	this->start();
	return {0, new_usals_sat_pos};
}



int dvb_frontend_t::tune_(const chdb::dvbc_mux_t& mux, const tune_options_t& tune_options) {
	// Clear old tune_mux_confirmation info
	this->clear_lock_status();
	this->reset_tuned_mux_tune_confirmation();
	bool blindscan = tune_options.use_blind_tune;

	dtdebug("Tuning adapter [ " << (int) adapter_no << "] DVB-C to " << mux << (blindscan ? "BLIND" : ""));

//////////////////////////////////
	current_delsys_type = chdb::delsys_type_t::DVB_C;
	this->num_constellation_samples = 0;
	cmdseq_t cmdseq;
	// any system

	cmdseq.add(DTV_FREQUENCY, mux.frequency * 1000); // For DVB-C, it is measured in Hz.
	cmdseq.add(DTV_INVERSION, mux.inversion);
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);
	// dvbc
	cmdseq.add(DTV_SYMBOL_RATE, mux.symbol_rate); // in Symbols/second
	cmdseq.add(DTV_MODULATION, mux.modulation);
	cmdseq.add(DTV_INNER_FEC, mux.fec_inner);
	if (mux.k.stream_id >= 0)
		cmdseq.add(DTV_STREAM_ID, mux.k.stream_id);

	auto w = ts.writeAccess();
	w->tune_count++;
	auto fefd = w->fefd;
	dtdebugx("change tune mode on adapter %d from %d to %d", (int) adapter_no,
					 (int) w->tune_mode, (int) tune_options.tune_mode);
	w->tune_mode = tune_options.tune_mode;
	assert(w->tune_mode == tune_mode_t::NORMAL || w->tune_mode == tune_mode_t::BLIND);
	int heartbeat_interval = 0;
	return cmdseq.tune(fefd, heartbeat_interval);
}

int dvb_frontend_t::tune_(const chdb::dvbt_mux_t& mux, const tune_options_t& tune_options) {
	// Clear old tune_mux_confirmation info
	this->clear_lock_status();
	this->reset_tuned_mux_tune_confirmation();
	bool blindscan = tune_options.use_blind_tune;

	dtdebug("Tuning adapter [ " << (int) adapter_no << "] DVB-T to " << mux << (blindscan ? "BLIND" : ""));

	current_delsys_type = chdb::delsys_type_t::DVB_T;
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

	if (((int)mux.delivery_system != SYS_DVBT) && ((int)mux.delivery_system != SYS_DVBT2)) {
		dterror("Unsupported delivery system");
		return -1;
	}
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)mux.delivery_system);

	cmdseq.add(DTV_FREQUENCY, mux.frequency * 1000); // For DVB-T, it is measured in Hz.
	cmdseq.add(DTV_BANDWIDTH_HZ, dvbt_bandwidth);
	cmdseq.add(DTV_CODE_RATE_HP, mux.HP_code_rate);
	cmdseq.add(DTV_CODE_RATE_LP, mux.LP_code_rate);
	cmdseq.add(DTV_MODULATION, mux.modulation);
	cmdseq.add(DTV_GUARD_INTERVAL, mux.guard_interval);
	cmdseq.add(DTV_TRANSMISSION_MODE, mux.transmission_mode);
	cmdseq.add(DTV_HIERARCHY, mux.hierarchy);

	if (mux.k.stream_id >= 0)
		cmdseq.add(DTV_STREAM_ID, mux.k.stream_id);

	auto w = ts.writeAccess();
	w->tune_count++;
	auto fefd = w->fefd;
	w->use_blind_tune = tune_options.use_blind_tune;
	dtdebugx("change tune mode on adapter %d from %d to %d",
					 (int) adapter_no, (int) w->tune_mode, (int) tune_options.tune_mode);
	w->tune_mode = tune_options.tune_mode;
	int heartbeat_interval = 0;
	return cmdseq.tune(fefd, heartbeat_interval);
}


template<typename mux_t>
int dvb_frontend_t::tune(const mux_t& mux, const tune_options_t& tune_options, bool user_requested) {
	{
		auto w = this->ts.writeAccess();
		w->tune_options = tune_options;
	}
	if (user_requested) {
		this->start_fe_and_dvbc_or_dvbt_mux(mux);
	} else
		this->sec_status.retune_count++;

	/*
		open frontend if it is not yet open
	*/

	auto muxname = chdb::to_str(mux);
	dtdebug("Tuning to DVBC/DVBT mux " << muxname.c_str());
	auto ret = this->tune_(mux, tune_options);
	if (ret < 0)
		return ret;
	return 0;
}


int dvb_frontend_t::start_lnb_spectrum_scan(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																						const tune_options_t& tune_options) {
	this->num_constellation_samples = 0;
	using namespace chdb;
	using namespace devdb;
	auto& options= tune_options.spectrum_scan_options;
	auto *conn = connection_for_rf_path(lnb, rf_path);
	assert(conn);
	this->clear_lock_status();
	{
		auto w = this->ts.writeAccess();
		w->tune_options = tune_options;
	}
	auto lnb_voltage = (fe_sec_voltage_t) devdb::lnb::voltage_for_pol(lnb, options.band_pol.pol);

	fe_sec_tone_mode_t tone = (options.band_pol.band == fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;

	// request spectrum scan
	cmdseq_t cmdseq;
	cmdseq.add(DTV_DELIVERY_SYSTEM, (int)SYS_DVBS);
	auto [start_freq, mid_freq, end_freq, lof_low, lof_high, inverted_spectrum] =
		devdb::lnb::band_frequencies(lnb, options.band_pol.band);
	start_freq = std::max(options.start_freq, start_freq);
	end_freq = std::min(options.end_freq, end_freq);
	switch (options.band_pol.band) {
	case devdb::fe_band_t::LOW:
		end_freq = std::min(options.end_freq, mid_freq);
		break;
	case devdb::fe_band_t::HIGH:
		start_freq = std::max(options.start_freq, mid_freq);
		break;
	default:
		assert(0);
	}
	assert(start_freq <= end_freq);

	dtdebug("Spectrum acquisition on lnb  " << lnb << " diseqc: lnb_id=" << lnb << " " <<
					conn->tune_string << " range=["
					<< start_freq << ", " << end_freq << "]");
	start_freq = devdb::lnb::driver_freq_for_freq(lnb, start_freq);
	end_freq = devdb::lnb::driver_freq_for_freq(lnb, end_freq - 1) + 1;
	if (start_freq > end_freq)
		std::swap(start_freq, end_freq); //e.g., this occurs for C-band (spectrum inversion)
	cmdseq.add(DTV_SCAN_START_FREQUENCY, start_freq);
	cmdseq.add(DTV_SCAN_END_FREQUENCY, end_freq);

	cmdseq.add(DTV_SCAN_RESOLUTION, options.resolution); // in kHz
	cmdseq.add(DTV_SCAN_FFT_SIZE, options.fft_size);		 // in kHz
	cmdseq.add(DTV_SYMBOL_RATE, 2000 * 1000);						 // controls tuner bandwidth (in Hz)

	auto w = ts.writeAccess();
	auto fefd = w->fefd;

	if ((ioctl(fefd, FE_SET_VOLTAGE, lnb_voltage))) {
		dterror("problem Setting the Voltage");
		return -1;
	}
	if (ioctl(fefd, FE_SET_TONE, tone) < 0) {
		dterror("problem Setting the Tone back");
		return -1;
	}
	while (1) {
		struct dvb_frontend_event event {};
		if (ioctl(fefd, FE_GET_EVENT, &event) < 0)
			break;
	}
	auto ret = cmdseq.spectrum(fefd, options.spectrum_method);

	w->tune_mode = tune_mode_t::SPECTRUM;
	w->tune_options.spectrum_scan_options = options;
	dtdebugx("tune: spectrum acquisition started ret=%d", ret);
	return ret;
}

void dvb_frontend_t::start_frontend_monitor() {
	assert(!monitor_thread.get()); //monitor_thread  should not be running
	auto self = shared_from_this();
	monitor_thread = fe_monitor_thread_t::make(adaptermgr->receiver, self);
}

void dvb_frontend_t::stop_frontend_monitor_and_wait() {
	if(monitor_thread)
		monitor_thread->stop_running(true);
}

devdb::usals_location_t dvb_frontend_t::get_usals_location() const {
	auto r = adaptermgr->receiver.options.readAccess();
	return r->usals_location;
}



int dvb_frontend_t::start_fe_and_lnb(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb) {
	// auto reservation_type = dvb_adapter_t::reservation_type_t::mux;
	int ret = 0;
	{
		this->sec_status.retune_count = 0;
		auto w = ts.writeAccess();
		w->reserved_mux = {};
		w->reserved_rf_path = rf_path;
		w->reserved_lnb = lnb;
		w->last_signal_info.reset();
	}
	if(!monitor_thread.get()) {
		start_frontend_monitor();
	} else {
		assert(this->ts.readAccess()->fefd >= 0);
	}
	return ret;
}

int dvb_frontend_t::start_fe_lnb_and_mux(const devdb::rf_path_t& rf_path, const devdb::lnb_t& lnb,
																				 const chdb::dvbs_mux_t& mux) {
	// auto reservation_type = dvb_adapter_t::reservation_type_t::mux;
		assert((chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::ACTIVE &&
					chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::PENDING &&
						chdb::mux_common_ptr(mux)->scan_status != chdb::scan_status_t::RETRY) ||
				 chdb::mux_common_ptr(mux)->scan_id >0);

	int ret = 0;
	{
		this->sec_status.retune_count = 0;
		auto w = this->ts.writeAccess();
		w->reserved_mux = mux;
		w->reserved_rf_path = rf_path;
		w->reserved_lnb = lnb;
		w->last_signal_info.reset();
	}
	if (!monitor_thread.get()) {
		start_frontend_monitor();
	} else {
		assert(this->ts.readAccess()->fefd >= 0);
	}
	return ret;
}




template<typename mux_t>
int dvb_frontend_t::start_fe_and_dvbc_or_dvbt_mux(const mux_t& mux) {
	{
		auto w = this->ts.writeAccess();
		this->sec_status.retune_count = 0;
		w->reserved_mux = mux;
		w->reserved_rf_path = devdb::rf_path_t();
		w->reserved_lnb = devdb::lnb_t();
		w->	last_signal_info.reset();
	}
	if (!monitor_thread.get()) {
		start_frontend_monitor();
	} else {
		assert(this->ts.readAccess()->fefd >= 0);
	}
	return 0;
}

int dvb_frontend_t::reset_ts() {
	auto w = ts.writeAccess();
	auto saved = w->dbfe;
	*w = {};
	w->dbfe = saved;
	return 0;
}

int dvb_frontend_t::release_fe() {
	dtdebugx("releasing frontend_monitor: fefd=%d", this->ts.readAccess()->fefd);
	if (monitor_thread.get()) {
		stop_frontend_monitor_and_wait();
		monitor_thread.reset();
	}
	{
		dtdebugx("release_fe: change tune mode on adapter %d: clear from %d", (int)adapter_no,
						 (int) this->ts.readAccess()->tune_mode);
		this->reset_ts();
		this->signal_monitor.assign({});
		this->sec_status = {};
	}
	return 0;
}


/** @brief Send a diseqc message and also control band/polarisation in the right order*
		DiSEqC 1.0, which allows switching between up to 4 satellite sources
		DiSEqC 1.1, which allows switching between up to 16 sources
		DiSEqC 1.2, which allows switching between up to 16 sources, and control of a single axis satellite motor
		DiSEqC 1.3, Usals
		DiSEqC 2.0, which adds bi-directional communications to DiSEqC 1.0
		DiSEqC 2.1, which adds bi-directional communications to DiSEqC 1.1
		DiSEqC 2.2, which adds bi-directional communications to DiSEqC 1.2

		Diseqc string:
		M = mini_diseqc
		C = committed   = 1.0
		U = uncommitted = 1.1
		X = goto position = 1.2
		P = positoner  = 1.3 = usals
		" "= 50 ms pause

		Returns <0 on error, 0 of no diseqc command was sent, 1 if at least 1 diseqc command was sent
*/
std::tuple<int ,int>
dvb_frontend_t::diseqc(bool skip_positioner) {

	auto [fefd, rf_path, lnb, lnb_connection, mux] = [this]() {
		auto r = this->ts.readAccess();
		const auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&r->reserved_mux);
		assert(dvbs_mux);
		auto *conn = connection_for_rf_path(r->reserved_lnb, r->reserved_rf_path);
		assert(conn);
		return std::make_tuple(r->fefd, r->reserved_rf_path, r->reserved_lnb, *conn, *dvbs_mux);
	}();

	if(!this->need_diseqc(rf_path, lnb))
		return {0, sat_pos_none};

	auto & diseqc_command = lnb_connection.tune_string;

	bool lnb_only = mux.k.sat_pos == sat_pos_none;
	int new_usals_sat_pos{sat_pos_none};

	auto can_move_dish_ = devdb::lnb::can_move_dish(lnb_connection);

	int ret{0};
	int i = 0;
	bool must_pause = false; // do we need a long pause before the next diseqc command?
	for (const char& command : diseqc_command) {
		bool repeated = false;
		for (auto j = 0; j < i - 1; ++j) {
			if (diseqc_command[j] == command) {
				repeated = true;
				break;
			}
		}

		switch (command) {
		case 'M': {

			if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return {-1, new_usals_sat_pos};
			msleep(must_pause ? 200 : 30);
			/*
				tone burst commands deal with simpler equipment.
				They use a 12.5 ms duration 22kHz burst for transmitting a 1
				and 9 shorter bursts within a 12.5 ms interval for a 0
				for an on signal.
				They allow switching between two satelites only
			*/
			auto b = std::min(lnb_connection.diseqc_mini, (uint8_t)1);
			ret = ioctl(fefd, FE_DISEQC_SEND_BURST, b);
			if (ret < 0) {
				dterror("problem sending the Tone Burst");
			}
			must_pause = !repeated;
		} break;
		case 'C': {
			// committed
			auto diseqc_10 = lnb_connection.diseqc_10;
			if (diseqc_10 < 0)
				break; // can be used to signal that it is off
			if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return {-1, new_usals_sat_pos};
			msleep(must_pause ? 200 : 30);
			unsigned int extra{0};
			if(!lnb_only) {
				int pol_v_r = ((int)mux.pol & 1);
				extra = ((pol_v_r * 2) | (devdb::lnb::band_for_mux(lnb, mux) ==  devdb::fe_band_t::HIGH ? 1 : 0));
			}
			ret = this->send_diseqc_message('C', diseqc_10 * 4, extra, repeated);
			if (ret < 0) {
				dterror("Sending Committed DiseqC message failed");
			}
			must_pause = !repeated;
		} break;
		case 'U': {
			auto diseqc_11 = lnb_connection.diseqc_11;
			if (diseqc_11 < 0)
				break; // can be used to signal that it is off
			// uncommitted
			if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return {-1, new_usals_sat_pos};

			msleep(must_pause ? 200 : 30);
			ret = this->send_diseqc_message('U', diseqc_11, 0, repeated);
			if (ret < 0) {
				dterror("Sending Uncommitted DiseqC message failed");
			}
			must_pause = !repeated;
		} break;
		case 'X': {
			if (skip_positioner || !can_move_dish_)
				break;
			if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return {-1, new_usals_sat_pos};
			msleep(must_pause ? 200 : 30);
			if (!lnb_only) {
				auto* lnb_network = (!lnb_only) ? devdb::lnb::get_network(lnb, mux.k.sat_pos) : nullptr;
				if (!lnb_network) {
					dterror("No network found");
				} else {// this is not usals!
					new_usals_sat_pos =lnb_network->sat_pos;
				}

				ret = this->send_diseqc_message('X', lnb_network->diseqc12, 0, repeated);
				if (ret < 0) {
					dterror("Sending Committed DiseqC message failed");
				}
			}
			must_pause = !repeated;
		} break;
		case 'P': {
			if (skip_positioner || !can_move_dish_)
				break;
			if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return {-1, new_usals_sat_pos};
			msleep(must_pause ? 200 : 30);
			int16_t usals_pos{sat_pos_none};
			if(!lnb_only) {
				auto* lnb_network = devdb::lnb::get_network(lnb, mux.k.sat_pos);
				if (!lnb_network) {
					dterror("No network found");
				} else {
					usals_pos = lnb_network->usals_pos;
					new_usals_sat_pos = usals_pos;
				}
			} else { //lnb only
				usals_pos = lnb.usals_pos;
			}
			if(usals_pos != sat_pos_none) {
				ret = this->send_positioner_message(devdb::positioner_cmd_t::GOTO_XX, usals_pos, repeated);
			} else
				ret = -1;
			if (ret < 0) {
				dterror("Sending Committed DiseqC message failed");
			}
			must_pause = !repeated;
		} break;
		case ' ': {
			msleep(50);
			must_pause = false;
		} break;
		}
		if (ret < 0)
			return {ret, new_usals_sat_pos};
	}

	msleep(20);
	return {1, new_usals_sat_pos};
}


/** @brief generate and sent the digital satellite equipment control "message",
 * specification is available from http://www.eutelsat.com/
 * See update_recomm_for_implim-1.pdf p. 9
 * This function will set the LNB voltage and the 22kHz tone. If a satellite switching is asked
 * it will send a diseqc message
 *
 * @param fd : the file descriptor of the frontend
 * @param diseqc10: diseqc10 port number (1 to 4, or 0 if nothing is to be sent)
 * @param diseqc11: diseqc11 port number (1 to ..., or 0 if nothing is to be sent)
 * @param pol_v_r : 1 : vertical or circular right, 0 : horizontal or circular left
 * @param hi_lo : the band for a dual band lnb
 * @param lnb_voltage_off : if one, force the 13/18V voltage to be 0 independantly of polarisation
 */
std::tuple<int,int> dvb_frontend_t::do_lnb_and_diseqc(devdb::fe_band_t band, fe_sec_voltage_t lnb_voltage) {
	/*

		22KHz: off = low band; on = high band
		13V = vertical or right-hand  18V = horizontal or low-hand
		TODO: change this to 18 Volt when using positioner
	*/

	dtdebugx("SENDING diseqc: retune_count=%d mode=%d", this->sec_status.retune_count,
					 this->ts.readAccess()->tune_options.subscription_type);

	auto fefd = this->ts.readAccess()->fefd;
	this->sec_status.set_voltage(fefd, lnb_voltage);

	// Note: the following is a NOOP in case no diseqc needs to be sent
	auto [ ret, new_usals_sat_pos] = diseqc(false /*skip_positioner*/);
	if (ret < 0)
		return {ret, new_usals_sat_pos};

	/*select the proper lnb band
		22KHz: off = low band; on = high band
	*/

	fe_sec_tone_mode_t tone = (band == devdb::fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;
	if (this->sec_status.set_tone(fefd, tone)<0) {
		return {-1, new_usals_sat_pos};
	}
	msleep(20);
	return {0, new_usals_sat_pos};
}

int sec_status_t::set_tone(int fefd, fe_sec_tone_mode mode) {
	if ((int)mode < 0) {
		assert(0);
		return -1;
	}
	if (mode == tone) {
		dtdebugx("No tone change needed: v=%d", mode);
		return 0;
	}
	tone = mode;
	dtdebugx("Setting tone: v=%d", mode);
	if (ioctl(fefd, FE_SET_TONE, mode) < 0 ) {
		dterrorx("problem setting tone=%d", mode);
		return -1;
	}
	return 1;
}

int sec_status_t::set_voltage(int fefd, fe_sec_voltage v) {
	if ((int)v < 0) {
		assert(0);
		return -1;
	}
	if (v == voltage) {
		dtdebugx("No voltage change needed: v=%d", v);
		return 0;
	} else {
		dtdebugx("Changing voltage from : v=%d to v=%d", voltage, v);
	}
	bool must_sleep = (voltage == SEC_VOLTAGE_OFF || voltage  <0);

	/*
		With an Amilko positioner, the positioner risks activating its current overlaod protection at startup,
		even if the motor is not moving, because of teh current the rotor passess through for lnb and potentially
		a switch. This problem is worse when the highest voltage is selected from a non-powered state.
		The following increases the voltage in two phase when 18V is requested. First 12V is selected; then we
		wait for the devises to start up (using less current and hopefully not triggering the current overload detection,
		Then we increase the voltage to the required one)
	 */
	if (voltage < 0 && v == SEC_VOLTAGE_18) {
		if (ioctl(fefd, FE_SET_VOLTAGE, SEC_VOLTAGE_13) < 0) {
			dterrorx("problem setting voltage %d", voltage);
			return -1;
		}
		dtdebug("sleeping extra at startup");
		msleep(200);
	}

	voltage = v;

	if (ioctl(fefd, FE_SET_VOLTAGE, voltage) < 0) {
		dterrorx("problem setting voltage %d", voltage);
		return -1;
	}
	//allow some time for the voltage on the equipment to stabilise before continuing
	if(must_sleep) {
		msleep(200);
	}

	return 1;
}



/*
	determine if we need to send a diseqc command and/or if we need to set voltage/tone on lnb
	always send diseqc if tuning has failed unless when lnb_use_count>1

	returns need_diseqc, need_lnb
	These can be {true, true}, {false, true} or {false, false}

*/
std::tuple<bool,bool>
dvb_frontend_t::need_diseqc_or_lnb(const devdb::rf_path_t& new_rf_path, const devdb::lnb_t& new_lnb,
																	 const chdb::dvbs_mux_t& new_mux,
																	 const devdb::resource_subscription_counts_t& use_counts) {
	if (!this->sec_status.is_tuned() && use_counts.rf_path == 1 && use_counts.dish <=1
			&& use_counts.rf_coupler<=1)
		return {true, true}; // always send diseqc if we were not tuned
#if 0
	bool is_dvbs =
		((int)new_mux.delivery_system == SYS_DVBS || new_mux.delivery_system == (chdb::fe_delsys_dvbs_t)SYS_DVBS2);
	if (!is_dvbs)
		return {false, false};
#endif
	if(use_counts.rf_path > 1 || use_counts.rf_coupler > 1) {
		dtdebugx("Preventing diseqc because rf_path is used more than once: use_count=%d", use_counts.rf_path);
		return {false, false};
	}
	auto r = this->ts.readAccess();
	if (new_lnb.k != r->reserved_lnb.k)
		return {true, true};
	if (!devdb::lnb::on_positioner(new_lnb))
		return {false, true};
	auto *conn = connection_for_rf_path(new_lnb, new_rf_path);
	assert(conn);
	bool active_rotor = (conn->rotor_control == devdb::rotor_control_t::ROTOR_MASTER_USALS ||
											 conn->rotor_control == devdb::rotor_control_t::ROTOR_MASTER_DISEQC12);
	if (!active_rotor)
		return {false, true};
	return {chdb::mux_key_ptr(r->reserved_mux)->sat_pos != new_mux.k.sat_pos, true};
}

bool dvb_frontend_t::need_diseqc(const devdb::rf_path_t& new_rf_path, const devdb::lnb_t& new_lnb) {
	if (!this->sec_status.is_tuned())
		return true; // always send diseqc if we were not tuned
	auto r = this->ts.readAccess();
	return (new_lnb.k != r->reserved_lnb.k || new_rf_path != r->reserved_rf_path);
}

int dvb_frontend_t::do_lnb(devdb::fe_band_t band, fe_sec_voltage_t lnb_voltage) {
	/*

		22KHz: off = low band; on = high band
		13V = vertical or right-hand  18V = horizontal or low-hand
		TODO: change this to 18 Volt when using positioner
	*/

	auto fefd = this->ts.readAccess()->fefd;

	this->sec_status.set_voltage(fefd, lnb_voltage);
	/*select the proper lnb band
		22KHz: off = low band; on = high band
	*/

	fe_sec_tone_mode_t tone = (band == devdb::fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;
	if (this->sec_status.set_tone(fefd, tone)<0) {
			dterror("problem Setting the Tone back");
			return -1;
	}
	return 0;
}


int dvb_frontend_t::positioner_cmd(devdb::positioner_cmd_t cmd, int par) {
	if (!devdb::lnb::can_move_dish(ts.readAccess()->reserved_lnb_connection()))
		return -1;

	/*
		turn off tone to not interfere with diseqc
	*/
	auto fefd = this->ts.readAccess()->fefd;
	auto old_tone = this->sec_status.get_tone();
	auto old_voltage = this->sec_status.get_voltage();


	if(old_voltage<0 /*unknown*/ || old_voltage == SEC_VOLTAGE_OFF)
		this->sec_status.set_voltage(fefd, SEC_VOLTAGE_18);
	//turn tone off to send command
	if (this->sec_status.set_tone(fefd, SEC_TONE_OFF) < 0)
		return -1;
	msleep(15);

	auto ret = this->send_positioner_message(cmd, par);
	if (old_voltage >=0 && /* avoid the case where old voltage was "unknown" */
			this->sec_status.set_voltage(fefd, old_voltage) < 0)
		return -1;
	if (old_tone >=0 && /* avoid the case where old mode was "unknown" */
			this->sec_status.set_tone(fefd, old_tone) < 0)
		return -1;
	return ret;
}


//instantiations
template bool dvb_frontend_t::is_tuned_to(const chdb::dvbs_mux_t& mux,
																					const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::dvbc_mux_t& mux,
																					const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::dvbt_mux_t& mux,
																					const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid) const;
template bool dvb_frontend_t::is_tuned_to(const chdb::any_mux_t& mux,
																					const devdb::rf_path_t* required_rf_path, bool ignore_t2mi_pid) const;

template int dvb_frontend_t::start_fe_and_dvbc_or_dvbt_mux<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux);
template int dvb_frontend_t::start_fe_and_dvbc_or_dvbt_mux<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux);

template int dvb_frontend_t::tune<chdb::dvbc_mux_t>(const chdb::dvbc_mux_t& mux, const tune_options_t& tune_options,
																										bool user_requested);
template int dvb_frontend_t::tune<chdb::dvbt_mux_t>(const chdb::dvbt_mux_t& mux, const tune_options_t& tune_options,
																										bool user_requested);
