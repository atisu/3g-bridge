AC_PREREQ(2.59)

AC_INIT([3G Bridge], [1.2], [zfarkas@sztaki.hu])

AC_CONFIG_AUX_DIR([scripts])
AC_CONFIG_MACRO_DIR([cf])

AM_INIT_AUTOMAKE([1.9 foreign no-dist-gzip dist-bzip2])

AC_GNU_SOURCE

AC_PROG_LIBTOOL
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

dnl Make sure configure remembers the value of PKG_CONFIG_PATH
AC_ARG_VAR([PKG_CONFIG_PATH], [Path where pkg-config looks for .pc files])

dnl ===================================================================
dnl Check for 3rd party software
dnl ===================================================================

dnl -------------------------------------------------------------------
dnl MySQL
dnl -------------------------------------------------------------------

AC_PATH_PROG([MYSQL_CONFIG], [mysql_config], [none])
if test "$MYSQL_CONFIG" = none; then
	AC_MSG_ERROR([MySQL development environment is not installed])
fi

MYSQL_CPPFLAGS=`$MYSQL_CONFIG --include`
AC_SUBST([MYSQL_CPPFLAGS])
MYSQL_LIBS=`$MYSQL_CONFIG --libs_r`
AC_SUBST([MYSQL_LIBS])

dnl -------------------------------------------------------------------
dnl glib
dnl -------------------------------------------------------------------

AM_PATH_GLIB_2_0([2.10.0])
if test "$no_glib" = yes; then
	AC_MSG_ERROR([glib libraries were not found])
fi

GMODULE_LIBS=`pkg-config --libs gmodule-2.0`
GTHREAD_LIBS=`pkg-config --libs gthread-2.0`
AC_SUBST([GMODULE_LIBS])
AC_SUBST([GTHREAD_LIBS])

dnl -------------------------------------------------------------------
dnl DC-API
dnl -------------------------------------------------------------------

AC_ARG_WITH([dcapi], AS_HELP_STRING([--with-dcapi], [Enable DC-API backend @<:@no@:>@]),
	[], [with_dcapi=no])
if test "$with_dcapi" != no; then
	SZDG_DCAPI_MASTER([], [AC_MSG_ERROR([DC-API master libraries are missing])])
fi
if test "$with_dcapi" != no; then
	AC_DEFINE([HAVE_DCAPI], [1], [Define if DC-API is requested.])
fi
AM_CONDITIONAL([HAVE_DCAPI], [test "$with_dcapi" != no])

dnl -------------------------------------------------------------------
dnl EGEE/gLite
dnl -------------------------------------------------------------------

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

dnl -------------------------------------------------------------------
dnl EC2
dnl -------------------------------------------------------------------

AC_ARG_WITH([ec2], AS_HELP_STRING([--with-ec2], [Enable Amazon EC2/ Eucalyptus backend @<:@no@:>@]),
        [], [with_ec2=no])
if test "$with_ec2" != no; then
        AC_DEFINE([HAVE_EC2], [1], [Define if Amazon EC2/ Eucalyptus is requested.])
fi
AM_CONDITIONAL([HAVE_EC2], [test "$with_ec2" != no])

dnl -------------------------------------------------------------------
dnl Meta-job support
dnl -------------------------------------------------------------------

AC_ARG_WITH([metajob], AS_HELP_STRING([--with-metajob], [Enable meta-job support @<:@no@:>@]),
        [], [with_ec2=no])
if test "$with_metajob" != no; then
        AC_DEFINE([HAVE_METAJOB], [1], [Define if meta-job feautre is requested.])
fi
AM_CONDITIONAL([HAVE_METAJOB], [test "$with_metajob" != no])

dnl -------------------------------------------------------------------
dnl XtremWeb
dnl -------------------------------------------------------------------

dnl Check for XtremWeb backend
AC_ARG_WITH([xtremweb], AS_HELP_STRING([--with-xtremweb], [Enable XtremWeb backend @<:@no@:>@]),
	[], [with_xtremweb=no])
if test "$with_xtremweb" != no; then
	AC_DEFINE([HAVE_XTREMWEB], [1], [Define if XtremWeb support is requested.])
fi
AM_CONDITIONAL([HAVE_XTREMWEB], [test "$with_xtremweb" != no])

dnl -------------------------------------------------------------------
dnl Jobwrapper
dnl -------------------------------------------------------------------

AC_ARG_WITH([jobwrapper], AS_HELP_STRING([--with-jobwrapper], [Enable Jobwrapper application @<:@no@:>@]),
	[], [with_jobwrapper=no])
AM_CONDITIONAL([HAVE_JOBWRAPPER], [test "$with_jobwrapper" != no])

dnl -------------------------------------------------------------------
dnl gsoap & libcurl
dnl -------------------------------------------------------------------

AC_ARG_WITH([gsoap], AS_HELP_STRING([--with-gsoap], [Use gSOAP @<:@auto@:>@]),
	[], [with_gsoap=auto])
no_gsoap=
if test "$with_gsoap" = no; then
	no_gsoap=yes
else
	if test "$with_gsoap" = auto; then
		PKG_CHECK_MODULES([GSOAP], [gsoap++], [], [no_gsoap=yes])
		if test "$no_gsoap" != yes; then
			GSOAP_IMPORT="`pkg-config --variable includedir gsoap++`/gsoap"
		fi
	else
		GSOAP_CFLAGS="-I$with_gsoap/include"
		GSOAP_LIBS="-L$with_gsoap/lib -lgsoap++"
		GSOAP_IMPORT="$with_gsoap/share/gsoap/import"
		AC_SUBST([GSOAP_CFLAGS])
		AC_SUBST([GSOAP_LIBS])
	fi
fi
if test "$no_gsoap" != yes; then
	LIBCURL_CHECK_CONFIG([yes], [7.12.0], [],
		[AC_MSG_ERROR([CURL development files are missing])])

	AC_PATH_PROG([SOAPCPP2], [soapcpp2], [false], "$PATH:$with_gsoap/bin")
	AC_PATH_PROG([WSDL2H], [wsdl2h], [false], "$PATH:$with_gsoap/bin")
	if test "$SOAPCPP2" = false -o "$WSDL2H" = false; then
		no_gsoap=yes
	fi
	AC_SUBST([GSOAP_IMPORT])
fi
if test "$with_gsoap" = yes -a "$no_gsoap" = yes; then
	AC_MSG_ERROR([gsoap was not found])
fi
AM_CONDITIONAL([HAVE_GSOAP], [test "$no_gsoap" != yes])

dnl -------------------------------------------------------------------
dnl BES
dnl -------------------------------------------------------------------

AC_ARG_WITH([bes-libs], AS_HELP_STRING([--with-bes-libs=DIR], [Use BES libs in @<:@DIR@:>@]),
	[], [with_bes_libs=none])
no_bes=
if test "$with_bes_libs" = none; then
	no_bes=yes
else
	BES_LIBS="$with_bes_libs"
	AC_SUBST([BES_LIBS])
fi
AM_CONDITIONAL([HAVE_BES], [test "$no_bes" != yes])

dnl -------------------------------------------------------------------
dnl libuuid
dnl -------------------------------------------------------------------

AC_CHECK_LIB([uuid], [uuid_parse], [true], [AC_MSG_ERROR([libuuid was not found])])
AC_CHECK_HEADER([uuid/uuid.h],, [AC_MSG_ERROR([uuid/uuid.h was not found])])

dnl -------------------------------------------------------------------
dnl Java
dnl -------------------------------------------------------------------

MON_PKG_JAVA
if test -z "$no_java"; then
	AC_DEFINE([HAVE_JAVA], [1], [Define if Java support is requested.])
fi

dnl -------------------------------------------------------------------
dnl BOINC
dnl -------------------------------------------------------------------

AC_ARG_WITH([boinc], AS_HELP_STRING([--with-boinc], [Use BOINC in @<:@/usr@:>@]),
	[], [with_boinc=/usr])

if test "${with_boinc}" != no; then
	# Use default value if nothing is specified
	if test "${with_boinc}" = yes; then
		with_boinc=/usr
	fi
	BOINC_CPPFLAGS="-I${with_boinc}/include/BOINC"
	if test "${with_boinc}" != /usr; then
		BOINC_LDFLAGS="-L${with_boinc}/lib"
	fi
	save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="-pthread $CPPFLAGS $BOINC_CPPFLAGS"
	save_LIBS="$LIBS"
	LIBS="$LIBS $BOINC_LDFLAGS"
	AC_CHECK_HEADERS([boinc_api.h],,[with_boinc=no])
	AC_LANG_PUSH([C++])
	AC_CHECK_LIB([boinc_api],[boinc_init_options],,[with_boinc=no],[-lboinc])
	AC_LANG_POP([C++])
	CPPFLAGS="$save_CPPFLAGS"
	LIBS="$save_LIBS"

	AC_SUBST(BOINC_CPPFLAGS)
	AC_SUBST(BOINC_LDFLAGS)
fi
AM_CONDITIONAL([HAVE_BOINC], [test "$with_boinc" != no])
AM_CONDITIONAL([HAVE_JOBWRAPPER], [test "$with_boinc" != no -a "$with_jobwrapper" != no])

dnl ===================================================================
dnl Tools needed to build the documentation

AC_PATH_PROG([XSLTPROC], [xsltproc], [no])
if test "$XSLTPROC" != no; then
	XSLTPROC="$XSLTPROC --nonet"
	AC_CACHE_CHECK([for DocBook XML manpage stylesheets], [gg_cv_docbook_man],
	[
		cat >conftest.xml <<"EOF"
<?xml version="1.0"?>
	<!-- "http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd" @<:@ -->
<?xml-stylesheet href="http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl" type="text/xsl"?>
<!DOCTYPE refentry PUBLIC "-//OASIS//DTD DocBook XML V4.5//EN"
	"http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd" @<:@
@:>@>
<refentry id="test">
<refmeta>
    <refentrytitle>TEST</refentrytitle>
    <manvolnum>test</manvolnum>
</refmeta>
</refentry>
EOF
		$XSLTPROC --noout http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl conftest.xml >/dev/null 2>/dev/null
		rm -f .test
		result=$?
		if test $result = 0; then
			gg_cv_docbook_man=yes
		else
			gg_cv_docbook_man=no
		fi
	])
fi
AM_CONDITIONAL([HAVE_DOCBOOK_MAN], [test "$gg_cv_docbook_man" = yes])

dnl ===================================================================
dnl Create output files

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile
		 doc/Makefile
		 plugins/Makefile
		 3g-bridge.spec])
AC_OUTPUT
