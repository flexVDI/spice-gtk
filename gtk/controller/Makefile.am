NULL =

AM_CPPFLAGS = $(GIO_CFLAGS) $(PROTOCOL_CFLAGS)
AM_LDFLAGS = $(GIO_LIBS)
AM_VALAFLAGS =							\
	--pkg gio-2.0						\
	--pkg spice-protocol --vapidir=$(top_srcdir)/data	\
	--pkg custom --vapidir=$(srcdir)			\
	-C -H controller.h -g					\
	$(NULL)

if OS_WIN32
AM_VALAFLAGS += --pkg gio-windows-2.0
else
AM_VALAFLAGS += --pkg gio-unix-2.0
endif

if !OS_WIN32
noinst_LTLIBRARIES = libcontroller.la
noinst_PROGRAMS = test-controller
endif

BUILT_SOURCES = controller.vala.stamp

libcontroller_la_VALASOURCES =			\
	menu.vala				\
	controller.vala				\
	$(NULL)
libcontroller_la_SOURCES = \
	custom.h				\
	$(libcontroller_la_VALASOURCES:.vala=.c)

test_controller_SOURCES = test.c
test_controller_LDADD = libcontroller.la

controller.vala.stamp: $(libcontroller_la_VALASOURCES) custom.vapi
	$(VALA_V)$(VALAC) $(VALAFLAGS) $(AM_VALAFLAGS) $(libcontroller_la_VALASOURCES)
	@touch $@

EXTRA_DIST =					\
	$(libcontroller_la_VALASOURCES)		\
	controller.h				\
	controller.vala.stamp			\
	custom.vapi				\
	$(NULL)

vala-clean:
	rm -f `find . -name "*.stamp"`

