AC_PREREQ([2.57])

AC_INIT([spice-gtk], [m4_esyscmd(build-aux/git-version-gen .tarball-version)],
        [spice-devel@lists.freedesktop.org])

AC_CONFIG_MACRO_DIR([m4])
m4_include([spice-common/m4/spice-deps.m4])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign dist-bzip2 -Wall -Werror -Wno-portability])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
LT_INIT([disable-static win32-dll])
AM_MAINTAINER_MODE

IT_PROG_INTLTOOL([0.40.0])
GETTEXT_PACKAGE=spice-gtk
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GLIB_GNU_GETTEXT


SPICE_GTK_LOCALEDIR=[${datadir}/locale]
AC_SUBST(SPICE_GTK_LOCALEDIR)

GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

AC_PROG_CC
AC_PROG_CC_C99
if test "x$ac_cv_prog_cc_c99" = xno; then
    AC_MSG_ERROR([C99 compiler is required.])
fi


AC_CHECK_PROG([STOW], [stow], [yes], [no])
AS_IF([test "x$STOW" = "xyes" && test -d /usr/local/stow], [
    AC_MSG_NOTICE([*** Found /usr/local/stow: default install prefix set to /usr/local/stow/${PACKAGE_NAME} ***])
    ac_default_prefix="/usr/local/stow/${PACKAGE_NAME}"
])

AC_PROG_INSTALL
AC_CANONICAL_HOST
AC_PROG_LIBTOOL
AM_PROG_CC_C_O
AC_C_BIGENDIAN
AM_PATH_PYTHON(,, [:])
RRA_LD_VERSION_SCRIPT

AC_MSG_CHECKING([for native Win32])
case "$host_os" in
     *mingw*|*cygwin*)
        os_win32=yes
        gio_os=gio-windows-2.0
        red_target=Windows
        ;;
     *)
        os_win32=no
        gio_os=gio-unix-2.0
        red_target=Unix
        ;;
esac
AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL([OS_WIN32],[test "$os_win32" = "yes"])

AC_CHECK_HEADERS([sys/socket.h netinet/in.h arpa/inet.h])
AC_CHECK_HEADERS([termios.h])
AC_CHECK_HEADERS([epoxy/egl.h],
                 [have_egl=yes],
                 [have_egl=no])
AC_MSG_CHECKING([if we can use EGL in libepoxy)])
AC_MSG_RESULT([$have_egl])
AM_CONDITIONAL([HAVE_EGL],[test "$have_egl" = "yes"])
AS_IF([test "$have_egl" = "yes"],
       AC_DEFINE([HAVE_EGL], [1], [Define if supporting EGL]))

AC_CHECK_LIBM
AC_SUBST(LIBM)

AC_CONFIG_SUBDIRS([spice-common])
PKG_CHECK_MODULES([SPICE_PROTOCOL], [spice-protocol >= 0.12.13])

COMMON_CFLAGS='-I${top_builddir}/spice-common/ -I${top_srcdir}/spice-common/ ${SPICE_PROTOCOL_CFLAGS}'
AC_SUBST(COMMON_CFLAGS)

SPICE_GTK_MAJOR_VERSION=`echo $PACKAGE_VERSION | cut -d. -f1`
SPICE_GTK_MINOR_VERSION=`echo $PACKAGE_VERSION | cut -d. -f2`
SPICE_GTK_MICRO_VERSION=`echo $PACKAGE_VERSION | cut -d. -f3 | cut -d- -f1`
AS_IF([test "x$SPICE_GTK_MICRO_VERSION" = "x"], [SPICE_GTK_MICRO_VERSION=0])

AC_SUBST(SPICE_GTK_MAJOR_VERSION)
AC_SUBST(SPICE_GTK_MINOR_VERSION)
AC_SUBST(SPICE_GTK_MICRO_VERSION)

dnl =========================================================================
dnl Chek optional features

SPICE_GLIB_REQUIRES=""
SPICE_GTK_REQUIRES=""

PKG_CHECK_MODULES(PIXMAN, pixman-1 >= 0.17.7)

SPICE_GLIB_REQUIRES="${SPICE_GLIB_REQUIRES} pixman-1 >= 0.17.7"

PKG_CHECK_MODULES(SSL, openssl)

SPICE_GLIB_REQUIRES="${SPICE_GLIB_REQUIRES} openssl"

SPICE_CHECK_SASL

AC_MSG_CHECKING([which gtk+ version to compile against])
AC_ARG_WITH([gtk],
  [AS_HELP_STRING([--with-gtk=@<:@3.0/no@:>@],[which gtk+ version to compile against @<:@default=3.0@:>@])],
  [case "$with_gtk" in
     3.0) AC_MSG_RESULT([$with_gtk]) ;;
     no) AC_MSG_RESULT([none]) ;;
     *) AC_MSG_ERROR([invalid gtk version specified]) ;;
   esac],
  [with_gtk=3.0])

case "$with_gtk" in
  3.0) GTK_REQUIRED=3.12
       GTK_ENCODED_VERSION="GDK_VERSION_3_12"
       ;;
  no)
       AS_IF([test x$enable_gtk_doc = xyes],
             [AC_MSG_ERROR([Without GTK+, gtk-doc must be disabled])])
esac

AC_SUBST([GTK_REQUIRED])
AM_CONDITIONAL([WITH_GTK],[test "$with_gtk" != "no"])

AS_IF([test "x$with_gtk" != "xno"],
      [AS_IF([test "x$os_win32" = "xyes"],
             [PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQUIRED)],
             [PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQUIRED epoxy)])]
      [GTK_CFLAGS="$GTK_CFLAGS -DGDK_VERSION_MIN_REQUIRED=$GTK_ENCODED_VERSION \
                               -DGDK_VERSION_MAX_ALLOWED=$GTK_ENCODED_VERSION"])
SPICE_GTK_REQUIRES="${SPICE_GTK_REQUIRES} gtk+-3.0 >= $GTK_REQUIRED"

# Check for gdk_event_get_scancode function
# This was added in Gdk 3.22
# The check allows the usage of the function in case the function is
# backported or in case of compilation from Gdk master branch
old_LIBS="$LIBS"
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $GTK_CFLAGS"
LIBS="$LIBS $GTK_LIBS"
AC_CHECK_FUNCS(gdk_event_get_scancode)
LIBS="$old_LIBS"
CFLAGS="$old_CFLAGS"

PKG_CHECK_EXISTS([gtk+-x11-$with_gtk], [PKG_CHECK_MODULES(X11, x11)])
AC_CHECK_HEADERS([X11/XKBlib.h])

AC_ARG_WITH([pnp-ids-path],
  AC_HELP_STRING([--with-pnp-ids-path],
                 [Specify the path to pnp.ids @<:@default=(internal)@:>@]),
  [],
  [with_pnp_ids_path="\${pnpdatadir}/pnp.ids"])

AM_CONDITIONAL(USE_INTERNAL_PNP_IDS, test "x$with_pnp_ids_path" = "x\${pnpdatadir}/pnp.ids")
PNP_IDS=$with_pnp_ids_path
AC_SUBST(PNP_IDS)
if test "x$with_pnp_ids_path" = "x\${pnpdatadir}/pnp.ids"; then
        EXTERNAL_PNP_IDS="no (internal)"
else
        EXTERNAL_PNP_IDS="$with_pnp_ids_path"
fi

AC_CHECK_FUNCS(clearenv strtok_r)

# Keep these two definitions in agreement.
GLIB2_REQUIRED="2.36"
GLIB2_ENCODED_VERSION="GLIB_VERSION_2_36"

PKG_CHECK_MODULES(GLIB2, glib-2.0 >= $GLIB2_REQUIRED)
GLIB2_CFLAGS="$GLIB2_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=$GLIB2_ENCODED_VERSION \
  -DGLIB_VERSION_MAX_ALLOWED=$GLIB2_ENCODED_VERSION"

PKG_CHECK_MODULES(GOBJECT2, gobject-2.0)

PKG_CHECK_MODULES(GIO, gio-2.0 >= 2.36 $gio_os)

PKG_CHECK_MODULES(CAIRO, cairo >= 1.2.0)

PKG_CHECK_MODULES(GTHREAD, gthread-2.0)

AC_ARG_ENABLE([webdav],
  AS_HELP_STRING([--enable-webdav=@<:@auto/yes/no@:>@],
                 [Enable webdav support @<:@default=auto@:>@]),
  [],
  [enable_webdav="auto"])

if test "x$enable_webdav" = "xno"; then
  have_phodav="no"
else
  PKG_CHECK_MODULES(PHODAV, [libphodav-2.0 glib-2.0 >= 2.43.90 libsoup-2.4 >= 2.49.91], [have_phodav=yes], [have_phodav=no])

  if test "x$have_phodav" = "xno" && test "x$enable_webdav" = "xyes"; then
    AC_MSG_ERROR([webdav support explicitly requested, but some required packages are not available])
  fi
fi
AS_IF([test "x$have_phodav" = "xyes"],
       AC_DEFINE([USE_PHODAV], [1], [Define if supporting phodav]))

AM_CONDITIONAL([WITH_PHODAV], [test "x$have_phodav" = "xyes"])

AC_ARG_WITH([audio],
  AS_HELP_STRING([--with-audio=@<:@gstreamer/pulse/auto/no@:>@], [For legacy compatibility only]),
  [SPICE_WARNING([--with-audio is deprecated. Use --enable-pulse and/or --enable-gstaudio instead])
   case "$with_audio" in
   pulse)     enable_pulse="yes"; enable_gstaudio="no" ;;
   gstreamer) enable_pulse="no";  enable_gstaudio="yes" ;;
   no)        enable_pulse="no";  enable_gstaudio="no" ;;
   esac
])

AC_ARG_ENABLE([pulse],
  AS_HELP_STRING([--enable-pulse=@<:@yes/auto/no@:>@], [Enable the PulseAudio backend @<:@default=auto@:>@]),
  [],
  [enable_pulse="auto"])
AS_IF([test "x$enable_pulse" != "xno"],
      [PKG_CHECK_MODULES(PULSE, [libpulse libpulse-mainloop-glib],
         [AC_DEFINE([HAVE_PULSE], 1, [Have PulseAudio support?])
          enable_pulse="yes"],
         [AS_IF([test "x$enable_pulse" = "xyes"],
                AC_MSG_ERROR([PulseAudio requested but not found]))
          enable_pulse="no"
      ])
])
AM_CONDITIONAL([HAVE_PULSE], [test "x$enable_pulse" = "xyes"])

AC_ARG_ENABLE([gstaudio],
  AS_HELP_STRING([--enable-gstaudio=@<:@yes/auto/no@:>@], [Enable the GStreamer 1.0 audio backend @<:@default=auto@:>@]),
  [],
  [enable_gstaudio="auto"])
AS_IF([test "x$enable_gstaudio" != "xno"],
      [SPICE_CHECK_GSTREAMER(GSTAUDIO, 1.0, [gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-audio-1.0],
         [SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gst-plugins-base 1.0], [audioconvert audioresample appsink])
          SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gst-plugins-good 1.0], [autoaudiosrc])
          AS_IF([test x"$missing_gstreamer_elements" = "xyes"],
                SPICE_WARNING([The GStreamer 1.0 audio backend can be built but may not work.]))
         ],
         [AS_IF([test "x$enable_gstaudio" = "xyes"],
                AC_MSG_ERROR([GStreamer 1.0 audio requested but not found]))
         ])
      ], [have_gstaudio="no"]
)
AM_CONDITIONAL([HAVE_GSTAUDIO], [test "x$have_gstaudio" = "xyes"])

AS_IF([test "x$enable_pulse$have_gstaudio" = "xnono"],
      [SPICE_WARNING([No PulseAudio or GStreamer 1.0 audio decoder, audio will not be streamed])
])

AC_ARG_ENABLE([gstvideo],
  AS_HELP_STRING([--enable-gstvideo=@<:@auto/yes/no@:>@],
                 [Enable GStreamer video support @<:@default=auto@:>@]),
  [],
  [enable_gstvideo="auto"])
AS_IF([test "x$enable_gstvideo" != "xno"],
      [SPICE_CHECK_GSTREAMER(GSTVIDEO, 1.0,
         [gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-video-1.0],
         [missing_gstreamer_elements=""
          SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gst-plugins-base 1.0], [appsrc videoconvert appsink])
          SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gst-plugins-good 1.0], [jpegdec vp8dec vp9dec])
          SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gst-plugins-bad 1.0], [h264parse])
          SPICE_CHECK_GSTREAMER_ELEMENTS($GST_INSPECT_1_0, [gstreamer-libav 1.0], [avdec_h264])
          AS_IF([test x"$missing_gstreamer_elements" = "xyes"],
                SPICE_WARNING([The GStreamer video decoder can be built but may not work.]))
         ],
         [AS_IF([test "x$enable_gstvideo" = "xyes"],
                AC_MSG_ERROR([GStreamer 1.0 video requested but not found]))
         ])
      ], [have_gstvideo="no"]
)
AM_CONDITIONAL([HAVE_GSTVIDEO], [test "x$have_gstvideo" = "xyes"])

AC_ARG_ENABLE([builtin-mjpeg],
  AS_HELP_STRING([--enable-builtin-mjpeg], [Enable the builtin mjpeg video decoder @<:@default=yes@:>@]),
  [],
  enable_builtin_mjpeg="yes")
AS_IF([test "x$enable_builtin_mjpeg" = "xyes"],
      [AC_DEFINE([HAVE_BUILTIN_MJPEG], 1, [Use the builtin mjpeg decoder?])])
AM_CONDITIONAL(HAVE_BUILTIN_MJPEG, [test "x$enable_builtin_mjpeg" != "xno"])

AS_IF([test "x$enable_builtin_mjpeg$enable_gstvideo" = "xnono"],
      [SPICE_WARNING([No builtin MJPEG or GStreamer decoder, video will not be streamed])])

AC_CHECK_LIB(jpeg, jpeg_destroy_decompress,
    AC_MSG_CHECKING([for jpeglib.h])
    AC_TRY_CPP(
[#include <stdio.h>
#undef PACKAGE
#undef VERSION
#undef HAVE_STDLIB_H
#include <jpeglib.h>],
        JPEG_LIBS='-ljpeg'
        AC_MSG_RESULT($jpeg_ok),
	AC_MSG_ERROR([jpeglib.h not found])),
    AC_MSG_ERROR([libjpeg not found]))
AC_SUBST(JPEG_LIBS)

AC_CHECK_LIB(z, deflate, Z_LIBS='-lz', AC_MSG_ERROR([zlib not found]))
AC_SUBST(Z_LIBS)

SPICE_CHECK_SMARTCARD
AM_CONDITIONAL([WITH_SMARTCARD], [test "x$have_smartcard" = "xyes"])

AC_ARG_ENABLE([usbredir],
  AS_HELP_STRING([--enable-usbredir=@<:@auto/yes/no@:>@],
                 [Enable usbredir support @<:@default=auto@:>@]),
  [],
  [enable_usbredir="auto"])

if test "x$enable_usbredir" = "xno"; then
  have_usbredir="no"
else
  if ${PKG_CONFIG} libusbredirparser-0.5; then
    PKG_CHECK_MODULES([USBREDIR],
                      [libusb-1.0 >= 1.0.9 libusbredirhost libusbredirparser-0.5],
                      [have_usbredir=yes],
                      [have_usbredir=no])
  else
    PKG_CHECK_MODULES([USBREDIR],
                      [libusb-1.0 >= 1.0.9 libusbredirhost >= 0.4.2 libusbredirparser >= 0.4],
                      [have_usbredir=yes],
                      [have_usbredir=no])
  fi
  if test "x$have_usbredir" = "xno" && test "x$enable_usbredir" = "xyes"; then
    AC_MSG_ERROR([usbredir support explicitly requested, but some required packages are not available])
  fi

  # On non windows we need either libusb hotplug support or gudev
  if test "x$have_usbredir" = "xyes" && test "x$os_win32" = "xno"; then
    PKG_CHECK_MODULES([LIBUSB_HOTPLUG], [libusb-1.0 >= 1.0.16],
                      [have_libusb_hotplug=yes], [have_libusb_hotplug=no])
    if test "x$have_libusb_hotplug" = "xyes"; then
      AC_DEFINE([USE_LIBUSB_HOTPLUG], [1], [Define if libusb has hotplug support])
      with_usbredir_hotplug="with libusb hotplug"
    else
      PKG_CHECK_MODULES([GUDEV],
                        [gudev-1.0],
                        [have_gudev=yes],
                        [have_gudev=no])

      if test "x$have_gudev" = "xno" && test "x$enable_usbredir" = "xyes"; then
        AC_MSG_ERROR([usbredir requested but required gudev is not available])
      fi
      if test "x$have_gudev" = "xyes"; then
        AC_DEFINE([USE_GUDEV], [1], [Define if supporting gudev])
        with_usbredir_hotplug="with gudev hotplug"
      else
        have_usbredir=no
      fi
    fi
  fi

  if test "x$have_usbredir" = "xyes"; then
    AC_DEFINE([USE_USBREDIR], [1], [Define if supporting usbredir proxying])
  fi
fi
AM_CONDITIONAL([WITH_USBREDIR], [test "x$have_usbredir" = "xyes"])

AC_ARG_ENABLE([polkit],
  AS_HELP_STRING([--enable-polkit=@<:@auto/yes/no@:>@],
                 [Enable PolicyKit support (for the usb acl helper)@<:@default=auto@:>@]),
  [],
  [enable_polkit="auto"])

if test "x$have_usbredir" = "xyes" && test "x$enable_polkit" != "xno"; then
  PKG_CHECK_MODULES([POLKIT], [polkit-gobject-1 >= 0.96],
                              [have_polkit=yes],
                              [have_polkit=no])
  AC_CHECK_HEADER([acl/libacl.h], [], [have_polkit=no])
  AC_CHECK_LIB([acl], [acl_get_file], [ACL_LIBS=-lacl], [have_polkit=no])
  if test "x$enable_polkit" = "xyes" && test "x$have_polkit" = "xno"; then
    AC_MSG_ERROR([PolicyKit support explicitly requested, but some required packages are not available])
  fi

  if test "x$have_polkit" = "xyes"; then
    AC_SUBST(ACL_LIBS)
    AC_DEFINE([USE_POLKIT], [1], [Define if supporting polkit])
  fi
  AM_CONDITIONAL([WITH_POLKIT], [test "x$have_polkit" = "xyes"])
  POLICYDIR=`${PKG_CONFIG} polkit-gobject-1 --variable=policydir`
  AC_SUBST(POLICYDIR)
  # Check for polkit_authority_get_sync()
  AC_CHECK_LIB([polkit-gobject-1], [polkit_authority_get_sync], ac_have_pk_auth_get_sync="1", ac_have_pk_auth_get_sync="0")
  AC_DEFINE_UNQUOTED([HAVE_POLKIT_AUTHORITY_GET_SYNC], $ac_have_pk_auth_get_sync, [Define if you have a polkit with polkit_authority_get_sync()])
  AC_CHECK_LIB([polkit-gobject-1], [polkit_authorization_result_get_dismissed], ac_have_pk_authorization_result_get_dismissed="1", ac_have_pk_authorization_result_get_dismissed="0")
  AC_DEFINE_UNQUOTED([HAVE_POLKIT_AUTHORIZATION_RESULT_GET_DISMISSED], $ac_have_pk_authorization_result_get_dismissed, [Define if you have a polkit with polkit_authorization_result_get_dismissed()])
else
  AM_CONDITIONAL(WITH_POLKIT, false)
fi

if test "x$have_usbredir" = "xyes" && test "x$have_polkit" != "xyes"; then
  AC_MSG_WARN([Building with usbredir support, but *not* building the usb acl helper])
fi

AC_ARG_ENABLE([pie],
  AS_HELP_STRING([--enable-pie=@<:@auto/yes/no@:>@],
                 [Enable position-independent-executable support (for the usb acl helper)@<:@default=auto@:>@]),
  [],
  [enable_pie="auto"])

if test "x$have_polkit" = "xyes" && test "x$enable_pie" != "xno"; then
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  CFLAGS="$CFLAGS -fPIE"
  LDFLAGS="$LDFLAGS -pie -Wl,-z,relro -Wl,-z,now"
  AC_MSG_CHECKING([for PIE support])
  AC_LINK_IFELSE([AC_LANG_SOURCE([void main () {}])],
                 [have_pie=yes],
                 [have_pie=no])
  AC_MSG_RESULT([$have_pie])
  if test "x$have_pie" = "xno" && test "x$enable_pie" = "xyes"; then
    AC_MSG_ERROR([pie support explicitly requested, but your toolchain does not support it])
  fi
  if test "x$have_pie" = "xyes"; then
    PIE_CFLAGS="-fPIE"
    PIE_LDFLAGS="-pie -Wl,-z,relro -Wl,-z,now"
    AC_SUBST(PIE_CFLAGS)
    AC_SUBST(PIE_LDFLAGS)
  fi
  CFLAGS="$save_CFLAGS"
  LDFLAGS="$save_LDFLAGS"
fi

AC_ARG_WITH([usb-acl-helper-dir],
  AS_HELP_STRING([--with-usb-acl-helper-dir=DIR],
                 [Directory where the USB ACL helper binary should be installed]),
  [ACL_HELPER_DIR="$with_usb_acl_helper_dir"],
  [ACL_HELPER_DIR="${bindir}/"])
AC_SUBST([ACL_HELPER_DIR])

AC_ARG_WITH([usb-ids-path],
  AC_HELP_STRING([--with-usb-ids-path],
                 [Specify the path to usb.ids @<:@default=auto@:>@]),
  [USB_IDS="$with_usb_ids_path"],
  [USB_IDS="auto"])
AC_MSG_CHECKING([for usb.ids])
if test "x$USB_IDS" = "xauto"; then
  if test -n "$PKG_CONFIG"; then
    USB_IDS=$($PKG_CONFIG --variable=usbids usbutils)
  else
    USB_IDS=
  fi
fi
if test -n "$USB_IDS"; then
  AC_MSG_RESULT([$USB_IDS])
  AC_SUBST(USB_IDS)
  AC_DEFINE([WITH_USBIDS], [1], [Define if compiling with usb.ids support])
else
  AC_MSG_RESULT([not found])
fi

AC_ARG_WITH([coroutine],
  AS_HELP_STRING([--with-coroutine=@<:@ucontext/gthread/winfiber/auto@:>@],
                 [use ucontext or GThread for coroutines @<:@default=auto@:>@]),
  [],
  [with_coroutine=auto])

case $with_coroutine in
  ucontext|gthread|winfiber|auto) ;;
  *) AC_MSG_ERROR(Unsupported coroutine type)
esac

if test "$with_coroutine" = "auto"; then
  if test "$os_win32" = "yes"; then
    with_coroutine=winfiber
  else
    with_coroutine=ucontext
  fi
fi

if test "$with_coroutine" = "ucontext"; then
  AC_CHECK_FUNC(makecontext, [],[with_coroutine=gthread])
  AC_CHECK_FUNC(swapcontext, [],[with_coroutine=gthread])
  AC_CHECK_FUNC(getcontext, [],[with_coroutine=gthread])
fi

WITH_UCONTEXT=0
WITH_GTHREAD=0
WITH_WINFIBER=0

case $with_coroutine in
  ucontext) WITH_UCONTEXT=1 ;;
  gthread) WITH_GTHREAD=1 ;;
  winfiber) WITH_WINFIBER=1 ;;
  *) AC_MSG_ERROR(Unsupported coroutine type)
esac

AC_DEFINE_UNQUOTED([WITH_UCONTEXT],[$WITH_UCONTEXT], [Whether to use ucontext coroutine impl])
AM_CONDITIONAL(WITH_UCONTEXT, [test "x$WITH_UCONTEXT" = "x1"])

AC_DEFINE_UNQUOTED([WITH_WINFIBER],[$WITH_WINFIBER], [Whether to use fiber coroutine impl])
AM_CONDITIONAL(WITH_WINFIBER, [test "x$WITH_WINFIBER" = "x1"])

AC_DEFINE_UNQUOTED([WITH_GTHREAD],[$WITH_GTHREAD], [Whether to use gthread coroutine impl])
AM_CONDITIONAL(WITH_GTHREAD, [test "x$WITH_GTHREAD" = "x1"])

AM_CONDITIONAL([HAVE_INTROSPECTION], [test "0" = "1"])
m4_ifdef([GOBJECT_INTROSPECTION_CHECK],[
    PKG_CHECK_EXISTS([GOBJECT_INTROSPECTION],
            [gobject-introspection-1.0 >= 0.9.4],
            [has_symbol_prefix=yes], [:])
    GOBJECT_INTROSPECTION_CHECK([0.6.7])
])
AM_CONDITIONAL([G_IR_SCANNER_SYMBOL_PREFIX], [test "x$has_symbol_prefix" = "xyes"])

AC_ARG_ENABLE([controller],
  AS_HELP_STRING([--enable-controller], [Enable controller build @<:@default=yes@:>@]),
  [],
  enable_controller="yes")

AM_CONDITIONAL(WITH_CONTROLLER, [test "x$enable_controller" != "xno"])

AC_ARG_ENABLE([vala],
  AS_HELP_STRING([--enable-vala], [Check for vala requirements @<:@default=no@:>@]),
  [],
  enable_vala="no")

VALA_REQUIRED=0.14
if test x$enable_vala = xyes ; then
    # check for vala
    AM_PROG_VALAC([$VALA_REQUIRED])
    AC_PATH_PROG(VAPIGEN, vapigen, no)
    if test "x$VAPIGEN" == "xno"; then
        AC_MSG_ERROR([Cannot find the "vapigen" binary in your PATH])
    fi
    AC_SUBST(VAPIGEN)
fi

AM_CONDITIONAL(WITH_VALA, [test "x$enable_vala" = "xyes"])

VAPIDIR="${datadir}/vala/vapi"
AC_SUBST(VAPIDIR)

AC_ARG_ENABLE([dbus],
  AS_HELP_STRING([--enable-dbus=@<:@auto/yes/no@:>@],
                 [Enable dbus support for desktop integration (disabling automount) @<:@default=auto@:>@]),
  [],
  [enable_dbus="auto"])

have_dbus=no
if test "x$enable_dbus" != "xno"; then
  AC_DEFINE([USE_GDBUS], [1], [Define if supporting gdbus])
  have_dbus=yes
else
  SPICE_WARNING([No D-Bus support, desktop integration and USB redirection may not work properly])
fi

SPICE_CHECK_LZ4

dnl ===========================================================================
dnl check compiler flags

# some glib/gstreamer enums use 1 << 31
dontwarn="-Wshift-overflow=2"

SPICE_COMPILE_WARNINGS([$dontwarn])

SPICE_CFLAGS="$SPICE_CFLAGS $WARN_CFLAGS"

AC_SUBST(SPICE_CFLAGS)

SPICE_GLIB_CFLAGS="$PIXMAN_CFLAGS $PULSE_CFLAGS $GSTAUDIO_CFLAGS $GSTVIDEO_CFLAGS $GLIB2_CFLAGS $GIO_CFLAGS $GOBJECT2_CFLAGS $SSL_CFLAGS $SASL_CFLAGS"
SPICE_GTK_CFLAGS="$SPICE_GLIB_CFLAGS $GTK_CFLAGS "

AC_SUBST(SPICE_GLIB_CFLAGS)
AC_SUBST(SPICE_GTK_CFLAGS)

AC_SUBST(SPICE_GLIB_REQUIRES)
AC_SUBST(SPICE_GTK_REQUIRES)

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AM_CONDITIONAL([BUILD_TESTS], [test x"$enable_static" = xyes])

AC_OUTPUT([
Makefile
spice-client-glib-2.0.pc
spice-client-gtk-3.0.pc
spice-controller.pc
data/Makefile
po/Makefile.in
src/Makefile
src/spice-version.h
src/controller/Makefile
tools/Makefile
doc/Makefile
doc/reference/Makefile
man/Makefile
vapi/Makefile
tests/Makefile
])

dnl ==========================================================================
AC_MSG_NOTICE([

        Spice-Gtk $VERSION
        ==============

        prefix:                   ${prefix}
        c compiler:               ${CC}
        Target:                   ${red_target}

        Gtk:                      ${with_gtk}
        Coroutine:                ${with_coroutine}
        PulseAudio:               ${enable_pulse}
        GStreamer Audio:          ${have_gstaudio}
        GStreamer Video:          ${have_gstvideo}
        SASL support:             ${have_sasl}
        Smartcard support:        ${have_smartcard}
        USB redirection support:  ${have_usbredir} ${with_usbredir_hotplug}
        DBus:                     ${have_dbus}
        WebDAV support:           ${have_phodav}
        LZ4 support:              ${have_lz4}

        Now type 'make' to build $PACKAGE

])
SPICE_PRINT_MESSAGES
