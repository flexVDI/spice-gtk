#!/bin/sh

set -e # exit on errors

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

gtkdocize
autoreconf -v --force --install
intltoolize -f

if [ -z "$NOCONFIGURE" ]; then
    "$srcdir"/configure --enable-maintainer-mode ${1+"$@"}
fi

