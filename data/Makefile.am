NULL=

# TODO: not sure if we want to do that
#desktopdir       = $(datadir)/applications
#desktop_in_files = spicy.desktop.in
#desktop_DATA     = $(desktop_in_files:.desktop.in=.desktop)
#@INTLTOOL_DESKTOP_RULE@
#
#MIMEFILES = spice-mime.xml.in
#mimedir = $(datadir)/mime/packages
#mime_DATA = spice-mime.xml
#@INTLTOOL_XML_RULE@

EXTRA_DIST =					\
	spicy-for-windows.nsis			\
	spice-mime.xml.in spicy.desktop.in      \
	$(desktop_in_files)			\
	$(NULL)

CLEANFILES =					\
	$(desktop_DATA)				\
	$(NULL)

DISTCLEANFILES =				\
	$(desktop_DATA)				\
	$(NULL)

vapidir = $(VAPIDIR)
vapi_DATA = spice-protocol.vapi

-include $(top_srcdir)/git.mk
