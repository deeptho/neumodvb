/*
 * (c) deeptho@gmail.com 2019-2022
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
#pragma once
#include "spectrum_algo.h"

typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;
//#define DEBUGXXX
#define dprintk printf

struct spectrum_peak_t {
	u32 freq;				 // frequency of current peak
	s32 symbol_rate; // estimated symbolrate of current peak
	s32 mean_snr;
	s32 min_snr;
	s32 mean_level;
	s32 min_level;
};

/*
	state for spectrum scan
*/

struct spectrum_scan_state_t {
	// user specified input parameters
	int fft_size; // for fft
	s32 start_frequency;
	s32 end_frequency;
	int snr_w;					// window_size to look for snr peaks
	int threshold;			// minimum peak amplitude required
	int threshold2;			// minimum peak amplitude required
	int mincount;				// minimum number of above threshold detections to count as rise/fall

	// outputs
	bool spectrum_present;
	u32* freq;
	s32* spectrum;
	spectrum_peak_t* candidate_frequencies;
	int spectrum_len;
	int num_candidates;
	int num_good;
	int num_bad;

	// state
	bool scan_in_progress;
};

struct spectrum_peak_internal_t {
	s32 idx;			// index at which we last found a peak
	s32 freq;			// frequency of current peak
	s32 bw;				// bandwidth of current peak
	s32 rise_idx; // location of last processed rising peak
	s32 fall_idx; // location of last processed falling peak
	s32 mean_snr;
	s32 min_snr;
	s32 mean_level;
	s32 min_level;
};


struct scan_internal_t {
	s32* rs;						// running sum
	s32 start_idx;			// analysis starts at spectrum[start_idx]
	s32 end_idx;				// analysis end at spectrum[end_idx]-1
	s32 next_frequency; // If we found a transponder last time, this is the frequency just above the transponder bandwidth

	struct spectrum_peak_internal_t last_peak;

#ifdef TEST
	s32 last_rise_idx; // location of last processed rising peak

	s32 last_fall_idx; // location of last processed falling peak
#endif

	u8* peak_marks;
	struct spectrum_peak_internal_t* peaks;
	int num_peaks;
	int max_num_peaks;
	int w; // window_size to look for peaks
	void check();

};
