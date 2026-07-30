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

#include <nanobind/nanobind.h>
#include <nanobind/make_iterator.h>
#include "util/logger.h"
#include <stdio.h>

#include "stackstring/stackstring.h"


namespace nb = nanobind;

//Not needed if stl.h is not included
//PYBIND11_MAKE_OPAQUE(ss::vector_<int>);

inline void export_ss(nb::module_ &m)
{
	nb::class_<ss::string_>(m, "ss_string")
		.def("__repr__",  [](const ss::string_& x) { return std::string(x.c_str());})
	;
}


//see https://github.com/pybind/pybind11/blob/master/tests/test_sequences_and_iterators.cpp
template<typename T>
inline void export_ss_vector_(nb::module_ &m, const char* pytypename)
{
	static bool called = false;
	if(called)
		return;

	nb::class_<ss::vector_<T>>(m, pytypename)
		.def(nb::init<>())
		.def("__len__", [](const ss::vector_<T> &v) {
											return v.size(); })
		.def("erase", [](ss::vector_<T> &v, size_t idx) {
										 v.erase(idx); })
		.def("index", [](ss::vector_<T> &v, const T& val) {
										 return v.index_of(val); })
		.def("resize", [](ss::vector_<T> &v, size_t size) {
										 v.resize(size); })
		.def("push_back", [](ss::vector_<T> &v, const T& val) {
                		 v.resize_no_init(v.size()+1);
										 new(&v[v.size()-1]) T(val);
		})
		.def("assign", [](ss::vector_<T>& v, nb::list l) {
			v.clear();
			for(auto p: l) {
				//if constexpr (!nb::detail::cast_is_temporary_value_reference<T>::value) {
				auto pv =  nb::cast<T>(p);
				v.push_back(pv);
					//} else {
					//auto pv =  p.cast<T&>();
					//v.push_back(pv);
			}
			})
		.def("__iter__", [](ss::vector_<T> &v) {
			return nb::make_iterator(nb::type<ss::vector_<T>>(), "iterator", v.buffer(), v.buffer()+v.size());
			}
			,nb::keep_alive<0, 1>() /* Essential: keep object alive while iterator exists */
			)
		.def("__getitem__", [](ss::vector_<T> &v, int i) -> T& {
			if(i>=(signed)v.size()) {
				dterrorf("Index out of range");
				assert(0);
				throw nb::index_error();
			}
				//if (i==5)
				//	printf("[%p] get [{:d}]\n", &v, i);
				return v[i];
		}
			,nb::rv_policy::copy
			)
#ifdef TODO
		//TODO: this returns copies instead of a real slice
		.def("__getitem__", [](const ss::vector_<T> &v, nb::slice slice) {
				size_t start, stop, step, slicelength;
				if (!slice.compute(v.size(), &start, &stop, &step, &slicelength))
					throw nb::error_already_set();
				auto *ret = new ss::vector_<T>();
				ret->reserve(slicelength);
				for (size_t i = 0; i < slicelength; ++i) {
					(*ret)[i] = v[start]; start += step;
				}
				return ret; })
#endif
		.def("__setitem__", [](ss::vector_<T> &v, int i, const T& val) {
													v[i]=val; }
			)

		.def("__setitem__", [](ss::vector_<T> &s, nb::slice slice, nb::list list) {
						auto [start, stop, step, slicelength] = slice.compute(s.size());
            if (slicelength != nb::len(list))
							throw std::runtime_error("Left and right hand size of slice assignment have different sizes!");
						for (auto v: list) {
							s[start] = nb::cast<T>(v);
							start += step;
            }
		})
		;
}

#define xstr(a) str(a)
#define str__(a) #a

#define export_ss_vector(m, pytypename) \
	export_ss_vector_<pytypename>(m, str__(pytypename) "_vector" )
