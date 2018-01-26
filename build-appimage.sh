#!/bin/bash

BIN=$1
shift
if ! [ -x "$BIN" ]; then
    echo "Usage: $0 spicy_path [extra_deps]"
    exit 1
fi

set -e

libs="libnopoll libspice-client-glib libspice-client-gtk libssl libcrypto libjpeg libusbredir \
      libcups libflexvdi-spice-client libpulse libva libgstreamer-1 libffi $*"
libs=$(for lib in $libs; do echo -n "-e $lib "; done)
if ldd "$BIN" | grep -q libva; then
    intel_driver=`pkg-config libva --variable driverdir`/i965_drv_video.so
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

mkdir -p $TMPDIR/usr/bin $TMPDIR/usr/lib/gstreamer-1.0 $TMPDIR/usr/share/icons
cp "$BIN" $TMPDIR/usr/bin/spicy
ldd "$BIN" | sed 's;.* => \(/.*\) (.*;\1;' | grep $libs | xargs -r cp -t $TMPDIR/usr/lib
if [ -f "$intel_driver" ]; then
    cp "$intel_driver" $TMPDIR/usr/lib
fi
cp $(pkg-config gstreamer-1.0 --variable pluginsdir)/libgst{app,coreelements,audioconvert,audioresample,autodetect,playback,jpeg,videofilter,videoconvert,videoscale,deinterlace,alsa,pulseaudio}.so "$TMPDIR"/usr/lib/gstreamer-1.0
ldd "$TMPDIR"/usr/lib/gstreamer-1.0/* | sed 's;.* => \(/.*\) (.*;\1;' | grep $(pkg-config gstreamer-1.0 --variable libdir) | grep $libs -e libgst | sort -u | xargs -r cp -t "$TMPDIR"/usr/lib
cp $(pkg-config gstreamer-1.0 --variable prefix)/libexec/gstreamer-1.0/gst-plugin-scanner "$TMPDIR"/usr/bin

find $TMPDIR/usr/{bin,lib} -type f -exec chmod 755 \{\} + -exec strip -s \{\} +
cp -r "$ICONSDIR"/flexvdi $TMPDIR/usr/share/icons

cat > $TMPDIR/AppRun <<\EOF
#!/bin/sh
HERE=$(dirname $(readlink -f "${0}"))
export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GST_PLUGIN_SYSTEM_PATH="${HERE}"/usr/lib/gstreamer-1.0
export GST_PLUGIN_SCANNER="${HERE}"/usr/bin/gst-plugin-scanner
if which pax11publish > /dev/null; then
    eval $(pax11publish -i)
fi
export LIBVA_DRIVERS_PATH="${HERE}"/usr/lib
"${HERE}"/usr/bin/spicy "$@"
EOF
chmod 755 $TMPDIR/AppRun $TMPDIR/usr/bin/spicy $TMPDIR/usr/lib/*

appimagetool -n $TMPDIR ./spicy.appimage || {
    echo appimagetool not found, creating just a tar.gz archive
    tar czf ./spicy.appimage.tar.gz -C $TMPDIR .
}


