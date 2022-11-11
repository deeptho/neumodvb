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
#include "spectrum_algo_private.h"

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
	bool print1 = (cand->freq/1000 >=10717) && (cand->freq/1000 <=10742) ;
	if (cand->mean_snr < ss->threshold2) {
#if 1
		if(print1)
			dprintk("Rejecting too weak candidate: %dkHz BW=%dkHz snr=%ddB/%ddB level=%ddB\n", cand->freq, cand->bw,
							cand->mean_snr, cand->dip_snr, cand->dip_level);
#endif
		return -1;
	}

	for (i = 0; i < si->num_peaks; ++i) {
		spectrum_peak_internal_t* old = &si->peaks[i];
		bool print =  (old->freq/1000 >=10717) && (old->freq/1000 <=10742) ;
		print |= print1;
		bool overlaps = ((old->rise_idx >= cand->rise_idx && old->rise_idx <= cand->fall_idx) ||
										 (old->fall_idx >= cand->rise_idx && old->fall_idx <= cand->fall_idx));
		if( cand->bw >= old->bw && overlaps) {

			if( old->dip_snr > cand->dip_snr/*ss->threshold2*/) {
#if 1
				if(print)
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
#if 1
				if(print)
					dprintk("Overwriting peak (%d, %d) %dkHz BW=%dkHz snr=%ddB/%ddB with broader peak (%d, %d) %dkHz BW=%dkHz snr=%ddB/%ddB\n",
									old->rise_idx, old->fall_idx,
									old->freq, old->bw, old->mean_snr, old->dip_snr,
									cand->rise_idx, cand->fall_idx,
									cand->freq, cand->bw, cand->mean_snr, cand->dip_snr
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
			if (cand->dip_snr >=  old->dip_snr/*ss->threshold2*/) {//larger window less noisy/more reliable
#if 1
				if(print)
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

static void falling_kernel(spectrum_scan_state_t* ss, struct scan_internal_t* si,
													 float* response_ret) {
	s32 delta = (si->w * 16) / 200;
	s32 w = si->w;
	int n = ss->spectrum_len;
	int i;
	int besti{-1};
	float best{0};
	if(delta <= 2 || 2*delta >= si->w)
		delta = 1;
	if (delta == 0)
		delta = 1;
	for (i = w; i < n - delta; ++i) {
		s32 power = (si->rs[i] - si->rs[i - w]) / w;
		s32 right = ss->spectrum[i + delta];
		auto response = power - right;
		if (response > ss->threshold) {
			// mark complete peak if not already on a peak
			if(besti >=0 && i - besti >= si->w/4)
				besti = -1;
			if(besti >=0 && best <= response) {
				si->peak_marks[besti] &= ~FALLING;
				if (response_ret)
					response_ret[besti] = 0;
				besti = -1;
			}

			if(besti >= 0 && best  > response)
				continue; //current peak is weak and overlaps with previous one

			si->peak_marks[i] |= FALLING;
			if (response_ret)
				response_ret[i] = response;
			if(response > best || besti<0) {
				besti = i;
				best = response;
			}
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
static void rising_kernel(spectrum_scan_state_t* ss, struct scan_internal_t* si,
													float* response_ret) {
	s32 delta = (si->w * 16) / 200; // rise interval
	s32 w = si->w;							// plateau interval
	int n = ss->spectrum_len;
	int i;
	int besti{-1};
	float best{0};
	int count{0};
	if(delta <= 2 || 2*delta >= si->w)
		delta = 1;
	if (delta == 0)
		delta = 1;
	for (i = n - w - 1; i >= delta; --i) {
		s32 power = (si->rs[i + w] - si->rs[i]) / w;
		s32 left = ss->spectrum[i - delta];
		auto response = power - left;
		if (response > ss->threshold) {
			// erase small overlapping peaks
			if(besti >=0 && besti - i >= si->w/4)
				besti = -1;
			if(besti >=0 && best <= response) {
				count -= 	!!(si->peak_marks[besti] & ~RISING);
				si->peak_marks[besti] &= ~RISING;
				if (response_ret)
					response_ret[besti] = 0;
				besti = -1;
			}

			if(besti >= 0 && best  > response)
				continue; //current peak is weak and overlaps with previous one

			si->peak_marks[i] |= RISING;
			count++;
			if (response_ret)
				response_ret[i] = response;
			if(response > best || besti<0) {
				besti = i;
				best = response;
			}
		}
	}
	printf("w=%d; %d rising peaks\n", si->w, count);
}


void stid135_spectral_init_level(struct spectrum_scan_state_t* ss,
																 struct scan_internal_t* si,
																 float* falling_response_ret,
																 float* rising_response_ret) {
	si->start_idx = (si->w * 16) / 200;
	if (si->start_idx == 0)
		si->start_idx++;
	si->end_idx = ss->spectrum_len - (si->w * 16) / 200;
	if (si->end_idx == ss->spectrum_len)
		si->end_idx--;
	si->last_peak.idx = -1;
	memset(si->peak_marks, 0, sizeof(si->peak_marks[0]) * ss->spectrum_len);
	falling_kernel(ss, si, falling_response_ret);
	rising_kernel(ss, si, rising_response_ret);
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
	s32 delta, left, right, lowest_left, lowest_right, lowest_middle;
	int i, lowest_left_i, lowest_right_i, left_3db_i, right_3db_i;
	int left_top_i, right_top_i;
	s32 thresh;
	int count;
	if(rise_idx == 250 && fall_idx==964 && si->w==490)
		printf("here\n");
	delta = (si->w * 16) / 100; //15% one sided extension
	if(delta <= 2 || 2*delta >= si->w)
		delta = 1;
	if (delta == 0)
		delta = 1;
	assert(rise_idx > si->start_idx && rise_idx < fall_idx && fall_idx < si->end_idx);
	// candidate found; peak is between last_rise and current idx
	assert(fall_idx - delta < ss->spectrum_len);
	assert(rise_idx + delta >=0);

	//compute average height of peak
	si->last_peak.mean_level =
		(si->rs[fall_idx] - si->rs[rise_idx]) / (fall_idx - rise_idx);
	//compute threshold for bandwidth computation
	thresh = si->last_peak.mean_level -3000;

	//comute the deepest values near the peak
	left = rise_idx - delta;
	if(left < 0)
		left = 0;
	right = fall_idx + delta;
	if(right > ss->spectrum_len)
		right = ss->spectrum_len;

	lowest_left = ss->spectrum[left];
	lowest_left_i = left;
	lowest_right = ss->spectrum[right-1];
	lowest_right_i = right - 1;
	assert(left >= 0);
	assert(right <= ss->spectrum_len);

	for(i=left ; i < rise_idx ; ++i) {
		if(lowest_left > ss->spectrum[i]) {
			lowest_left = ss->spectrum[i];
			lowest_left_i = i;
		}
	}
	for(i=fall_idx+1; i < right ; ++i) {
		if(lowest_right > ss->spectrum[i]) {
			lowest_right = ss->spectrum[i];
			lowest_right_i = i;
		}
	}

	//compute the 3dB limits of the peak
	for(i=lowest_left_i, count=0; i < lowest_right_i; ++i) {
		if(ss->spectrum[i] >= thresh) {
			if(count==0)
				left_3db_i = i;
			if(++count >= si->w/8) //require at least si->w //8 values above threshold
				break;
		} else
			count=0;
	}
	if(count==0)
		left_3db_i = lowest_left_i;

	for(i=lowest_right_i, count=0; i >= lowest_left_i; --i) {
		if(ss->spectrum[i] >= thresh) {
			if(count==0)
				right_3db_i = i;
			if(++count >= si->w/8) //require at least si->w //8 values above threshold
				break;
		} else
			count = 0;
	}
	if(count==0)
		right_3db_i = lowest_right_i;

	assert(right_3db_i >= left_3db_i);

	//compute bounds of top plateau
	for(i=left_3db_i; i <= right_3db_i; ++i) {
		if(ss->spectrum[i] >= si->last_peak.mean_level - 500)
			break;
	}
	left_top_i = i;
	for(i=right_3db_i; i > left_top_i; --i) {
		if(ss->spectrum[i] >= si->last_peak.mean_level - 500)
			break;
	}
	right_top_i = i;

	//compute the lowest dip level in central peak
	si->last_peak.dip_level = ss->spectrum[left_3db_i];
	for(i=left_3db_i; i <= right_3db_i; ++i) {
		if(si->last_peak.dip_level > ss->spectrum[i])
			si->last_peak.dip_level = ss->spectrum[i];
	}
	si->last_peak.idx = (right_3db_i + left_3db_i) / 2;
	si->last_peak.dip_snr = si->last_peak.dip_level -  std::min(lowest_right, lowest_left);
	si->last_peak.freq = ss->freq[si->last_peak.idx]; // in kHz
	si->last_peak.bw = ss->freq[right_3db_i] - ss->freq[left_3db_i]; // in kHz
	si->last_peak.fall_idx = right_3db_i;
	si->last_peak.rise_idx = left_3db_i;

	si->last_peak.mean_snr = si->last_peak.mean_level - std::min(lowest_right, lowest_left); //weakest di	si->last_peak.dip_snr = si->last_peak.dip_level - std::mean(lowest_right, lowest_left); //weakest dip defines noise level
	if((si->last_peak.freq/1000 >=10729) && (si->last_peak.freq/1000 <=10729))
		printf("here\n");
#ifdef DEBUGXXX
	dprintk("CANDIDATE1: %d %dkHz [%d - %d] "
					"BW=%dkHz snr=%dmdB "
					"level=[%d %d %d]mdB w=%d\n",
					si->last_peak.idx, si->last_peak.freq, ss->freq[lowest_left_i], ss->freq[lowest_right_i],
					si->last_peak.bw, si->last_peak.mean_snr,
					ss->spectrum[lowest_left_i], si->last_peak.mean_level, ss->spectrum[lowest_right_i],
					si->w);
#endif

	return 0;
}

int stid135_spectral_scan_init(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* spectrum,
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

static int scan_level(struct spectrum_scan_state_t *ss, struct scan_internal_t *si,
											s32* spectrum, u32* freq, int spectrum_len, int w) {
	int fall_idx;
	int rise_idx;
	si->w = w;
	stid135_spectral_init_level(ss, si);
	fall_idx = find_falling(si, si->start_idx, si->end_idx);
	for(; fall_idx < si->end_idx && fall_idx >=0;
			fall_idx = find_falling(si, fall_idx+1, si->end_idx)) {
		//find  a pair of rising and falling peaks, separaed by 1 to 1.5 times si.w
		int ll = fall_idx - (si->w * 150)/100;
		if (ll < si->start_idx)
			ll  = si->start_idx;
		rise_idx = find_rising(si, ll, fall_idx - si->w);
		for(; rise_idx >= ll && rise_idx>=0; rise_idx = find_rising(si, ll, rise_idx-1)) {
			//compute parameters of the candidate peak
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
	return 0;
}

static int scan_all(struct spectrum_scan_state_t *ss, struct scan_internal_t *si,
										 s32* spectrum, u32* freq, int spectrum_len) {
	int window_idx=0;
	stid135_spectral_scan_init(ss, si, spectrum, freq, spectrum_len);
	for(window_idx=0; window_idx < (int)(sizeof(windows) / sizeof(windows[0])); ++window_idx) {
		auto w = windows[window_idx];
		scan_level(ss, si, spectrum, freq, spectrum_len, w);
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
		p.symbol_rate = si.peaks[j].bw*1000; //top plateau approx SR. 10% underestimation
		p.snr = si.peaks[j].mean_snr;
		p.level = si.peaks[j].mean_level;
		res.push_back(p);
	}
}
