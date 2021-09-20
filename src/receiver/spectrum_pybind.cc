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
#include "spectrum_algo.h"

namespace py = pybind11;


static float windowed_max(float* p, int starti, int endi) {
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
	assert(x2 >= x1);
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



PYBIND11_MODULE(pyspectrum, m) {
	m.doc() = R"pbdoc(

	)pbdoc";
	;
	m
		.def("find_annot_locations", &find_annot_locations)
		;
}
