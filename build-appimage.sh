#!/bin/bash

set -e

BIN=$1
shift
if ! [ -x "$BIN" ]; then
    echo "Usage: $0 spicy_path [extra_deps]"
    exit 1
fi

libs="libnopoll libspice-client-glib libspice-client-gtk $*"
if ldd "$BIN" | grep -q flexvdi; then
    libs="$libs libcups libflexvdi-spice-client"
fi
if ldd "$BIN" | grep -q pulse; then
    libs="$libs libpulse"
fi
SRCDIR=`dirname "$0"`
ICONSDIR="$SRCDIR"/icons

TMPDIR=`mktemp -d`
trap "rm -fr $TMPDIR" EXIT

cat > $TMPDIR/spicy.desktop << EOF
[Desktop Entry]
Name=Spice client
Exec=spicy
Icon=flexvdi
EOF
cp "$ICONSDIR"/flexvdi.png $TMPDIR

mkdir -p $TMPDIR/usr/bin $TMPDIR/usr/lib $TMPDIR/usr/share/icons
cp "$BIN" $TMPDIR/usr/bin/spicy
DEPS=`ldd "$BIN"`
for lib in $libs; do
    cp `echo "$DEPS" | grep $lib | sed 's;.* => \(/.*\) (.*;\1;'` $TMPDIR/usr/lib
done
chmod 755 $TMPDIR/usr/bin/* $TMPDIR/usr/lib/*
strip -s $TMPDIR/usr/bin/* $TMPDIR/usr/lib/*
cp -r "$ICONSDIR"/flexvdi $TMPDIR/usr/share/icons

cat > $TMPDIR/AppRun <<\EOF
#!/bin/sh
HERE=$(dirname $(readlink -f "${0}"))
export LD_LIBRARY_PATH="${HERE}"/usr/lib:$LD_LIBRARY_PATH
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export PULSE_COOKIE=`mktemp`
trap 'rm -f $PULSE_COOKIE' EXIT
xprop -root PULSE_COOKIE | cut -d '"' -f 2 | while read -N2 code; do printf "\x$code"; done > $PULSE_COOKIE
export PULSE_SERVER=`xprop -root PULSE_SERVER | cut -d '"' -f 2 | cut -d '}' -f 2`
"${HERE}"/usr/bin/spicy $@
EOF
chmod 755 $TMPDIR/AppRun $TMPDIR/usr/bin/spicy $TMPDIR/usr/lib/*

appimagetool -n $TMPDIR ./spicy.appimage || {
    echo appimagetool not found, creating just a tar.gz archive
    tar czf ./spicy.appimage.tar.gz -C $TMPDIR .
}


