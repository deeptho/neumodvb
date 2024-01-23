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
#ifdef	_DTASSERT_H

# undef	_DTASSERT_H
# undef	dtassert
# undef __DTASSERT_VOID_CAST

# ifdef	__USE_GNU
#  undef dtassert_perror
# endif

#endif /* dtassert.h	*/

#define	_DTASSERT_H	1
#include <features.h>

#if defined __cplusplus && __GNUC_PREREQ (2,95)
# define __DTASSERT_VOID_CAST static_cast<void>
#else
# define __DTASSERT_VOID_CAST (void)
#endif

/* void dtassert (int expression);

   If NDEBUG is defined, do nothing.
   If not, and EXPRESSION is zero, print an error message and abort.  */

#ifdef	NDEBUG
#undef assert
# define assert(expr)		(__DTASSERT_VOID_CAST (0))

/* void dtassert_perror (int errnum);

   If NDEBUG is defined, do nothing.  If not, and ERRNUM is not zero, print an
   error message with the error text for ERRNUM and abort.
   (This is a GNU extension.) */

# ifdef	__USE_GNU
#undef assert_perror
#  define assert_perror(errnum)	(__DTASSERT_VOID_CAST (0))
# endif

#else /* Not NDEBUG.  */

__BEGIN_DECLS

/* This prints an "Dtassertion failed" message and aborts.  */
extern void __dtassert_fail (const char *__dtassertion, const char *__file,
			   unsigned int __line, const char *__function)
     __THROW;

/* Likewise, but prints the error text for ERRNUM.  */
extern void __dtassert_perror_fail (int __errnum, const char *__file,
				  unsigned int __line, const char *__function)
     __THROW;


/* The following is not at all used here but needed for standard
   compliance.  */
extern void __dtassert (const char *__dtassertion, const char *__file, int __line)
     __THROW;


__END_DECLS

#if defined assert
#undef assert
#endif

#if defined assert_perror
#undef assert_perror
#endif

/* When possible, define dtassert so that it does not add extra
   parentheses around EXPR.  Otherwise, those added parentheses would
   suppress warnings we'd expect to be detected by gcc's -Wparentheses.  */
# if defined __cplusplus
#  define assert(expr)							\
     (static_cast <bool> (expr)						\
      ? void (0)							\
      : __dtassert_fail (#expr, __FILE__, __LINE__, __DTASSERT_FUNCTION))
# elif !defined __GNUC__ || defined __STRICT_ANSI__
#  define assert(expr)							\
    ((expr)								\
     ? __DTASSERT_VOID_CAST (0)						\
     : __dtassert_fail (#expr, __FILE__, __LINE__, __DTASSERT_FUNCTION))
# else
/* The first occurrence of EXPR is not evaluated due to the sizeof,
   but will trigger any pedantic warnings masked by the __extension__
   for the second occurrence.  The ternary operator is required to
   support function pointers and bit fields in this context, and to
   suppress the evaluation of variable length arrays.  */
#  define assert(expr)							\
  ((void) sizeof ((expr) ? 1 : 0), __extension__ ({			\
      if (expr)								\
        ; /* empty */							\
      else								\
        __dtassert_fail (#expr, __FILE__, __LINE__, __DTASSERT_FUNCTION);	\
    }))
# endif

# ifdef	__USE_GNU
#  define assert_perror(errnum)						\
  (!(errnum)								\
   ? __DTASSERT_VOID_CAST (0)						\
   : __dtassert_perror_fail ((errnum), __FILE__, __LINE__, __DTASSERT_FUNCTION))
# endif

/* Version 2.4 and later of GCC define a magical variable `__PRETTY_FUNCTION__'
   which contains the name of the function currently being defined.
   This is broken in G++ before version 2.6.
   C9x has a similar variable called __func__, but prefer the GCC one since
   it demangles C++ function names.  */
# if defined __cplusplus ? __GNUC_PREREQ (2, 6) : __GNUC_PREREQ (2, 4)
#   define __DTASSERT_FUNCTION	__extension__ __PRETTY_FUNCTION__
# else
#  if defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
#   define __DTASSERT_FUNCTION	__func__
#  else
#   define __DTASSERT_FUNCTION	((const char *) 0)
#  endif
# endif

#endif /* NDEBUG.  */


#if defined __USE_ISOC11 && !defined __cplusplus
# undef static_assert
# define static_assert _Static_assert
#endif
