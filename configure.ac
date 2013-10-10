AC_INIT(irqbalance,1.0.7)
AC_PREREQ(2.12)dnl
AM_CONFIG_HEADER(config.h)

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
AM_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_AWK

AC_ARG_ENABLE([numa],
  AS_HELP_STRING([--disable-numa], [enable numa support (default is auto)]))
AS_IF([test "$enable_numa" = "no"],[
  ac_cv_header_numa_h=no
  ac_cv_lib_numa_numa_available=no
])

AC_HEADER_STDC
AC_CHECK_HEADERS([numa.h])

AC_CHECK_FUNCS(getopt_long)

AC_CHECK_LIB(numa, numa_available)
AC_CHECK_LIB(m, floor)

AC_C_CONST
AC_C_INLINE
AM_PROG_CC_C_O

AC_ARG_WITH([glib2],
  [AS_HELP_STRING([--without-glib2],
    [Don't use system glib2 library. Use local implementation instead.])],
  [],
  [with_glib2=check])

local_glib2=
AS_IF(
  [test "x$with_glib2" = xyes],
  [PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.28])],
  
  [test "x$with_glib2" = xno],
  [local_glib2="yes"],

  [PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.28], [], [local_glib2="yes"])]
)

AS_IF(
  [test "x$local_glib2" = xyes],
  [
  GLIB_CFLAGS=-I./glib-local
  GLIB_LIBS=glib-local/libglib.a
  AC_SUBST(GLIB_CFLAGS)
  AC_SUBST(GLIB_LIBS)
  AC_MSG_WARN(Using locale implementation of GList functions)
  ]
)

AM_CONDITIONAL([LOCAL_GLIB], [test "x$local_glib2" = "xyes"])

AC_ARG_WITH([libcap-ng],
  AS_HELP_STRING([libcap-ng], [Add libcap-ng-support @<:@default=auto@:>@]))

AS_IF(
  [test "x$libcap_ng" != "xno"],
  [
  PKG_CHECK_MODULES([LIBCAP_NG], [libcap-ng],
    [AC_DEFINE(HAVE_LIBCAP_NG,1,[libcap-ng support])],
    [
     AS_IF(
       [test "x$libcap_ng" = "xyes"],
       [
       AC_MSG_ERROR([libcap-ng not found])
       ]
       )
    ]
  )
  ]
)

AC_OUTPUT(Makefile glib-local/Makefile)

AC_MSG_NOTICE()
AC_MSG_NOTICE([irqbalance Version:     $VERSION])
AC_MSG_NOTICE([Target:                 $target])
AC_MSG_NOTICE([Installation prefix:    $prefix])
AC_MSG_NOTICE([Compiler:               $CC])
AC_MSG_NOTICE([Compiler flags: $CFLAGS])
