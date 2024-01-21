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
#include "CLI/CLI.hpp"
#include "neumofrontend.h"
#include <algorithm>
#include <cassert>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <linux/dvb/version.h>
#include <linux/limits.h>
#include <pthread.h>
#include <regex>
#include <resolv.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <values.h>

dtv_fe_constellation_sample samples[65536];

int tune_it(int fefd, int frequency_, bool pol_is_v);
int do_lnb_and_diseqc(int fefd, int frequency, bool pol_is_v);
int tune(int fefd, int frequency, bool pol_is_v);


static constexpr int make_code(int pls_mode, int pls_code, int timeout = 0) {
	return (timeout & 0xff) | ((pls_code & 0x3FFFF) << 8) | (((pls_mode)&0x3) << 26);
}

static constexpr int lnb_universal_slof = 11700 * 1000UL;
static constexpr int lnb_universal_lof_low = 9750 * 1000UL;
static constexpr int lnb_universal_lof_high = 10600 * 1000UL;
static constexpr int lnb_wideband_lof = 10400 * 1000UL;
static constexpr int lnb_wideband_uk_lof = 10410 * 1000UL;
static constexpr int lnb_c_lof = 5150 * 1000UL;

enum blindscan_method_t {
	SCAN_SWEEP = 1, // old method: steps to the frequency bands and starts a blindscan tune
	SCAN_FFT,		 // scans for rising and falling frequency peaks and launches blindscan there
};

enum lnb_type_t {
	UNIVERSAL_LNB = 1,
	WIDEBAND_LNB,
	WIDEBAND_UK_LNB,
	C_LNB,
};

enum class command_t : int {
	TUNE,
	IQ};

enum class algo_t : int {
	BLIND,
	WARM,
	COLD};

/* resolution = 500kHz : 60s
	 resolution = 1MHz : 31s
	 resolution = 2MHz : 16s
*/
struct options_t {
	command_t command{command_t::TUNE};
	algo_t algo{algo_t::BLIND};
	lnb_type_t lnb_type{UNIVERSAL_LNB};
	int search_range{10000}; // in kHz
	int freq{-1};
	int symbol_rate{-1}; // in kHz
	fe_modulation modulation{PSK_8};
	fe_delivery_system delivery_system{SYS_DVBS2};
	int pol = 0;

	std::string filename_pattern{"/tmp/%s_a%d_%.3f%c.dat"};
	std::string pls;
	std::vector<uint32_t> pls_codes = {
		//In use on 5.0W
		make_code(0, 16416),
		make_code(0, 8),
		make_code(1, 121212),
		make_code(1, 262140),
		make_code(1, 50416)
		};
	int start_pls_code{-1};
	int end_pls_code{-1};
	int pls_search_timeout{25}; // in ms
	int adapter_no{0};
	int rf_in{-1};
	int frontend_no{0};
	std::string diseqc{"UC"};
	int uncommitted{-1};
	int committed{-1};
	int num_samples{1024};
	int32_t stream_id{-1}; //

	options_t() = default;
	void parse_pls(const std::vector<std::string>& pls_entries);
	int parse_options(int argc, char** argv);
};

options_t options;

int band_for_freq(int32_t frequency)		{
	switch(options.lnb_type) {
	case UNIVERSAL_LNB:
		if (frequency < lnb_universal_slof) {
			return  0;
		} else {
			return 1;
		}
		break;

	case WIDEBAND_LNB:
		return 0;
		break;

	case WIDEBAND_UK_LNB:
		return 0;
		break;

	case C_LNB:
		return 0;
		break;
	}
	return 0;
}

int32_t driver_freq_for_freq(int32_t frequency)		{
	switch(options.lnb_type) {
	case UNIVERSAL_LNB:
		if (frequency < lnb_universal_slof) {
			return  frequency - lnb_universal_lof_low;
		} else {
			return frequency - lnb_universal_lof_high;
		}
		break;

	case WIDEBAND_LNB:
		return frequency - lnb_wideband_lof;
		break;
	case WIDEBAND_UK_LNB:
		return frequency - lnb_wideband_uk_lof;
		break;

	case C_LNB:
		return lnb_c_lof - frequency;
		break;
	}
	return frequency;
}

uint32_t freq_for_driver_freq(int32_t frequency, int band)		{
	switch(options.lnb_type) {
	case UNIVERSAL_LNB:
		if (!band) {
			return  frequency + lnb_universal_lof_low;
		} else {
			return frequency + lnb_universal_lof_high;
		}
		break;
	case WIDEBAND_LNB:
		return frequency + lnb_wideband_lof;
		break;
	case WIDEBAND_UK_LNB:
		return frequency + lnb_wideband_uk_lof;
		break;
	case C_LNB:
		return lnb_c_lof - frequency;
		break;
	}
	return frequency;
}

void options_t::parse_pls(const std::vector<std::string>& pls_entries) {
	const std::regex base_regex("(ROOT|GOLD|COMBO)\\+([0-9]{1,6})");
	std::smatch base_match;
	for (auto m : pls_entries) {
		int mode;
		int code;
		bool inited = false;
		if (std::regex_match(m, base_match, base_regex)) {
			// The first sub_match is the whole string; the next
			// sub_match is the first parenthesized expression.
			if (base_match.size() >= 2) {
				std::ssub_match base_sub_match = base_match[1];
				auto mode_ = base_sub_match.str();
				if (!mode_.compare("ROOT"))
					mode = 0;
				else if (!mode_.compare("GOLD"))
					mode = 1;
				else if (!mode_.compare("COMBO"))
					mode = 2;
				else {
					printf("mode=/%s/\n", mode_.c_str());
					throw std::runtime_error("Invalid PLS mode");
				}
			}
			if (base_match.size() >= 3) {
				std::ssub_match base_sub_match = base_match[2];
				auto code_ = base_sub_match.str();
				if (sscanf(code_.c_str(), "%d", &code) != 1)
					throw std::runtime_error("Invalid PLS code");
			}
			if (!inited) {
				pls_codes.clear();
				inited = true;
			}
			pls_codes.push_back(make_code(mode, code));
		}
		printf(" %d:%d", mode, code);
	}
	printf("\n");
}

std::map<std::string, fe_delivery_system> delsys_map{
	{"DVBC", SYS_DVBC_ANNEX_A}, {"DVBT", SYS_DVBT},			{"DSS", SYS_DSS},				{"DVBS", SYS_DVBS},
	{"DVBS2", SYS_DVBS2},				{"DVBH", SYS_DVBH},			{"ISDBT", SYS_ISDBT},		{"ISDBS", SYS_ISDBS},
	{"ISDBC", SYS_ISDBC},				{"ATSC", SYS_ATSC},			{"ATSCMH", SYS_ATSCMH}, {"DTMB", SYS_DTMB},
	{"CMMB", SYS_CMMB},					{"DAB", SYS_DAB},				{"DVBT2", SYS_DVBT2},		{"TURBO", SYS_TURBO},
	{"DVBC2", SYS_DVBC2},				{"DVBS2X", SYS_DVBS2X}, {"DCII", SYS_DCII},			{"AUTO", SYS_AUTO}};

std::map<std::string, fe_modulation> modulation_map{
	{"QPSK", QPSK},						{"QAM_16", QAM_16},				{"QAM_32", QAM_32},			 {"QAM_64", QAM_64},
	{"QAM_128", QAM_128},			{"QAM_256", QAM_256},			{"QAM_AUTO", QAM_AUTO},	 {"VSB_8", VSB_8},
	{"VSB_16", VSB_16},				{"PSK_8", PSK_8},					{"APSK_16", APSK_16},		 {"APSK_32", APSK_32},
	{"DQPSK", DQPSK},					{"QAM_4_NR", QAM_4_NR},		{"C_QPSK", C_QPSK},			 {"I_QPSK", I_QPSK},
	{"Q_QPSK", Q_QPSK},				{"C_OQPSK", C_OQPSK},			{"QAM_512", QAM_512},		 {"QAM_1024", QAM_1024},
	{"QAM_4096", QAM_4096},		{"APSK_64", APSK_64},			{"APSK_128", APSK_128},	 {"APSK_256", APSK_256},
	{"APSK_8L", APSK_8L},			{"APSK_16L", APSK_16L},		{"APSK_32L", APSK_32L},	 {"APSK_64L", APSK_64L},
	{"APSK_128L", APSK_128L}, {"APSK_256L", APSK_256L}, {"APSK_1024", APSK_1024}};

int options_t::parse_options(int argc, char** argv) {
	CLI::App app{"DVB tuning program"};
	std::map<std::string, command_t>
		command_map{{"tune", command_t::TUNE},
		{"iq", command_t::IQ}
	};
	std::map<std::string, algo_t>
		algo_map{{"blind", algo_t::BLIND},
						 {"warm", algo_t::WARM},
						 {"cold", algo_t::COLD}
	};
	std::map<std::string, int> pol_map{{"V", 2}, {"H", 1}};
	std::map<std::string, int> pls_map{{"ROOT", 0}, {"GOLD", 1}, {"COMBO", 1}};
	std::vector<std::string> pls_entries;

	app.add_option("-c,--command", command, "Command to execute", true)
		->transform(CLI::CheckedTransformer(command_map, CLI::ignore_case));

	app.add_option("-A,--algo", algo, "Algorithm for tuning", true)
		->transform(CLI::CheckedTransformer(algo_map, CLI::ignore_case));

	app.add_option("-a,--adapter", adapter_no, "Adapter number", true);
	app.add_option("-r,--rf-in", rf_in, "RF input", true);
	app.add_option("--frontend", frontend_no, "Frontend number", true);

	app.add_option("-S,--symbol-rate", symbol_rate, "Symbolrate (kHz)", true);
	app.add_option("-m,--modulation", modulation, "modulation", true)
		->transform(CLI::CheckedTransformer(modulation_map, CLI::ignore_case));
	app.add_option("--delsys", delivery_system, "Delivery system", true)
		->transform(CLI::CheckedTransformer(delsys_map, CLI::ignore_case));

	app.add_option("-R,--search-range", search_range, "Search range (kHz)", true);

	app.add_option("-p,--pol", pol, "Polarization to scan", true)
		->transform(CLI::CheckedTransformer(pol_map, CLI::ignore_case));

	app.add_option("-n,--num-samples", num_samples, "Number of IQ samples to fetch", true);
	app.add_option("-f,--frequency", freq, "Frequency to tune for getting IQ samples", true);
	app.add_option("-s,--stream-id", stream_id, "stream_id to select", true);

	app.add_option("--pls-code", pls_entries, "PLS mode (ROOT, GOLD, COMBO) and code (number) to scan, separated by +",
								 true);
	app.add_option("--start-pls-code", start_pls_code, "Start of PLS code range to start (mode=ROOT!)", true);
	app.add_option("--end-pls-code", end_pls_code, "End of PLS code range to start (mode=ROOT!)", true);
	app.add_option("-T,--pls-search-timeout", pls_search_timeout, "Search range timeout", true);

	app.add_option("-d,--diseqc", diseqc,
								 "DiSEqC command string (C: send committed command; "
								 "U: send uncommitted command",
								 true);
	app.add_option("-U,--uncommitted", uncommitted, "Uncommitted switch number (lowest is 0)", true);
	app.add_option("-C,--committed", committed, "Committed switch number (lowest is 0)", true);

	try {
		app.parse(argc, argv);
	} catch (const CLI::ParseError& e) {
		app.exit(e);
		return -1;
	}
	if (options.freq < 4800000)
		options.lnb_type = C_LNB;
	parse_pls(pls_entries);
	printf("adapter=%d\n", adapter_no);
	printf("rf_in=%d\n", rf_in);
	printf("frontend=%d\n", frontend_no);

	printf("freq=%d\n", freq);

	printf("pol=%d\n", pol);
	assert(symbol_rate < 0 || symbol_rate <= 60000);
	printf("pls_codes[%ld]={ ", pls_codes.size());
	for (auto c : pls_codes)
		printf("%d, ", c);
	printf("}\n");

	printf("diseqc=%s: U=%d C=%d\n", diseqc.c_str(), uncommitted, committed);

	return 0;
}

static int epoll_timeout = 5000000; // in ms

//#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

/** @brief Print the status
 * Print the status contained in festatus, this status says if the card is lock, sync etc.
 *
 * @param festatus the status to display
 */
void print_tuner_status(fe_status_t festatus) {
	// printf("FE_STATUS:");
	if (festatus & FE_HAS_SIGNAL)
		printf("     FE_HAS_SIGNAL : found something above the noise level");
	if (festatus & FE_HAS_CARRIER)
		printf("     FE_HAS_CARRIER : found a DVB signal");
	if (festatus & FE_HAS_VITERBI)
		printf("     FE_HAS_VITERBI : FEC is stable");
	if (festatus & FE_HAS_SYNC)
		printf("     FE_HAS_SYNC : found sync bytes");
	if (festatus & FE_HAS_LOCK)
		printf("     FE_HAS_LOCK : everything's working...");
	if (festatus & FE_TIMEDOUT)
		printf("     FE_TIMEDOUT : no lock within the last about 2 seconds");
	if (festatus & (FE_HAS_TIMING_LOCK|FE_OUT_OF_RESOURCES))
		printf("     FE_REINIT : frontend has timing loop locked");
	printf("---");
}

int check_lock_status(int fefd) {
	fe_status_t status;
	while (ioctl(fefd, FE_READ_STATUS, &status) < 0) {
		if (errno == EINTR) {
			continue;
		}
		printf("FE_READ_STATUS: %s", strerror(errno));
		return -1;
	}
	bool signal = status & FE_HAS_SIGNAL;
	bool carrier = status & FE_HAS_CARRIER;
	bool viterbi = status & FE_HAS_VITERBI;
	bool has_sync = status & FE_HAS_SYNC;
	bool has_lock = status & FE_HAS_LOCK;
	bool timedout = status & FE_TIMEDOUT;

	printf("\tFE_READ_STATUS: stat=%d, signal=%d carrier=%d viterbi=%d sync=%d timedout=%d locked=%d\n", status, signal,
				 carrier, viterbi, has_sync, timedout, has_lock);

	return status & FE_HAS_LOCK;
}

/** The structure for a diseqc command*/
struct diseqc_cmd {
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};

/** @brief Wait msec miliseconds
 */
static inline void msleep(uint32_t msec) {
	struct timespec req = {msec / 1000, 1000000 * (msec % 1000)};
	while (nanosleep(&req, &req))
		;
}

#define FREQ_MULT 1000

#define CBAND_LOF 5150

void save_constellation_samples(bool pol_is_v, struct dtv_fe_constellation& cs);

std::tuple<int, int> getinfo(FILE* fpout, int fefd, bool pol_is_v, int allowed_freq_min,
														 int lo_frequency) {

	bool get_constellation = options.command == command_t::IQ;
	auto num_constellation_samples = options.num_samples;

	struct dtv_property p[] = {
		{.cmd = DTV_DELIVERY_SYSTEM}, // 0 DVB-S, 9 DVB-S2
		{.cmd = DTV_FREQUENCY},
		{.cmd = DTV_VOLTAGE}, // 0 - 13V Vertical, 1 - 18V horizontal, 2 - Voltage OFF
		{.cmd = DTV_SYMBOL_RATE},
		{.cmd = DTV_STAT_SIGNAL_STRENGTH},
		{.cmd = DTV_STAT_CNR},
		{.cmd = DTV_MODULATION}, // 5 - QPSK, 6 - 8PSK
		{.cmd = DTV_INNER_FEC},
		{.cmd = DTV_INVERSION},
		{.cmd = DTV_ROLLOFF},
		{.cmd = DTV_PILOT}, // 0 - ON, 1 - OFF
		{.cmd = DTV_TONE},
		{.cmd = DTV_STREAM_ID},
		{.cmd = DTV_SCRAMBLING_SEQUENCE_INDEX},
		{.cmd = DTV_ISI_LIST},
		{.cmd = DTV_MATYPE},
		{.cmd = DTV_CONSTELLATION},
	};
	struct dtv_properties cmdseq = {
		.num = sizeof(p)/sizeof(p[0]),
		.props = p
	};
	if(!get_constellation)
		cmdseq.num -= 1;
	else {
		if (num_constellation_samples > sizeof(samples) / sizeof(samples[0]))
			num_constellation_samples = sizeof(samples) / sizeof(samples[0]);

		auto& cs = cmdseq.props[cmdseq.num - 1].u.constellation;

		cs.num_samples = num_constellation_samples;
		cs.samples = &samples[0];

	}

	if(ioctl(fefd, FE_GET_PROPERTY, &cmdseq)<0) {
		printf("ioctl failed: %s\n", strerror(errno));
		assert(0); // todo: handle EINTR
		return std::make_tuple(allowed_freq_min, 1000000);
	}

	int i = 0;
	int dtv_delivery_system_prop = cmdseq.props[i++].u.data;
	int dtv_frequency_prop = cmdseq.props[i++].u.data; // in kHz (DVB-S)  or in Hz (DVB-C and DVB-T)
	int dtv_voltage_prop = cmdseq.props[i++].u.data;
	int dtv_symbol_rate_prop = cmdseq.props[i++].u.data; // in Hz

	auto dtv_stat_signal_strength_prop = cmdseq.props[i++].u.st;
	auto dtv_stat_cnr_prop = cmdseq.props[i++].u.st;

	int dtv_modulation_prop = cmdseq.props[i++].u.data;
	int dtv_inner_fec_prop = cmdseq.props[i++].u.data;
	int dtv_inversion_prop = cmdseq.props[i++].u.data;
	int dtv_rolloff_prop = cmdseq.props[i++].u.data;
	int dtv_pilot_prop = cmdseq.props[i++].u.data;
	int dtv_tone_prop = cmdseq.props[i++].u.data;
	int dtv_stream_id_prop = cmdseq.props[i++].u.data;
	int dtv_scrambling_sequence_index_prop = cmdseq.props[i++].u.data;

	assert(cmdseq.props[i].u.buffer.len == 32);
	uint32_t* isi_bitset = (uint32_t*)cmdseq.props[i++].u.buffer.data; // TODO: we can only return 32 out of 256
																																		 // entries...

	int matype = cmdseq.props[i++].u.data;

	if(get_constellation) {

	 	auto& cs = cmdseq.props[i++].u.constellation;
		assert(cs.num_samples >= 0);
		assert((int)cs.num_samples <= sizeof(samples) / sizeof(samples[0]));
		save_constellation_samples(pol_is_v, cs);
	}

	assert(i == cmdseq.num);
	// int dtv_bandwidth_hz_prop = cmdseq.props[12].u.data;
	int currentfreq;
	int currentpol;
	int currentsr;
	int currentsys;
	int currentfec;
	int currentmod;
	int currentinv;
	int currentrol;
	int currentpil;

	currentfreq = (dtv_frequency_prop + (signed)lo_frequency);
	currentpol = dtv_voltage_prop;
	currentsr = dtv_symbol_rate_prop;
	currentsys = dtv_delivery_system_prop;
	currentfec = dtv_inner_fec_prop;
	currentmod = dtv_modulation_prop;
	currentinv = dtv_inversion_prop;
	currentrol = dtv_rolloff_prop;
	currentpil = dtv_pilot_prop;

	if (currentfreq < allowed_freq_min)
		return std::make_tuple(currentfreq, (135 * (currentsr / FREQ_MULT)) / (2 * 100));
	if (dtv_frequency_prop != 0)
		printf("RESULT: freq=%-8.3f%c ", currentfreq / (double)FREQ_MULT, pol_is_v ? 'V' : 'H');
	else
		printf("RESULT: freq=%-8.3f ", dtv_frequency_prop / (double)FREQ_MULT);

	printf("Symrate=%-5d ", currentsr / FREQ_MULT);

	printf("Stream=%-5d pls_mode=%2d:%5d ",
				 (dtv_stream_id_prop & 0xff) == 0xff ? -1 : (dtv_stream_id_prop & 0xff),
				 (dtv_stream_id_prop >> 26) & 0x3,
				 (dtv_stream_id_prop >> 8) & 0x3FFFF);
	int num_isi = 0;
	for (int i = 0; i < 256; ++i) {
		int j = i / 32;
		auto mask = ((uint32_t)1) << (i % 32);
		if (isi_bitset[j] & mask) {
			if (num_isi == 0)
				printf("ISI list:");
			printf(" %d", i);
			num_isi++;
		}
	}
	if (num_isi > 0) {
		printf("\n");
	}

	printf("MATYPE: 0x%x\n", matype);

	for (int i = 0; i < dtv_stat_signal_strength_prop.len; ++i) {
		if (dtv_stat_signal_strength_prop.stat[i].scale == FE_SCALE_DECIBEL)
			printf("SIG=%4.2lfdB ", dtv_stat_signal_strength_prop.stat[i].svalue / 1000.);
		else if (dtv_stat_signal_strength_prop.stat[i].scale == FE_SCALE_RELATIVE)
			printf("SIG=%3lld%% ", (dtv_stat_signal_strength_prop.stat[i].uvalue * 100) / 65535);
		else if (dtv_stat_signal_strength_prop.stat[i].scale == FE_SCALE_NOT_AVAILABLE)
			printf("SIG=%3lld?? ", (dtv_stat_signal_strength_prop.stat[i].uvalue * 100) / 65536);
	}

	for (int i = 0; i < dtv_stat_cnr_prop.len; ++i) {
		if (dtv_stat_cnr_prop.stat[i].scale == FE_SCALE_DECIBEL)
			printf("CNR=%4.2lfdB ", dtv_stat_cnr_prop.stat[i].svalue / 1000.);
		else if (dtv_stat_cnr_prop.stat[i].scale == FE_SCALE_RELATIVE)
			printf("CNR=%3lld%% ", (dtv_stat_cnr_prop.stat[i].uvalue * 100) / 65535);
		else if (dtv_stat_cnr_prop.stat[i].scale == FE_SCALE_NOT_AVAILABLE)
			printf("CNR=%3lld?? ", (dtv_stat_cnr_prop.stat[i].uvalue * 100) / 65537);
	}

	switch (dtv_delivery_system_prop) {
	case 4:
		printf("DSS    ");
		break;
	case 5:
		printf("DVB-S  ");
		break;
	case 6:
		printf("DVB-S2 ");
		break;
	default:
		printf("SYS(%d) ", dtv_delivery_system_prop);
		break;
	}

	switch (dtv_modulation_prop) {
	case 0:
		printf("QPSK ");
		break;
	case 9:
		printf("8PSK ");
		break;
	default:
		printf("MOD(%d) ", dtv_modulation_prop);
		break;
	}

	extern const char* fe_code_rates[];
	printf("%s ", fe_code_rates[dtv_inner_fec_prop]);

	switch (dtv_inversion_prop) {
	case 0:
		printf("INV_OFF ");
		break;
	case 1:
		printf("INV_ON  ");
		break;
	case 2:
		printf("INVAUTO ");
		break;
	default:
		printf("INV (%d) ", dtv_inversion_prop);
		break;
	}

	switch (dtv_pilot_prop) {
	case 0:
		printf("PIL_ON  ");
		break;
	case 1:
		printf("PIL_OFF ");
		break;
	case 2:
		printf("PILAUTO ");
		break;
	default:
		printf("PIL (%d ) ", dtv_pilot_prop);
		break;
	}

	switch (dtv_rolloff_prop) {
	case 0:
		printf("ROLL_35\n");
		break;
	case 1:
		printf("ROLL_20\n");
		break;
	case 2:
		printf("ROLL_25\n");
		break;
	case 3:
		printf("ROLL_AUTO\n");
		break;
	default:
		printf("ROLL(%d)\n", dtv_rolloff_prop);
		break;
	}

	if (fpout) {
		fprintf(fpout, "S%d %d %c %d %d/%d AUTO %s \n", dtv_delivery_system_prop == 6 ? 2 : 1,
						dtv_frequency_prop + (signed)lo_frequency, pol_is_v ? 'V' : 'H', currentsr,
						dtv_inner_fec_prop < 8					 ? dtv_inner_fec_prop
						: dtv_inner_fec_prop == FEC_3_5	 ? 3
						: dtv_inner_fec_prop == FEC_9_10 ? 9
						: dtv_inner_fec_prop == FEC_2_5	 ? 2
						: 0,
						dtv_inner_fec_prop < 8					 ? (dtv_inner_fec_prop + 1)
						: dtv_inner_fec_prop == FEC_3_5	 ? 5
						: dtv_inner_fec_prop == FEC_9_10 ? 10
						: dtv_inner_fec_prop == FEC_2_5	 ? 5
						: 0,
						dtv_modulation_prop == 0	 ? "QPSK"
						: dtv_modulation_prop == 9 ? "8PSK"
						: "AUTO");
		fflush(fpout);
	}

	return std::make_tuple(currentfreq, (135 * (currentsr / FREQ_MULT)) / (2 * 100));
}


void save_constellation_samples(bool pol_is_v, struct dtv_fe_constellation& cs) {

	char fname[512];
	sprintf(fname, options.filename_pattern.c_str(),  "iq", options.adapter_no,
					options.freq/1000.,
					pol_is_v? 'V':'H');
	printf("Constellation has %d samples\n", cs.num_samples);
	if (cs.num_samples > 0) {
			FILE* fpout = fopen(fname, "w");
			assert(fpout);
			for (int i = 0; i < cs.num_samples; ++i)
				fprintf(fpout, "%d %d\n", cs.samples[i].real, cs.samples[i].imag);
			fclose(fpout);
		}  else
		printf("\tNO SAMPLES\n");
}

void close_frontend(int fefd);

int get_extended_frontend_info(int fefd) {
	struct dvb_frontend_extended_info fe_info {}; // front_end_info
	// auto now =time(NULL);
	// This does not produce anything useful. Driver would have to be adapted

	int res;
	if ((res = ioctl(fefd, FE_GET_EXTENDED_INFO, &fe_info) < 0)) {
		printf("FE_GET_EXTENDED_INFO failed: blindscan drivers probably not installed\n");
		close_frontend(fefd);
		return -1;
	}
	printf("Name of card: %s\n", fe_info.card_name);
	printf("Name of adapter: %s\n", fe_info.adapter_name);
	printf("Name of frontend: %s\n", fe_info.card_name);
	/*fe_info.frequency_min
		fe_info.frequency_max
		fe_info.symbolrate_min
		fe_info.symbolrate_max
		fe_info.caps:
	*/

	struct dtv_property properties[16];
	memset(properties, 0, sizeof(properties));
	unsigned int i = 0;
	properties[i++].cmd = DTV_ENUM_DELSYS;
	properties[i++].cmd = DTV_DELIVERY_SYSTEM;
	struct dtv_properties props = {.num = i, .props = properties};

	if ((ioctl(fefd, FE_GET_PROPERTY, &props)) == -1) {
		printf("FE_GET_PROPERTY failed: %s", strerror(errno));
		close_frontend(fefd);
		return -1;
	}

	//auto current_fe_type = chdb::linuxdvb_fe_delsys_to_type (fe_info.type);
	//auto& supported_delsys = properties[0].u.buffer.data;
//	int num_delsys =  properties[0].u.buffer.len;
#if 0
	auto tst =dump_caps((chdb::fe_caps_t)fe_info.caps);
	printf("CAPS: %s", tst);
	fe.delsys.resize(num_delsys);
	for(int i=0 ; i<num_delsys; ++i) {
		auto delsys = (chdb::fe_delsys_t) supported_delsys[i];
		//auto fe_type = chdb::delsys_to_type (delsys);
		auto* s = enum_to_str(delsys);
		printf("delsys[" << i << "]=" << s);
		changed |= (i >= fe.delsys.size() || fe.delsys[i].fe_type!= delsys);
		fe.delsys[i].fe_type = delsys;
	}
#endif
	return 0;
}

int get_frontend_info(int fefd)
{
	struct dvb_frontend_info fe_info{}; //front_end_info
	//auto now =time(NULL);
	//This does not produce anything useful. Driver would have to be adapted

	int res;
	if ( (res = ioctl(fefd, FE_GET_INFO, &fe_info) < 0)){
		printf("FE_GET_INFO failed: %s\n", strerror(errno));
		close_frontend(fefd);
		return -1;
	}
	printf("Name of card: %s\n", fe_info.name);
	/*fe_info.frequency_min
		fe_info.frequency_max
		fe_info.symbolrate_min
		fe_info.symbolrate_max
		fe_info.caps:
	*/


	struct dtv_property properties[16];
	memset(properties, 0, sizeof(properties));
	unsigned int i=0;
	properties[i++].cmd      = DTV_ENUM_DELSYS;
	properties[i++].cmd      = DTV_DELIVERY_SYSTEM;
	struct dtv_properties props ={
		.num=i,
		.props = properties
	};

	if ((ioctl(fefd, FE_GET_PROPERTY, &props)) == -1) {
		printf("FE_GET_PROPERTY failed: %s", strerror(errno));
		//set_interrupted(ERROR_TUNE<<8);
		close_frontend(fefd);
		return -1;
	}

	//auto current_fe_type = chdb::linuxdvb_fe_delsys_to_type (fe_info.type);
	//auto& supported_delsys = properties[0].u.buffer.data;
//	int num_delsys =  properties[0].u.buffer.len;
#if 0
	auto tst =dump_caps((chdb::fe_caps_t)fe_info.caps);
	printf("CAPS: %s", tst);
	fe.delsys.resize(num_delsys);
	for(int i=0 ; i<num_delsys; ++i) {
		auto delsys = (chdb::fe_delsys_t) supported_delsys[i];
		//auto fe_type = chdb::delsys_to_type (delsys);
		auto* s = enum_to_str(delsys);
		printf("delsys[" << i << "]=" << s);
		changed |= (i >= fe.delsys.size() || fe.delsys[i].fe_type!= delsys);
		fe.delsys[i].fe_type = delsys;
	}
#endif
	return 0;
}

int open_frontend(const char* frontend_fname) {
	const bool rw = true;
	int rw_flag = rw ? O_RDWR : O_RDONLY;
	int fefd = open(frontend_fname, rw_flag | O_NONBLOCK);
	if (fefd < 0) {
		printf("open_frontend failed: %s\n", strerror(errno));
		return -1;
	}
	return fefd;
}

void close_frontend(int fefd) {
	if (fefd < 0)
		return;
	if (::close(fefd) < 0) {
		printf("close_frontend failed: %s: ", strerror(errno));
		return;
	}
}

struct cmdseq_t {
	struct dtv_properties cmdseq {};
	std::array<struct dtv_property, 16> props;

	cmdseq_t() { cmdseq.props = &props[0]; }
	template <typename T> void add(int cmd, T data) {
		assert(cmdseq.num < props.size() - 1);
		memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
		cmdseq.props[cmdseq.num].cmd = cmd;
		cmdseq.props[cmdseq.num].u.data = (int)data;
		cmdseq.num++;
	};

	void add(int cmd, const dtv_fe_constellation& constellation) {
		assert(cmdseq.num < props.size() - 1);
		memset(&cmdseq.props[cmdseq.num], 0, sizeof(cmdseq.props[cmdseq.num]));
		cmdseq.props[cmdseq.num].cmd = cmd;
		cmdseq.props[cmdseq.num].u.constellation = constellation;
		cmdseq.num++;
	};

	void add_pls_codes(int cmd, uint32_t* codes, int num_codes) {
		assert(cmdseq.num < props.size() - 1);
		auto* tvp = &cmdseq.props[cmdseq.num];
		memset(tvp, 0, sizeof(cmdseq.props[cmdseq.num]));
		tvp->cmd = cmd;
		tvp->u.pls_search_codes.num_codes = num_codes;
		tvp->u.pls_search_codes.codes = codes;
		cmdseq.num++;
	};

	void add_pls_range(int cmd, uint32_t pls_start, uint32_t pls_end, int timeout) {
		assert(cmdseq.num < props.size() - 1);
		auto* tvp = &cmdseq.props[cmdseq.num];
		memset(tvp, 0, sizeof(cmdseq.props[cmdseq.num]));
		tvp->cmd = cmd;
		printf("adding scramble code range:%d-%d\n", pls_start, pls_end);
		pls_start = make_code(0, pls_start, timeout);
		pls_end = make_code(0, pls_end, timeout);
		memcpy(&tvp->u.buffer.data[0 * sizeof(uint32_t)], &pls_start, sizeof(pls_start));
		memcpy(&tvp->u.buffer.data[1 * sizeof(uint32_t)], &pls_end, sizeof(pls_end));
		tvp->u.buffer.len = 2 * sizeof(uint32_t);
		cmdseq.num++;
	};

	int tune(int fefd, bool dotune = true) {
		if (dotune)
			add(DTV_TUNE, 0);
		if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
			printf("FE_SET_PROPERTY failed: %s\n", strerror(errno));
			return -1;
		}
		return 0;
	}

	int scan(int fefd, bool init) {
		add(DTV_SCAN, init);
		if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
			printf("FE_SET_PROPERTY failed: %s\n", strerror(errno));
			return -1;
		}
		return 0;
	}
	int spectrum(int fefd, dtv_fe_spectrum_method method) {
		add(DTV_SPECTRUM, method);
		if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
			printf("FE_SET_PROPERTY failed: %s\n", strerror(errno));
			return -1;
		}
		return 0;
	}
	int constellation_samples(int fefd, int num_samples, int constel_select = 0,
														dtv_fe_constellation_method method = CONSTELLATION_METHOD_DEFAULT) {
		struct dtv_fe_constellation cs {
			.num_samples = (__u32)num_samples, .method = (__u8)method
		};
		add(DTV_CONSTELLATION, cs);
		if ((ioctl(fefd, FE_SET_PROPERTY, &cmdseq)) == -1) {
			printf("FE_SET_PROPERTY failed: %s\n", strerror(errno));
			return -1;
		}
		return 0;
	}
};

int clear(int fefd) {
	struct dtv_property pclear[] = {
		{
			.cmd = DTV_CLEAR,
		},

	};
	struct dtv_properties cmdclear = {
		.num = 1,
		.props = pclear
	};
	if ((ioctl(fefd, FE_SET_PROPERTY, &cmdclear)) == -1) {
		printf("FE_SET_PROPERTY clear failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int tune(int fefd, int frequency, bool pol_is_v) {
	printf("Tuning to DVBS1/2 %.3f%c\n", frequency / 1000., pol_is_v ? 'V' : 'H');
	if (clear(fefd) < 0)
		return -1;

	do_lnb_and_diseqc(fefd, frequency, pol_is_v);
	return tune_it(fefd, frequency, pol_is_v);
}

/*
	pls_mode>=0 means that only this pls will be scanned (no unscrambled transponders)
	Usually it is better to use pls_modes; these will be used in addition to unscrambled
*/
int tune_it(int fefd, int frequency_, bool pol_is_v) {
	cmdseq_t cmdseq;

	auto frequency= driver_freq_for_freq(frequency_);
	if (options.algo == algo_t::BLIND) {
		printf("BLIND TUNE search-range=%d\n", options.search_range);
		cmdseq.add(DTV_ALGORITHM, ALGORITHM_BLIND);
		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)SYS_AUTO);

		cmdseq.add(DTV_SEARCH_RANGE, options.search_range * 1000); // how far carrier may shift
		if (options.symbol_rate > 0)
			cmdseq.add(DTV_SYMBOL_RATE, options.symbol_rate * 1000); // controls tuner bandwidth
		// cmdseq.add(DTV_DELIVERY_SYSTEM,  SYS_DVBS2);
		cmdseq.add(DTV_FREQUENCY, frequency); // For satellite delivery systems, it is measured in kHz.

		if (options.pls_codes.size() > 0)
			cmdseq.add_pls_codes(DTV_PLS_SEARCH_LIST, &options.pls_codes[0], options.pls_codes.size());

		if (options.end_pls_code > options.start_pls_code)
			cmdseq.add_pls_range(DTV_PLS_SEARCH_RANGE, options.start_pls_code, options.end_pls_code,
													 options.pls_search_timeout);
		cmdseq.add(DTV_STREAM_ID, options.stream_id);
	} else {
		bool warm = options.algo == algo_t::WARM;
		printf("%s TUNE search-range=%d\n", warm ? "WARM" : "COLD", options.search_range);
		cmdseq.add(DTV_ALGORITHM, warm ? ALGORITHM_WARM : ALGORITHM_COLD);
		cmdseq.add(DTV_DELIVERY_SYSTEM, (int)options.delivery_system);
		cmdseq.add(DTV_MODULATION, (int)options.modulation);
		// cmdseq.add(DTV_VOLTAGE,  1-polarisation);
		cmdseq.add(DTV_SEARCH_RANGE, options.search_range * 1000); // how far carrier may shift
		if (options.symbol_rate > 0)
			cmdseq.add(DTV_SYMBOL_RATE, options.symbol_rate * 1000); // controls tuner bandwidth
		// cmdseq.add(DTV_DELIVERY_SYSTEM,  SYS_DVBS2);
		cmdseq.add(DTV_FREQUENCY, frequency); // For satellite delivery systems, it is measured in kHz.

		if (options.pls_codes.size() > 0)
			cmdseq.add_pls_codes(DTV_PLS_SEARCH_LIST, &options.pls_codes[0], options.pls_codes.size());

		if (options.end_pls_code > options.start_pls_code)
			cmdseq.add_pls_range(DTV_PLS_SEARCH_RANGE, options.start_pls_code, options.end_pls_code,
													 options.pls_search_timeout);
		auto stream_id = options.pls_codes.size() > 0
			? (options.stream_id < 0 ? -1 : (options.stream_id & 0xff) | options.pls_codes[0])
			: (options.stream_id < 0 ? -1 : (options.stream_id & 0xff));
		cmdseq.add(DTV_STREAM_ID, stream_id);
	}
#if 0
	if (options.rf_in >=0) {
		printf("select rf_in=%d\n", options.rf_in);
		cmdseq.add(DTV_RF_INPUT, options.rf_in);
	}
#endif
	bool get_constellation = options.command == command_t::IQ;
	auto num_constellation_samples = options.num_samples;
	if (get_constellation) {
		dtv_fe_constellation constellation;
		constellation.num_samples = num_constellation_samples;
		constellation.samples = nullptr;
		constellation.method = CONSTELLATION_METHOD_DEFAULT;
		constellation.constel_select = 1;
		cmdseq.add(DTV_CONSTELLATION, constellation);
	}
	return cmdseq.tune(fefd);
}

#if 0
int tune_next(int fefd) {
	cmdseq_t cmdseq;

	printf("NEXT SCAN\n");
	cmdseq.add(DTV_ALGORITHM, ALGORITHM_SEARCH_NEXT);
	// cmdseq.add(DTV_DELIVERY_SYSTEM,  SYS_DVBS);
	return cmdseq.tune(fefd);
}
#endif
/** @brief generate and diseqc message for a committed or uncommitted switch
 * specification is available from http://www.eutelsat.com/
 * @param extra: extra bits to set polarisation and band; not sure if this does anything useful
 */
int send_diseqc_message(int fefd, char switch_type, unsigned char port, unsigned char extra, bool repeated) {

	struct dvb_diseqc_master_cmd cmd {};
	// Framing byte : Command from master, no reply required, first transmission : 0xe0
	cmd.msg[0] = repeated ? 0xe1 : 0xe0;
	// Address byte : Any LNB, switcher or SMATV
	cmd.msg[1] = 0x10;
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

	int err;
	if ((err = ioctl(fefd, FE_DISEQC_SEND_MASTER_CMD, &cmd))) {
		printf("problem sending the DiseqC message\n");
		return -1;
	}
	return 0;
}

int hi_lo(unsigned int frequency) { return (frequency >= lnb_universal_slof); }

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
int diseqc(int fefd, bool pol_is_v, bool band_is_high) {
	/*
		turn off tone to not interfere with diseqc
	*/
	bool tone_off_called = false;
	auto tone_off = [&]() {
		int err;
		if (tone_off_called)
			return 0;
		tone_off_called = true;
		if ((err = ioctl(fefd, FE_SET_TONE, SEC_TONE_OFF))) {
			printf("problem Setting the Tone OFF");
			return -1;
		}
		return 1;
	};

	int ret;
	bool must_pause = false; // do we need a long pause before the next diseqc command?
	int diseqc_num_repeats = 2;
	for (int repeated = 0; repeated <= diseqc_num_repeats; ++repeated) {

		for (const char& command : options.diseqc) {
			switch (command) {
			case 'M': {
				if (tone_off() < 0)
					return -1;
				msleep(must_pause ? 200 : 30);
				/*
					tone burst commands deal with simpler equipment.
					They use a 12.5 ms duration 22kHz burst for transmitting a 1
					and 9 shorter bursts within a 12.5 ms interval for a 0
					for an on signal.
					They allow swithcing between two satelites only
				*/

				must_pause = !repeated;
			} break;
			case 'C': {
				if (options.committed < 0)
					continue;
				// committed
				if (tone_off() < 0)
					return -1;
				msleep(must_pause ? 200 : 30);
				assert(options.pol == 1 || options.pol == 2);
				int extra = (pol_is_v ? 0 : 2) | (band_is_high ? 1 : 0);
				ret = send_diseqc_message(fefd, 'C', options.committed * 4, extra, repeated);
				if (ret < 0) {
					printf("Sending Committed DiseqC message failed");
				}
				must_pause = !repeated;
			} break;
			case 'U': {
				if (options.uncommitted < 0)
					continue;
				// uncommitted
				if (tone_off() < 0)
					return -1;

				msleep(must_pause ? 200 : 30);
				ret = send_diseqc_message(fefd, 'U', options.uncommitted, 0, repeated);
				if (ret < 0) {
					printf("Sending Uncommitted DiseqC message failed");
				}
				must_pause = !repeated;
			} break;
			case ' ': {
				msleep(50);
				must_pause = false;
			} break;
			}
			if (ret < 0)
				return ret;
		}
	}
	if( must_pause)
		msleep(100);
	return tone_off_called ? 1 : 0;
}

int do_lnb_and_diseqc(int fefd, int frequency, bool pol_is_v) {

	/*TODO: compute a new diseqc_command string based on
		last tuned lnb, such that needless switching is avoided
		This needs:
		-after successful tuning: old_lnb... needs to be stored
		-after unsuccessful tuning, second attempt should use full diseqc
	*/
	int ret;

#ifndef SET_VOLTAGE_TONE_DURING_TUNE
	// this ioctl is also performed internally in modern kernels; save some time

	/*

		22KHz: off = low band; on = high band
		13V = vertical or right-hand  18V = horizontal or low-hand
		TODO: change this to 18 Volt when using positioner
	*/
	if (options.rf_in >=0) {
		printf("select rf_in=%d\n", options.rf_in);
		if ((ret = ioctl(fefd, FE_SET_RF_INPUT, (int32_t) options.rf_in))) {
			printf("problem Setting rf_input\n");
			return -1;
		}
	}

	fe_sec_voltage_t lnb_voltage = pol_is_v ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;
	if ((ret = ioctl(fefd, FE_SET_VOLTAGE, lnb_voltage))) {
		printf("problem Setting voltage\n");
		return -1;
	}

#endif

	bool band_is_high = hi_lo(frequency);

	// Note: the following is a NOOP in case no diseqc needs to be sent
	ret = diseqc(fefd, pol_is_v, band_is_high);
	if (ret < 0)
		return ret;
#ifndef SET_VOLTAGE_TONE_DURING_TUNE
	bool tone_turned_off = ret > 0;

	/*select the proper lnb band
		22KHz: off = low band; on = high band
	*/
	if (tone_turned_off) {
		fe_sec_tone_mode_t tone = band_is_high ? SEC_TONE_ON : SEC_TONE_OFF;
		ret = ioctl(fefd, FE_SET_TONE, tone);
		if (ret < 0) {
			printf("problem Setting the Tone back\n");
			return -1;
		}
	}
#endif
	return 0;
}

uint32_t scan_freq(int fefd, int efd, int frequency, bool pol_is_v) {
	int ret = 0;
	printf("==========================\n");
	while (1) {
		struct dvb_frontend_event event {};
		if (ioctl(fefd, FE_GET_EVENT, &event) < 0)
			break;
	}

	ret = tune(fefd, frequency, pol_is_v);
	if (ret != 0) {
		printf("Tune FAILED\n");
		exit(1);
	}

	struct dvb_frontend_event event {};
	bool timedout = false;
	bool locked = false;
	int count = 0;
	while (count < 3 && !timedout && !locked) {
		struct epoll_event events[1]{{}};
		auto s = epoll_wait(efd, events, 1, epoll_timeout);
		if (s < 0)
			printf("\tEPOLL failed: err=%s\n", strerror(errno));
		if (s == 0) {
			auto old = frequency;
			printf("\tTIMEOUT freq: old=%.3f new=%.3f\n", old / (float)FREQ_MULT, frequency / (float)FREQ_MULT);
			timedout = true;
			break;
		}
		int r = ioctl(fefd, FE_GET_EVENT, &event);
		if (r < 0)
			printf("\tFE_GET_EVENT stat=%d err=%s\n", event.status, strerror(errno));
		else {
			timedout = event.status & FE_TIMEDOUT;
			locked = event.status & FE_HAS_VITERBI;
			if (count >= 1)
				printf("\tFE_GET_EVENT: stat=%d, timedout=%d locked=%d\n", event.status, timedout, locked);
			count++;
		}
	}

	if (timedout)
		return frequency;

	if (check_lock_status(fefd)) {
		auto old = frequency;
		auto band  = band_for_freq(frequency);
		auto [found_freq, bw2] = getinfo(NULL, fefd, pol_is_v, frequency - options.search_range / 2, band);
		frequency = found_freq + bw2;
		frequency += options.search_range / 2;
	} else
		printf("\tnot locked\n");
	return frequency;
}

int main_tune(int fefd) {
	int uncommitted = 0;
	assert(options.pol == 1 || options.pol == 2);
	bool pol_is_v = (options.pol == 2);
	printf("Tuning\n");
	clear(fefd);
	int efd = epoll_create1(0); // create an epoll instance

	struct epoll_event ep;
	memset(&ep, 0, sizeof(ep));

	ep.data.fd = fefd;																	 // user data
	ep.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET; // edge triggered!
	int s = epoll_ctl(efd, EPOLL_CTL_ADD, fefd, &ep);
	if (s < 0)
		printf("EPOLL Failed: err=%s\n", strerror(errno));
	assert(s == 0);

	scan_freq(fefd, efd, options.freq, pol_is_v);
	for (;;)
		sleep(100);
	return 0;
}

int main_constellation(int fefd) {
	int uncommitted = 0;
	bool pol_is_v = (options.pol == 2);
	assert(options.pol == 1 || options.pol == 2);
	printf("Getting IQ samples\n");
	clear(fefd);
	int efd = epoll_create1(0); // create an epoll instance

	struct epoll_event ep;
	memset(&ep, 0, sizeof(ep));

	ep.data.fd = fefd;																	 // user data
	ep.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET; // edge triggered!
	int s = epoll_ctl(efd, EPOLL_CTL_ADD, fefd, &ep);
	if (s < 0)
		printf("EPOLL Failed: err=%s\n", strerror(errno));
	assert(s == 0);

	scan_freq(fefd, efd, options.freq, pol_is_v);
	return 0;
}

int main(int argc, char** argv) {
	bool has_blindscan{false};
	if (options.parse_options(argc, argv) < 0)
		return -1;
	if(std::filesystem::exists("/sys/module/dvb_core/info/version")) {
		printf("Blindscan drivers found\n");
			has_blindscan = true;
	} else {
		printf("!!!!Blindscan drivers not installed  - only regular tuning will work!!!!\n");
		options.algo = algo_t::WARM;
	}

	char dev[512];
	sprintf(dev, "/dev/dvb/adapter%d/frontend%d", options.adapter_no, options.frontend_no);
	int fefd = open_frontend(dev);
	if (fefd < 0) {
		exit(1);
	}
	if(has_blindscan ?
		 get_extended_frontend_info(fefd) : get_frontend_info(fefd)) {
		exit(1);
	}
	int ret = 0;
	switch (options.command) {
	case command_t::TUNE:
		ret |= main_tune(fefd);
		break;
	case command_t::IQ:
		ret |= main_constellation(fefd);
		break;
	default:
		break;
	}
	return ret;
}
