#!/bin/sh
libtoolize --copy --force || exit 1
aclocal -I m4
automake --foreign -a -c
autoconf
