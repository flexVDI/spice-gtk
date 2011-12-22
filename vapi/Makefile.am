NULL =
CLEANFILES =

BUILT_VAPI =						\
	spice-client-glib-2.0.vapi			\
	spice-client-gtk-$(SPICE_GTK_API_VERSION).vapi	\
	$(NULL)

vapidir = $(datadir)/vala/vapi
vapi_DATA =						\
	spice-client-gtk-$(SPICE_GTK_API_VERSION).deps	\
	spice-client-glib-2.0.deps			\
	$(BUILT_VAPI)					\
	$(NULL)

EXTRA_DIST =						\
	$(vapi_DATA)					\
	spice-client-gtk-2.0.vapi			\
	spice-client-gtk-3.0.vapi			\
	spice-client-gtk-2.0.deps			\
	spice-client-gtk-3.0.deps

if HAVE_INTROSPECTION
if WITH_VALA

CLEANFILES += $(BUILT_VAPI)

spice-client-glib-2.0.vapi: $(top_builddir)/gtk/SpiceClientGLib-2.0.gir
	$(AM_V_GEN)$(VAPIGEN)			\
		--library spice-client-glib-2.0	\
		$<

spice-client-gtk-$(SPICE_GTK_API_VERSION).vapi: $(top_builddir)/gtk/SpiceClientGtk-$(SPICE_GTK_API_VERSION).gir spice-client-glib-2.0.vapi
	$(AM_V_GEN)$(VAPIGEN)						\
		--vapidir=$(builddir)					\
		--girdir=$(top_builddir)/gtk				\
		--pkg spice-client-glib-2.0				\
		--pkg gtk+-$(GTK_API_VERSION)				\
		--library spice-client-gtk-$(SPICE_GTK_API_VERSION)	\
		$<

endif
endif

-include $(top_srcdir)/git.mk
