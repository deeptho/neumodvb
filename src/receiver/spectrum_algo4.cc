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


#ifdef DEBUGXXX
void scan_internal_t::check() {
#if 0
	if(!debug_found)
		return;
	for(int i=0; i< num_peaks; ++i) {
		auto& cand  = peaks[i];
		if(cand.freq >= debug_freq -100 && cand.freq <= debug_freq +100) {
			dprintk("!!!! found\n");
			return;
		}
	}
	dprintk("!!!! no longer present\n");
#endif
}
#endif

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

static s32 windows[] = {
	2,    4,    6,    8,   10,   12,   14,   16,   18,   20,   22,
	24,   26,   28,   30,   32,   34,   36,   38,   40,   42,   44,
	46,   48,   50,   52,   54,   56,   58,   60,   62,   64,   66,
	68,   72,   74,   76,   80,   82,   86,   88,   92,   94,   98,
	102,  106,  108,  112,  116,  120,  126,  130,  134,  140,  144,
	150,  154,  160,  166,  172,  178,  184,  190,  198,  204,  212,
	220,  228,  236,  244,  252,  262,  270,  280,  290,  300,  312,
	322,  334,  346,  358,  370,  384,  398,  412,  426,  442,  456,
	474,  490,  508,  526,  544,  564,  584,  604,  626,  648,  670,
	694,  720,  744,  772,  798,  826,  856,  886,  918,  950,  984,
	1020, 1056, 1094, 1132, 1172, 1214, 1256, 1302, 1348, 1396, 1444,
	1496, 1548, 1604, 1660, 1720, 1780, 1844, 1910, 1976, 2048
};

static int check_candidate_tp(struct spectrum_scan_state_t* ss, struct scan_internal_t* si)
{
	int i;
	spectrum_peak_internal_t* cand = &si->last_peak;
	if (cand->mean_snr < ss->threshold2) {
#ifdef DEBUGXXX
		dprintk("Rejecting too weak candidate: %dkHz BW=%dkHz snr=%ddB/%ddB level=%ddB\n", cand->freq, cand->bw,
						cand->mean_snr, cand->min_snr, cand->min_level);
#endif
		return -1;
	}

	for (i = 0; i < si->num_peaks; ++i) {
		spectrum_peak_internal_t* old = &si->peaks[i];
		// older contained/overlapping transponder with smaller bandwidth
		if( cand->bw >= old->bw && ((old->rise_idx >= cand->rise_idx && old->rise_idx <= cand->fall_idx) ||
																(old->fall_idx >= cand->rise_idx && old->fall_idx <= cand->fall_idx))) {
			if( old->min_snr > cand->min_snr/*ss->threshold2*/) {
#ifdef DEBUGXXX
				dprintk("Rejecting peak because it contains other peaks: new: %dkHz BW=%dkHz old: %dkHz BW=%dkHz w=%d\n",
								cand->freq, cand->bw,
								old->freq, old->bw,
								si->w);
#endif
#ifdef DEBUGXXX
				si->check();
#endif
				return -1;
			} else {
#ifdef DEBUGXXX
				dprintk("Overwriting peak  %dkHz BW=%dkHz snr=%ddB/%ddB with broader peak (%d, %d) %dkHz BW=%dkHz snr=%ddB/%ddB\n",
								old->freq, old->bw, old->mean_snr, old->min_snr,
								cand->rise_idx, cand->fall_idx,
								cand->freq, cand->bw, cand->mean_snr, cand->min_snr
					);
#endif //TODO: handle equal tps

				assert(i>=0 && i < si->num_peaks);
				assert(i+1 < si->max_num_peaks);
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * (si->num_peaks - i - 1));
				--i;
				si->num_peaks--;
#ifdef DEBUGXXX
				si->check();
#endif
				continue;
			}
		}

		// older contained/overlapping transponder with larger bandwidth; cand has smaller window size
		if (cand->bw <= old->bw &&
				((cand->rise_idx >= old->rise_idx && cand->rise_idx <= old->fall_idx) ||
				 (cand->fall_idx >= old->rise_idx && cand->fall_idx <= old->fall_idx))) {
			if (cand->min_snr >=  old->min_snr/*ss->threshold2*/) {//larger window less noisy/more reliable
#ifdef DEBUGXXX
				dprintk("Removing older peak because it contains other peaks %dkHz BW=%dkHz\n", old->freq, old->bw);
#endif
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * (si->num_peaks - i - 1));
				si->num_peaks--;
				--i;
#ifdef DEBUGXXX
				si->check();
#endif
				continue;
			} else {
#ifdef DEBUGXXX
				si->check();
#endif
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
	s32 w = si->w;
	int n = ss->spectrum_len;
	int i;
	if (delta == 0)
		delta = 1;
	for (i = w; i < n - delta; ++i) {
		s32 power = (si->rs[i] - si->rs[i - w]) / w;
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
	s32 w = si->w;							// plateau interval
	int n = ss->spectrum_len;
	int i;
	if (delta == 0)
		delta = 1;
	for (i = n - w - 1; i >= delta; --i) {
		s32 power = (si->rs[i + w] - si->rs[i]) / w;
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
	si->last_peak.idx = -1;
	memset(si->peak_marks, 0, sizeof(si->peak_marks[0]) * ss->spectrum_len);
	falling_kernel(ss, si);
	rising_kernel(ss, si);
	// fix_kernel(si->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold, ss->mincount);
}

static inline int find_falling(struct scan_internal_t* si, int start, int end) {
	int i;
	for(i = start; i < end; ++i) {
		if (si->peak_marks[i] & FALLING) {
			return i;
		}
	}
	return -1;
}

static inline int find_rising(struct scan_internal_t* si, int start, int end) {
	int i;
	for(i = end; i > start; --i) {
		if (si->peak_marks[i] & RISING) {
			return i;
		}
	}
	return -1;
}


static int process_candidate(struct spectrum_scan_state_t* ss, struct scan_internal_t* si,
														 int rise_idx, int fall_idx) {
	assert(rise_idx > si->start_idx && rise_idx < fall_idx && fall_idx < si->end_idx);
	// candidate found; peak is between last_rise and current idx
	si->last_peak.idx = (rise_idx + fall_idx) / 2;
	si->last_peak.freq = ss->freq[si->last_peak.idx]; // in kHz

	si->last_peak.bw = ss->freq[fall_idx] - ss->freq[rise_idx]; // in kHz
	si->last_peak.mean_level =
		(si->rs[fall_idx] - si->rs[rise_idx]) / (fall_idx - rise_idx);
	{
		s32 delta, left, right, lowest_left, lowest_right, lowest_middle;
		int i;
		delta = (si->w * 15) / 100;
		if(delta < 2)
			delta = 2;
		left = rise_idx - delta;
		if(left < 0)
			left = 0;
		right = fall_idx + delta;
		if(right > ss->spectrum_len)
			right = ss->spectrum_len;
		lowest_left = ss->spectrum[left];
		lowest_right = ss->spectrum[right-1];
		assert(left >= 0);
		assert(right <= ss->spectrum_len);

		for(i=left ; i < rise_idx ; ++i) {
			if(lowest_left > ss->spectrum[i])
				lowest_left = ss->spectrum[i];
		}

		for(i=fall_idx+1; i < right ; ++i) {
			if(lowest_right > ss->spectrum[i])
				lowest_right = ss->spectrum[i];
		}

		delta = (si->w * 10) / 100;
		//delta =0;
		if(delta < 2)
			delta = 2;
		left = rise_idx + delta;
		if(left > fall_idx)
			left = fall_idx;
		right = fall_idx - delta;
		if(right < rise_idx)
			right = rise_idx;
		lowest_middle = ss->spectrum[left];
		for(i=left; i < right ; ++i) {
			if(lowest_middle > ss->spectrum[i])
				lowest_middle = ss->spectrum[i];
		}
		si->last_peak.min_level = lowest_middle;
		si->last_peak.mean_snr = si->last_peak.mean_level - std::max(lowest_right, lowest_left); //weakest dip defines noise level
		si->last_peak.min_snr = si->last_peak.min_level - std::max(lowest_right, lowest_left); //weakest dip defines noise level
#ifdef DEBUGXXX
		dprintk("CANDIDATE1: %d %dkHz [%d - %d] BW=%dkHz snr=%ddB/%ddB level=%ddB (%d %d) w=%d\n", si->last_peak.idx,
						si->last_peak.freq, ss->freq[rise_idx], ss->freq[fall_idx],
						si->last_peak.bw,
						si->last_peak.mean_snr, si->last_peak.min_snr, si->last_peak.min_level, lowest_left, lowest_right, si->w);
#endif
	}

	si->last_peak.fall_idx = fall_idx;
	si->last_peak.rise_idx = rise_idx;
	return 0;
}

static int stid135_spectral_scan_init(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* spectrum,
																			 u32* freq, int len) {
	si->max_num_peaks = 1024;
	ss->spectrum = spectrum;
	ss->freq = freq;
	ss->spectrum_len = len;

	ss->scan_in_progress = true;

	// ss->w =17;
	ss->snr_w = 35; //percentage
	// ss->threshold = 2000;
	// ss->mincount = 3;

	si->peak_marks = (u8*)malloc(ss->spectrum_len * (sizeof(si->peak_marks[0])));
	si->peaks = (struct spectrum_peak_internal_t*)malloc(si->max_num_peaks * (sizeof(si->peaks[0])));
	si->rs = (s32*)malloc(ss->spectrum_len * (sizeof(si->rs[0])));
	running_sum(si->rs, ss->spectrum, ss->spectrum_len);
	si->num_peaks = 0;
	if (!ss->spectrum) {
		return -ENOMEM;
	}
	return 0;
}





static int scan_all(struct spectrum_scan_state_t *ss, struct scan_internal_t *si,
										 s32* spectrum, u32* freq, int spectrum_len) {
	int window_idx=0;
	int fall_idx;
	int rise_idx;
	stid135_spectral_scan_init(ss, si, spectrum, freq, spectrum_len);
	for(window_idx=0; window_idx < (int)(sizeof(windows) / sizeof(windows[0])); ++window_idx) {
		si->w = windows[window_idx];
		stid135_spectral_init_level(ss, si);
		fall_idx = find_falling(si, si->start_idx, si->end_idx);
		for(; fall_idx < si->end_idx && fall_idx >=0; fall_idx = find_falling(si, fall_idx+1, si->end_idx)) {
			int ll = fall_idx - (si->w * 150)/100;
			if (ll < si->start_idx)
				ll  = si->start_idx;
			rise_idx = find_rising(si, ll, fall_idx - si->w);
			for(; rise_idx >= ll && rise_idx>=0; rise_idx = find_rising(si, ll, rise_idx-1)) {
				process_candidate(ss, si, rise_idx, fall_idx);
				if (check_candidate_tp(ss, si) >= 0) {

#if 0
					dprintk("Next frequency to scan: [%d] %dkHz SNR=%d level=%ddB BW=%d\n", ret, si->last_peak.freq, si->last_peak.snr, si->last_peak.level,
									si->last_peak.bw);
#endif
					si->peaks[si->num_peaks++] = si->last_peak;
					if (si->num_peaks >= si->max_num_peaks)
						return -1;
				}
			}
		}
	}
	return 0;
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
	int j = 0;

	scan_all(&ss, &si, sig.buffer(), freq.buffer(), sig.size());

	qsort(&si.peaks[0], si.num_peaks, sizeof(si.peaks[0]), cmp_fn);
	res.clear();
	for (j = 0; j < si.num_peaks; ++j) {
		spectral_peak_t p;
		p.freq= si.peaks[j].freq;
		p.symbol_rate = si.peaks[j].bw* 1250;
		p.snr = si.peaks[j].mean_snr;
		p.level = si.peaks[j].min_level;
		res.push_back(p);
	}
}
