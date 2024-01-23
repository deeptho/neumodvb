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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstddef>
#include <string.h>
#include <stdlib.h>


extern "C" {
off_t filesize_fd(int fd);
}



inline pid_t gettid() {
	return syscall(SYS_gettid);
}

//#define IMPL1
#ifdef IMPL1
namespace alignment {

	template <class T>
		struct alignment {
			typedef alignment<T> self_t;
			char x;
			T test;
			static inline size_t alignmentof() {
				//self_t t;
				//return (char*)&t.test -(char*)&t;
				return sizeof(alignment<T>)-sizeof(T);
			}


		alignment() : test(*(T*)NULL) {}
		};


};

#define alignmentof(T) alignment::alignment<T>::alignmentof()
#endif

#if defined(offsetof)
#undef offsetof
#endif

#define dtoffsetof(T,F) ((unsigned int)((char *)&((T *)0x1000L)->F - (char *)0x1000L))
#define offsetof(T,F) dtoffsetof(T,F)

//#define IMPL2
#ifdef IMPL2
namespace util {
	template <class T>
	struct align {

		struct tmp {
			char x;
		T test;
		};

		enum {a=offsetof(tmp, test)};
	};

};


#define alignmentof(T) util::align<T>::a;
#endif


#define IMPL_3
#ifdef IMPL_3
//see https://sites.google.com/a/monkeyspeak.com/monkeyspeak-documents/computing-memory-alignment
//data structures are aligned at  "stride"
//size of such a data structure  is always a multiple of "stride"

namespace util {
template <typename T>
struct Tchar {
	T t;
	char c;
};

};


inline int atoi_safe(const char *str) {
	return str?atoi(str):0;
}


#define strideof(T)																		 \
	((sizeof(util::Tchar<T>) > sizeof(T)) ?							 \
	 sizeof(util::Tchar<T>)-sizeof(T) : sizeof(T))

#define alignmentof(T) strideof(T)

#endif

#define STATIC_ASSERT( condition )																			\
    typedef char assert_failed_ ## __FILE__ ## __LINE__ [ (condition) ? 1 : -1 ];
