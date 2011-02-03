NULL =

COMMON_DIR=$(SPICE_COMMON_SRCDIR)
CLIENT_DIR=$(top_srcdir)/client

EXTRA_DIST =					\
	spice-marshal.txt			\
	keymap-gen.pl				\
	keymaps.csv				\
	decode-glz-tmpl.c			\
	map-file				\
	$(NULL)

bin_PROGRAMS = spicy snappy
lib_LTLIBRARIES = \
	libspice-client-glib-2.0.la

if HAVE_GTK_2
lib_LTLIBRARIES += \
	libspice-client-gtk-2.0.la
else
lib_LTLIBRARIES += \
	libspice-client-gtk-3.0.la
endif

if HAVE_LD_VERSION_SCRIPT
VERSION_LDFLAGS = -Wl,--version-script=${srcdir}/map-file
else
VERSION_LDFLAGS = -export-symbols ${srcdir}/sym-file
endif

KEYMAP_GEN = $(srcdir)/keymap-gen.pl

KEYMAPS = \
	vncdisplaykeymap_xorgevdev2xtkbd.c \
	vncdisplaykeymap_xorgkbd2xtkbd.c \
	vncdisplaykeymap_xorgxquartz2xtkbd.c \
	vncdisplaykeymap_xorgxwin2xtkbd.c \
	vncdisplaykeymap_osx2xtkbd.c \
	vncdisplaykeymap_win322xtkbd.c

SPICE_COMMON_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"GSpice\"       \
	-DSW_CANVAS_CACHE		\
	-DSPICE_GTK_LOCALEDIR=\"${SPICE_GTK_LOCALEDIR}\" \
	\
	-I$(COMMON_DIR)			\
	-I$(CLIENT_DIR)			\
	-I$(CLIENT_DIR)/x11		\
	\
	$(PROTOCOL_CFLAGS)		\
	$(PIXMAN_CFLAGS)		\
	$(CELT051_CFLAGS)		\
	$(PULSE_CFLAGS)			\
	$(GTK_CFLAGS)			\
	$(CAIRO_CFLAGS)			\
	$(GLIB2_CFLAGS)			\
	$(GIO_CFLAGS)			\
	$(GOBJECT2_CFLAGS)		\
	$(SSL_CFLAGS)			\
	$(SASL_CFLAGS)			\
	$(GST_CFLAGS)			\
	$(NULL)

AM_CPPFLAGS = \
	$(SPICE_COMMON_CPPFLAGS)	\
	$(SPICE_CFLAGS)

# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
SPICE_GTK_LDFLAGS_COMMON =		\
	-version-number 1:0:0		\
	-no-undefined			\
	$(VERSION_LDFLAGS)		\
	$(NULL)

SPICE_GTK_LIBADD_COMMON =		\
	libspice-client-glib-2.0.la	\
	$(GTK_LIBS)			\
	$(CAIRO_LIBS)			\
	$(NULL)

SPICE_GTK_SOURCES_COMMON =		\
	spice-widget.c			\
	spice-widget.h			\
	spice-widget-priv.h		\
	spice-widget-enums.c		\
	spice-widget-enums.h		\
	vncdisplaykeymap.c		\
	vncdisplaykeymap.h		\
	spice-grabsequence.c		\
	spice-grabsequence.h		\
	$(NULL)

if WITH_X11
SPICE_GTK_SOURCES_COMMON +=		\
	spice-widget-x11.c		\
	$(NULL)
else
SPICE_GTK_SOURCES_COMMON +=		\
	spice-widget-cairo.c		\
	$(NULL)
endif

if HAVE_GTK_2
libspice_client_gtk_2_0_la_LDFLAGS = $(SPICE_GTK_LDFLAGS_COMMON)
libspice_client_gtk_2_0_la_LIBADD = $(SPICE_GTK_LIBADD_COMMON)
libspice_client_gtk_2_0_la_SOURCES = $(SPICE_GTK_SOURCES_COMMON)
else
libspice_client_gtk_3_0_la_LDFLAGS = $(SPICE_GTK_LDFLAGS_COMMON)
libspice_client_gtk_3_0_la_LIBADD = $(SPICE_GTK_LIBADD_COMMON)
libspice_client_gtk_3_0_la_SOURCES = $(SPICE_GTK_SOURCES_COMMON)
endif


libspice_client_gtkincludedir = $(includedir)/spice-client-gtk-$(SPICE_GTK_API_VERSION)
libspice_client_gtkinclude_HEADERS =	\
	spice-widget.h			\
	spice-widget-enums.h		\
	spice-grabsequence.h		\
	$(NULL)

libspice_client_glib_2_0_la_LDFLAGS =	\
	-version-number 2:0:1		\
	-no-undefined			\
	$(VERSION_LDFLAGS)		\
	$(NULL)

libspice_client_glib_2_0_la_LIBADD =	\
	$(GLIB2_LIBS)			\
	$(GIO_LIBS)			\
	$(GOBJECT2_LIBS)		\
	$(CELT051_LIBS)			\
	$(JPEG_LIBS)			\
	$(Z_LIBS)			\
	$(PIXMAN_LIBS)			\
	$(SSL_LIBS)			\
	$(PULSE_LIBS)			\
	$(GST_LIBS)			\
	$(SASL_LIBS)			\
	$(NULL)

libspice_client_glib_2_0_la_SOURCES =	\
	spice-audio.c			\
	spice-audio.h			\
	spice-client.h			\
	spice-common.h			\
	spice-util.h			\
	spice-util.c			\
	\
	spice-session.c			\
	spice-session.h			\
	spice-session-priv.h		\
	spice-channel.c			\
	spice-channel.h			\
	spice-channel-cache.h		\
	spice-glib-enums.h		\
	spice-glib-enums.c		\
	spice-channel-priv.h		\
	spice-marshal.c			\
	spice-marshal.h			\
	generated_demarshallers.c	\
	generated_demarshallers1.c	\
	generated_marshallers.c		\
	generated_marshallers1.c	\
	coroutine.h			\
	gio-coroutine.c			\
	gio-coroutine.h			\
	\
	channel-base.c			\
	channel-main.h			\
	channel-main.c			\
	channel-display.h		\
	channel-display.c		\
	channel-display-priv.h		\
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
	$(COMMON_DIR)/ssl_verify.c	\
	$(COMMON_DIR)/ssl_verify.h	\
	$(NULL)

if WITH_PULSE
libspice_client_glib_2_0_la_SOURCES +=	\
	spice-pulse.c			\
	spice-pulse.h			\
	$(NULL)
endif

if WITH_GSTAUDIO
libspice_client_glib_2_0_la_SOURCES +=	\
	spice-gstaudio.c		\
	spice-gstaudio.h		\
	$(NULL)
endif

if WITH_UCONTEXT
libspice_client_glib_2_0_la_SOURCES += continuation.h continuation.c coroutine_ucontext.c
EXTRA_DIST += coroutine_gthread.c
else
libspice_client_glib_2_0_la_SOURCES += coroutine_gthread.c
EXTRA_DIST += continuation.h continuation.c coroutine_ucontext.c
endif


libspice_client_glibincludedir = $(includedir)/spice-client-glib-2.0
libspice_client_glibinclude_HEADERS =	\
	spice-audio.h			\
	spice-client.h			\
	spice-types.h			\
	spice-session.h			\
	spice-channel.h			\
	spice-channel-enums.h		\
	spice-glib-enums.h		\
	spice-util.h			\
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

spicy_LDADD =				\
	libspice-client-gtk-$(SPICE_GTK_API_VERSION).la		\
	libspice-client-glib-2.0.la		\
	$(NULL)


snappy_SOURCES =			\
	snappy.c			\
	spice-cmdline.h			\
	spice-cmdline.c			\
	$(NULL)

snappy_LDADD =				\
	libspice-client-glib-2.0.la		\
	$(NULL)


spice-channel.c: spice-marshal.h

spice-marshal.c: spice-marshal.txt
	$(AM_V_GEN)echo "#include \"spice-marshal.h\"" > $@ && \
		glib-genmarshal --body $< >> $@ || (rm -f $@ && exit 1)

spice-marshal.h: spice-marshal.txt
	$(AM_V_GEN)glib-genmarshal --header $< > $@ || (rm -f $@ && exit 1)

spice-glib-enums.c: spice-channel.h channel-inputs.h spice-session.h
	$(AM_V_GEN)glib-mkenums --fhead "#include <glib-object.h>\n" \
			--fhead "#include \"spice-glib-enums.h\"\n\n" \
			--fprod "\n#include \"spice-session.h\"\n" \
			--fprod "\n#include \"spice-channel.h\"\n" \
			--fprod "\n#include \"channel-inputs.h\"\n" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$^ > $@

spice-glib-enums.h: spice-channel.h channel-inputs.h spice-session.h
	$(AM_V_GEN)glib-mkenums --fhead "#ifndef SPICE_GLIB_ENUMS_H\n" \
			--fhead "#define SPICE_GLIB_ENUMS_H\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* SPICE_CHANNEL_ENUMS_H */\n" \
			--eprod "#define SPICE_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$^ >  $@

spice-widget-enums.c: spice-widget.h
	$(AM_V_GEN)glib-mkenums --fhead "#include <glib-object.h>\n" \
			--fhead "#include \"spice-widget-enums.h\"\n\n" \
			--fprod "\n#include \"spice-widget.h\"\n" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$< > $@

spice-widget-enums.h: spice-widget.h
	$(AM_V_GEN)glib-mkenums --fhead "#ifndef SPICE_WIDGET_ENUMS_H\n" \
			--fhead "#define SPICE_WIDGET_ENUMS_H\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* SPICE_WIDGET_ENUMS_H */\n" \
			--eprod "#define SPICE_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$< >  $@


generated_demarshallers.c: $(top_srcdir)/spice.proto
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-demarshallers --client --include messages.h $< $@

generated_demarshallers1.c: $(top_srcdir)/spice1.proto
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-demarshallers --client --include messages.h --prefix 1 --ptrsize 8 $< $@

generated_marshallers.c: $(top_srcdir)/spice.proto
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-marshallers -P --include messages.h --include marshallers.h --client $< $@

generated_marshallers1.c: $(top_srcdir)/spice1.proto
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/spice_codegen.py --generate-marshallers -P --include messages.h --include marshallers.h --client --prefix 1 --ptrsize 8 $< $@

vncdisplaykeymap.c: $(KEYMAPS)

$(KEYMAPS): $(KEYMAP_GEN) keymaps.csv

vncdisplaykeymap_xorgevdev2xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgevdev xtkbd > $@ || rm $@

vncdisplaykeymap_xorgkbd2xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgkbd xtkbd > $@ || rm $@

vncdisplaykeymap_xorgxquartz2xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgxquartz xtkbd > $@ || rm $@

vncdisplaykeymap_xorgxwin2xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv xorgxwin xtkbd > $@ || rm $@

vncdisplaykeymap_osx2xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv osx xtkbd > $@ || rm $@

vncdisplaykeymap_win322xtkbd.c:
	$(AM_V_GEN)$(KEYMAP_GEN) $(srcdir)/keymaps.csv win32 xtkbd > $@ || rm $@

BUILT_SOURCES = spice-marshal.c spice-marshal.h \
	generated_demarshallers.c generated_demarshallers1.c \
	generated_marshallers.c generated_marshallers1.c \
	spice-glib-enums.c spice-glib-enums.h \
	spice-widget-enums.c spice-widget-enums.h \
	$(KEYMAPS)

CLEANFILES = $(BUILT_SOURCES)

if WITH_PYTHON
pyexec_LTLIBRARIES = SpiceClientGtk.la

SpiceClientGtk_la_LIBADD = libspice-client-gtk-2.0.la libspice-client-glib-2.0.la $(PYGTK_LIBS)
SpiceClientGtk_la_CFLAGS = $(GTK_CFLAGS) $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
SpiceClientGtk_la_LDFLAGS = -module -avoid-version -fPIC
SpiceClientGtk_la_SOURCES = spice-client-gtk-module.c spice-client-gtk-module.defs.c

CODEGENDIR = `pkg-config --variable=codegendir pygtk-2.0`
DEFSDIR = `pkg-config --variable=defsdir pygtk-2.0`

spice-client-gtk.defs: $(libspice_client_gtkinclude_HEADERS) $(libspice_client_glibinclude_HEADERS)
	$(AM_V_GEN)$(PYTHON) $(CODEGENDIR)/h2def.py -f spice-client-gtk-manual.defs $(libspice_client_gtkinclude_HEADERS) $(libspice_client_glibinclude_HEADERS) > $@

spice-client-gtk-module.defs.c: spice-client-gtk.override spice-client-gtk.defs spice-client-gtk-manual.defs
	@cat $(srcdir)/spice-client-gtk.defs $(srcdir)/spice-client-gtk-manual.defs > tmp.defs
	$(AM_V_GEN)pygobject-codegen-2.0 --prefix spice \
			  --register $(DEFSDIR)/gdk-types.defs \
			  --register $(DEFSDIR)/gtk-types.defs \
			  --override $(srcdir)/spice-client-gtk.override \
			  tmp.defs > $@
	@rm tmp.defs

CLEANFILES += spice-client-gtk-module.defs.c
EXTRA_DIST += spice-client-gtk.override spice-client-gtk-manual.defs spice-client-gtk.defs
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
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(builddir) $(PREFIX_ARGS)
INTROSPECTION_COMPILER_ARGS = --includedir=$(builddir)

if HAVE_INTROSPECTION
glib_introspection_files = $(libspice_client_glibinclude_HEADERS)
gtk_introspection_files = $(libspice_client_gtkinclude_HEADERS)

SpiceClientGLib-2.0.gir: libspice-client-glib-2.0.la
SpiceClientGLib_2_0_gir_INCLUDES = GObject-2.0
SpiceClientGLib_2_0_gir_CFLAGS = $(SPICE_COMMON_CPPFLAGS)
SpiceClientGLib_2_0_gir_LIBS = libspice-client-glib-2.0.la
SpiceClientGLib_2_0_gir_FILES = $(glib_introspection_files)
INTROSPECTION_GIRS += SpiceClientGLib-2.0.gir

if HAVE_GTK_2
SpiceClientGtk-2.0.gir: libspice-client-gtk-2.0.la SpiceClientGLib-2.0.gir
SpiceClientGtk_2_0_gir_INCLUDES = GObject-2.0 Gtk-2.0 SpiceClientGLib-2.0
SpiceClientGtk_2_0_gir_CFLAGS = $(SPICE_COMMON_CPPFLAGS)
SpiceClientGtk_2_0_gir_LIBS = libspice-client-gtk-2.0.la libspice-client-glib-2.0.la
SpiceClientGtk_2_0_gir_FILES = $(gtk_introspection_files)
else
SpiceClientGtk-3.0.gir: libspice-client-gtk-3.0.la SpiceClientGLib-2.0.gir
SpiceClientGtk_3_0_gir_INCLUDES = GObject-2.0 Gtk-3.0 SpiceClientGLib-2.0
SpiceClientGtk_3_0_gir_CFLAGS = $(SPICE_COMMON_CPPFLAGS)
SpiceClientGtk_3_0_gir_LIBS = libspice-client-gtk-3.0.la libspice-client-glib-2.0.la
SpiceClientGtk_3_0_gir_FILES = $(gtk_introspection_files)
endif
INTROSPECTION_GIRS += SpiceClientGtk-$(SPICE_GTK_API_VERSION).gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)
endif

update-map-file:
	( echo "SPICEGTK_1 {" ; \
	  echo "global:" ; \
	  ctags -f - --c-kinds=p $(libspice_client_gtkinclude_HEADERS) $(libspice_client_glibinclude_HEADERS) | awk '/^spice_/ { print $$1 ";" }' | sort ; \
	  echo "local:" ;  \
	  echo "*;" ; \
	  echo "};" ) > $(srcdir)/map-file

-include $(top_srcdir)/git.mk
