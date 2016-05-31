NULL =

AUTOMAKE_OPTIONS = 1.6
DOC_MODULE = spice-gtk

# The top-level SGML file
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml

# Source code location
DOC_SOURCE_DIR = $(top_srcdir)/src

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS = \
	--deprecated-guards="SPICE_DISABLE_DEPRECATED" \
	--ignore-decorators="G_GNUC_INTERNAL"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS = --xml-mode --output-format=xml

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/src/*.h
CFILE_GLOB = $(top_srcdir)/src/*.c

# Header files to ignore when scanning. Use base file name, no paths
IGNORE_HFILES=					\
	bio-gio.h				\
	channel-display-priv.h			\
	channel-usbredir-priv.h			\
	client_sw_canvas.h			\
	continuation.h				\
	controller				\
	coroutine.h				\
	decode.h				\
	desktop-integration.h			\
	display					\
	gio-coroutine.h				\
	giopipe.h				\
	smartcard-manager-priv.h		\
	spice-audio-priv.h			\
	spice-channel-cache.h			\
	spice-channel-priv.h			\
	spice-cmdline.h				\
	spice-common.h				\
	spice-grab-sequence-prive.h		\
	spice-gstaudio.h			\
	spice-gtk-session-priv.h		\
	spice-marshal.h				\
	spice-pulse.h				\
	spice-session-priv.h			\
	spice-uri-priv.h			\
	spice-util-priv.h			\
	spice-widget-priv.h			\
	spicy-connect.h				\
	usb-acl-helper.h			\
	usb-device-manager-priv.h		\
	usbdk_api.h				\
	usbutil.h				\
	vmcstream.h				\
	vncdisplaykeymap.h			\
	win-usb-clerk.h				\
	win-usb-dev.h				\
	win-usb-driver-install.h		\
	wocky-http-proxy.h			\
	$(NULL)

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
GTKDOC_CFLAGS = -I$(top_srcdir) -I$(top_builddir) -I$(top_srcdir)/src -I$(top_builddir)/src $(SPICE_GLIB_CFLAGS) $(SPICE_GTK_CFLAGS) $(COMMON_CFLAGS) -DSPICE_COMPILATION
GTKDOC_LIBS = $(top_builddir)/src/libspice-client-glib-2.0.la $(top_builddir)/src/libspice-client-gtk-3.0.la $(GTK_LIBS)

include $(top_srcdir)/gtk-doc.make

# Comment this out if you want 'make check' to test you doc status
# and run some sanity checks
if ENABLE_GTK_DOC
TESTS_ENVIRONMENT = cd $(srcdir) && \
  DOC_MODULE=$(DOC_MODULE) DOC_MAIN_SGML_FILE=$(DOC_MAIN_SGML_FILE) \
  SRCDIR=$(abs_srcdir) BUILDDIR=$(abs_builddir)
#TESTS = $(GTKDOC_CHECK)
endif

-include $(top_srcdir)/git.mk
