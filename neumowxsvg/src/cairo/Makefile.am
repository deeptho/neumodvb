noinst_LTLIBRARIES = libwxsvg_cairo.la
libwxsvg_cairo_la_SOURCES = SVGCanvasCairo.cpp SVGCanvasPathCairo.cpp SVGCanvasTextCairo.cpp SVGCanvasImageCairo.cpp
libwxsvg_cairo_la_LIBADD = $(CAIRO_LIBS)


AM_CPPFLAGS = -fPIC -I$(top_srcdir)/include -I$(top_srcdir)/include/wxSVG $(CAIRO_CFLAGS) $(FREETYPE_CFLAGS)
