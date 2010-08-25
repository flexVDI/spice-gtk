NULL =

COMMON_DIR=$(SPICE_COMMON_SRCDIR)
CLIENT_DIR=$(top_srcdir)/client

EXTRA_DIST = spice-marshall.txt keymap-gen.pl keymaps.csv

bin_PROGRAMS = spicy snappy
lib_LTLIBRARIES = \
	libspice-client-gtk.la		\
	libspice-client-pulse.la	\
	libspice-client-glib.la

KEYMAP_GEN = $(srcdir)/keymap-gen.pl

KEYMAPS = \
	vncdisplaykeymap_xorgevdev2xtkbd.c \
	vncdisplaykeymap_xorgkbd2xtkbd.c \
	vncdisplaykeymap_xorgxquartz2xtkbd.c \
	vncdisplaykeymap_xorgxwin2xtkbd.c \
	vncdisplaykeymap_osx2xtkbd.c \
	vncdisplaykeymap_win322xtkbd.c

WARN = -Wall -Wno-sign-compare -Wmissing-prototypes -Wpointer-arith -Wunused

INCLUDES = \
	$(WARN)				\
	-DSW_CANVAS_CACHE		\
	\
	-I$(COMMON_DIR)			\
	-I$(CLIENT_DIR)			\
	-I$(CLIENT_DIR)/x11		\
	\
	$(PROTOCOL_CFLAGS)		\
	$(PIXMAN_CFLAGS)		\
	$(CELT051_CFLAGS)		\
	$(PULSE_CFLAGS)			\
	$(GTK2_CFLAGS)			\
	$(GLIB2_CFLAGS)			\
	$(GOBJECT2_CFLAGS)		\
	$(SSL_CFLAGS)			\
	$(NULL)


libspice_client_gtk_la_LDFLAGS =	\
	-version-number 0:0:1		\
	-no-undefined			\
	$(NULL)

libspice_client_gtk_la_LIBADD =		\
	$(GTK2_LIBS)			\
	$(NULL)

libspice_client_gtk_la_SOURCES =	\
	spice-widget.c			\
	spice-widget.h			\
	vncdisplaykeymap.c		\
	vncdisplaykeymap.h		\
	$(NULL)

libspice_client_gtkincludedir = $(includedir)/spice-client
libspice_client_gtkinclude_HEADERS =	\
	spice-widget.h			\
	$(NULL)


libspice_client_pulse_la_LDFLAGS =	\
	-version-number 0:0:1		\
	-no-undefined			\
	$(NULL)

libspice_client_pulse_la_LIBADD =	\
	$(PULSE_LIBS)			\
	$(NULL)

libspice_client_pulse_la_SOURCES =	\
	spice-pulse.c			\
	spice-pulse.h			\
	$(NULL)

libspice_client_pulseincludedir = $(includedir)/spice-client
libspice_client_pulseinclude_HEADERS =	\
	spice-pulse.h			\
	$(NULL)


libspice_client_glib_la_LDFLAGS =	\
	-version-number 0:0:1		\
	-no-undefined			\
	$(NULL)

libspice_client_glib_la_LIBADD =	\
	$(GLIB2_LIBS)			\
	$(GOBJECT2_LIBS)		\
	$(CELT051_LIBS)			\
	$(JPEG_LIBS)			\
	$(Z_LIBS)			\
	$(PIXMAN_LIBS)			\
	$(SSL_LIBS)			\
	$(NULL)

libspice_client_glib_la_SOURCES =	\
	spice-client.h			\
	spice-common.h			\
	\
	spice-events.c			\
	spice-events.h			\
	spice-session.c			\
	spice-session.h			\
	spice-channel.c			\
	spice-channel.h			\
	spice-channel-priv.h		\
	tcp.c				\
	tcp.h				\
	spice-marshal.c			\
	spice-marshal.h			\
	generated_demarshallers.c	\
	generated_demarshallers1.c	\
	generated_marshallers.c		\
	generated_marshallers1.c	\
	\
	channel-base.c			\
	channel-main.h			\
	channel-main.c			\
	channel-display.h		\
	channel-display.c		\
	channel-display-mjpeg.c		\
	channel-cursor.h		\
	channel-cursor.c		\
	channel-inputs.h		\
	channel-inputs.c		\
	channel-playback.h		\
	channel-playback.c		\
	channel-record.h		\
	channel-record.c		\
	\
	decode.h			\
	decode-glz.c			\
	decode-jpeg.c			\
	decode-zlib.c			\
	\
	$(COMMON_DIR)/mem.c		\
	$(COMMON_DIR)/mem.h		\
	$(COMMON_DIR)/marshaller.c	\
	$(COMMON_DIR)/marshaller.h	\
	$(COMMON_DIR)/canvas_utils.c	\
	$(COMMON_DIR)/canvas_utils.h	\
	$(COMMON_DIR)/sw_canvas.c	\
	$(COMMON_DIR)/sw_canvas.h	\
	$(COMMON_DIR)/pixman_utils.c	\
	$(COMMON_DIR)/pixman_utils.h	\
	$(COMMON_DIR)/lines.c		\
	$(COMMON_DIR)/lines.h		\
	$(COMMON_DIR)/rop3.c		\
	$(COMMON_DIR)/rop3.h		\
	$(COMMON_DIR)/quic.c		\
	$(COMMON_DIR)/quic.h		\
	$(COMMON_DIR)/lz.c		\
	$(COMMON_DIR)/lz.h		\
	$(NULL)

libspice_client_gtkincludedir = $(includedir)/spice-client
libspice_client_gtkinclude_HEADERS =	\
	spice-client.h			\
	spice-types.h			\
	spice-session.h			\
	spice-channel.h			\
	channel-main.h			\
	channel-display.h		\
	channel-cursor.h		\
	channel-inputs.h		\
	channel-playback.h		\
	channel-record.h		\
	$(NULL)


spicy_SOURCES =				\
	spicy.c				\
	$(NULL)

spicy_DEPENDENCIES =			\
	$(lib_LTLIBRARIES)		\
	$(NULL)

spicy_LDFLAGS =				\
	-lspice-client-gtk		\
	-lspice-client-pulse		\
	-lspice-client-glib		\
	$(NULL)


snappy_SOURCES =			\
	snappy.c			\
	$(NULL)

snappy_DEPENDENCIES =			\
	$(lib_LTLIBRARIES)		\
	$(NULL)

snappy_LDFLAGS =			\
	-lspice-client-glib		\
	$(NULL)


spice-channel.c: spice-marshal.h

spice-marshal.c: spice-marshal.txt
	$(AM_V_GEN)echo "#include \"spice-marshal.h\"" > $@ && \
		glib-genmarshal --body $< >> $@ || (rm -f $@ && exit 1)

spice-marshal.h: spice-marshal.txt
	$(AM_V_GEN)glib-genmarshal --header $< > $@ || (rm -f $@ && exit 1)

generated_demarshallers.c: $(top_srcdir)/spice.proto
	$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-demarshallers --client --include messages.h $< $@

generated_demarshallers1.c: $(top_srcdir)/spice1.proto
	$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-demarshallers --client --include messages.h --prefix 1 --ptrsize 8 $< $@

generated_marshallers.c: $(top_srcdir)/spice.proto
	$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-marshallers -P --include messages.h --include marshallers.h --client $< $@

generated_marshallers1.c: $(top_srcdir)/spice1.proto
	$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-marshallers -P --include messages.h --include marshallers.h --client --prefix 1 --ptrsize 8 $< $@

vncdisplaykeymap.c: $(KEYMAPS)

$(KEYMAPS): $(KEYMAP_GEN) keymaps.csv

vncdisplaykeymap_xorgevdev2xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgevdev xtkbd > $@ || rm $@

vncdisplaykeymap_xorgkbd2xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgkbd xtkbd > $@ || rm $@

vncdisplaykeymap_xorgxquartz2xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgxquartz xtkbd > $@ || rm $@

vncdisplaykeymap_xorgxwin2xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgxwin xtkbd > $@ || rm $@

vncdisplaykeymap_osx2xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv osx xtkbd > $@ || rm $@

vncdisplaykeymap_win322xtkbd.c:
	$(KEYMAP_GEN) $(srcdir)/keymaps.csv win32 xtkbd > $@ || rm $@

BUILT_SOURCES = spice-marshal.c spice-marshal.h \
	generated_demarshallers.c generated_demarshallers1.c \
	generated_marshallers.c generated_marshallers1.c \
	$(KEYMAPS)

CLEANFILES = $(BUILT_SOURCES)
