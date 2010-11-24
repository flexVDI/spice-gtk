ACLOCAL_AMFLAGS = -I m4

SUBDIRS = common gtk po python_modules

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = spice-client-glib.pc spice-client-gtk.pc

INTLTOOL_FILES = intltool-extract.in		\
                 intltool-merge.in		\
                 intltool-update.in

DISTCLEANFILES = $(pkgconfig_DATA)		\
		 intltool-extract		\
                 intltool-merge			\
                 intltool-update		\
                 po/.intltool-merge-cache

EXTRA_DIST = spice.proto spice1.proto spice_codegen.py $(INTLTOOL_FILES)

DISTCHECK_CONFIGURE_FLAGS =  --enable-introspection
