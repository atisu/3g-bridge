AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@
AM_LDFLAGS = -module -avoid-version
AM_LIBADD = @MYSQL_LIBS@ @GLIB_LIBS@

pkglib_LTLIBRARIES =

if HAVE_BOINC
pkglib_LTLIBRARIES += BOINC_monitor.la
endif

BOINC_monitor_la_SOURCES = BOINC_monitor.cpp BOINC_monitor.h
BOINC_monitor_la_LIBADD = -lsched $(AM_LIBADD)
