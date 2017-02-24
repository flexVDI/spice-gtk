bin_PROGRAMS = spicy-stats spicy-screenshot

TOOLS_CPPFLAGS =			\
	-DSPICE_COMPILATION		\
	-I$(top_builddir)/src		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/src		\
	$(COMMON_CFLAGS)		\
	$(GLIB2_CFLAGS)			\
	$(GIO_CFLAGS)			\
	$(SMARTCARD_CFLAGS)		\
	$(SPICE_CFLAGS)			\
	$(NULL)

if WITH_GTK
bin_PROGRAMS += spicy
TOOLS_CPPFLAGS += $(GTK_CFLAGS)
endif

spicy_SOURCES =				\
	spicy.c				\
	spicy-connect.h 		\
	spicy-connect.c 		\
	spice-cmdline.h			\
	spice-cmdline.c			\
	$(NULL)

spicy_LDADD =				\
	$(top_builddir)/src/libspice-client-gtk-3.0.la	\
	$(top_builddir)/src/libspice-client-glib-2.0.la	\
	$(GTK_LIBS) \
	$(NULL)

# FIXME: GtkAction and lots of GtkUIManager APIs are deprecated
spicy_CPPFLAGS =			\
	$(TOOLS_CPPFLAGS)		\
	-DSPICE_DISABLE_DEPRECATED	\
	-Wno-deprecated-declarations	\
	$(NULL)

spicy_screenshot_SOURCES =		\
	spicy-screenshot.c		\
	spice-cmdline.h			\
	spice-cmdline.c			\
	$(NULL)

spicy_screenshot_LDADD =		\
	$(top_builddir)/src/libspice-client-glib-2.0.la	\
	$(GOBJECT2_LIBS)		\
	$(NULL)

spicy_screenshot_CPPFLAGS =		\
	$(TOOLS_CPPFLAGS)		\
	$(NULL)

spicy_stats_SOURCES =			\
	spicy-stats.c			\
	spice-cmdline.h			\
	spice-cmdline.c			\
	$(NULL)

spicy_stats_LDADD =			\
	$(top_builddir)/src/libspice-client-glib-2.0.la	\
	$(GOBJECT2_LIBS) \
	$(NULL)

spicy_stats_CPPFLAGS =			\
	$(TOOLS_CPPFLAGS)		\
	$(NULL)

-include $(top_srcdir)/git.mk
