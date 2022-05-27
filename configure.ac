AC_INIT(irqbalance,1.9.0)
AC_PREREQ(2.12)dnl
AM_CONFIG_HEADER(config.h)

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign] [subdir-objects])
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

PKG_CHECK_MODULES([NUMA], [numa], [has_numa=yes], [AC_CHECK_LIB(numa, numa_available)])
AC_CHECK_LIB(m, floor)

PKG_CHECK_MODULES([GLIB2], [glib-2.0], [], [AC_MSG_ERROR([glib-2.0 is required])])

PKG_CHECK_MODULES([NCURSESW], [ncursesw], [has_ncursesw=yes], [AC_CHECK_LIB(curses, mvprintw)])
AS_IF([test "x$has_ncursesw" = "xyes"], [
  AC_SUBST([NCURSESW_CFLAGS])
  AC_SUBST([NCURSESW_LIBS])
  LIBS="$LIBS $NCURSESW_LIBS"
  AC_SUBST([LIBS])
])

AC_C_CONST
AC_C_INLINE
AM_PROG_CC_C_O

AC_ARG_WITH([irqbalance-ui],
  [AS_HELP_STRING([--without-irqbalance-ui],
    [Dont build the irqbalance ui component])],
    [with_irqbalanceui=$withval], [with_irqbalanceui=yes])
AS_IF(
  [test "x$with_irqbalanceui" = "xyes"], [
    AC_DEFINE([HAVE_IRQBALANCEUI], 1, [Build irqbalance ui component.])
])
AM_CONDITIONAL([IRQBALANCEUI], [test x$with_irqbalanceui = xyes])

AC_ARG_WITH([systemd],
  [ AS_HELP_STRING([--with-systemd],[Add systemd-lib support])]
)
AS_IF(
  [test "x$with_systemd" = xyes], [
    PKG_CHECK_MODULES([SYSTEMD], [libsystemd], [journal_lib=yes], [journal_lib=no])
    AS_IF([test "x$journal_lib" != "xyes"], [
      PKG_CHECK_MODULES([SYSTEMD], [libsystemd-journal], [journal_lib=yes])
    ])
    AC_DEFINE(HAVE_LIBSYSTEMD, 1, [systemd support])
    AC_CHECK_LIB([systemd], [sd_journal_print_with_location])
    AC_CHECK_LIB([systemd], [sd_journal_print])
])

AC_ARG_WITH([libcap-ng],
  AS_HELP_STRING([libcap-ng], [Add libcap-ng-support @<:@default=auto@:>@]))

AS_IF(
  [test "x$with_libcap_ng" != "xno"],
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

AC_OUTPUT(Makefile tests/Makefile)

AC_MSG_NOTICE()
AC_MSG_NOTICE([irqbalance Version:     $VERSION])
AC_MSG_NOTICE([Target:                 $target])
AC_MSG_NOTICE([Installation prefix:    $prefix])
AC_MSG_NOTICE([Compiler:               $CC])
AC_MSG_NOTICE([Compiler flags: $CFLAGS])
