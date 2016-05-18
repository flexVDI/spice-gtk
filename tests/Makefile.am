NULL =

noinst_PROGRAMS =
TESTS = coroutine				\
	util					\
	session					\
	test-spice-uri				\
	test-file-transfer			\
	$(NULL)

if WITH_PHODAV
TESTS += pipe
endif

if WITH_POLKIT
TESTS += usb-acl-helper
noinst_PROGRAMS += mock-acl-helper
endif

noinst_PROGRAMS += $(TESTS)

AM_CPPFLAGS =					\
	$(COMMON_CFLAGS)			\
	$(GIO_CFLAGS)				\
	$(SMARTCARD_CFLAGS)			\
	-I$(top_srcdir)/src			\
	-I$(top_builddir)/src			\
	-DG_LOG_DOMAIN=\"GSpice\"		\
	$(NULL)

AM_LDFLAGS = $(GIO_LIBS) -static

LDADD =							\
	$(top_builddir)/src/libspice-client-glib-2.0.la	\
	$(NULL)

util_SOURCES = util.c
coroutine_SOURCES = coroutine.c
session_SOURCES = session.c
pipe_SOURCES = pipe.c
test_spice_uri_SOURCES = uri.c
test_file_transfer_SOURCES = file-transfer.c
usb_acl_helper_SOURCES = usb-acl-helper.c
usb_acl_helper_CFLAGS = -DTESTDIR=\"$(abs_builddir)\"
mock_acl_helper_SOURCES = mock-acl-helper.c

-include $(top_srcdir)/git.mk
