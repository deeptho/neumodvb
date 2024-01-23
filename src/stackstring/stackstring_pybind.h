/*
 * Neumo dvb (C) 2019-2024 deeptho@gmail.com
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

#include <pybind11/pybind11.h>
#include <stdio.h>

#include "stackstring/stackstring.h"


namespace py = pybind11;

//Not needed if stl.h is not included
//PYBIND11_MAKE_OPAQUE(ss::vector_<int>);

inline void export_ss(py::module &m)
{
	py::class_<ss::string_>(m, "ss_string")
		.def("__repr__",  [](const ss::string_& x) { return std::string(x.c_str());})
	;
}


//see https://github.com/pybind/pybind11/blob/master/tests/test_sequences_and_iterators.cpp
template<typename T>
inline void export_ss_vector_(py::module &m, const char* pytypename)
{
	static bool called = false;
	if(called)
		return;

	py::class_<ss::vector_<T>>(m, pytypename)
		.def(py::init<>())
		.def("__len__", [](const ss::vector_<T> &v) {
											//printf("[%p] len\n", &v);
											return v.size(); })
		.def("erase", [](ss::vector_<T> &v, size_t idx) {
											//printf("[%p] len\n", &v);
										 v.erase(idx); })
		.def("index", [](ss::vector_<T> &v, const T& val) {
										 return v.index_of(val); })
		.def("resize", [](ss::vector_<T> &v, size_t size) {
											//printf("[%p] len\n", &v);
										 v.resize(size); })
		.def("push_back", [](ss::vector_<T> &v, const T& val) {
                		 v.resize_no_init(v.size()+1);
										 new(&v[v.size()-1]) T(val);
		})
		//.def("assign", py::overload_cast<const py::list&>(&assign_from_list))
		.def("assign", [](ss::vector_<T>& v, py::list l) {
			v.clear();
			for(auto p: l) {
				if constexpr (!pybind11::detail::cast_is_temporary_value_reference<T>::value) {
					auto pv =  p.cast<T>();
					v.push_back(pv);
				} else {
					auto pv =  p.cast<T&>();
					v.push_back(pv);
				}
			}})
		.def("__iter__", [](ss::vector_<T> &v) {
				//printf("[%p] iter\n", &v);
         return py::make_iterator(v.buffer(), v.buffer()+v.size());
			}
			,py::keep_alive<0, 1>() /* Essential: keep object alive while iterator exists */
			)
		.def("__getitem__", [](ss::vector_<T> &v, int i) -> T& {
				if(i>=(signed)v.size())
					throw py::index_error();
				//if (i==5)
				//	printf("[%p] get [{:d}]\n", &v, i);
				return v[i];
		}
			,py::return_value_policy::copy
			)
#ifdef TODO
		//TODO: this returns copies instead of a real slice
		.def("__getitem__", [](const ss::vector_<T> &v, py::slice slice) {
				size_t start, stop, step, slicelength;
				if (!slice.compute(v.size(), &start, &stop, &step, &slicelength))
					throw py::error_already_set();
				auto *ret = new ss::vector_<T>();
				ret->reserve(slicelength);
				for (size_t i = 0; i < slicelength; ++i) {
					(*ret)[i] = v[start]; start += step;
				}
				return ret; })
#endif
		.def("__setitem__", [](ss::vector_<T> &v, int i, const T& val) {
				//printf("[%p] set [{:d}]\n", &v, i);
													v[i]=val; }
			)

		.def("__setitem__", [](ss::vector_<T> &s, py::slice slice, py::list list) {
            size_t start, stop, step, slicelength;
            if (!slice.compute(s.size(), &start, &stop, &step, &slicelength))
                throw py::error_already_set();
            if (slicelength != py::len(list))
                throw std::runtime_error("Left and right hand size of slice assignment have different sizes!");
						for (auto& v: list) {
							s[start] = v.cast<T>(); start += step;
            }
												})
		;
}

#define xstr(a) str(a)
#define str__(a) #a

#define export_ss_vector(m, pytypename) \
	export_ss_vector_<pytypename>(m, str__(pytypename) "_vector" )
