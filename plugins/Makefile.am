AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-symbols=symbols.exp
AM_LIBADD = @MYSQL_LIBS@ @GLIB_LIBS@
AM_JAVACFLAGS = -g
EXTRA_DIST = symbols.exp

pkglib_LTLIBRARIES = Null_handler.la Null_monitor.la

CLEANFILES =
BUILT_SOURCES =

# Override automake defaults
CLASSPATH_ENV =
JAVAROOT = $(top_builddir)/plugins

if HAVE_DCAPI
pkglib_LTLIBRARIES += DC-API_handler.la DC-API-Single_handler.la
if HAVE_BOINC
pkglib_LTLIBRARIES += BOINC_monitor.la
endif
endif

if HAVE_EGEE
pkglib_LTLIBRARIES += EGEE_handler.la
endif

if HAVE_XTREMWEB
pkglib_LTLIBRARIES += XTREMWEB_handler.la
endif

if HAVE_JAVA
pkglib_LTLIBRARIES += Java_handler.la
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

Java_handler_la_SOURCES = Java_handler.cpp Java_handler.h
Java_handler_la_CPPFLAGS = $(JAVA_INC) $(AM_CPPFLAGS) -DJAVA_CLASSPATH='"${javadir}/${JARNAME}"' -DPKGLIBDIR='"${pkglibdir}"'

if HAVE_JAVA

javadir = $(datadir)/java
java_DATA = $(JARNAME)

JARNAME = 3g-bridge-plugin-$(PACKAGE_VERSION).jar

CLASSDIR = hu/sztaki/lpds/G3Bridge
JAVA_SRC = \
		$(CLASSDIR)/GridHandler.java \
		$(CLASSDIR)/Job.java \
		$(CLASSDIR)/Logger.java \
		$(CLASSDIR)/LogLevel.java \
		$(CLASSDIR)/RuntimeBridgeException.java

noinst_JAVA = $(JAVA_SRC)

JAVA_CLASSES = $(JAVA_SRC:.java=.class)
JAVA_CLASSNAMES = $(subst /,.,$(JAVA_CLASSES:.class=))

CLEANFILES += $(JAVA_CLASSES) $(JARNAME)

$(JARNAME): classnoinst.stamp
	$(JAR) -cf $@ $(JAVA_CLASSES)

endif
