ACLOCAL_AMFLAGS = -I m4
NULL =

SUBDIRS = common gtk po python_modules doc data vapi

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =					\
	spice-client-glib-2.0.pc			\
	spice-client-gtk-$(SPICE_GTK_API_VERSION).pc	\
	spice-controller.pc

INTLTOOL_FILES = intltool-extract.in		\
		 intltool-merge.in		\
		 intltool-update.in		\
		 $(NULL)

DISTCLEANFILES = $(pkgconfig_DATA)		\
		 intltool-extract		\
		 intltool-merge			\
		 intltool-update		\
		 gnome-doc-utils.make		\
		 gtk-doc.make			\
		 po/.intltool-merge-cache	\
		 $(NULL)

EXTRA_DIST = spice.proto			\
	     spice1.proto			\
	     spice_codegen.py			\
	     build-aux/git-version-gen		\
	     gtk-doc.make			\
	     .version				\
	     THANKS				\
	     $(INTLTOOL_FILES)			\
	     $(NULL)

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

DISTCHECK_CONFIGURE_FLAGS =			\
	--enable-introspection			\
	--disable-vala				\
	--disable-usbredir			\
	--enable-gtk-doc			\
	--enable-werror				\
	--with-gtk=3.0				\
	$(NULL)

spicy-for-windows.exe:
	PATH=$$PATH:/usr/i686-pc-mingw32/sys-root/mingw/bin/:/usr/i686-pc-mingw32/sys-root/mingw/lib \
	 nsiswrapper --run --name "spicy" --outfile "spicy-for-windows.exe" --with-gtk \
	 /usr/i686-pc-mingw32/sys-root/mingw/bin/spicy.exe /usr/i686-pc-mingw32/sys-root/mingw/bin/snappy.exe \
	 /usr/i686-pc-mingw32/sys-root/mingw/bin/libpangoft2-1.0-0.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/bin/libgdk_pixbuf-2.0-0.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/bin/libgdk-win32-2.0-0.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/bin/gst-*.exe \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstaudio*.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstdirectsoundsink.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstdirectsoundsrc.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstapp.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstautodetect.dll \
	 /usr/i686-pc-mingw32/sys-root/mingw/lib/gstreamer-0.10/libgstcoreelements.dll

-include $(top_srcdir)/git.mk

