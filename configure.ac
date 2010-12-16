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

# Check for the CPU we are using
#
AC_MSG_CHECKING(for x86 or x86-64 platform)
case $host_cpu in
  i386|i486|i586|i686|i786|k6|k7|arm|armv7)
	variant=32
        ;;
  x86_64)
	variant=64
	;;
  *)
	AC_MSG_RESULT(no)
	echo Only x86 and x86-64 are supported
	exit 1
esac
AC_MSG_RESULT($variant bit)
AM_CONDITIONAL([X86_64], [test "$variant" = 64])

AC_MSG_CHECKING([for native Win32])
case "$host" in
  *-*-mingw*)
    os_win32=yes
    ;;
  *)
    os_win32=no
    ;;
esac
AC_MSG_RESULT([$os_win32])

case $host in
  *-*-linux*)
    os_linux=yes
    ;;
esac

dnl =========================================================================
dnl Check OS target

AC_MSG_CHECKING([for some Win32 platform])
case "$host" in
  *-*-mingw*|*-*-cygwin*)
    platform_win32=yes
    ;;
  *)
    platform_win32=no
    ;;
esac
AC_MSG_RESULT([$platform_win32])
if test "$platform_win32" = yes; then
  red_target=windows
else
  red_target=x11
fi
AC_SUBST(red_target)

AM_CONDITIONAL(OS_WIN32, test "$os_win32" = "yes")
AM_CONDITIONAL(OS_UNIX, test "$os_win32" != "yes")
AM_CONDITIONAL(OS_LINUX, test "$os_linux" = "yes")

AC_CHECK_HEADERS([sys/ipc.h sys/shm.h])

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

PKG_CHECK_MODULES(GTK2, gtk+-2.0)
AC_SUBST(GTK2_CFLAGS)
AC_SUBST(GTK2_LIBS)

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

AS_IF([test "x$with_x11" = "xyes"],
      [AC_DEFINE([WITH_X11], 1, [Use X11 backend?])])
AM_CONDITIONAL([WITH_X11], [test "x$with_x11" = "xyes"])

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

WITH_PYTHON=$withval
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

SPICE_GLIB_CFLAGS="$PROTOCOL_CFLAGS $PIXMAN_CFLAGS $CELT051_CFLAGS $PULSE_CFLAGS $GST_CFLAGS $GLIB2_CFLAGS $GIO_CFLAGS $GOBJECT2_CFLAGS $SSL_CFLAGS"
SPICE_GTK_CFLAGS="$SPICE_GLIB_CFLAGS $GTK2_CFLAGS "

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
spice-client-glib.pc
spice-client-gtk.pc
common/Makefile
common/win/Makefile
common/win/my_getopt-1.5/Makefile
po/Makefile.in
python_modules/Makefile
gtk/Makefile
doc/Makefile
doc/reference/Makefile
])

dnl ==========================================================================
echo "

        Spice-Gtk $VERSION
        ==============

        prefix:                   ${prefix}
        c compiler:               ${CC}
        c++ compiler:	          ${CXX}

        Coroutine:                ${with_coroutine}
        Audio:                    ${with_audio}
        Target:                   ${red_target}

        Now type 'make' to build $PACKAGE
"
