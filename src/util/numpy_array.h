/*
 * Neumo dvb (C) 2019-2026 deeptho@gmail.com
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
#include <nanobind/ndarray.h>
#include <numeric>
#include <array>
namespace nb = nanobind;

template<typename T>
nb::ndarray<T, nb::numpy, nb::c_contig>
numpy_array(size_t ndim, const size_t* shape) {
	std::array<int64_t, 4> strides;
	int size = 1;
	for (int i = ndim-1; i >=0; --i) {
		strides[i] = size;
		size *= shape[i];
	}

	auto * data = new T[size];
	// Delete 'data' when the 'owner' capsule expires
	auto owner = nb::capsule(data, [](void *p) noexcept {
		delete[] (T *) p;
	});
	return {data, ndim, shape, owner, strides.data()};
}

#if 0
template<typename T>
nb::ndarray<T, nb::numpy, nb::c_contig>
numpy_array(size_t size) {
	return numpy_array<T>(1, &size);
}
#endif


template<typename T>
nb::ndarray<T, nb::numpy, nb::c_contig>
numpy_array(std::initializer_list<size_t> shape) {
	size_t ndim = shape.size();
	const size_t* shape_ = shape.begin();
	return numpy_array<T>(ndim, shape_);
}
