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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //for std::optional
#include <pybind11/stl_bind.h>
#include <pybind11/numpy.h>
#include <stdio.h>


namespace py = pybind11;

typedef int32_t s32;
typedef uint8_t u8;

#define dprintk printf
struct spectrum_scan_state_t {
	bool spectrum_present;
	bool scan_in_progress;

	s32* freq;
	s32* spectrum;
	s32* rs; //running sum

	int spectrum_len;
	int fft_size; //for fft
	s32 sample_step; //bandwithj of one spectral bin in kHz
	s32 start_frequency;
	s32 end_frequency;
	s32 frequency_step;
	s32 range; //bandwidth of current fft in kHz (covers the whole spectrum, not just the useable part)
	int mode; //for fft

	s32 start_idx ;//analysis starts at spectrum[start_idx]
	s32 end_idx ;//analysis end at spectrum[end_idx]-1
	s32 current_idx; //position at which we last stopped processing
	s32 window_idx ; //index of current window size
	s32 next_frequency; // If we found a transponder last time, this is the frequency just above the transponder bandwidth

	s32 last_peak_idx; //index at which we last found a peak
	s32 last_peak_freq; //frequency of current peak
	s32 last_peak_bw; //frequency of current peak
	bool last_peak_reported;

	s32 last_rise_idx; //location of last processed rising peak
	s32 last_fall_idx; //location of last processed falling peak
	s32 last_peak_snr; //snr of last detected candidate

	int w; //window_size to look for peaks
	int snr_w; //window_size to look for snr peaks
	int threshold; //minimum peak amplitude required
	int mincount; //minimum number of above threshold detections to count as rise/fall
	s32 lo_frequency_hz;


	u8* peak_marks;

};


enum slope_t {
	NONE = 0,
	FALLING=1,
	RISING=2
};

#define MAXW 71

static int32_t max_(int32_t*a, int n)
{
	int i;
	int32_t ret=a[0];
	for(i=0; i<n;++i)
		if(a[i]> ret)
			ret=a[i];
	return ret;
}


static void morpho_2(s32* pmax, s32*pmin, int n)
{
	int count=0;
	bool peak_found=false;
	int i;
	for(i=0; i< n-1; ++i) {
		pmax[i] = pmax[i]>pmax[i+1] ? pmax[i]: pmax[i+1];
		pmin[i] = pmin[i]<pmin[i+1] ? pmin[i]: pmin[i+1];
	}
}

static void morpho_(const s32*psig, s32*pres, s32* pmax, s32*pmin,  int n, int max_level, int thresh)
{
	int count=0;
	bool peak_found=false;
	int level;
	int i;
	int k;
	memset(pres, 0, sizeof(pres[0])*n);
	memcpy(pmin, psig, sizeof(psig[0])*n);
	memcpy(pmax, psig, sizeof(psig[0])*n);
	for(level=1; level<= max_level; ++level) {
		morpho_2(pmax, pmin, n);
		for (i=0; i< n-level; ++i)
			if(pmax[i]-pmin[i]<=thresh) {
				for(k=0; k < level && i+k <n;++k)
					pres[i] = level;
			}
	}
}

static void clean_(s32*psig, s32*pres, int n)
{
	int count=0;
	bool peak_found=false;
	int level;
	int i;
	int k;
	s32 mean;
	s32 last = psig[0];
	int j;
	for (i=0; i< n; ++i) {
		if(pres[i] == pres[last]) {
			mean += psig[i];
		} else {

			if(i>last)
				mean/= (i-last);
			for(j=last; j<i;++j)
				psig[i] = mean;
			mean =psig[i];
			last=i;
		}
	}
}

static void running_sum(int32_t* pout, int32_t* psig, int n)
{
	int i;
	int accu=psig[0];
	for(i=0;i<n;++i) {
		accu += psig[i];
		pout[i]=accu;
	}
}

//static s32 windows[]={985,  657, 437, 291, 195, 129,  85,  57,  37,  25,  17,  11,   7, 5,   3};

static s32 windows[]={985,  657};
//static s32 windows[]={437};

static int stid135_spectral_scan_startV2(struct spectrum_scan_state_t* ss, s32*spectrum, int len)
{

	ss->spectrum =spectrum;
	ss->spectrum_len = len;

	ss->scan_in_progress =true;

	ss->window_idx = 0;
	ss->w = windows[ss->window_idx];
	ss->start_idx = ((windows[0]-1)*115)/200;
	ss->end_idx = ss->spectrum_len - ss->start_idx;
	ss->current_idx = ss->start_idx;

	ss->last_peak_idx = -1;
	ss->last_rise_idx = -1;
	ss->last_fall_idx = -1;
	ss->last_peak_reported = true;
	//ss->w =17;
	ss->snr_w = 35; ////percentage
	//ss->threshold = 2000;
	//ss->mincount = 3;

	ss->peak_marks = (u8*)malloc(ss->spectrum_len * (sizeof(ss->peak_marks[0])));
	ss->freq = (s32*)malloc(ss->spectrum_len * (sizeof(ss->freq[0])));
	ss->rs = (s32*)malloc(ss->spectrum_len * (sizeof(ss->rs[0])));

	for(int i=0; i <ss->spectrum_len; ++i)
		ss->freq[i]=i*ss->frequency_step;
	if (!ss->spectrum) {
		return -ENOMEM;
	}

	memset(ss->peak_marks, 0, sizeof(ss->peak_marks[0])*ss->spectrum_len);
	running_sum(ss->rs , ss->spectrum, ss->spectrum_len);
	return 0;
}

static int next_candidate_tpV2(struct spectrum_scan_state_t* ss)
{
	s32 offset;
	s32 w2;
	s32 n = ss->spectrum_len;

	while(ss->window_idx <  sizeof(windows)/sizeof(windows[0])) {
		if (ss->current_idx >= ss->end_idx) { //we reached end of a window
			if(++ss->window_idx >=  sizeof(windows)/sizeof(windows[0]))
				return -1; //all windows done
			ss->w = windows[ss->window_idx]; //switch to next window size
			offset = ((ss->w-1)*115)/200;
			ss->start_idx = offset;
			ss->end_idx = n - offset;
			ss->current_idx = ss->start_idx;
		}
		offset = ((ss->w-1)*115)/200;
		w2= (ss->w-1)/2;

		for(; ss->current_idx < ss->end_idx; ++ ss->current_idx) {
			s32 power = (ss->rs[ss->current_idx + w2]- ss->rs[ss->current_idx -w2])/ss->w;
			s32 left = ss->spectrum[ss->current_idx - offset];
			s32 right = ss->spectrum[ss->current_idx + offset];
			if(power - left >= ss->threshold && power-right >= ss->threshold) {
				s32 snr = left<right ? power - left : power - right;
				if(!ss->last_peak_reported) { //candidate was found but not reported
					if(snr < ss->last_peak_snr) {
						//current snr is worse
						continue;
					}
				}
				ss->last_peak_reported = false; //new peak
				ss->last_peak_idx = ss->current_idx;
				ss->last_rise_idx = ss->current_idx - offset;
				ss->last_fall_idx = ss->current_idx + offset;
				ss->last_peak_freq =
					ss->freq[ss->current_idx]; //in kHz
				ss->last_peak_bw = ss->freq[ss->last_fall_idx] - ss->freq[ss->last_rise_idx]; //in kHz
				ss->last_peak_snr = snr;
#if 0
				dprintk("pre-CANDIDATE: %d %dkHz BW=%dkHz snr=%ddB\n", ss->last_peak_idx, ss->last_peak_freq,
								ss->last_peak_bw, ss->last_peak_snr);
#endif
			} else { //below threshold again
				if(!ss->last_peak_reported) {
					ss->last_peak_reported = true;
					return ss->last_peak_idx;
				}
			}
		}
	}

	if(!ss->last_peak_reported) {
		ss->last_peak_reported = true;
		return ss->last_peak_idx;
	}
	return -1;
}



/*
	candidate right edges of transponder
	w is window size
	n is size of signal
*/
static void falling_kernel(uint8_t* pres, int32_t* psig, int n, int w, int thresh, int mincount)
{
	int count=0;
	bool peak_found=false;
	int i;
	int32_t temp[MAXW];
	if(w >MAXW)
		w=MAXW;
	for(i=0;i<w;++i)
		temp[i]=psig[n-1];
	for(i=0; i< n; ++i) {
		int32_t s = psig[i];
		int32_t left_max;
		temp[i%w] = s;
		left_max = max_(temp, w);
		if (left_max-s > thresh)
			count++;
		else
			count =0;
		if(count>=mincount) {
			//mark complete peak if not already on a peak
			if(!peak_found) {
				//printk("SET FALL\n");
				pres[i] |=FALLING;
			}
			peak_found = true;
		} else {
			peak_found =false;
		}
	}
}

/*
	candidate left edges of transponder
	w is window size
	n is size of signal
*/
static void rising_kernel(uint8_t* pres, int32_t* psig, int n, int w, int thresh, int mincount)
{
	int count=0;
	bool peak_found=false;
	int i;
	s32 temp[MAXW];
	if(w>MAXW)
		w=MAXW;
	for(i=0;i<w;++i)
		temp[i]=psig[n-1];
	for(i=n-1; i>=0; --i) {
		int32_t s = psig[i];
		int32_t right_max;
		temp[i%w] = s;
		right_max = max_(temp, w);
		if (right_max-s > thresh)
			count++;
		else
			count =0;
		if(count>=mincount) {
			//mark complete peak if not already on a peak
			if(!peak_found) {
				//printk("SET RISE\n");
					pres[i] |= RISING;
			}
			peak_found = true;
		} else {
			peak_found =false;
		}
	}
}

static void fix_kernel(uint8_t* pres, int32_t* psig, int n, int w, int thresh, int mincount)
{

	//bool peak_found=false;
	int i;
	//int32_t temp[MAXW];
	int last=0;
	if(w>MAXW)
		w=MAXW;


	for(i=0; i<n; ++i) {
		if(i>0) {
			if(pres[i]!=NONE) {
				if((pres[i] & RISING ) && (pres[last] &RISING)) {
					falling_kernel(pres+last, psig+last, i-last+1, w, thresh, mincount);
				}
				if((pres[i] & FALLING ) && (pres[last] & FALLING)) {
					rising_kernel(pres+last, psig+last, i-last+1, w, thresh, mincount);
				}
				last = i;
			}
		}
	}
}

static s32 peak_snr(struct spectrum_scan_state_t* ss)
{
	s32 mean=0;
	s32 min1=0;
	s32 min2=0;
	int i;
	s32 w = (ss->snr_w * (ss->last_fall_idx - ss->last_rise_idx))/100;
	if( ss->last_fall_idx<=  ss->last_rise_idx)
		return -99000;
	for(i=ss->last_rise_idx; i< ss->last_fall_idx; ++i)
		mean += ss->spectrum[i];
	mean /=(ss->last_fall_idx - ss->last_rise_idx);

	i= ss->last_rise_idx - w;
	if(i<0)
		i=0;
	min1 = ss->spectrum[ss->last_rise_idx];
	for(; i < ss->last_rise_idx; ++i)
		if (ss->spectrum[i] < min1)
			min1 = ss->spectrum[i];

	i= ss->last_fall_idx + w;
	if(i> ss->spectrum_len)
		i = ss->spectrum_len;
	min2 = ss->spectrum[ss->last_fall_idx];
	for(; i > ss->last_fall_idx; --i)
		if (ss->spectrum[i] < min2)
			min2 = ss->spectrum[i];

	if (min2<min1)
		min1= min2;
	return mean - min1;
}


//returns index of a peak in the spectrum
static int next_candidate_tpV1(struct spectrum_scan_state_t* ss)
{
	s32 snr;
	for(; ss->current_idx < ss->end_idx; ++ss->current_idx) {
		if(ss->peak_marks[ss->current_idx] & FALLING) {
#if 0
			dprintk("FALLING FOUND at %d last_rise=%d last fall =%d\n",
							ss->current_idx, ss->last_rise_idx, ss->last_fall_idx);
#endif
			if(ss->last_rise_idx > ss->last_fall_idx && ss->last_rise_idx>=0) {

				//candidate found; peak is between last_rise and current idx
				ss->last_peak_idx = (ss->last_rise_idx + ss->current_idx)/2;
				ss->last_peak_freq =
					ss->freq[ss->last_peak_idx]; //in kHz
				ss->last_peak_bw = ss->freq[ss->current_idx] - ss->freq[ss->last_rise_idx]; //in kHz
				dprintk("CANDIDATE: %d %dkHz BW=%dkHz snr=%ddB\n", ss->last_peak_idx, ss->last_peak_freq,
								ss->last_peak_bw, snr);
				ss->last_fall_idx = ss->current_idx;
				ss->last_peak_snr = peak_snr(ss);
				if(ss->peak_marks[ss->current_idx]& RISING)
					ss->last_rise_idx = ss->current_idx;
				ss->current_idx++;
				return ss->last_peak_idx;
			}

			ss->last_fall_idx = ss->current_idx;
		}

		if(ss->peak_marks[ss->current_idx]& RISING)
			ss->last_rise_idx = ss->current_idx;
	}
	return -1;
}



int stid135_spectral_scan_startV1(struct spectrum_scan_state_t* ss, s32*spectrum, int len, int frequency_step)
{

	ss->frequency_step = frequency_step;
	ss->spectrum =spectrum;
	ss->spectrum_len = len;

	ss->scan_in_progress =true;

	ss->start_idx =  0;
	ss->end_idx = ss->spectrum_len;
	ss->current_idx = 0;
	ss->last_peak_idx = -1;
	ss->last_rise_idx = -1;
	ss->last_fall_idx = -1;

	//ss->w =17;
	ss->snr_w = 35; ////percentage
	//ss->threshold = 2000;
	//ss->mincount = 3;

	ss->peak_marks = (u8*)malloc(ss->spectrum_len * (sizeof(ss->peak_marks[0])));
	ss->freq = (s32*)malloc(ss->spectrum_len * (sizeof(ss->freq[0])));
	ss->rs = (s32*)malloc(ss->spectrum_len * (sizeof(ss->rs[0])));

	for(int i=0; i <ss->spectrum_len; ++i)
		ss->freq[i]=i*ss->frequency_step;
	if (!ss->spectrum) {
		return -ENOMEM;
	}

	memset(ss->peak_marks, 0, sizeof(ss->peak_marks[0])*ss->spectrum_len);
	falling_kernel(ss->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold, ss->mincount);
	rising_kernel(ss->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold, ss->mincount);
	fix_kernel(ss->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold, ss->mincount);
//	fix_kernel(ss->peak_marks, ss->spectrum, ss->spectrum_len, ss->w, ss->threshold);
	//dump_data("marks", ss->peak_marks, ss->spectrum_len);
	return 0;
}

struct spectrum_scan_state_t ss;

int stid135_spectral_scan_next(struct spectrum_scan_state_t* ss,   s32 *frequency_ret, s32* snr_ret)
{
	int ret=0;
	while(ret>=0) {
		ret = next_candidate_tpV1(ss);
		if(ret>=0) {
			dprintk("Next frequency to scan: [%d] %dkHz SNR=%d BW=%d\n", ret, ss->last_peak_freq,
							ss->last_peak_snr, ss->last_peak_bw);
			*frequency_ret =  ss->last_peak_freq;
			*snr_ret =  ss->last_peak_snr;
			return  ss->last_peak_idx;
		} else {
			dprintk("Current subband fully scanned: current_idx=%d end_idx=%d\n", ss->current_idx, ss->end_idx);
		}
	}
	return -1;
}

py::array_t<int> find_kernel_tps(py::array_t<int> sig, int w, int thresh, int mincount, int frequency_step)
{
	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");
	ss.threshold = thresh;
	ss.w = w;
	ss.mincount = mincount;
	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int,  py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infosig.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	assert(stridesig==1);
	assert(strideres==1);
	int last_rise=-1;
	int last_fall=-1;
	stid135_spectral_scan_startV1(&ss, psig, sig.size(), frequency_step);
	int ret= 0;
	s32 frequency;
	s32 snr;
	int i=0;
	while(ret>=0) {
		ret=stid135_spectral_scan_next(&ss,   &frequency, &snr);
		auto bw = ss.freq[ss.current_idx] - ss.freq[ss.last_rise_idx];
		printf("FREQ=%d BW=%d SNR=%ddB\n", frequency, bw, snr);
		if(ret>=0) {
			pres[i++*strideres]= frequency;
			pres[i++*strideres]= bw;
			pres[i++*strideres]= snr;
		}
	}
	res.resize({i});
	res.resize({i/3,3});
	return res;
}

py::array_t<int> morpho(py::array_t<int> sig, int max_level, int thresh)
{
	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");
	ss.threshold = thresh;
	//ss.w = w;
	//s.max_level = max_level;
	//ss.mincount = mincount;

	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	auto s = infosig.shape;
	s[0] *=3;

	py::array_t<int,  py::array::f_style> res(s);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infosig.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	assert(stridesig==1);
	assert(strideres==1);
	int last_rise=-1;
	int last_fall=-1;
	int ret= 0;
	s32 frequency;
	s32 snr;

	morpho_(psig, &pres[0], &pres[n], &pres[2*n], n, max_level, thresh);
	memcpy(&pres[n], psig, n*sizeof(psig[0]));
	clean_(&pres[n], pres, n);
	res.resize({3*n});
	res.resize({3,n});
	return res;
}

static void falling_(int* pres, int* psig, int n, int w, int thresh)
{
	bool peak_found=false;
	std::vector<int> temp1(w);
	std::fill(temp1.begin(), temp1.end(), psig[0]);

	for(int i=0; i< n; ++i) {
		int j1 = i%w;
		auto s = psig[i];
		auto& r = pres[i];
		temp1[j1] = s;
		auto it = max_element(std::begin(temp1), std::end(temp1)); // c++11
		auto left_max = *it;
		auto xxx= max_(&temp1[0],w);
		assert(xxx==left_max);
		r =0;
		int count=0;
		if(left_max-s > thresh) {
			//mark complete peak if not already on a peak
			if(!peak_found) {
					pres[i]= 1;
				}
			peak_found = true;
		} else {
			peak_found =false;
		}
	}
}

static void rising_(int* pres, int* psig, int n, int w, int thresh)
{
	bool peak_found=false;
	std::vector<int> temp1(w);
	std::fill(temp1.begin(), temp1.end(), psig[n-1]);

	for(int i=n-1; i>=0; --i) {
		int j1 = i%w;
		auto s = psig[i];
		auto& r = pres[i];
		temp1[j1] = s;
		auto it = max_element(std::begin(temp1), std::end(temp1)); // c++11
		auto right_max = *it;
		auto xxx= max_(&temp1[0],w);
		assert(xxx==right_max);
		r =0;
		int count=0;
		if(right_max-s > thresh) {
			//mark complete peak if not already on a peak
			if(!peak_found) {
					pres[i]= 1;
				}
			peak_found = true;
		} else {
			peak_found =false;
		}
	}
}




uint8_t test[500000];
py::array_t<int> find_tps(py::array_t<int> sig, int w, int thresh, int mincount)
{

	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");

	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int,  py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infores.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	assert(stridesig==1);
	assert(strideres==1);
	int last_rise=-1;
	int last_fall=-1;

	std::vector<int> rise(n); //right max
	rising_(&rise[0], psig, n, w, thresh);

	memset(test, 0, sizeof(test));
	rising_kernel(test, psig, sig.size(), w, thresh, mincount);
	{
		for(int i=0; i<sig.size();++i) {
			auto a = (rise[i]!=0);
			auto b = (test[i] &RISING)!=0 ;
			if(a!=b)
				printf("RISE differs at i=%d\n",i);
		}
	}

	std::vector<int> fall(n); //left max
	falling_(&fall[0], psig, n, w, thresh);

	memset(test, 0, sizeof(test));
	falling_kernel(test, psig, sig.size(), w, thresh, mincount);
	{
		for(int i=0; i<sig.size();++i) {
			auto a = fall[i]!=0 ;
			auto b = (test[i] &FALLING)!=0 ;
			if(a!=b)
				printf("FALL differs at i=%d\n",i);
		}
	}


	for(int i=0; i< n; ++i) {
		pres[i]=0;
		if(fall[i]) {
			if(last_rise> last_fall && last_rise>=0) {
				//candidate found
				for(int j=last_rise; j<=i; ++j)
					pres[j]=1;
			}

			last_fall = i;
		}

		if(rise[i])
			last_rise = i;
	}



	return res;
}


static py::array_t<int> falling(py::array_t<int> sig, int w, int thresh, int frequency_step)
{

	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");

	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int,  py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infores.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	stid135_spectral_scan_startV1(&ss, psig, n, frequency_step);
	for(int i=0; i< n;++i)
		pres[i] = (ss.peak_marks[i]& FALLING)? 1:0;
	return res;
}

static py::array_t<int> rising(py::array_t<int> sig, int w, int thresh, int frequency_step)
{

	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");

	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int,  py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infores.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	stid135_spectral_scan_startV1(&ss, psig, n, frequency_step);
	for(int i=0; i< n;++i)
		pres[i] = (ss.peak_marks[i]& RISING)? 1:0;
	return res;
}


py::array_t<int> find_tpsV2(py::array_t<int> sig, int w, int thresh)
{

	py::buffer_info infosig = sig.request();
	if (infosig.ndim!=1)
		throw std::runtime_error("Bad number of dimensions");

	int* psig =  (int *) infosig.ptr;
	int stridesig = infosig.strides[0]/sizeof(int);
	int n = infosig.shape[0];
	py::array_t<int,  py::array::f_style> res(infosig.shape);
	py::buffer_info infores = res.request();
	bool peak_found=false;
	int strideres = infores.strides[0]/sizeof(int);
	int* pres =  (int *) infores.ptr;
	assert(stridesig==1);
	assert(strideres==1);
	int last_rise=-1;
	int last_fall=-1;

	std::vector<int> rise(n); //right max
	rising_(&rise[0], psig, n, w, thresh);

	std::vector<int> fall(n); //left max
	falling_(&fall[0], psig, n, w, thresh);

	for(int i=0; i< n; ++i) {
		pres[i]=0;
		if(fall[i]) {
			if(last_rise> last_fall && last_rise>=0) {
				//candidate found
				int j;
				for(j=last_rise; j>=0; --j) {
					if (psig[j]< psig[i]-thresh/2)
						break;
				}
#if 0
				if(j!=last_rise)
					printf("correcting: i=%d j=%d last_rise=%d\n", i,j,last_rise);
				last_rise=j;
#endif
				for(int j=last_rise; j<=i; ++j)
					pres[j]=1;
			}

			last_fall = i;
		}

		if(rise[i])
			last_rise = i;
	}



	return res;
}

PYBIND11_MODULE(pyspectrum1, m) {
	m.doc() = R"pbdoc(

	)pbdoc";
	;
	m.def("find_tps", &find_tps)
		.def("find_kernel_tps", &find_kernel_tps)
		.def("rising", &rising)
		.def("falling", &falling)
		.def("morpho", &morpho);

}
