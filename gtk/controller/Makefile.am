NULL =

AM_CPPFLAGS = $(GIO_CFLAGS) $(PROTOCOL_CFLAGS)
AM_LDFLAGS = $(GIO_LIBS)
AM_VALAFLAGS =							\
	--pkg gio-2.0						\
	--pkg spice-protocol --vapidir=$(top_srcdir)/data	\
	--pkg custom --vapidir=$(srcdir)			\
	-C -g							\
	$(NULL)

if OS_WIN32
AM_VALAFLAGS += --pkg gio-windows-2.0
else
AM_VALAFLAGS += --pkg gio-unix-2.0
endif

if !OS_WIN32
lib_LTLIBRARIES = libspice-controller.la
noinst_PROGRAMS = test-controller
endif

BUILT_SOURCES = controller.vala.stamp

libspice_controller_la_VALASOURCES =		\
	menu.vala				\
	controller.vala				\
	$(NULL)
libspice_controller_la_SOURCES =		\
	custom.h				\
	$(libspice_controller_la_VALASOURCES:.vala=.c)
libspice_controllerincludedir = $(includedir)/spice-controller
libspice_controllerinclude_HEADERS =		\
	spice-controller.h

test_controller_SOURCES = test.c
test_controller_LDADD = libspice-controller.la

controller.vala.stamp: $(libspice_controller_la_VALASOURCES) custom.vapi
	$(VALA_V)$(VALAC) $(VALAFLAGS) $(AM_VALAFLAGS) $(libspice_controller_la_VALASOURCES) -H spice-controller.h
	@touch $@

EXTRA_DIST =					\
	$(libspice_controller_la_VALASOURCES)	\
	spice-controller.h			\
	controller.vala.stamp			\
	custom.vapi				\
	$(NULL)

vala-clean:
	rm -f `find . -name "*.stamp"`

