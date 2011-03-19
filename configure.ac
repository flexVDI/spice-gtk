AC_PREREQ([2.57])

AC_INIT([spice-gtk], [m4_esyscmd(build-aux/git-version-gen .tarball-version)],
        [spice-devel@lists.freedesktop.org])

AC_CONFIG_MACRO_DIR([m4])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign dist-bzip2 -Wall -Werror])
LT_INIT
AM_MAINTAINER_MODE

IT_PROG_INTLTOOL([0.40.0])
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

AC_SUBST([GETTEXT_PACKAGE], [spice-gtk])
AC_DEFINE([GETTEXT_PACKAGE], [PACKAGE_TARNAME], [Define to the gettext package name.])

SPICE_GTK_LOCALEDIR=[${datadir}/locale]
AC_SUBST(SPICE_GTK_LOCALEDIR)

GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

# Define default SPICE_COMMON_SRCDIR
SPICE_COMMON_SRCDIR='$(top_srcdir)'/common

AS_IF([test "$CFLAGS" = ""], [], [_cflags_is_set=yes])
AC_PROG_CC
AC_PROG_CC_C99
if test x"$ac_cv_prog_cc_c99" = xno; then
    AC_MSG_ERROR([C99 compiler is required.])
fi

AS_IF([test "$CXXFLAGS" = ""], [], [_cxxflags_is_set=yes])
AC_PROG_CXX
AC_PROG_INSTALL
AC_CANONICAL_HOST
AC_PROG_LIBTOOL
AM_PROG_CC_C_O
AC_C_BIGENDIAN
AC_PATH_PROGS(PYTHON, python2 python)
RRA_LD_VERSION_SCRIPT

AC_CHECK_HEADERS([sys/ipc.h sys/shm.h])
AC_CHECK_HEADERS([sys/socket.h netinet/in.h arpa/inet.h])

dnl =========================================================================
dnl Chek optional features
PKG_CHECK_MODULES(PROTOCOL, spice-protocol >= 0.6.3)
AC_SUBST(PROTOCOL_CFLAGS)

SPICE_REQUIRES=""

PKG_CHECK_MODULES(PIXMAN, pixman-1 >= 0.17.7)
AC_SUBST(PIXMAN_CFLAGS)
AC_SUBST(PIXMAN_LIBS)
SPICE_REQUIRES+=" pixman-1 >= 0.17.7"

PKG_CHECK_MODULES(CELT051, celt051 >= 0.5.1.1)
AC_SUBST(CELT051_CFLAGS)
AC_SUBST(CELT051_LIBS)
AC_SUBST(CELT051_LIBDIR)
SPICE_REQUIRES+=" celt051 >= 0.5.1.1"

PKG_CHECK_MODULES(SSL, openssl)
AC_SUBST(SSL_CFLAGS)
AC_SUBST(SSL_LIBS)
SPICE_REQUIRES+=" openssl"

dnl Cyrus SASL
AC_ARG_WITH([sasl],
  [AS_HELP_STRING([--with-sasl],
    [use cyrus SASL for authentication @<:@default=check@:>@])],
  [],
  [with_sasl=check])

SASL_CFLAGS=
SASL_LIBS=
enable_sasl=no
if test "x$with_sasl" != "xno"; then
  if test "x$with_sasl" != "xyes" -a "x$with_sasl" != "xcheck"; then
    SASL_CFLAGS="-I$with_sasl"
    SASL_LIBS="-L$with_sasl"
  fi
  fail=0
  old_cflags="$CFLAGS"
  old_libs="$LIBS"
  CFLAGS="$CFLAGS $SASL_CFLAGS"
  LIBS="$LIBS $SASL_LIBS"
  AC_CHECK_HEADER([sasl/sasl.h],[],[
    if test "x$with_sasl" != "xcheck" ; then
        with_sasl=no
    else
        fail=1
    fi])
  if test "x$with_sasl" != "xno" ; then
    AC_CHECK_LIB([sasl2], [sasl_client_init],[with_sasl2=yes],[with_sasl2=no])
  fi
  if test "x$with_sasl2" = "xno" -a "x$with_sasl" != "xno" ; then
    AC_CHECK_LIB([sasl], [sasl_client_init],[with_sasl=yes],[with_sasl=no])
  fi
  if test "x$with_sasl2" = "xyes"; then
    SASL_LIBS="$SASL_LIBS -lsasl2"
  elif test "x$with_sasl" = "xyes"; then
    SASL_LIBS="$SASL_LIBS -lsasl"
  else
    AC_MSG_ERROR([You must install the Cyrus SASL development package in order to compile spice-gtk])
 fi
  CFLAGS="$old_cflags"
  LIBS="$old_libs"
  if test "x$with_sasl2" = "xyes" -o "x$with_sasl" = "xyes" ; then
    AC_DEFINE_UNQUOTED([HAVE_SASL], 1,
      [whether Cyrus SASL is available for authentication])
    enable_sasl=yes
  fi
fi
AM_CONDITIONAL([HAVE_SASL], [test "x$with_sasl2" = "xyes" -o "x$with_sasl" = "xyes"])
AC_SUBST([SASL_CFLAGS])
AC_SUBST([SASL_LIBS])


AC_MSG_CHECKING([which gtk+ version to compile against])
AC_ARG_WITH([gtk],
  [AS_HELP_STRING([--with-gtk=2.0|3.0],[which gtk+ version to compile against (default: 2.0)])],
  [case "$with_gtk" in
     2.0|3.0) ;;
     *) AC_MSG_ERROR([invalid gtk version specified]) ;;
   esac],
  [with_gtk=2.0])
AC_MSG_RESULT([$with_gtk])

case "$with_gtk" in
  2.0) GTK_API_VERSION=2.0
       GTK_REQUIRED=2.18.0
       SPICE_GTK_API_VERSION=2.0
       ;;
  3.0) GTK_API_VERSION=3.0
       GTK_REQUIRED=2.91.3
       SPICE_GTK_API_VERSION=3.0
       ;;
esac

AC_SUBST([GTK_API_VERSION])
AC_SUBST([GTK_REQUIRED])
AC_SUBST([SPICE_GTK_API_VERSION])
AM_CONDITIONAL([HAVE_GTK_2],[test "$with_gtk" = "2.0"])
AM_CONDITIONAL([HAVE_GTK_3],[test "$with_gtk" = "3.0"])

PKG_CHECK_MODULES(GTK, gtk+-$GTK_API_VERSION >= $GTK_REQUIRED)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(X11, x11, [have_x11=yes], [have_x11=no])
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)
AS_IF([test "x$have_x11" = "xyes"], [AC_DEFINE([HAVE_X11], 1, [Have x11?])])

PKG_CHECK_MODULES(XRANDR, x11 xrandr, [have_xrandr=yes], [have_xrandr=no])
AC_SUBST(XRANDR_CFLAGS)
AC_SUBST(XRANDR_LIBS)
AS_IF([test "x$have_xrandr" = "xyes"], [AC_DEFINE([HAVE_RANDR], 1, [Have xrandr?])])

AC_ARG_WITH(pnp-ids-path,
              [AC_HELP_STRING([--with-pnp-ids-path],
                              [Specify the path to pnp.ids @<:@default=(internal)@:>@])],,
                              [with_pnp_ids_path="\${pnpdatadir}/pnp.ids"])

AM_CONDITIONAL(USE_INTERNAL_PNP_IDS, test "x$with_pnp_ids_path" = "x\${pnpdatadir}/pnp.ids")
PNP_IDS=$with_pnp_ids_path
AC_SUBST(PNP_IDS)
if test "x$with_pnp_ids_path" = "x\${pnpdatadir}/pnp.ids"; then
        EXTERNAL_PNP_IDS="no (internal)"
else
        EXTERNAL_PNP_IDS="$with_pnp_ids_path"
fi

PKG_CHECK_MODULES(GLIB2, glib-2.0 >= 2.22)
AC_SUBST(GLIB2_CFLAGS)
AC_SUBST(GLIB2_LIBS)

PKG_CHECK_MODULES(GOBJECT2, gobject-2.0)
AC_SUBST(GOBJECT2_CFLAGS)
AC_SUBST(GOBJECT2_LIBS)

PKG_CHECK_MODULES(GIO, gio-2.0 >= 2.10.0)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(CAIRO, cairo >= 1.2.0)
AC_SUBST(CAIRO_CFLAGS)
AC_SUBST(CAIRO_LIBS)

PKG_CHECK_MODULES(GTHREAD, gthread-2.0 > 2.0.0)
AC_SUBST(GTHREAD_CFLAGS)
AC_SUBST(GTHREAD_LIBS)

AC_ARG_WITH([audio],
    AS_HELP_STRING([--with-audio], [Select audio backend @<:@default=pulse@:>@]), [], [with_audio=pulse])

case $with_audio in
  gstreamer|pulse|no*)
    ;;
  *)
    AC_MSG_ERROR(Unsupported audio backend)
esac

AS_IF([test "x$with_audio" = "xpulse"],
      [PKG_CHECK_MODULES(PULSE, libpulse libpulse-mainloop-glib, [have_pulse=yes], [have_pulse=no])],
      [have_pulse=no])

AS_IF([test "x$have_pulse" = "xyes"],
      [AC_DEFINE([WITH_PULSE], 1, [Have pulseaudio?])],
      [AS_IF([test "x$with_audio" = "xpulse"],
             [AC_MSG_ERROR([PulseAudio requested but not found])
      ])
])
AM_CONDITIONAL([WITH_PULSE], [test "x$have_pulse" = "xyes"])
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)

AS_IF([test "x$with_audio" = "xgstreamer"],
      [PKG_CHECK_MODULES(GST, gstreamer-0.10 gstreamer-base-0.10, [have_gst=yes GST_LIBS="$GST_LIBS -lgstapp-0.10"], [have_gst=no])],
      [have_gst=no])

AS_IF([test "x$have_gst" = "xyes"],
      [AC_DEFINE([WITH_GSTAUDIO], 1, [Have GStreamer?])],
      [AS_IF([test "x$with_audio" = "xgstreamer"],
             [AC_MSG_ERROR([GStreamer requested but not found])
      ])
])
AM_CONDITIONAL([WITH_GSTAUDIO], [test "x$have_gst" = "xyes"])
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

AC_ARG_WITH([x11],
    AS_HELP_STRING([--with-x11], [Use x11/shm display backend instead of cairo.]))

AC_MSG_CHECKING([What rendering backend to use..])
if [test "x$with_x11" = "xyes" ]
then
  if [test "$GTK_API_VERSION" = "3.0" ]
  then
    AC_MSG_ERROR([X11 SHM backend is not compatible with GTK3])
  fi
  AC_MSG_RESULT([x11])
else
  AC_MSG_RESULT([cairo])
fi

AS_IF([test "x$with_x11" = "xyes"],
      [AC_DEFINE([WITH_X11], 1, [Use X11 backend?])])
AM_CONDITIONAL([WITH_X11], [test "x$with_x11" = "xyes"])

AC_CHECK_HEADERS([X11/XKBlib.h])

# Add parameter for (partial) static linkage of spice client.
# this is used to achive single binary package for all (?) distros.
AC_ARG_ENABLE(static-linkage, 
	AS_HELP_STRING([--enable-static-linkage], [will generate spice client binary with static linkage to external libraries]),
	[SPICEC_STATIC_LINKAGE_BSTATIC=["-Wl,-Bstatic"];
	SPICEC_STATIC_LINKAGE_BDYNAMIC=["-Wl,-Bdynamic"]])


AS_IF([test "$_cflags_is_set" = "yes"], [], [
    CFLAGS="-g -O2"
])


AS_IF([test "$_cxxflags_is_set" = "yes"], [], [
    CXXFLAGS="-g -O2"
])

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

WITH_UCONTEXT=1

AC_ARG_WITH(coroutine,
 AS_HELP_STRING([--with-coroutine=ucontext/gthread], [use ucontext or GThread for coroutines]),
  [],[with_coroutine=ucontext])

case $with_coroutine in
  ucontext)
    ;;
  gthread)
    ;;
  *)
    AC_MSG_ERROR(Unsupported coroutine type)
esac

if test "$with_coroutine" = "ucontext"; then
  AC_CHECK_FUNC(makecontext, [],[with_coroutine=gthread])
  AC_CHECK_FUNC(swapcontext, [],[with_coroutine=gthread])
  AC_CHECK_FUNC(getcontext, [],[with_coroutine=gthread])
fi

if test "$with_coroutine" = "gthread"; then
  # gthread is required anyway
  WITH_UCONTEXT=0
fi

AC_DEFINE_UNQUOTED(WITH_UCONTEXT,[$WITH_UCONTEXT], [Whether to use ucontext coroutine impl])
AM_CONDITIONAL(WITH_UCONTEXT, [test "$WITH_UCONTEXT" != "0"])

GOBJECT_INTROSPECTION_CHECK([0.6.7])
PKG_CHECK_MODULES([GOBJECT_INTROSPECTION],
        [gobject-introspection-1.0 >= 0.9.4],
        [has_symbol_prefix=yes], [:])
AM_CONDITIONAL([G_IR_SCANNER_SYMBOL_PREFIX], [test "x$has_symbol_prefix" = "xyes"])

AC_ARG_WITH(python,
  AS_HELP_STRING([--with-python], [build python bindings]),
[case "${withval}" in
   yes|no) ;;
   *)      AC_MSG_ERROR([bad value ${withval} for python option]) ;;
 esac],[withval=yes])

if test "$with_gtk" = "3.0"
then
  WITH_PYTHON=no
else
  WITH_PYTHON=$withval
fi

if test "$WITH_PYTHON" = "yes"; then
  PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= 2.0.0)
  AC_SUBST(PYGTK_CFLAGS)
  AC_SUBST(PYGTK_LIBS)

  AM_PATH_PYTHON

  AC_MSG_CHECKING([whether $PYTHON version >= 2.0])
  HAVE_PYTHON_REQUIRED=no
  AM_PYTHON_CHECK_VERSION([$PYTHON], [2.0],
      [HAVE_PYTHON_REQUIRED="yes"],
      [HAVE_PYTHON_REQUIRED="no"])

  AC_MSG_RESULT($HAVE_PYTHON_REQUIRED)

  if test "x$HAVE_PYTHON_REQUIRED" != "xyes"
  then
    AC_MSG_ERROR("No suitable python found")
  fi

  AM_CHECK_PYTHON_HEADERS(have_python_headers=yes,have_python_headers=no)

  if test "x$have_python_headers" != "xyes"
  then
    AC_MSG_ERROR("No python development headers found")
  fi
fi

AM_CONDITIONAL(WITH_PYTHON, [test "$WITH_PYTHON" = "yes"])

dnl ===========================================================================
dnl check compiler flags

AC_DEFUN([SPICE_CC_TRY_FLAG], [
  AC_MSG_CHECKING([whether $CC supports $1])

  spice_save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $1"

  AC_COMPILE_IFELSE([ ], [spice_cc_flag=yes], [spice_cc_flag=no])
  CFLAGS="$spice_save_CFLAGS"

  if test "x$spice_cc_flag" = "xyes"; then
    ifelse([$2], , :, [$2])
  else
    ifelse([$3], , :, [$3])
  fi
  AC_MSG_RESULT([$spice_cc_flag])
])


dnl Use lots of warning flags with with gcc and compatible compilers

dnl Note: if you change the following variable, the cache is automatically
dnl skipped and all flags rechecked.  So there's no need to do anything
dnl else.  If for any reason you need to force a recheck, just change
dnl MAYBE_WARN in an ignorable way (like adding whitespace)

dnl MAYBE_WARN="-Wall -Wno-sign-compare -Werror -Wno-deprecated-declarations"

MAYBE_WARN="-Wall -Wno-sign-compare -Wno-deprecated-declarations"

AC_ARG_ENABLE(werror,
AS_HELP_STRING([--enable-werror], [Use -Werror (if supported)]),
set_werror="$enableval",[
if test -d $srcdir/.git; then
	is_git_version=true
	set_werror=yes
else
	set_werror=no
fi
])


# invalidate cached value if MAYBE_WARN has changed
if test "x$spice_cv_warn_maybe" != "x$MAYBE_WARN"; then
	unset spice_cv_warn_cflags
fi
AC_CACHE_CHECK([for supported warning flags], spice_cv_warn_cflags, [
	echo
	WARN_CFLAGS=""

	# Some warning options are not supported by all versions of
	# gcc, so test all desired options against the current
	# compiler.
	#
	# Note that there are some order dependencies
	# here. Specifically, an option that disables a warning will
	# have no net effect if a later option then enables that
	# warnings, (perhaps implicitly). So we put some grouped
	# options (-Wall and -Wextra) up front and the -Wno options
	# last.

	for W in $MAYBE_WARN; do
		SPICE_CC_TRY_FLAG([$W], [WARN_CFLAGS="$WARN_CFLAGS $W"])
	done
	if test "$set_werror" != "no"; then
		SPICE_CC_TRY_FLAG(["-Werror"], [WARN_CFLAGS="$WARN_CFLAGS -Werror"])
	fi

	spice_cv_warn_cflags=$WARN_CFLAGS
	spice_cv_warn_maybe=$MAYBE_WARN

	AC_MSG_CHECKING([which warning flags were supported])])
WARN_CFLAGS="$spice_cv_warn_cflags"
SPICE_CFLAGS="$SPICE_CFLAGS $WARN_CFLAGS"

# We only wish to enable attribute(warn_unused_result) if we can prevent
# gcc from generating thousands of warnings about the misapplication of the
# attribute to void functions and variables.
AC_MSG_CHECKING([how to enable unused result warnings])
warn_unused_result=""
if echo $WARN_CFLAGS | grep -e '-Wno-attributes' >/dev/null; then
    AC_TRY_COMPILE([__attribute__((__warn_unused_result__))
	int f (int i) { return i; }], [],
	[warn_unused_result="__attribute__((__warn_unused_result__))"])
fi
AC_DEFINE_UNQUOTED([WARN_UNUSED_RESULT], [$warn_unused_result],
	  [Define to the value your compiler uses to support the warn-unused-result attribute])
AC_MSG_RESULT([$warn_unused_result])

AC_SUBST(SPICE_CFLAGS)

SPICE_GLIB_CFLAGS="$PROTOCOL_CFLAGS $PIXMAN_CFLAGS $CELT051_CFLAGS $PULSE_CFLAGS $GST_CFLAGS $GLIB2_CFLAGS $GIO_CFLAGS $GOBJECT2_CFLAGS $SSL_CFLAGS @SASL_CFLAGS"
SPICE_GTK_CFLAGS="$SPICE_GLIB_CFLAGS $GTK_CFLAGS "

AC_SUBST(SPICE_GLIB_CFLAGS)
AC_SUBST(SPICE_GTK_CFLAGS)

dnl =========================================================================
dnl -fvisibility stuff

have_gcc4=no
AC_MSG_CHECKING(for -fvisibility)
AC_COMPILE_IFELSE([
#if defined(__GNUC__) && (__GNUC__ >= 4)
#else
error Need GCC 4.0 for visibility
#endif
int main () { return 0; }
], have_gcc4=yes)

if test "x$have_gcc4" = "xyes"; then
   VISIBILITY_HIDDEN_CFLAGS="-fvisibility=hidden"
fi
AC_MSG_RESULT($have_gcc4)
AC_SUBST(VISIBILITY_HIDDEN_CFLAGS)

AC_SUBST(SPICE_COMMON_SRCDIR)
AC_SUBST(SPICE_REQUIRES)

AC_SUBST([SPICEC_STATIC_LINKAGE_BSTATIC])
AC_SUBST([SPICEC_STATIC_LINKAGE_BDYNAMIC])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_OUTPUT([
Makefile
spice-client-glib-2.0.pc
spice-client-gtk-2.0.pc
spice-client-gtk-3.0.pc
common/Makefile
common/win/Makefile
common/win/my_getopt-1.5/Makefile
data/Makefile
data/spicy.desktop.in
po/Makefile.in
python_modules/Makefile
gtk/Makefile
doc/Makefile
doc/reference/Makefile
])

dnl ==========================================================================
AC_MSG_NOTICE([

        Spice-Gtk $VERSION
        ==============

        prefix:                   ${prefix}
        c compiler:               ${CC}
        c++ compiler:             ${CXX}

        Coroutine:                ${with_coroutine}
        Audio:                    ${with_audio}
        Target:                   ${red_target}
        SASL support:             ${enable_sasl}
        Gtk:                      $GTK_API_VERSION

        Now type 'make' to build $PACKAGE

])
