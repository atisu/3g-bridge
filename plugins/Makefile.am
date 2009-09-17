AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@
AM_LDFLAGS = -module -avoid-version
AM_LIBADD = @MYSQL_LIBS@ @GLIB_LIBS@

pkglib_LTLIBRARIES = Null_handler.la Null_monitor.la

if HAVE_BOINC
pkglib_LTLIBRARIES += BOINC_monitor.la
endif

if HAVE_DCAPI
pkglib_LTLIBRARIES += DC-API_handler.la DC-API-Single_handler.la
endif

if HAVE_EGEE
pkglib_LTLIBRARIES += EGEE_handler.la
endif

if HAVE_XTREMWEB
pkglib_LTLIBRARIES += XTREMWEB_handler.la
endif

BOINC_monitor_la_SOURCES = BOINC_monitor.cpp BOINC_monitor.h
BOINC_monitor_la_LIBADD = -lsched $(AM_LIBADD)

DC_API_handler_la_SOURCES = DC-API_handler.cpp DC-API_handler.h
DC_API_handler_la_LIBADD = @DCAPI_MASTER_LIBS@ $(AM_LIBADD)

DC_API_Single_handler_la_SOURCES = DC-API-Single_handler.cpp DC-API-Single_handler.h
DC_API_Single_handler_la_LIBADD = @DCAPI_MASTER_LIBS@ $(AM_LIBADD)

EGEE_handler_la_SOURCES = EGEE_handler.cpp EGEE_handler.h
EGEE_handler_la_CPPFLAGS = @EGEE_CPPFLAGS@ $(AM_CPPFLAGS)
EGEE_handler_la_LIBADD = @EGEE_LIBS@ $(AM_LIBADD)

Null_handler_la_SOURCES = Null_handler.cpp Null_handler.h

Null_monitor_la_SOURCES = Null_monitor.cpp Null_monitor.h

XTREMWEB_handler_la_SOURCES = XTREMWEB_handler.cpp XTREMWEB_handler.h
