NULL =

noinst_PROGRAMS =
TESTS = test-coroutine				\
	test-util				\
	test-session				\
	test-spice-uri				\
	test-file-transfer			\
	$(NULL)

if WITH_PHODAV
TESTS += test-pipe
endif

if WITH_POLKIT
TESTS += test-usb-acl-helper
noinst_PROGRAMS += test-mock-acl-helper
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

test_util_SOURCES = util.c
test_coroutine_SOURCES = coroutine.c
test_session_SOURCES = session.c
test_pipe_SOURCES = pipe.c
test_spice_uri_SOURCES = uri.c
test_file_transfer_SOURCES = file-transfer.c
test_usb_acl_helper_SOURCES = usb-acl-helper.c
test_usb_acl_helper_CFLAGS = -DTESTDIR=\"$(abs_builddir)\"
test_mock_acl_helper_SOURCES = mock-acl-helper.c

-include $(top_srcdir)/git.mk
