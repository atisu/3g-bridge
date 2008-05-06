dnl
dnl EGEE Backend
dnl
dnl Check for the EGEE-specific development files
dnl

AC_DEFUN([GLOBUS_CONF], [
	AC_ARG_WITH([globus], AS_HELP_STRING([--with-globus],
		[Use Globus in @<:@/opt/globus@:>@]),,
		[with_globus=/opt/globus])
	CPPFLAGS="$CPPFLAGS -I${with_globus}/include/gcc32dbgpthr"
	LIBS="$LIBS -L${with_globus}/lib -lssl_gcc32dbgpthr -lglobus_ftp_client_gcc32dbgpthr"
])

AC_DEFUN([CLASSADS_CONF], [
	AC_ARG_WITH([classads], AS_HELP_STRING([--with-classads],
		[Use Classads in @<:@/opt/classads@:>@]),,
		[with_classads=/opt/classads])
	CPPFLAGS="$CPPFLAGS -I${with_classads}/include"
	LIBS="$LIBS -L${with_classads}/lib -lclassad"
])

AC_DEFUN([GLITE_CONF], [
	AC_ARG_WITH([glite], AS_HELP_STRING([--with-glite],
		[Use gLite in @<:@/opt/glite@:>@]),,
		[with_glite=/opt/glite])
	CPPFLAGS="$CPPFLAGS -I${with_glite}/include -DWANT_NAMESPACES"
	LIBS="$LIBS -L${with_glite}/lib -lglite_wms_wmproxy_api_cpp -lglite_wmsutils_jobid -lglite_wmsutils_exception -lglite_wmsutils_classads -lglite_lb_clientpp_gcc32dbgpthr -lglite_jdl_api_cpp -lgridsite"
])

AC_DEFUN([EGEE_BE], [
	AC_REQUIRE([GLOBUS_CONF])
	AC_REQUIRE([CLASSADS_CONF])
	AC_REQUIRE([GLITE_CONF])

	LIBS="$LIBS -lxml2 -lboost_filesystem -lboost_regex"
])