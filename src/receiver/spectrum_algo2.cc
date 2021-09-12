/*
 * Neumo dvb (C) 2019-2021 deeptho@gmail.com
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
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include <pybind11/stl_bind.h>
#include <stdio.h>

namespace py = pybind11;

typedef int32_t s32;
typedef uint8_t u8;

#define dprintk printf

struct spectrum_peak_t {
	s32 freq;				 // frequency of current peak
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
	s32 frequency_step; // bandwidth of one spectral bin in kHz
	int snr_w;					// window_size to look for snr peaks
	int threshold;			// minimum peak amplitude required
	int threshold2;			// minimum peak amplitude required
	int mincount;				// minimum number of above threshold detections to count as rise/fall

	// outputs
	bool spectrum_present;
	s32* freq;
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

static s32 max_(s32* a, int n) {
	int i;
	s32 ret = a[0];
	for (i = 0; i < n; ++i)
		if (a[i] > ret)
			ret = a[i];
	return ret;
}

static void clean_(s32* psig, s32* pres, int n) {
	int count = 0;
	bool peak_found = false;
	int level;
	int i;
	int k;
	s32 mean;
	s32 last = psig[0];
	int j;
	for (i = 0; i < n; ++i) {
		if (pres[i] == pres[last]) {
			mean += psig[i];
		} else {

			if (i > last)
				mean /= (i - last);
			for (j = last; j < i; ++j)
				psig[i] = mean;
			mean = psig[i];
			last = i;
		}
	}
}

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
	352, 384, 416, 448, 480, 512, 576, 640, 704, 768, 832, 896, 960, 1024, 2048};

static int check_candidate_tp(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	int i;
	s32 min_ = ss->spectrum[si->last_peak.idx];
	s32 max_ = ss->spectrum[si->last_peak.idx];
	s32 offset = (si->last_fall_idx - si->last_rise_idx) / 4;
	spectrum_peak_internal_t* cand = &si->last_peak;
	if (cand->snr < ss->threshold2) {
		dprintk("Rejecting too weak candidate\n");
		return -1;
	}
	for (i = 0; i < si->num_peaks; ++i) {
		spectrum_peak_internal_t* old = &si->peaks[i];
		// older contained/overlapping transponder with smaller bandwidth
		if (cand->bw >= old->bw && ((old->rise_idx >= cand->rise_idx && old->rise_idx <= cand->fall_idx) ||
																(old->fall_idx >= cand->rise_idx && old->fall_idx <= cand->fall_idx))) {
			if (old->level - cand->level >= ss->threshold2) {
				// dprintk("Rejecting peak because it contains other peaks\n");
				return -1;
			} else {
#if 0
				dprintk("Overwriting peak because it contains other peaks\n");
				*old = *cand;
				return -1;
#else
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * si->num_peaks - i - 1);
				--i;
				si->num_peaks--;
				continue;
#endif
			}
		}

		// older contained/overlapping transponder with larger bandwidth
		if ((cand->bw < old->bw && cand->rise_idx >= old->rise_idx && cand->rise_idx <= old->fall_idx) ||
				(cand->fall_idx >= old->rise_idx && cand->fall_idx <= old->fall_idx)) {
			if (cand->level - old->level >= ss->threshold2) {
#if 0
				dprintk("Rejecting peak because it contains other peaks\n");
				*old = *cand;
				return -1;
#else
				memmove(&si->peaks[i], &si->peaks[i + 1], sizeof(si->peaks[0]) * si->num_peaks - i - 1);
				si->num_peaks--;
				--i;
				continue;
#endif
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
																			 int len, int frequency_step) {
	si->max_num_peaks = 1024;
	ss->frequency_step = frequency_step;
	ss->spectrum = spectrum;
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
	ss->freq = (s32*)malloc(ss->spectrum_len * (sizeof(ss->freq[0])));
	si->rs = (s32*)malloc(ss->spectrum_len * (sizeof(si->rs[0])));
	si->num_peaks = 0;
	for (int i = 0; i < ss->spectrum_len; ++i)
		ss->freq[i] = i * ss->frequency_step;
	if (!ss->spectrum) {
		return -ENOMEM;
	}
	stid135_spectral_init_level(ss, si);
	return 0;
}

static int stid135_spectral_scan_end(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* spectrum,
																		 int len, int frequency_step) {
	int i;

	free(si->peak_marks);
	free(si->rs);
	si->num_peaks = 0;
	for (i = 0; i < ss->spectrum_len; ++i)
		ss->freq[i] = i * ss->frequency_step;
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

/*!
	returns index of a peak in the spectrum
*/

static int next_candidate_this_level(struct spectrum_scan_state_t* ss, struct scan_internal_t* si) {
	s32 snr;
	for (; si->current_idx < si->end_idx; ++si->current_idx) {
		if (si->peak_marks[si->current_idx] & FALLING) {
			if (si->last_rise_idx > si->last_fall_idx && si->last_rise_idx >= 0 &&
					si->current_idx - si->last_rise_idx <= si->w &&				 // maximum windows size
					si->current_idx - si->last_rise_idx >= (si->w * 2) / 3 // minimum windows size
				) {

				// candidate found; peak is between last_rise and current idx
				si->last_peak.idx = (si->last_rise_idx + si->current_idx) / 2;
				si->last_peak.freq = ss->freq[si->last_peak.idx]; // in kHz
				assert(si->current_idx - si->last_rise_idx <= si->w);
				si->last_peak.bw = ss->freq[si->current_idx] - ss->freq[si->last_rise_idx]; // in kHz
				dprintk("CANDIDATE: %d %dkHz BW=%dkHz snr=%ddB\n", si->last_peak.idx, si->last_peak.freq, si->last_peak.bw,
								snr);
				si->last_fall_idx = si->current_idx;
#if 0
				si->last_peak_snr = peak_snr(ss);
#else
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
	s32 offset;
	s32 w2;
	s32 n = ss->spectrum_len;
	int ret;
	while (si->window_idx < sizeof(windows) / sizeof(windows[0])) {
		if (si->current_idx >= si->end_idx) { // we reached end of a window
			if (++si->window_idx >= sizeof(windows) / sizeof(windows[0]))
				return -1;										 // all windows done
			si->w = windows[si->window_idx]; // switch to next window size
			stid135_spectral_init_level(ss, si);
		}
		ret = next_candidate_this_level(ss, si);
		if (ret < 0)
			continue;
		dprintk("CANDIDATE: %d %dkHz BW=%dkHz snr=%ddB\n", si->last_peak.idx, si->last_peak.freq, si->last_peak.bw,
						si->last_peak_snr);
		return ret;
	}
	return -1;
}

struct spectrum_scan_state_t ss;

static int stid135_spectral_scan_next(struct spectrum_scan_state_t* ss, struct scan_internal_t* si, s32* frequency_ret,
																			s32* snr_ret) {
	int ret = 0;
	while (ret >= 0) {
		ret = next_candidate_tp(ss, si);
		if (ret >= 0) {
			if (check_candidate_tp(ss, si) >= 0) {
				dprintk("Next frequency to scan: [%d] %dkHz SNR=%d BW=%d\n", ret, si->last_peak.freq, si->last_peak_snr,
								si->last_peak.bw);
				*frequency_ret = si->last_peak.freq;
				*snr_ret = si->last_peak_snr;
				return si->last_peak.idx;
			}
		} else {
			dprintk("Current subband fully scanned: current_idx=%d end_idx=%d\n", si->current_idx, si->end_idx);
		}
	}
	return -1;
}

static int cmp_fn(const void* pa, const void* pb) {
	spectrum_peak_internal_t* a = (spectrum_peak_internal_t*)pa;
	spectrum_peak_internal_t* b = (spectrum_peak_internal_t*)pb;
	return a->freq - b->freq;
}

static py::array_t<int> find_kernel_tps(py::array_t<int> sig, int w, int thresh, int mincount, int frequency_step) {
	py::buffer_info infosig = sig.request();
	if (infosig.ndim != 1)
		throw std::runtime_error("Bad number of dimensions");
	struct scan_internal_t si;
	ss.threshold = thresh;
	ss.threshold2 = thresh;
	si.w = w;
	ss.mincount = mincount;
	int* psig = (int*)infosig.ptr;
	int stridesig = infosig.strides[0] / sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int, py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found = false;
	int strideres = infosig.strides[0] / sizeof(int);
	int* pres = (int*)infores.ptr;
	assert(stridesig == 1);
	assert(strideres == 1);
	int last_rise = -1;
	int last_fall = -1;
	stid135_spectral_scan_start(&ss, &si, psig, sig.size(), frequency_step);
	int ret = 0;
	s32 frequency;
	s32 snr;
	int i = 0, j = 0;
	while (ret >= 0) {
		if (si.num_peaks >= si.max_num_peaks)
			break;
		ret = stid135_spectral_scan_next(&ss, &si, &frequency, &snr);
		auto bw = si.last_peak.bw;
		printf("FREQ=%d BW=%d SNR=%ddB\n", frequency, bw, snr);
		if (ret >= 0) {
			si.peaks[si.num_peaks++] = si.last_peak;
			dprintk("NP=%d\n", si.num_peaks);
		}
	}
	qsort(&si.peaks[0], si.num_peaks, sizeof(si.peaks[0]), cmp_fn);
	for (j = 0; j < si.num_peaks; ++j) {
		pres[i++ * strideres] = si.peaks[j].freq;
		pres[i++ * strideres] = si.peaks[j].bw;
		pres[i++ * strideres] = si.peaks[j].level;
		pres[i++ * strideres] = si.peaks[j].snr;
	}
	res.resize({i});
	res.resize({i / 4, 4});
	return res;
}

float windowed_max(float* p, int starti, int endi) {
	auto res = std::numeric_limits<float>::lowest();
	for (int i = starti; i < endi; ++i)
		res = std::max(res, p[i]);
	return res;
}

/*
	overlapping when both left aligned


    +------------+
		|     2      |
 		+------------+ y2
		x2           |
		             |
  +------------+ |
	|     1      |
	+------------+ y1
  x1           |
               |+------------+ y2+h
               ||     2      |
				       |+------------+ y2
				       |x2           |
				                     |
				                     |
                             x2+w

*/
static bool overlapping_la(int x1, float y1, int x2, float y2, int w, int h) {
	if (x2 >= x1 + w)
		return false;
	assert(x2 > x1);
	if (y2 >= y1 + h)
		return false;
	return true;
}

/*
overlapping when left + right aligned


   +------------+
   |     1      |
 	 +------------+ y1
   |     +------------+
   |     |     2      |
	 x1    +------------+ y2
                      |
                      |


  x1           |
               |+------------+ y2+h
               ||     2      |
				       |+------------+ y2
				       |x2           |
				                     |
				                     |
                             x2+w

 */

/*
	sig: spectrum signal
	annotx: list of annotation  x coordinates
	w/h=width/height of annotation box
	offset = vertical offset of annotation box
*/
static py::object find_annot_locations(py::array_t<float> sig, py::array_t<int> annotx, int w, float h, float offset) {
	py::buffer_info infosig = sig.request();
	if (infosig.ndim != 1)
		throw std::runtime_error("Bad number of dimensions");
	auto* psig = (float*)infosig.ptr;
	int stridesig = infosig.strides[0] / sizeof(int);

	py::buffer_info infoannotx = annotx.request();
	if (infoannotx.ndim != 1)
		throw std::runtime_error("Bad number of dimensions");
	int* px = (int*)infoannotx.ptr;
	int stridex = infoannotx.strides[0] / sizeof(int);
	int nx = infoannotx.shape[0];

	py::array_t<float, py::array::f_style> annoty(infoannotx.shape);
	py::buffer_info infoannoty = annoty.request();
	int strideannoty = infoannoty.strides[0] / sizeof(float);
	auto* py = (float*)infoannoty.ptr;

	py::array_t<uint8_t, py::array::f_style> leftrightflag(infoannotx.shape);
	py::buffer_info infoleftrightflag = leftrightflag.request();
	// int strideannoty = infoannoty.strides[0]/sizeof(int);
	auto* plr = (uint8_t*)infoleftrightflag.ptr;

	int n = infosig.shape[0];
	int na = infoannotx.shape[0];

	std::vector<float> lefty;
	std::vector<float> righty;
	lefty.resize(na);	 // labels right aligned, vertical position increases from left to right
	righty.resize(na); // labels left aligned, vertical position increases from right to left

	float scale = 1.0;
	float initial_maxy = std::numeric_limits<float>::lowest();
	float initial_miny = std::numeric_limits<float>::max();
	for (int i = 0; i < n; ++i) {
		initial_maxy = std::max(initial_maxy, psig[i]);
		initial_miny = std::min(initial_miny, psig[i]);
	}

	for (int attempt = 0; attempt < 1; ++attempt) {
		float max_increase = std::numeric_limits<float>::lowest();
		float y_at_max_increase = psig[0];
		for (int i = 0; i < na; ++i) {
			auto x = px[i];
			assert(x < n);
			lefty[i] = windowed_max(psig, std::max(0, x - w), x) + offset * scale;
			if (i > 0 && overlapping_la(px[i - 1] - w, lefty[i - 1], x - w, lefty[i], w, h * scale))
				lefty[i] = lefty[i - 1] + h;
		}

		for (int i = na - 1; i >= 0; --i) {
			auto x = px[i];

			assert(x < n);
			righty[i] = windowed_max(psig, x, std::min(n, x + w)) + offset;
			if (i < na - 1 &&
					overlapping_la(nx - 1 - px[i + 1] - w, righty[i + 1], nx - 1 - x - w, righty[i], w, h * scale)) {
				righty[i] = righty[i + 1] + h;
			}
		}

		float maxy = std::numeric_limits<float>::lowest();
		for (int i = 0; i < na; ++i) {
			auto y1 = lefty[i];
			auto y2 = righty[i];
#if 1
			if (i > 0 && plr[i - 1] == 0 &&		 // this sticks out to the right and is left aligned
					(px[i - 1] + w > px[i] - w) && // there is horizontal overlap
					(y1 <= py[i - 1] + h * scale)	 // and the rightmost box is lower
				) {
				// sticking out to the left is not allowed
				plr[i] = 0;
				py[i] = y2;
			} else {
				plr[i] = (y1 <= y2);
				py[i] = std::min(y1, y2);
			}
			maxy = std::max(py[i], maxy);
			auto increase = py[i] - psig[px[i]];
			if (increase > max_increase) {
				y_at_max_increase = psig[px[i]];
				max_increase = increase;
			}
#else
			plr[i] = false;
			py[i] = y2;

#endif
		}
		// scale = (initial_maxy-initial_miny) / (maxy -initial_miny);

		/*(1/scale)*(y_at_max_increase-initial_miny) + max_increase = (initial_maxy- initial_miny);
		 */
		scale = (initial_maxy + h + offset - initial_miny - max_increase) < 0
			? 2
			: (y_at_max_increase - initial_miny + h + offset) /
			(initial_maxy + h + offset - initial_miny - max_increase);
	}
	scale = 1.0;
	return py::make_tuple(annoty, leftrightflag, initial_miny,
												h + offset + scale * (initial_maxy - initial_miny) + initial_miny);
}

uint8_t test[500000];

PYBIND11_MODULE(pyspectrum, m) {
	m.doc() = R"pbdoc(

	)pbdoc";
	;
	m
#if 0
		.def("find_tps", &find_tps)
#endif
		.def("find_kernel_tps", &find_kernel_tps)
		.def("find_annot_locations", &find_annot_locations)
#if 0
		.def("rising", &rising)
		.def("falling", &falling)
		.def("morpho", &morpho)
#endif
		;
}
