AC_PREREQ(2.59)

AC_INIT([3G Bridge], [0.9], [zfarkas@sztaki.hu])

AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([cf])

AM_INIT_AUTOMAKE([1.9 foreign no-dist-gzip dist-bzip2])

AC_PROG_CC
AC_PROG_CXX

dnl We need sed for substituting the XML templates
AC_PATH_PROG([SED], [sed])

dnl Enable useful warnings
if test "$ac_cv_c_compiler_gnu" = yes; then
	CFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -Wsign-compare ${CFLAGS}"
fi
if test "$ac_cv_cxx_compiler_gnu" = yes; then
	CXXFLAGS="-Wall -Wsign-compare ${CXXFLAGS}"
fi

dnl ===================================================================
dnl Check for 3rd party software
dnl ===================================================================

AC_PATH_PROG([MYSQL_CONFIG], [mysql_config], [none])
if test $MYSQL_CONFIG = none; then
	AC_MSG_ERROR([MySQL development environment is not installed])
fi

MYSQL_CPPFLAGS=`$MYSQL_CONFIG --include`
AC_SUBST([MYSQL_CPPFLAGS])
MYSQL_LIBS=`$MYSQL_CONFIG --libs_r`
AC_SUBST([MYSQL_LIBS])

AM_PATH_GLIB_2_0([2.10.0],,, [gthread])
if test "$no_glib" = yes; then
	AC_MSG_ERROR([glib libraries were not found])
fi

AC_ARG_WITH([dcapi], AS_HELP_STRING([--with-dcapi], [Enable DC-API backend @<:@no@:>@]),
	[], [with_dcapi=no])
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

AC_ARG_VAR([PKG_CONFIG_PATH], [Path where pkg-config looks for .pc files])
AC_ARG_WITH([gsoap], AS_HELP_STRING([--with-gsoap], [Use gSOAP in @<:@/usr@:>@]),
	[], [with_gsoap=/usr])
if test "$with_gsoap" != no; then
	PKG_CHECK_MODULES([GSOAP], [gsoap++], [], [with_gsoap=no])
fi
if test "$with_gsoap" != no; then
	LIBCURL_CHECK_CONFIG([yes], [7.15.0], [],
		[AC_MSG_ERROR([CURL development files are missing])])

	if test "$with_gsoap" = "/usr"; then
		GSOAP_IMPORT="/usr/include/gsoap"
	else
		GSOAP_IMPORT="$with_gsoap/share/gsoap/import"
	fi
	AC_PATH_PROG([SOAPCPP2], [soapcpp2], [], "$with_gsoap/bin")
	AC_PATH_PROG([WSDL2H], [wsdl2h], [], "$with_gsoap/bin")
	GSOAP_CFLAGS="-I$with_gsoap/include"
	AC_SUBST([GSOAP_IMPORT])
	AC_SUBST([GSOAP_CFLAGS])
fi
AM_CONDITIONAL([GSOAP], [test "$with_gsoap" != no])

AC_ARG_WITH([boinc], AS_HELP_STRING([--with-boinc], [Use BOINC in @<:@/usr@:>@]),
	[], [with_boinc=/usr])
if test "${with_boinc}" != no; then
	BOINC_CPPFLAGS="-I${with_boinc}/include/BOINC"
    	if test "${with_boinc}" != /usr; then
		BOINC_LDFLAGS="-L${with_boinc}/lib"
	fi
	AC_SUBST(BOINC_CPPFLAGS)
	AC_SUBST(BOINC_LDFLAGS)
fi

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
