dnl
dnl EGEE Backend
dnl
dnl Check for the EGEE-specific development files
dnl

AC_DEFUN([GLOBUS_CONF], [
	AC_ARG_WITH([globus], AS_HELP_STRING([--with-globus],
		[Use Globus in @<:@/opt/globus@:>@]),,
		[with_globus=/opt/globus])
	EGEE_CPPFLAGS="$EGEE_CPPFLAGS -I${with_globus}/include/gcc64dbgpthr"
	EGEE_LIBS="$EGEE_LIBS -L${with_globus}/lib -lmyproxy_gcc64dbgpthr -lglobus_ftp_client_gcc64dbgpthr -lglobus_gass_copy_gcc64dbgpthr"
])

AC_DEFUN([CLASSADS_CONF], [
	AC_ARG_WITH([classads], AS_HELP_STRING([--with-classads],
		[Use Classads in @<:@/opt/classads@:>@]),,
		[with_classads=/opt/classads])
	EGEE_CPPFLAGS="$EGEE_CPPFLAGS -I${with_classads}/include/classad -I${with_classads}/include -DWANT_CLASSAD_NAMESPACE"
	EGEE_LIBS="$EGEE_LIBS -L${with_classads}/lib64 -lclassad_ns"
])

AC_DEFUN([CARES_CONF], [
	AC_ARG_WITH([cares], AS_HELP_STRING([--with-cares],
		[Use C-Ares in @<:@/opt/c-ares@:>@]),,
		[with_cares=/opt/c-ares])
	EGEE_CPPFLAGS="$EGEE_CPPFLAGS -I${with_cares}/include"
	EGEE_LIBS="$EGEE_LIBS -L${with_cares}/lib -lcares"
])

AC_DEFUN([GLITE_CONF], [
	AC_ARG_WITH([glite], AS_HELP_STRING([--with-glite],
		[Use gLite in @<:@/opt/glite@:>@]),,
		[with_glite=/opt/glite])
	EGEE_CPPFLAGS="$EGEE_CPPFLAGS -I${with_glite}/include -DWANT_NAMESPACES"
	EGEE_LIBS="$EGEE_LIBS -L${with_glite}/lib64 -L${with_glite}/lib -lglite_wms_wmproxy_api_cpp -lglite_wmsutils_exception -lglite_wmsutils_classads -lglite_lb_clientpp_gcc64dbgpthr -lglite_jdl_api_cpp -lgridsite"
])

AC_DEFUN([EGEE_BE], [
	AC_REQUIRE([GLOBUS_CONF])
	AC_REQUIRE([GLITE_CONF])
	AC_REQUIRE([CARES_CONF])
	AC_REQUIRE([CLASSADS_CONF])

	EGEE_LIBS="$EGEE_LIBS -lxml2 -lboost_filesystem -lboost_regex"
	AC_SUBST([EGEE_LIBS])
	AC_SUBST([EGEE_CPPFLAGS])
])
