/*
 * Neumo dvb (C) 2019-2023 deeptho@gmail.com
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
#include <assert.h>
#if 0
#ifndef NDEBUG
void assert_fail(const char *assertion, const char *file, unsigned line, const char *function)
    __attribute__ ((noreturn));
    #undef assert
    #define assert(expr)            \
        ((expr)                     \
        ? __ASSERT_VOID_CAST (0)    \
        : assert_fail (__STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION))
#endif /* NDEBUG */
#endif

void assert_fail_stop(const char *assertion, const char *file, unsigned line, const char *function) throw();
#define assert_suspend(expr)				\
	((expr)														\
	 ? __ASSERT_VOID_CAST (0)					\
	 : assert_fail_stop(__STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION))
