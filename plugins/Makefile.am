AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@
#AM_LDFLAGS = -module -avoid-version -export-symbols=symbols.exp
AM_LIBADD = @MYSQL_LIBS@ @GLIB_LIBS@
AM_JAVACFLAGS = -g

#pkglib_LTLIBRARIES = Null_handler.la Null_monitor.la
pkglib_LTLIBRARIES = 

if HAVE_BOINC
pkglib_LTLIBRARIES += libBOINC_monitor.la
endif

libBOINC_monitor_la_SOURCES = BOINC_monitor.cpp BOINC_monitor.h
libBOINC_monitor_la_LIBADD = -lsched $(AM_LIBADD)
