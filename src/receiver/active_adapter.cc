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

/** @brief Wait msec miliseconds
 */
static inline void msleep(uint32_t msec) {
	struct timespec req = {msec / 1000, 1000000 * (msec % 1000)};
	while (nanosleep(&req, &req))
		;
}

#define FREQ_MULT 1000

#define CBAND_LOF 5150000

/** @brief Check the status of the card
		Returns must_tune, must_restart_si (boolean)
*/
std::tuple<bool, bool> active_adapter_t::check_status() {
	if (!current_fe)
		return {};
	auto status = current_fe->get_lock_status();
	bool is_locked = status.fe_status & FE_HAS_LOCK;
	int lock_lost = status.lock_lost;
	bool must_tune = false;
	bool must_reinit_si = false;

	si.scan_state.locked = is_locked;
	if (!is_locked) {
		pol_band_status.set_tune_status(false); // ensures that diseqc will be sent again
	} else
		pol_band_status.set_tune_status(true); // ensures that diseqc will be sent again

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
				if (tune_options.retune_mode == retune_mode_t::AUTO) {
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

int active_adapter_t::change_delivery_system(chdb::fe_delsys_t delsys) {
	if (current_delsys == delsys)
		return 0;
	current_delsys = delsys;
	return 0;
}

/*
	determine if we need to send a diseqc command
	always send diseqc if tuning has failed
*/
bool active_adapter_t::need_diseqc(const chdb::lnb_t& new_lnb, const chdb::dvbs_mux_t& new_mux) {
	if (!pol_band_status.is_tuned())
		return true; // always send diseqc if we were not tuned
	bool is_dvbs =
		((int)new_mux.delivery_system == SYS_DVBS || new_mux.delivery_system == (chdb::fe_delsys_dvbs_t)SYS_DVBS2);
	if (!is_dvbs)
		return false;
	if (new_lnb.k != current_lnb().k)
		return true;
	if (!chdb::on_rotor(new_lnb))
		return false;
	bool active_rotor = (new_lnb.rotor_control == chdb::rotor_control_t::ROTOR_MASTER_USALS ||
											 new_lnb.rotor_control == chdb::rotor_control_t::ROTOR_MASTER_DISEQC12);
	if (!active_rotor)
		return false;
	return chdb::mux_key_ptr(current_tp())->sat_pos != new_mux.k.sat_pos;
}

int active_adapter_t::lnb_scan(const chdb::lnb_t& lnb, tune_options_t tune_options) {
	retune_count = 0;
	this->tune_options = tune_options;
	switch (tune_options.tune_mode) {
	case tune_mode_t::SPECTRUM:
		return lnb_spectrum_scan(lnb, tune_options);
	case tune_mode_t::SCAN_BLIND:
		return lnb_blind_scan(lnb, tune_options);
	default:
		set_current_lnb(lnb);
		return 0;
		break;
	}
	assert(0);
	return 0;
}

int active_adapter_t::tune(const chdb::lnb_t& lnb, const chdb::dvbs_mux_t& mux, tune_options_t tune_options,
													 bool user_requested) {

	this->tune_options = tune_options;
	if (user_requested)
		retune_count = 0;
	dttime_init();
	auto muxname = chdb::to_str(mux);
	dtdebug("Tuning to DVBS mux " << muxname.c_str() << " diseqc: lnb_id=" << lnb << " " << lnb.tune_string);
	// needs to be at very start!
	bool need_diseqc = this->need_diseqc(lnb, mux);

	set_current_tp(mux);

	set_current_lnb(lnb);
	auto delsys = (chdb::fe_delsys_t)mux.delivery_system;
	if (current_fe->clear() < 0) /*this call takes 500ms for the tas2101, probably because the driver's tuning loop \
																 is slow to react*/
		return -1;
	if (change_delivery_system(delsys) < 0)
		return -1;
	dtdebug("tune: change_delivery_system done");
	dttime(300);

	const auto* cmux = std::get_if<chdb::dvbs_mux_t>(&current_tp());
	assert(cmux);
	auto band = chdb::lnb::band_for_mux(lnb, *cmux);
	if (need_diseqc) {
		do_lnb_and_diseqc(band, (fe_sec_voltage_t)chdb::lnb::voltage_for_pol(lnb, cmux->pol));
		dtdebug("tune: do_lnb_and_diseqc done");
	} else {
		do_lnb(band, (fe_sec_voltage_t)chdb::lnb::voltage_for_pol(lnb, cmux->pol));
		dtdebug("tune: do_lnb done");
	}

	dttime(300);
	auto ret = tune_it(tune_options, chdb::delsys_type_t::DVB_S);
	dtdebugx("tune: tune_it done ret=%d\n", ret);
	dttime(100);
	return ret;
}

int active_adapter_t::retune(const chdb::lnb_t& lnb) {
	const auto* mux = std::get_if<chdb::dvbs_mux_t>(&current_tp());
	assert(mux);
	bool user_requested = false;
	retune_count++;
	si.reset();
	return tune(lnb, *mux, tune_options, user_requested);
}

template <typename mux_t> inline int active_adapter_t::retune() {
	const auto* mux = std::get_if<mux_t>(&current_tp());
	assert(mux);
	bool user_requested = false;
	retune_count++;
	si.reset();
	return tune(*mux, tune_options, user_requested);
}

int active_adapter_t::restart_tune() {

	visit_variant(
		current_tp(),
		[this](const dvbs_mux_t& mux) {
			bool user_requested = true;
			tune(current_lnb(), mux, tune_options, user_requested);
		},
		[this](const dvbc_mux_t& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		},
		[this](const dvbt_mux_t& mux) {
			bool user_requested = true;
			tune(mux, tune_options, user_requested);
		});
	return 0;
}

int active_adapter_t::tune(const chdb::dvbc_mux_t& mux, tune_options_t tune_options, bool user_requested) {
	this->tune_options = tune_options;
	if (user_requested)
		retune_count = 0;
	dttime_init();
	/*
		open frontend if it is not yet open
	*/

	auto muxname = chdb::to_str(mux);
	dtdebug("Tuning to DVBC mux " << muxname.c_str());
	set_current_tp(mux);
	auto delsys = (chdb::fe_delsys_t)mux.delivery_system;
	if (change_delivery_system(delsys) < 0)
		return -1;
	dttime(100);
	auto ret = tune_it(tune_options, chdb::delsys_type_t::DVB_C);
	dttime(100);
	return ret;
}

int active_adapter_t::tune(const chdb::dvbt_mux_t& mux, tune_options_t tune_options, bool user_requested) {
	this->tune_options = tune_options;
	if (user_requested)
		retune_count = 0;

	/*
		open frontend if it is not yet open
	*/

	auto muxname = chdb::to_str(mux);
	dtdebug("Tuning to DVBT mux " << muxname.c_str());
	set_current_tp(mux);
	auto delsys = (chdb::fe_delsys_t)mux.delivery_system;
	if (change_delivery_system(delsys) < 0)
		return -1;
	return tune_it(tune_options, chdb::delsys_type_t::DVB_T);
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

void active_adapter_t::monitor() {
	if (tune_options.tune_mode != tune_mode_t::NORMAL && tune_options.tune_mode != tune_mode_t::MUX_BLIND)
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

	/*usually scan_report will be called by process_si_data, but on bad muxes data may not
		be present. scan_report runs with a max frequency of 1 call per 2 seconds
	*/
	si.scan_report(must_retune);

	if (must_retune) {
		visit_variant(
			current_tp(),
			[this](const dvbs_mux_t& mux) {
				dttime_init();
				retune(current_lnb());
				dttime(100);
			},
			[this](const dvbc_mux_t& mux) { retune<dvbc_mux_t>(); },
			[this](const dvbt_mux_t& mux) { retune<dvbt_mux_t>(); });
	} else if (must_reinit_si) {
		init_si(tune_options.scan_target);
	}

}

int active_adapter_t::lnb_blind_scan(const chdb::lnb_t& lnb, tune_options_t tune_options) {
	return 0;
}

int active_adapter_t::lnb_spectrum_scan(const chdb::lnb_t& lnb, tune_options_t tune_options) {
	//dttime_init();
	// needs to be at very start!
#if 0
	bool need_diseqc = true;
#endif
	set_current_tp({});

	set_current_lnb(lnb);
#if 0
	if (need_diseqc) {
		auto band = tune_options.spectrum_scan_options.band_pol.band;
		auto pol = tune_options.spectrum_scan_options.band_pol.pol;
		auto voltage = ((pol == fe_polarisation_t::V || pol == fe_polarisation_t::R) ^ lnb.swapped_polarisation)
		? SEC_VOLTAGE_13 : SEC_VOLTAGE_18;
		do_lnb_and_diseqc(band, voltage);
	}
	dtdebug("tune: diseqc done");
#endif
	int ret = current_fe->start_lnb_spectrum_scan(lnb, tune_options.spectrum_scan_options);
	//dttime(100);
	return ret;
}

// tuner_id and display_strength: only for logging purposes
int active_adapter_t::tune_it(const tune_options_t tune_options, chdb::delsys_type_t delsys_type) {
	if (!current_fe)
		return -1;
	tune_state = WAITING_FOR_LOCK;
	// make sure that signal_info messages will contain correct status
	current_fe->update_tuned_mux_tune_confirmation({});

	this->tune_options = tune_options;
	struct dvb_frontend_parameters feparams;

	// no warning
	memset(&feparams, 0, sizeof(struct dvb_frontend_parameters));

	/** @todo here check the capabilities of the card*/

	dtdebug("Using adapter  " << get_adapter_no());
	bool blindscan = tune_options.is_blind();
	feparams.inversion = INVERSION_AUTO;
	auto ret = -1;

	{
		auto c = *mux_common_ptr(current_tp());
		if (c.is_template && c.freq_from_si) {
			auto w = tuned_mux.writeAccess();
			mux_common_ptr(*w)->freq_from_si = false;
		}
	}
	switch (delsys_type) {
	case chdb::delsys_type_t::DVB_T: { // DVB-T
		const auto* mux = std::get_if<chdb::dvbt_mux_t>(&current_tp());
		if (!mux) {
			dterror("Attempting to tune non DVB-T mux");
		} else {
			dtdebug("Tuning adapter [ " << get_adapter_no() << "] DVB-T to " << mux << (blindscan ? "BLIND" : ""));
			ret = current_fe->tune(*mux, blindscan);
		}
	} break;
	case chdb::delsys_type_t::DVB_S: { // DVB-S
		const auto* mux = std::get_if<chdb::dvbs_mux_t>(&current_tp());
		if (!mux) {
			dterror("Attempting to tune non DVB-S mux");
		} else {
			dttime_init();
			ret = current_fe->tune(current_lnb(), *mux, tune_options);
			dtdebugx("ret=%d", ret);
			dttime(100);
		}
	} break;
	case chdb::delsys_type_t::DVB_C: { // DVB-C

		const auto* mux = std::get_if<chdb::dvbc_mux_t>(&current_tp());
		if (!mux) {
			dterror("Attempting to tune non DVB-C mux");
		} else {
			dtdebug("Tuning adapter [ " << get_adapter_no() << "] DVB-C to " << mux << (blindscan ? "BLIND" : ""));
			ret = current_fe->tune(*mux, blindscan);
		}
	} break;
#ifdef ATSC
	case FE_ATSC: { // ATSC

		auto* mux = std::get_if<chdb::dvbc_mux_t>(&current_tp);
		if (!mux) {
			dterror("Attempting to tune non ATSC mux");
		} else
			ret = tune_it(*mux, log_strength, retry);
	} break;
#endif
	default:
		dterror("Unknown FE type: " << int(delsys_type));
		return -1;
	}
	dtdebugx("Now ret=%d", ret);
	if (ret < 0)
		return ret;

	tune_start_time = system_clock_t::now();
	si.deactivate();
	return 0;
}

int active_adapter_t::hi_lo(const chdb::lnb_t& lnb, const chdb::any_mux_t& tp) {
	if (lnb.k.lnb_type == lnb_type_t::UNIV) {
		auto* mux = std::get_if<chdb::dvbs_mux_t>(&tp);
		assert(mux);
		// Universal lnb : two bands, hi and low one and two local oscilators
		return (mux->frequency >= dvb_frontend_t::lnb_slof);
	} else if (lnb.k.lnb_type == lnb_type_t::KU) {
		// LNB_STANDARD one band and one local oscillator
		return 0;
	} else if (lnb.k.lnb_type == lnb_type_t::C) {
		// one band and one local oscillator
		return 0;
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
int active_adapter_t::diseqc(const std::string& diseqc_command) {
	if (!current_fe)
		return -1;
	const auto* mux = std::get_if<chdb::dvbs_mux_t>(&current_tp());
	assert(mux);
	/*
		turn off tone to not interfere with diseqc
	*/
	auto fefd = current_fe->ts.readAccess()->fefd;

	auto can_move_dish_ = can_move_dish(current_lnb());

	int ret;
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

			if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return -1;
			msleep(must_pause ? 100 : 30);
			/*
				tone burst commands deal with simpler equipment.
				They use a 12.5 ms duration 22kHz burst for transmitting a 1
				and 9 shorter bursts within a 12.5 ms interval for a 0
				for an on signal.
				They allow switching between two satelites only
			*/
			auto b = std::min(current_lnb().diseqc_mini, (uint8_t)1);
			ret = ioctl(fefd, FE_DISEQC_SEND_BURST, b);
			if (ret < 0) {
				dterror("problem sending the Tone Burst\n");
			}
			must_pause = !repeated;
		} break;
		case 'C': {
			// committed
			auto diseqc_10 = current_lnb().diseqc_10;
			if (diseqc_10 < 0)
				break; // can be used to signal that it is off
			if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return -1;
			msleep(must_pause ? 100 : 30);
			int pol_v_r = ((int)mux->pol & 1);
			unsigned int extra = ((pol_v_r * 2) | hi_lo(current_lnb(), current_tp()));
			ret = current_fe->send_diseqc_message('C', diseqc_10 * 4, extra, repeated);
			if (ret < 0) {
				dterror("Sending Committed DiseqC message failed");
			}
			must_pause = !repeated;
		} break;
		case 'U': {
			auto diseqc_11 = current_lnb().diseqc_11;
			if (diseqc_11 < 0)
				break; // can be used to signal that it is off
			// uncommitted
			if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return -1;

			msleep(must_pause ? 100 : 30);
			ret = current_fe->send_diseqc_message('U', diseqc_11, 0, repeated);
			if (ret < 0) {
				dterror("Sending Uncommitted DiseqC message failed");
			}
			must_pause = !repeated;
		} break;
		case 'X': {
			if (!can_move_dish_)
				break;
			if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return -1;
			msleep(must_pause ? 100 : 30);
			auto* lnb_network = chdb::lnb::get_network(current_lnb(), mux->k.sat_pos);
			if (!lnb_network) {
				dterror("No network found");
			} else {// this is not usals!
				lnb_update_usals_pos(lnb_network->sat_pos);
				ret = current_fe->send_diseqc_message('X', lnb_network->diseqc12, 0, repeated);
				if (ret < 0) {
					dterror("Sending Committed DiseqC message failed");
				}
			}
			must_pause = !repeated;
		} break;
		case 'P': {
			if (!can_move_dish_)
				break;
			if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
				return -1;
			msleep(must_pause ? 100 : 30);
			auto* lnb_network = chdb::lnb::get_network(current_lnb(), mux->k.sat_pos);
			if (!lnb_network) {
				dterror("No network found");
			} else {
				auto usals_pos = lnb_network->usals_pos;
				lnb_update_usals_pos(lnb_network->usals_pos);
				ret = current_fe->send_positioner_message(chdb::positioner_cmd_t::GOTO_XX, usals_pos, repeated);
				if (ret < 0) {
					dterror("Sending Committed DiseqC message failed");
				}
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
	if( must_pause)
		msleep(100);
	return 1;
}

int active_adapter_t::do_lnb(chdb::fe_band_t band, fe_sec_voltage_t lnb_voltage) {
	if (!current_fe)
		return -1;
	/*

		22KHz: off = low band; on = high band
		13V = vertical or right-hand  18V = horizontal or low-hand
		TODO: change this to 18 Volt when using positioner
	*/

	auto fefd = current_fe->ts.readAccess()->fefd;

	pol_band_status.set_voltage(fefd, lnb_voltage);

	/*select the proper lnb band
		22KHz: off = low band; on = high band
	*/

	fe_sec_tone_mode_t tone = (band == fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;
	if (pol_band_status.set_tone(fefd, tone)<0) {
			dterror("problem Setting the Tone back\n");
			return -1;
	}
	return 0;
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
int active_adapter_t::do_lnb_and_diseqc(chdb::fe_band_t band, fe_sec_voltage_t lnb_voltage) {
	if (!current_fe)
		return -1;
	/*TODO: compute a new diseqc_command string based on
		last tuned lnb, such that needless switching is avoided
		This needs:
		-after successful tuning: old_lnb... needs to be stored
		-after unsuccessful tuning, second attempt should use full diseqc
	*/
	int ret;
	/*

		22KHz: off = low band; on = high band
		13V = vertical or right-hand  18V = horizontal or low-hand
		TODO: change this to 18 Volt when using positioner
	*/

	if (retune_count > 0 && tune_options.subscription_type != subscription_type_t::NORMAL) {
		dtdebugx("SKIPPING diseqc: retune_count=%d mode=%d", retune_count, tune_options.subscription_type);
		return 0;
	} else {
		dtdebugx("SENDING diseqc: retune_count=%d mode=%d", retune_count, tune_options.subscription_type);
	}

	auto fefd = current_fe->ts.readAccess()->fefd;

	pol_band_status.set_voltage(fefd, lnb_voltage);

	// Note: the following is a NOOP in case no diseqc needs to be sent
	ret = diseqc(current_lnb().tune_string);
	if (ret < 0)
		return ret;

	/*select the proper lnb band
		22KHz: off = low band; on = high band
	*/

	fe_sec_tone_mode_t tone = (band == fe_band_t::HIGH) ? SEC_TONE_ON : SEC_TONE_OFF;
	if (pol_band_status.set_tone(fefd, tone)<0) {
		return -1;
	}
	return 0;
}

active_adapter_t::active_adapter_t(receiver_t& receiver_, tuner_thread_t& tuner_thread_,
																	 std::shared_ptr<dvb_frontend_t>& current_fe_)
	: current_fe(current_fe_)
	, receiver(receiver_)
	, tuner_thread(tuner_thread_)
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

void active_adapter_t::update_lof(const ss::vector<int32_t, 2>& lof_offsets) {
	auto w = tuned_lnb.writeAccess();
	auto& lnb = *w;
	lnb.lof_offsets = lof_offsets;
	auto txn = receiver.chdb.wtxn();
	auto c = chdb::lnb_t::find_by_key(txn, lnb.k);
	if (c.is_valid()) {
		/*note that we do not update the full mux (e.g., when called from positioner_dialog user may want
			to not save some changes.
			So instead, we set only the lof_offsets
		*/
		auto lnb = c.current();
		lnb.lof_offsets = lof_offsets;
		put_record(txn, lnb);
		txn.commit();
	}
}

int active_adapter_t::deactivate() {
	tune_state = TUNE_INIT;
	end_si();
	active_services.clear();
	return 0;
}

int active_adapter_t::positioner_cmd(chdb::positioner_cmd_t cmd, int par) {
	if (!can_move_dish(current_lnb()))
		return -1;
	if (!current_fe) {
		dterror("no current_fe");
		return -1;
	}
	/*
		turn off tone to not interfere with diseqc
	*/
	auto fefd = current_fe->ts.readAccess()->fefd;
	auto old = pol_band_status.get_tone();
	if (pol_band_status.set_tone(fefd, SEC_TONE_OFF) < 0)
		return -1;
	msleep(15);
	auto ret = current_fe->send_positioner_message(cmd, par);
	if (old >=0 && /* avoid the case where old mode was "unknown" */
			pol_band_status.set_tone(fefd, old) < 0)
		return -1;
	return ret;
}

void active_adapter_t::lnb_update_usals_pos(int16_t usals_pos) {
	int dish_id = tuned_lnb.readAccess()->k.dish_id;
	auto wtxn = receiver.chdb.wtxn();
	int ret = chdb::dish::update_usals_pos(wtxn, dish_id, usals_pos);
	if( ret<0 )
		wtxn.abort();
	else
		wtxn.commit();

	auto w = tuned_lnb.writeAccess();
	auto& lnb = *w;
	if (usals_pos != lnb.usals_pos) {
		// measure how long it takes to move positioner
		usals_timer.start(lnb.usals_pos, usals_pos);
		lnb.usals_pos = usals_pos;
	}
}

void active_adapter_t::set_current_lnb(const chdb::lnb_t& lnb) {
		auto w = tuned_lnb.writeAccess();
		*w = lnb;
};

//only called from active_si_stream.h true, true and true, false and false, false,
void active_adapter_t::on_tuned_mux_change(const chdb::any_mux_t& mux) {
	auto w = tuned_mux.writeAccess();
	*w = mux;
	current_fe->update_tuned_mux_nit(current_tp());
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_dvb_stream_reader(ssize_t dmx_buffer_size) {
	return std::make_shared<dvb_stream_reader_t>(*this, dmx_buffer_size);
}

std::shared_ptr<stream_reader_t> active_adapter_t::make_embedded_stream_reader(int stream_pid,
																																							 ssize_t dmx_buffer_size) {
	auto [it, found] = find_in_map(stream_filters, stream_pid);
	std::shared_ptr<stream_filter_t> substream;
	if (found) {
		substream = it->second;
	} else {
		substream = std::make_shared<stream_filter_t>(*this, stream_pid, &tuner_thread.epx);
		stream_filters[stream_pid] = substream;
	}
	return std::make_shared<embedded_stream_reader_t>(*this, substream);
}

void active_adapter_t::add_embedded_si_stream(int stream_pid, bool start) {
	auto [it, found] = find_in_map(embedded_si_streams, stream_pid);
	if (found) {
		dtdebugx("Ignoring requet to add the same si stream twice");
		return;
	}
	auto reader = make_embedded_stream_reader(stream_pid);
	const bool is_embedded_si{true};
#ifndef NDEBUG
	auto [it1, inserted] =
#endif
		embedded_si_streams.try_emplace((uint16_t)stream_pid, receiver, std::move(reader), is_embedded_si);
	assert(inserted);
	if (start)
		it1->second.init(tune_options.scan_target);
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
	auto* dvbs_mux = std::get_if<chdb::dvbs_mux_t>(&current_tp());
	if (dvbs_mux && dvbs_mux->k.t2mi_pid > 0) {
		auto stream_pid = dvbs_mux->k.t2mi_pid;
		auto mux = *dvbs_mux;
		dtdebug("mux " << *dvbs_mux << " is an embedded stream");
		mux.k.t2mi_pid = 0;
		auto rtxn = receiver.chdb.wtxn();
		/*TODO: this will not detect almost duplicates in frequency (which should not be present anyway) or
		handle small differences in sat_pos*/
		auto c = chdb::find_by_mux_physical(rtxn, mux);
		if (c.is_valid()) {
			assert(mux_key_ptr(c.current())->sat_pos != sat_pos_none);
			set_current_tp(c.current());
			rtxn.abort();
		} else {
			rtxn.abort();
			auto wtxn = receiver.chdb.wtxn();
			auto am = current_tp();
			update_mux(wtxn, am, now, update_mux_preserve_t::NONE);
			set_current_tp(am);
			wtxn.commit();
		}
		add_embedded_si_stream(stream_pid);
	}
	si.init(scan_target);
	for (auto& [pid, si_] : embedded_si_streams) {
		si_.init(si.scan_target);
	}
}

void active_adapter_t::end_si() {
	for (auto& [pid, si_] : embedded_si_streams) {
		si_.deactivate();
	}
	si.deactivate();
	embedded_si_streams.clear();
	stream_filters.clear();
}



int pol_band_status_t::set_tone(int fefd, fe_sec_tone_mode mode) {
	if ((int)mode < 0) {
		assert(0);
		return -1;
	}
	if (mode == tone) {
		dtdebugx("No tone change needed: v=%d", mode);
		return 0;
	}
	tone = mode;

	if (ioctl(fefd, FE_SET_TONE, mode) < 0 ) {
		dterrorx("problem setting tone=%d", mode);
		return -1;
	}
	return 1;
}

int pol_band_status_t::set_voltage(int fefd, fe_sec_voltage v) {
	if ((int)v < 0) {
		assert(0);
		return -1;
	}
	if (v == voltage) {
		dtdebugx("No voltage change needed: v=%d", v);
		return 0;
	}
	voltage = v;

	if (ioctl(fefd, FE_SET_VOLTAGE, voltage) < 0) {
		dterrorx("problem setting voltage %d", voltage);
		return -1;
	}
	return 1;
}
