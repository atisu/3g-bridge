AC_PREREQ(2.59)

AC_INIT([Queue Manager], [0.5], [zfarkas@sztaki.hu])

AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([cf])

AM_INIT_AUTOMAKE([1.9 foreign])

AC_PROG_CXX

AC_PATH_PROG([MYSQL_CONFIG], [mysql_config], [none])
if test $MYSQL_CONFIG = none; then
	AC_MSG_ERROR([MySQL development environment is not installed])
fi

MYSQL_CPPFLAGS=`$MYSQL_CONFIG --include`
AC_SUBST([MYSQL_CPPFLAGS])

AC_LANG_PUSH([C++])

AC_CHECK_LIB([mysqlpp], [main], [],
	     [AC_MSG_ERROR([MySQL C++ bindings seem to be missing])])

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $MYSQL_CPPFLAGS"
AC_CHECK_HEADERS([mysql++/mysql++.h], [],
	     [AC_MSG_ERROR([MySQL C++ bindings seem to be missing])])
AC_CHECK_HEADERS([mysql++/ssqls.h], [], [], AC_LANG_SOURCE([
#ifdef HAVE_MYSQL___MYSQL___H
#include <mysql++/mysql++.h>
#endif
]))
CPPFLAGS="$save_CPPFLAGS"

AC_LANG_POP([C++])

AC_ARG_WITH([dcapi], AS_HELP_STRING([--with-dcapi], [Enable DC-API backend @<:@yes@:>@]),
	[], [with_dcapi=yes])
if test "$with_dcapi != no"; then
	SZDG_DCAPI_MASTER
fi
AM_CONDITIONAL([HAVE_DCAPI], [test "$with_dcapi" != no])

dnl Check for EGEE backend
AC_ARG_WITH([egee], AS_HELP_STRING([--with-egee], [Enable EGEE backend @<:@no@:>@]),
	[], [with_egee=no])
if test "$with_egee != no"; then
	EGEE_BE
fi
AM_CONDITIONAL([HAVE_EGEE], [test "$with_egee" != no])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
