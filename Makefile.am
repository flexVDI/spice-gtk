ACLOCAL_AMFLAGS = -I m4
NULL =

SUBDIRS = common gtk po python_modules doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = spice-client-glib.pc spice-client-gtk.pc

INTLTOOL_FILES = intltool-extract.in		\
                 intltool-merge.in		\
                 intltool-update.in

DISTCLEANFILES = $(pkgconfig_DATA)		\
		 intltool-extract		\
                 intltool-merge			\
                 intltool-update		\
                 gnome-doc-utils.make		\
                 gtk-doc.make                   \
                 po/.intltool-merge-cache

EXTRA_DIST = spice.proto			\
	     spice1.proto			\
	     spice_codegen.py			\
	     build-aux/git-version-gen		\
             gtk-doc.make			\
	     .version				\
	     $(INTLTOOL_FILES)

MAINTAINERCLEANFILES =				\
	ABOUT-NLS				\
	aclocal.m4				\
	config.h.in				\
	m4/*.m4					\
	build-aux/compile			\
	build-aux/config.guess			\
	build-aux/config.rpath			\
	build-aux/config.sub			\
	build-aux/depcomp			\
	build-aux/install-sh			\
	build-aux/ltmain.sh			\
	build-aux/missing			\
	build-aux/mkinstalldirs			\
	po/Makefile.in.in			\
	$(NULL)

# see git-version-gen
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

DISTCHECK_CONFIGURE_FLAGS = --enable-introspection --enable-werror --enable-gtk-doc

-include $(top_srcdir)/git.mk

