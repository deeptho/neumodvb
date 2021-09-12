# Process this file with autoconf to produce a configure script.

# initialization
AC_PREREQ(2.59)
AC_INIT([wxsvg], [1.5.22], [http://wxsvg.sourceforge.net/])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE(foreign)
AM_MAINTAINER_MODE

# library versioning (quoted from libtool doc)
# 1. Update the version information only immediately before a public release
# 2. If the library source code has changed then increment revision (`c:r:a' becomes `c:r+1:a').
# 3. If any interfaces have been added, removed, or changed since the last update, increment current,
#    and set revision to 0.
# 4. If any interfaces have been added since the last public release, then increment age.
# 5. If any interfaces have been removed since the last public release then set age to 0.

LIBRARY_VERSION=15:6:12 # current:revision:age -> libwxsvg.so.(C-A).(A).(R)
AC_SUBST(LIBRARY_VERSION)

# options
AM_OPTIONS_WXCONFIG
AC_ARG_ENABLE(debug,
 [  --enable-debug            Enable debugging information],
 USE_DEBUG="$enableval", USE_DEBUG="no")
AC_ARG_ENABLE(libav,
 [  --enable-libav            Enable libav decoder support (default: enabled)],
 [enable_libav=$enableval],[enable_libav=yes])
AC_ARG_ENABLE(render,
 [  --enable-render[=render]  Specify the render: cairo (default) or skia],
 [USE_RENDER=$enableval], [USE_RENDER=cairo])
AC_ARG_WITH(datarootdir,
 [  --with-datarootdir=DIR    Use DIR as the data root [[PREFIX/share]]],
 [datarootdir="\${prefix}/$with_datarootdir"], [datarootdir='$(prefix)/share'])
AC_SUBST(datarootdir)

AC_MSG_CHECKING(for install location)
case "$prefix" in
NONE)	AC_CACHE_VAL(m_cv_prefix,m_cv_prefix=$ac_default_prefix);;
*)	m_cv_prefix=$prefix ;;
esac
AC_MSG_RESULT($m_cv_prefix)
case "$m_cv_prefix" in /*) ;; *)
	AC_MSG_WARN([--prefix=$prefix must be an absolute path name, using $ac_default_prefix])
	m_cv_prefix=$ac_default_prefix
esac
prefix=$m_cv_prefix


# Checks for programs.
AC_LANG_CPLUSPLUS
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CXXCPP
PKG_PROG_PKG_CONFIG()

# Check for libtool
m4_pattern_allow([AC_PROG_LIBTOOL])
AC_PROG_LIBTOOL

# debug flags
case "$USE_DEBUG" in
yes)
  DEBUG_FLAGS="-g -Wall -DDEBUG"
  ;;    
*)
  DEBUG_FLAGS=""
  ;;
esac

dnl render
AM_CONDITIONAL(USE_RENDER_SKIA, [test "$USE_RENDER" = "skia"])
AM_CONDITIONAL(USE_RENDER_CAIRO, [test "$USE_RENDER" = "cairo"])
case "$USE_RENDER" in
cairo)
	# check for cairo
	PKG_CHECK_MODULES(CAIRO, cairo)
	CXXFLAGS="$CXXFLAGS $CAIRO_CFLAGS -DUSE_RENDER_CAIRO"
	LIBS="$LIBS $CAIRO_LIBS"
	case $host_os in
		*darwin*)
			font_engine=
			;;
		*)
			font_engine=pango
			;;
	esac
	
	# Font_engine
	case "$font_engine" in
	pango)
	  PKG_CHECK_MODULES(PANGO, pangocairo)
	  CXXFLAGS="$CXXFLAGS $PANGO_CFLAGS"
	  LIBS="$LIBS $PANGO_LIBS"
	  ;;
	*)
	  ;;
	esac
	;;
skia)
	# check skia
	AC_ARG_WITH([skia-dir],
		AS_HELP_STRING([--with-skia-dir=PATH], [Set path to the SKIA directory]),
		[CXXFLAGS+=" -I${withval}/include/ -I${withval}/include/config/ -I${withval}/include/core/ -I${withval}/include/effects/ -I${withval}/include/private -I${withval}/src/core"
		LIBS+=" -L${withval}/out/Static"],
		[AC_MSG_ERROR(Missing --with-skia-dir that specifies path to the SKIA directory.)]
	)
	AC_CHECK_LIB([skia], [main], [], [AC_MSG_ERROR([Could not find skia library])])
	AC_CHECK_HEADER([SkUserConfig.h], [], [AC_MSG_ERROR([Could not find skia header files])])
	AC_CHECK_HEADER([SkCanvas.h], [], [AC_MSG_ERROR([Could not find skia header files])])
	;;
*)
	AC_MSG_ERROR([*** Unknown render "$USE_RENDER"])
	;;
esac

# check wxWidgets >= 2.6.3
WX_CONFIG_CHECK(2.6.3, WXFOUND=1)
if test "$WXFOUND" != 1; then
    AC_MSG_ERROR([
        Please check that wx-config is in path, the directory
        where wxWidgets libraries are installed (returned by
        'wx-config --libs' command) is in LD_LIBRARY_PATH or
        equivalent variable and wxWidgets is version 2.6.3 or above.
    ])
fi
CXXFLAGS="$CXXFLAGS $DEBUG_FLAGS $WX_CXXFLAGS "
LIBS="$LIBS $WX_LIBS"

# check expat
PKG_CHECK_MODULES(EXPAT, expat)
CXXFLAGS="$CXXFLAGS $EXPAT_CFLAGS"
LIBS="$LIBS $EXPAT_LIBS"

# check libexif
PKG_CHECK_MODULES(LIBEXIF, libexif, [LIBS="$LIBS $LIBEXIF_LIBS"], [AC_MSG_ERROR([*** missing libexif ***])])

# check ffmpeg
AM_CONDITIONAL(USE_LIBAV, [test x$enable_libav = xyes])
if test x$enable_libav = xyes; then
  PKG_CHECK_MODULES(LIBAV, [libavformat >= 50.0.0 libavcodec >= 51.0.0 libavutil >= 49.0.0 libswscale])
  CXXFLAGS="$CXXFLAGS -DUSE_LIBAV $LIBAV_CFLAGS"
  LIBS="$LIBS $LIBAV_LIBS"
  AC_CHECK_HEADER([libavformat/avformat.h],[],[AC_MSG_ERROR([*** missing ffmpeg library])],[
#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS])
fi

# vars
AC_SUBST(LDFLAGS)
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(WX_CONFIG_NAME)

# create Makefiles
AC_CONFIG_FILES([
  Makefile
  libwxsvg.pc
  include/Makefile
  include/wxSVGXML/Makefile
  include/wxSVG/Makefile
  src/svgxml/Makefile
  src/Makefile
  src/cairo/Makefile
  src/skia/Makefile
  svgview/Makefile
])
AC_OUTPUT
