NULL =
CLEANFILES =

vapidir = $(datadir)/vala/vapi
vapi_DATA =						\
	spice-client-glib-2.0.vapi			\
	$(NULL)
dist_vapi_DATA =					\
	spice-client-glib-2.0.deps			\
	$(NULL)

if WITH_GTK
vapi_DATA += spice-client-gtk-3.0.vapi
dist_vapi_DATA += spice-client-gtk-3.0.deps
endif

EXTRA_DIST =						\
	spice-client-gtk-3.0.deps			\
	SpiceClientGLib-2.0.metadata			\
	$(NULL)

CLEANFILES += $(vapi_DATA)

spice-client-glib-2.0.vapi: $(top_builddir)/src/SpiceClientGLib-2.0.gir SpiceClientGLib-2.0.metadata
	$(AM_V_GEN)$(VAPIGEN) -q		\
		--metadatadir=$(srcdir)		\
		--library spice-client-glib-2.0	\
		--pkg gio-2.0			\
		$<

spice-client-gtk-3.0.vapi: $(top_builddir)/src/SpiceClientGtk-3.0.gir spice-client-glib-2.0.vapi
	$(AM_V_GEN)$(VAPIGEN) -q					\
		--vapidir=$(builddir)					\
		--girdir=$(top_builddir)/src				\
		--pkg spice-client-glib-2.0				\
		--pkg gtk+-3.0						\
		--library spice-client-gtk-3.0				\
		$<

-include $(top_srcdir)/git.mk
