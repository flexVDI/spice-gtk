#!/bin/sh

set -e # exit on errors

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

git submodule update --init --recursive

gtkdocize
autoreconf -v --force --install
intltoolize -f
if test ! -e gtk/controller/controller.vala.stamp; then
  enable_vala="--enable-vala"
fi

if [ -z "$NOCONFIGURE" ]; then
    echo "Running configure with --enable-maintainer-mode --enable-gtk-doc --with-gtk=3.0 $enable_vala ${1+"$@"}"
    "$srcdir"/configure --enable-maintainer-mode --enable-gtk-doc --with-gtk=3.0 $enable_vala ${1+"$@"}
fi

