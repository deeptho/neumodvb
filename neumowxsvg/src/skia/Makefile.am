noinst_LTLIBRARIES = libwxsvg_skia.la
libwxsvg_skia_la_SOURCES = SVGCanvasSkia.cpp SVGCanvasPathSkia.cpp Gradient.cpp
libwxsvg_skia_la_LIBADD = $(CAIRO_LIBS)

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/include/wxSVG $(SKIA_CFLAGS)
