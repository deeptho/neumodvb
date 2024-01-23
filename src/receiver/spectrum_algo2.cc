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
#include "spectrum_algo.h"


typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;

#define dprintk printf

struct spectrum_peak_t {
	u32 freq;				 // frequency of current peak
	s32 symbol_rate; // estimated symbolrate of current peak
	s32 snr;
	s32 level;
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
	s32 snr;
	s32 level;
};

struct scan_internal_t {
	s32* rs;						// running sum
	s32 start_idx;			// analysis starts at spectrum[start_idx]
	s32 end_idx;				// analysis end at spectrum[end_idx]-1
	s32 current_idx;		// position at which we last stopped processing
	s32 window_idx;			// index of current window size
	s32 next_frequency; // If we found a transponder last time, this is the frequency just above the transponder bandwidth

	struct spectrum_peak_internal_t last_peak;

	s32 last_rise_idx; // location of last processed rising peak
	s32 last_fall_idx; // location of last processed falling peak
	s32 last_peak_snr; // snr of last detected candidate

	u8* peak_marks;
	struct spectrum_peak_internal_t* peaks;
	int num_peaks;
	int max_num_peaks;
	int w; // window_size to look for peaks
};

enum slope_t
{
	NONE = 0,
	FALLING = 1,
	RISING = 2
};


static void running_sum(s32* pout, s32* psig, int n) {
	int i;
	int accu = psig[0];
	for (i = 0; i < n; ++i) {
		accu += psig[i];
		pout[i] = accu;
	}
}

static s32 windows[] = {2,	 4,		6,	 8,		10,	 12,	14,	 16,	20,	 24,	28,	 32,	40,	 48,	 56,	64,
	72,	 80,	88,	 96,	112, 128, 144, 160, 176, 192, 208, 224, 240, 256,	 288, 230,
	352, 384, 416, 448, 480, 512, 576, 640, 704, 768, 832, 896, 960, 1024,
	1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048};

static int check_candidate_tp(struct spectrum_scan_state_t* ss, struct scan_internal_t* si)
{
	int i;
	spectrum_peak_internal_t* cand = &si->last_peak;
	if (cand->snr < ss->threshold2) {
#if 0
		dprintk("Rejecting too weak candidate: %dkHz BW=%dkHz \n", cand->freq, cand->bw);
#endif
		return -1;
	}
	for (i = 0; i < si->num_peaks; ++i) {
		spectrum_peak_internal_t* old = &si->peaks[i];
		// older contained/overlapping transponder with smaller bandwidth
		if (cand->bw >= old->bw && ((old->rise_idx >= cand->rise_idx && old->rise_idx <= cand->fall_idx) ||
																(old->fall_idx >= cand->rise_idx && old->fall_idx <= cand->fall_idx))) {
			if (old->level - cand->level >= ss->threshold2) {
#if 0
				dprintk("Rejecting peak because it contains other peaks: new: %dkHz BW=%dkHz old: %dkHz BW=%dkHz w=%d\n",
								old->freq, old->bw,
								cand->freq, cand->bw,  si->w);
#endif
				return -1;
			} else {
#if 0
				dprintk("Overwriting peak %dkHz BW=%dkHz with broader peak %dkHz BW=%dkHz\n",
								old->freq, old->bw,
								cand->freq, cand->bw
					);
#endif
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * (si->num_peaks - i - 1));
				--i;
				si->num_peaks--;
				continue;
			}
		}

		// older contained/overlapping transponder with larger bandwidth; cand has larger window size
		if ((cand->bw < old->bw && cand->rise_idx >= old->rise_idx && cand->rise_idx <= old->fall_idx) ||
				(cand->fall_idx >= old->rise_idx && cand->fall_idx <= old->fall_idx)) {
			if (cand->level - old->level >= ss->threshold2) {
#if 0
				dprintk("Removing older peak because it contains other peaks %dkHz BW=%dkHz\n", old->freq, old->bw);
#endif
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * (si->num_peaks - i - 1));
				si->num_peaks--;
				--i;
				continue;
			} else {
				// dprintk("Overwriting peak because it contains other peaks\n");
				return -1;
			}
		}
	}
	return 0;
}

/*
	candidate right edges of transponder
	w is window size
	n is size of signal

  --------------\
                 \
                  --


*/
static void falling_kernel(spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	int count = 0;
	bool peak_found = false;
	s32 delta = (si->w * 16) / 200;
	s32 w2 = si->w / 2;
	int n = ss->spectrum_len;
	int i;
	if (delta == 0)
		delta = 1;
	for (i = w2; i < n - delta; ++i) {
		s32 power = (si->rs[i] - si->rs[i - w2]) / w2;
		s32 right = ss->spectrum[i + delta];
		if (power - right > ss->threshold)
			count++;
		else
			count = 0;
		if (count >= ss->mincount) {
			// mark complete peak if not already on a peak
			if (!peak_found) {
				si->peak_marks[i] |= FALLING;
			}
			peak_found = true;
		} else {
			peak_found = false;
		}
	}
}

/*
	candidate left edges of transponder
	w is window size
	n is size of signal
       /--------------
      /
-----/  |

*/
static void rising_kernel(spectrum_scan_state_t* ss, struct scan_internal_t* si) {

	int count = 0;
	bool peak_found = false;
	s32 delta = (si->w * 16) / 200; // rise interval
	s32 w2 = si->w / 2;							// plateau interval
	int n = ss->spectrum_len;
	int i;
	if (delta == 0)
		delta = 1;
	for (i = n - w2 - 1; i >= delta; --i) {
		s32 power = (si->rs[i + w2] - si->rs[i]) / w2;
		s32 left = ss->spectrum[i - delta];
		if (power - left > ss->threshold)
			count++;
		else
			count = 0;
		if (count >= ss->mincount) {
			// mark complete peak if not already on a peak
			if (!peak_found) {
				si->peak_marks[i] |= RISING;
			}
			peak_found = true;
		} else {
			peak_found = false;
		}
	}
}

static void stid135_spectral_init_level(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	si->start_idx = (si->w * 16) / 200;
	if (si->start_idx == 0)
		si->start_idx++;
	si->end_idx = ss->spectrum_len - (si->w * 16) / 200;
	if (si->end_idx == ss->spectrum_len)
		si->end_idx--;
	si->current_idx = si->start_idx;
	si->last_peak.idx = -1;
	si->last_rise_idx = -1;
	si->last_fall_idx = -1;
	memset(si->peak_marks, 0, sizeof(si->peak_marks[0]) * ss->spectrum_len);
	running_sum(si->rs, ss->spectrum, ss->spectrum_len);
	falling_kernel(ss, si);
	rising_kernel(ss, si);
	// fix_kernel(si->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold, ss->mincount);
}

static int stid135_spectral_scan_start(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* spectrum,
																			 u32* freq, int len) {
	si->max_num_peaks = 1024;
	ss->spectrum = spectrum;
	ss->freq = freq;
	ss->spectrum_len = len;

	ss->scan_in_progress = true;

	si->window_idx = 0;
	si->w = windows[si->window_idx];

	// ss->w =17;
	ss->snr_w = 35; //percentage
	// ss->threshold = 2000;
	// ss->mincount = 3;

	si->peak_marks = (u8*)malloc(ss->spectrum_len * (sizeof(si->peak_marks[0])));
	si->peaks = (struct spectrum_peak_internal_t*)malloc(si->max_num_peaks * (sizeof(si->peaks[0])));
	si->rs = (s32*)malloc(ss->spectrum_len * (sizeof(si->rs[0])));
	si->num_peaks = 0;
	if (!ss->spectrum) {
		return -ENOMEM;
	}
	stid135_spectral_init_level(ss, si);
	return 0;
}

#if 0
static int stid135_spectral_scan_end(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* spectrum,
																		 int len) {
	int i;

	free(si->peak_marks);
	free(si->rs);
	si->num_peaks = 0;
	assert(ss->candidate_frequencies == 0);
	ss->candidate_frequencies = (spectrum_peak_t*)malloc(si->num_peaks * sizeof(spectrum_peak_t));
	if (!ss->candidate_frequencies)
		return -ENOMEM;

	for (i = 0; i < si->num_peaks; ++i) {
		spectrum_peak_internal_t* pi = &si->peaks[i];
		spectrum_peak_t* p = &ss->candidate_frequencies[i];
		p->freq = pi->freq;
		p->symbol_rate = pi->bw * 1250;
		p->snr = pi->snr;
		p->level = pi->level;
	}
	free(si->peaks);
	return 0;
}
#endif


#if 0
static s32 peak_snr(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	s32 mean = 0;
	s32 min1 = 0;
	s32 min2 = 0;
	int i;
	s32 w = (ss->snr_w * (si->last_fall_idx - si->last_rise_idx)) / 100;
	if (si->last_fall_idx <= si->last_rise_idx)
		return -99000;
	for (i = si->last_rise_idx; i < si->last_fall_idx; ++i)
		mean += ss->spectrum[i];
	mean /= (si->last_fall_idx - si->last_rise_idx);

	i = si->last_rise_idx - w;
	if (i < 0)
		i = 0;
	min1 = ss->spectrum[si->last_rise_idx];
	for (; i < si->last_rise_idx; ++i)
		if (ss->spectrum[i] < min1)
			min1 = ss->spectrum[i];

	i = si->last_fall_idx + w;
	if (i > ss->spectrum_len)
		i = ss->spectrum_len;
	min2 = ss->spectrum[si->last_fall_idx];
	for (; i > si->last_fall_idx; --i)
		if (ss->spectrum[i] < min2)
			min2 = ss->spectrum[i];

	if (min2 < min1)
		min1 = min2;
	return mean - min1;
}
#endif

/*!
	returns index of a peak in the spectrum
*/

static int next_candidate_this_level(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	for (; si->current_idx < si->end_idx; ++si->current_idx) {
		if (si->peak_marks[si->current_idx] & FALLING) {
			if (si->last_rise_idx > si->last_fall_idx && si->last_rise_idx >= 0 &&
					si->current_idx - si->last_rise_idx <= si->w &&				 // maximum window size
					si->current_idx - si->last_rise_idx >= (si->w * 2) / 3 // minimum window size
				) {

				// candidate found; peak is between last_rise and current idx
				si->last_peak.idx = (si->last_rise_idx + si->current_idx) / 2;
				si->last_peak.freq = ss->freq[si->last_peak.idx]; // in kHz
				//assert(si->current_idx - si->last_rise_idx <= si->w);
				si->last_peak.bw = ss->freq[si->current_idx] - ss->freq[si->last_rise_idx]; // in kHz
				si->last_fall_idx = si->current_idx;
				assert(si->last_fall_idx == si->current_idx);
				si->last_peak.level =
					(si->rs[si->last_fall_idx] - si->rs[si->last_rise_idx]) / (si->last_fall_idx - si->last_rise_idx);
				{
					s32 delta = (si->w * 16) / 200;
					s32 left = si->last_rise_idx - delta;
					assert(left >= 0);
					s32 right = si->last_fall_idx + delta;
					assert(right < ss->spectrum_len);
					left = ss->spectrum[left];
					right = ss->spectrum[right];
					if (left < right)
						right = left;
					si->last_peak_snr = si->last_peak.level - right;
					si->last_peak.snr = si->last_peak_snr;
				}
#if 0
				dprintk("CANDIDATE1: %d %dkHz [%d - %d] BW=%dkHz snr=%ddB w=%d\n", si->last_peak.idx,
								si->last_peak.freq, ss->freq[si->last_rise_idx], ss->freq[si->current_idx],
								si->last_peak.bw,
								si->last_peak.snr, si->w);
#endif
				si->last_peak.rise_idx = si->last_rise_idx;
				si->last_peak.fall_idx = si->current_idx;
				if (si->peak_marks[si->current_idx] & RISING)
					si->last_rise_idx = si->current_idx;
				si->current_idx++;

				return si->last_peak.idx;
			}

			si->last_fall_idx = si->current_idx;
		}

		if (si->peak_marks[si->current_idx] & RISING)
			si->last_rise_idx = si->current_idx;
	}
	return -1;
}

static int next_candidate_tp(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	int ret;
	while (si->window_idx < (int)(sizeof(windows) / sizeof(windows[0]))) {
		if (si->current_idx >= si->end_idx) { // we reached end of a window
			if (++si->window_idx >= (int)(sizeof(windows) / sizeof(windows[0])))
				return -1;										 // all windows done
			si->w = windows[si->window_idx]; // switch to next window size
			stid135_spectral_init_level(ss, si);
		}
		ret = next_candidate_this_level(ss, si);
		if (ret < 0)
			continue;
#if 0
		dprintk("CANDIDATE2: %d %dkHz BW=%dkHz snr=%ddB w=%d\n", si->last_peak.idx, si->last_peak.freq, si->last_peak.bw,
						si->last_peak_snr, si->w);
#endif
		return ret;
	}
	return -1;
}

#if 0
struct spectrum_scan_state_t ss;
#endif

static int stid135_spectral_scan_next(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* frequency_ret,
																			s32* snr_ret) {
	int ret = 0;
	while (ret >= 0) {
		ret = next_candidate_tp(ss, si);
		if (ret >= 0) {
			if (check_candidate_tp(ss, si) >= 0) {
#if 0
				dprintk("Next frequency to scan: [%d] %dkHz SNR=%d BW=%d\n", ret, si->last_peak.freq, si->last_peak_snr,
								si->last_peak.bw);
#endif
				*frequency_ret = si->last_peak.freq;
				*snr_ret = si->last_peak_snr;
				return si->last_peak.idx;
			}
		} else {
#if 0
			dprintk("Current subband fully scanned: current_idx=%d end_idx=%d\n", si->current_idx, si->end_idx);
#endif
		}
	}
	return -1;
}

static int cmp_fn(const void* pa, const void* pb) {
	spectrum_peak_internal_t* a = (spectrum_peak_internal_t*)pa;
	spectrum_peak_internal_t* b = (spectrum_peak_internal_t*)pb;
	return a->freq - b->freq;
}

void find_tps(ss::vector_<spectral_peak_t>& res,	ss::vector_<int32_t>& sig, ss::vector_<uint32_t>& freq) {
	struct spectrum_scan_state_t ss;
	struct scan_internal_t si;
	ss.threshold = 3000;
	ss.threshold2 = 3000;
	ss.mincount = 1;
	assert(freq.size() == sig.size());
	stid135_spectral_scan_start(&ss, &si, sig.buffer(), freq.buffer(), sig.size());
	int ret = 0;
	s32 frequency;
	s32 snr;
	int j = 0;
	while (ret >= 0) {
		if (si.num_peaks >= si.max_num_peaks)
			break;
		ret = stid135_spectral_scan_next(&ss, &si, &frequency, &snr);
		if (ret >= 0) {
			si.peaks[si.num_peaks++] = si.last_peak;
#if 0
			dprintk("NP=%d\n", si.num_peaks);
#endif

		}
	}
	qsort(&si.peaks[0], si.num_peaks, sizeof(si.peaks[0]), cmp_fn);
	res.clear();
	for (j = 0; j < si.num_peaks; ++j) {
		spectral_peak_t p;
		p.freq= si.peaks[j].freq;
		p.symbol_rate = si.peaks[j].bw * 1250;
		p.snr = si.peaks[j].snr;
		p.level = si.peaks[j].level;
		res.push_back(p);
	}
}
