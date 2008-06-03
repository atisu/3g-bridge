AC_PREREQ(2.59)

AC_INIT([Queue Manager], [0.5], [zfarkas@sztaki.hu])

AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([cf])

AM_INIT_AUTOMAKE([1.9 foreign])

AC_PROG_CC
AC_PROG_CXX

dnl Enable useful warnings
if test "$ac_cv_c_compiler_gnu" = yes; then
	CFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -Wsign-compare ${CFLAGS}"
fi
if test "$ac_cv_cxx_compiler_gnu" = yes; then
	CXXFLAGS="-Wall -Wsign-compare ${CXXFLAGS}"
fi


AC_PATH_PROG([MYSQL_CONFIG], [mysql_config], [none])
if test $MYSQL_CONFIG = none; then
	AC_MSG_ERROR([MySQL development environment is not installed])
fi

dnl We need sed for substituting the XML templates
AC_PATH_PROG([SED], [sed])

MYSQL_CPPFLAGS=`$MYSQL_CONFIG --include`
AC_SUBST([MYSQL_CPPFLAGS])
MYSQL_LIBS=`$MYSQL_CONFIG --libs`
AC_SUBST([MYSQL_LIBS])

AC_ARG_WITH([dcapi], AS_HELP_STRING([--with-dcapi], [Enable DC-API backend @<:@yes@:>@]),
	[], [with_dcapi=yes])
if test "$with_dcapi" != no; then
	SZDG_DCAPI_MASTER
fi
if test "$with_dcapi" != no; then
	AC_DEFINE([HAVE_DCAPI], [1], [Define if DC-API is requested.])
fi
AM_CONDITIONAL([HAVE_DCAPI], [test "$with_dcapi" != no])

dnl Check for EGEE backend
AC_ARG_WITH([egee], AS_HELP_STRING([--with-egee], [Enable EGEE backend @<:@no@:>@]),
	[], [with_egee=no])
if test "$with_egee" != no; then
	EGEE_BE
fi
if test "$with_egee" != no; then
	AC_DEFINE([HAVE_EGEE], [1], [Define if EGEE support is requested.])
fi
AM_CONDITIONAL([HAVE_EGEE], [test "$with_egee" != no])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
