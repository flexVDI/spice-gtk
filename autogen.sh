#!/bin/sh

set -e # exit on errors

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

gtkdocize
autoreconf -v --force --install
intltoolize -f
if test ! -e gtk/controller/controller.stamp; then
  enable_vala="--enable-vala"
fi

if [ -z "$NOCONFIGURE" ]; then
    "$srcdir"/configure --enable-maintainer-mode $enable_vala ${1+"$@"}
fi

