AC_PREREQ(2.59)

AC_INIT([CancerGrid], [0.5], [zfarkas@sztaki.hu])

AM_INIT_AUTOMAKE([1.9 foreign])

AC_PROG_CXX

AC_PATH_PROG([MYSQL_CONFIG], [mysql_config], [none])
if test $MYSQL_CONFIG = none; then
	AC_MSG_ERROR([MySQL development environment is not installed])
fi

MYSQL_CPPFLAGS=`$MYSQL_CONFIG --include`
AC_SUBST(MYSQL_CPPFLAGS)

AC_LANG_PUSH([C++])

AC_CHECK_LIB([mysqlpp], [main], [],
	     [AC_MSG_ERROR([MySQL C++ bindings seem to be missing])])

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $MYSQL_CPPFLAGS"
AC_CHECK_HEADERS([mysql++/mysql++.h], [],
	     [AC_MSG_ERROR([MySQL C++ bindings seem to be missing])])
AC_CHECK_HEADERS([mysql++/custom.h mysql++/ssqls.h])
CPPFLAGS="$save_CPPFLAGS"

AC_CHECK_HEADERS([tclap/CmdLine.h], [],
	     [AC_MSG_ERROR([Templatized command-line argument parser headers are missing])])

dnl These are needed by tclap
AC_CHECK_HEADERS([sstream strstream])

AC_LANG_POP([C++])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
