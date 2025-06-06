AC_INIT(irqbalance,1.9.4)
AC_PREREQ(2.69)dnl
AC_CONFIG_HEADERS(config.h)

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign] [subdir-objects])
AM_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_AWK

AC_HEADER_STDC
AC_CHECK_HEADERS([numa.h])

AC_CHECK_FUNCS(getopt_long)

AC_ARG_ENABLE([staticbuild],
	AS_HELP_STRING([--enable-staticbuild], [enable static builds(default disabled)]))

AM_CONDITIONAL([STATICBUILD], [test "x$enable_staticbuild" = "xyes"])

# If we requested static building, enable that in pkg-config
AS_IF([test "x$enable_staticbuild" = "xyes"], [
	PKG_CONFIG="$PKG_CONFIG --static"],[])


AC_ARG_ENABLE([numa],
  AS_HELP_STRING([--disable-numa], [enable numa support (default is auto)]))
AS_IF([test "$enable_numa" = "no"],[
  ac_cv_header_numa_h=no
  ac_cv_lib_numa_numa_available=no
])
PKG_CHECK_MODULES([NUMA], [numa], [has_numa=yes], [AC_CHECK_LIB(numa, numa_available)])
AC_CHECK_LIB(m, floor)

PKG_CHECK_MODULES([GLIB2], [glib-2.0], [], [AC_MSG_ERROR([glib-2.0 is required])])

PKG_CHECK_MODULES([NCURSESW], [ncursesw], [has_ncursesw=yes], [AC_CHECK_LIB(curses, mvprintw)])

AC_CANONICAL_HOST

AC_ARG_ENABLE(thermal,
  AS_HELP_STRING([--enable-thermal], [enable thermal event support [default=auto]]),,
  AS_IF([test x"$host_cpu" = x"x86_64"], [enable_thermal=yes], [enable_thermal=no])
)

AS_IF([test x"$enable_thermal" = x"yes" && test x"$host_cpu" != x"x86_64"],
  AC_MSG_ERROR([no thermal events support on $host_cpu systems.]),
)

AS_IF([test x"$enable_thermal" = x"yes"],
  [PKG_CHECK_MODULES([LIBNL3], [libnl-3.0 libnl-genl-3.0], [have_thermal=yes],
    AC_MSG_NOTICE([no thermal event support as libnl-3.0 is unavailable.])
  )]
)

AS_IF([test "x$have_thermal" = xyes],
  AC_DEFINE([HAVE_THERMAL], 1, [Build irqbalance to support thermal events])
)
AM_CONDITIONAL([THERMAL], [test "x$have_thermal" = xyes])

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
    AS_IF([test "x$has_ncursesw" = "xyes"], [
        AC_SUBST([NCURSESW_CFLAGS])
        AC_SUBST([NCURSESW_LIBS])
        LIBS="$LIBS $NCURSESW_LIBS"
        AC_SUBST([LIBS])
   ])
])
AM_CONDITIONAL([IRQBALANCEUI], [test x$with_irqbalanceui = xyes])

AC_ARG_WITH([systemd],
  [AS_HELP_STRING([--with-systemd],[Add systemd-lib support])]
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

AC_ARG_WITH([pkgconfdir],
  [AS_HELP_STRING([--with-pkgconfdir=DIR],
    [Systemd Environment configs sourced by irqbalanced])],
    [pkgconfdir=$withval],
    [pkgconfdir="$prefix/etc/default"])
AC_SUBST([pkgconfdir])

AC_ARG_WITH([usrconfdir],
  [AS_HELP_STRING([--with-usrconfdir=DIR],
    [Systemd Environment user configs sourced by irqbalanced])],
    [usrconfdir=$withval],
    [usrconfdir="${sysconfdir}/default"])
AC_SUBST([usrconfdir])

AC_CONFIG_FILES([misc/irqbalance.service])

PKG_CHECK_VAR([systemdsystemunitdir], [systemd], [systemdsystemunitdir])

AC_OUTPUT(Makefile tests/Makefile)

AC_MSG_NOTICE()
AC_MSG_NOTICE([irqbalance Version:     $VERSION])
AC_MSG_NOTICE([Target:                 $target])
AC_MSG_NOTICE([Installation prefix:    $prefix])
AC_MSG_NOTICE([Compiler:               $CC])
AC_MSG_NOTICE([Compiler flags: $CFLAGS])
AC_MSG_NOTICE([Systemdunitdir:         $systemdsystemunitdir])
