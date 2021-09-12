SUBDIRS = src svgview include
ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libwxsvg.pc
EXTRA_DIST     = libwxsvg.pc.in
AM_CCFLAGS = -g -O2 -fPIC
AM_CXXFLAGS = -g -O2 -fPIC
AM_CFLAGS = -g -O2 -fPIC

run: all
	$(MAKE) -C svgview run
