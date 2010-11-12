NULL =

COMMON_DIR=$(SPICE_COMMON_SRCDIR)
CLIENT_DIR=$(top_srcdir)/client

EXTRA_DIST = spice-marshall.txt keymap-gen.pl keymaps.csv

bin_PROGRAMS = spicy snappy
lib_LTLIBRARIES = \
	libspice-client-gtk.la		\
	libspice-client-glib.la

KEYMAP_GEN = $(srcdir)/keymap-gen.pl

KEYMAPS = \
	vncdisplaykeymap_xorgevdev2xtkbd.c \
	vncdisplaykeymap_xorgkbd2xtkbd.c \
	vncdisplaykeymap_xorgxquartz2xtkbd.c \
	vncdisplaykeymap_xorgxwin2xtkbd.c \
	vncdisplaykeymap_osx2xtkbd.c \
	vncdisplaykeymap_win322xtkbd.c

INCLUDES = \
	-DG_LOG_DOMAIN=\"GSpice\"       \
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
	$(PULSE_LIBS)			\
	$(NULL)

libspice_client_gtk_la_SOURCES =	\
	spice-widget.c			\
	spice-widget.h			\
	vncdisplaykeymap.c		\
	vncdisplaykeymap.h		\
	spice-audio.c			\
	spice-audio.h			\
	spice-pulse.c			\
	spice-pulse.h			\
	$(NULL)

libspice_client_gtkincludedir = $(includedir)/spice-client
libspice_client_gtkinclude_HEADERS =	\
	spice-widget.h			\
	spice-audio.h			\
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
	spice-session.c			\
	spice-session.h			\
	spice-channel.c			\
	spice-channel.h			\
	spice-channel-enums.h		\
	spice-channel-enums.c		\
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
	$(COMMON_DIR)/region.c		\
	$(COMMON_DIR)/region.h		\
	$(NULL)

libspice_client_glibincludedir = $(includedir)/spice-client
libspice_client_glibinclude_HEADERS =	\
	spice-client.h			\
	spice-types.h			\
	spice-session.h			\
	spice-channel.h			\
	spice-channel-enums.h		\
	channel-main.h			\
	channel-display.h		\
	channel-cursor.h		\
	channel-inputs.h		\
	channel-playback.h		\
	channel-record.h		\
	$(NULL)


spicy_SOURCES =				\
	spicy.c				\
	spice-cmdline.h			\
	spice-cmdline.c			\
	$(NULL)

spicy_DEPENDENCIES =			\
	$(lib_LTLIBRARIES)		\
	$(NULL)

spicy_LDFLAGS =				\
	-lspice-client-gtk		\
	-lspice-client-glib		\
	$(NULL)


snappy_SOURCES =			\
	snappy.c			\
	spice-cmdline.h			\
	spice-cmdline.c			\
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

spice-channel-enums.c: spice-channel.h
	$(AM_V_GEN)glib-mkenums --fhead "#include <glib-object.h>\n" \
			--fhead "#include \"spice-channel-enums.h\"\n\n" \
			--fprod "\n#include \"spice-channel.h\"\n" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$< > $@

spice-channel-enums.h: spice-channel.h
	$(AM_V_GEN)glib-mkenums --fhead "#ifndef SPICE_CHANNEL_ENUMS_H\n" \
			--fhead "#define SPICE_CHANNEL_ENUMS_H\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* SPICE_CHANNEL_ENUMS_H */\n" \
			--eprod "#define SPICE_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$< >  $@

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
	spice-channel-enums.c spice-channel-enums.h \
	$(KEYMAPS)

CLEANFILES = $(BUILT_SOURCES)

if WITH_PYTHON
pyexec_LTLIBRARIES = SpiceClientGtk.la

SpiceClientGtk_la_LIBADD = libspice-client-gtk.la libspice-client-glib.la @PYGTK_LIBS@
SpiceClientGtk_la_CFLAGS = @GTK2_CFLAGS@ @PYTHON_INCLUDES@ @PYGTK_CFLAGS@
SpiceClientGtk_la_LDFLAGS = -module -avoid-version -fPIC
SpiceClientGtk_la_SOURCES = spice-client-gtk-module.c spice-client-gtk-module.defs.c

CODEGENDIR = $(shell pkg-config --variable=codegendir pygtk-2.0)
DEFSDIR = $(shell pkg-config --variable=defsdir pygtk-2.0)

spice-client-gtk.defs: $(libspice_client_gtkinclude_HEADERS) $(libspice_client_glibinclude_HEADERS)
	$(AM_V_GEN)$(PYTHON) $(CODEGENDIR)/h2def.py -f spice-client-gtk-manual.defs $(libspice_client_gtkinclude_HEADERS) $(libspice_client_glibinclude_HEADERS) > $@

spice-client-gtk-module.defs.c: spice-client-gtk.override spice-client-gtk.defs
	$(AM_V_GEN)pygobject-codegen-2.0 --prefix spice \
			  --register $(DEFSDIR)/gdk-types.defs \
			  --register $(DEFSDIR)/gtk-types.defs \
			  --override $(srcdir)/spice-client-gtk.override \
			  spice-client-gtk.defs spice-client-gtk-manual.defs > $@

CLEANFILES += spice-client-gtk.defs spice-client-gtk-module.defs.c
EXTRA_DIST += spice-client-gtk.override spice-client-gtk-manual.defs
else
EXTRA_DIST += spice-client-gtk.override spice-client-gtk-module.c spice-client-gtk-manual.defs
endif

-include $(INTROSPECTION_MAKEFILE)

if G_IR_SCANNER_SYMBOL_PREFIX
PREFIX_ARGS = --symbol-prefix=spice --identifier-prefix=Spice
else
PREFIX_ARGS = --strip-prefix=Spice
endif

INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) $(PREFIX_ARGS)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
glib_introspection_files = $(libspice_client_glibinclude_HEADERS)
gtk_introspection_files = $(libspice_client_gtkinclude_HEADERS)

SpiceClientGLib-1.0.gir: libspice-client-glib.la
SpiceClientGLib_1_0_gir_INCLUDES = GObject-2.0
SpiceClientGLib_1_0_gir_CFLAGS = $(INCLUDES)
SpiceClientGLib_1_0_gir_LIBS = libspice-client-glib.la
SpiceClientGLib_1_0_gir_FILES = $(glib_introspection_files)
INTROSPECTION_GIRS += SpiceClientGLib-1.0.gir

SpiceClientGtk-1.0.gir: libspice-client-gtk.la
SpiceClientGtk_1_0_gir_INCLUDES = GObject-2.0 Gtk-2.0 SpiceClientGLib-1.0
SpiceClientGtk_1_0_gir_CFLAGS = $(INCLUDES)
SpiceClientGtk_1_0_gir_LIBS = libspice-client-gtk.la libspice-client-glib.la
SpiceClientGtk_1_0_gir_FILES = $(gtk_introspection_files)
INTROSPECTION_GIRS += SpiceClientGtk-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)
endif
