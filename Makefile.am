ACLOCAL_AMFLAGS = -I m4
NULL =

SUBDIRS = spice-common src man po doc data tools

if BUILD_TESTS
SUBDIRS += tests
endif

if HAVE_INTROSPECTION
if WITH_VALA
SUBDIRS += vapi
endif
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =					\
	spice-client-glib-2.0.pc			\
	$(NULL)

if WITH_GTK
pkgconfig_DATA += spice-client-gtk-3.0.pc
endif

if WITH_CONTROLLER
pkgconfig_DATA += spice-controller.pc
endif

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST =					\
	build-aux/git-version-gen		\
	gtk-doc.make				\
	.version				\
	$(NULL)

MAINTAINERCLEANFILES =					\
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_GETTEXT)	\
	m4/gtk-doc.m4					\
	gtk-doc.make					\
	$(NULL)

# Generate the ChangeLog file
# and insert it into the directory we're about to use to create a tarball.
.PHONY: gen-ChangeLog gen-THANKS
gen-ChangeLog:
	if test -d .git || test -d ../.git; then				\
	  $(top_srcdir)/build-aux/gitlog-to-changelog > $(distdir)/cl-t;	\
	  rm -f $(distdir)/ChangeLog;						\
	  mv $(distdir)/cl-t $(distdir)/ChangeLog;				\
	fi

# see git-version-gen
dist-hook: gen-ChangeLog gen-THANKS
	echo $(VERSION) > $(distdir)/.tarball-version

BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

DISTCHECK_CONFIGURE_FLAGS =			\
	--enable-introspection			\
	--disable-vala				\
	--disable-usbredir			\
	--enable-gtk-doc			\
	--enable-werror				\
	--with-gtk=3.0				\
	$(NULL)

gen-THANKS:
	$(AM_V_GEN)if test -d .git || test -d ../.git; then						\
	  echo "The spice-gtk team would like to thank the following contributors:" > $(distdir)/t-t;	\
	  echo >> $(distdir)/t-t;									\
	  git log --format='%aN <%aE>' | sort -u >> $(distdir)/t-t;					\
	  rm -f $(distdir)/THANKS;									\
	  mv $(distdir)/t-t $(distdir)/THANKS;								\
	fi

-include $(top_srcdir)/git.mk
