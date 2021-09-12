bin_PROGRAMS = svgview
svgview_SOURCES = svgview.cpp
svgview_LDADD = ../src/libwxsvg.la

AM_CPPFLAGS = -I$(top_srcdir)/include

run: $(bin_PROGRAMS)
	./$(bin_PROGRAMS)
