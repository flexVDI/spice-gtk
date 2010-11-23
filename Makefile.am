ACLOCAL_AMFLAGS = -I m4

SUBDIRS = common gtk python_modules

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = spice-client-glib.pc spice-client-gtk.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = spice.proto spice1.proto spice_codegen.py

DISTCHECK_CONFIGURE_FLAGS =  --enable-introspection
