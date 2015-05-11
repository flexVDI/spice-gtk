NULL =

noinst_PROGRAMS =				\
	coroutine				\
	util					\
	session					\
	test_port_forward			\
	$(NULL)

if WITH_PHODAV
noinst_PROGRAMS += pipe
endif

TESTS = $(noinst_PROGRAMS)

AM_CPPFLAGS =					\
	$(GIO_CFLAGS)				\
	-I$(top_srcdir)/gtk			\
	-I$(top_builddir)/gtk			\
	-I$(top_srcdir)/spice-common/spice-protocol	\
	-DG_LOG_DOMAIN=\"GSpice\"		\
	$(NULL)

AM_LDFLAGS = $(GIO_LIBS) -static

LDADD =							\
	$(top_builddir)/gtk/libspice-client-glib-2.0.la	\
	$(NULL)

util_SOURCES = util.c
coroutine_SOURCES = coroutine.c
session_SOURCES = session.c
pipe_SOURCES = pipe.c
test_port_forward_SOURCES =			\
	test-port-forward.c			\
	$(NULL)


-include $(top_srcdir)/git.mk
