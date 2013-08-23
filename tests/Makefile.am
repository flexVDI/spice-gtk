NULL =

noinst_PROGRAMS = util
TESTS = $(noinst_PROGRAMS)

AM_CPPFLAGS =					\
	$(GIO_CFLAGS) -I$(top_srcdir)/gtk	\
	-DG_LOG_DOMAIN=\"GSpice\"		\
	$(NULL)
AM_LDFLAGS = $(GIO_LIBS)

util_SOURCES =					\
	$(top_srcdir)/gtk/spice-util-priv.h	\
	$(top_srcdir)/gtk/spice-util.c		\
	$(top_srcdir)/gtk/spice-util.h  	\
	util.c					\
	$(NULL)

