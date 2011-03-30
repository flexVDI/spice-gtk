NULL =

AM_CPPFLAGS = $(GIO_CFLAGS) $(PROTOCOL_CFLAGS)
AM_LDFLAGS = $(GIO_LIBS)
noinst_LIBRARIES = libcontroller.a
noinst_PROGRAMS = test-controller

libcontroller_a_SOURCES =			\
	menu.vala				\
	controller.vala				\
	$(NULL)
libcontroller_a_VALAFLAGS =					\
	--pkg gio-2.0						\
	--pkg spice-protocol --vapidir=$(top_srcdir)/data	\
	--pkg custom --vapidir=$(srcdir)			\
	-C -H controller.h -g					\
	$(NULL)

if OS_WIN32
libcontroller_a_VALAFLAGS += --pkg gio-windows-2.0
else
libcontroller_a_VALAFLAGS += --pkg gio-unix-2.0
endif

test_controller_SOURCES = test.c
test_controller_LDADD = libcontroller.a

vala-clean:
	rm -f `find . -name "*.stamp"`

