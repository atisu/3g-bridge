AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@
AM_LDFLAGS = -module -avoid-version -export-symbols=symbols.exp
AM_LIBADD = @MYSQL_LIBS@ @GLIB_LIBS@
AM_JAVACFLAGS = -g

pkglib_LTLIBRARIES = Null_handler.la Null_monitor.la

CLEANFILES =
BUILT_SOURCES =

# Override automake defaults
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

if HAVE_EC2
pkglib_LTLIBRARIES += EC2_handler.la
endif

pkglib_LTLIBRARIES += Metajob_handler.la

Metajob_handler_la_SOURCES = \
	MJ_parser.cpp MJ_parser.h \
	Metajob_handler.cpp Metajob_handler.h \
	autoarray $(top_builddir)/Conf.cpp
Metajob_handler_la_LIBADD = #TODO

BOINC_monitor_la_SOURCES = BOINC_monitor.cpp BOINC_monitor.h
BOINC_monitor_la_LIBADD = -lsched $(AM_LIBADD)

DC_API_handler_la_SOURCES = DC-API_handler.cpp DC-API_handler.h
DC_API_handler_la_LIBADD = @DCAPI_MASTER_LIBS@ $(AM_LIBADD)

DC_API_Single_handler_la_SOURCES = DC-API-Single_handler.cpp DC-API-Single_handler.h
DC_API_Single_handler_la_LIBADD = @DCAPI_MASTER_LIBS@ $(AM_LIBADD)

EGEE_handler_la_SOURCES = EGEE_handler.cpp EGEE_handler.h
EGEE_handler_la_CPPFLAGS = @EGEE_CPPFLAGS@ $(AM_CPPFLAGS) -DGLITE_VERSION=$(GLITE_VERSION)
EGEE_handler_la_LIBADD = @EGEE_LIBS@ $(AM_LIBADD)

Null_handler_la_SOURCES = Null_handler.cpp Null_handler.h

Null_monitor_la_SOURCES = Null_monitor.cpp Null_monitor.h

XTREMWEB_handler_la_SOURCES = XTREMWEB_handler.cpp XTREMWEB_handler.h
XTREMWEB_handler_la_LIBADD = -lssl

Java_handler_la_SOURCES = Java_handler.cpp Java_handler.h
Java_handler_la_CPPFLAGS = $(JAVA_INC) $(AM_CPPFLAGS) -DJAVA_CLASSPATH='"${javadir}/${JARNAME}"' -DPKGLIBDIR='"${pkglibdir}"'

EC2_handler_la_SOURCES = EC2Util.cpp EC2Util.h EC2_handler.cpp EC2_handler.h

CLASSDIR = hu/sztaki/lpds/G3Bridge
JAVA_SRC = \
		$(CLASSDIR)/GridHandler.java \
		$(CLASSDIR)/FileRef.java \
		$(CLASSDIR)/Job.java \
		$(CLASSDIR)/Logger.java \
		$(CLASSDIR)/LogLevel.java \
		$(CLASSDIR)/RuntimeBridgeException.java

JAVA_BES_SRC = \
		$(CLASSDIR)/plugin/bes/BESHandler.java \
		$(CLASSDIR)/plugin/bes/JsdlDocBuilder.java \
		$(CLASSDIR)/plugin/bes/WrapperBuilder.java

EXTRA_DIST = symbols.exp $(JAVA_SRC) $(JAVA_BES_SRC)

if HAVE_JAVA

noinst_JAVA = $(JAVA_SRC)
JAVACFLAGS =

if HAVE_BES
noinst_JAVA += $(JAVA_BES_SRC)
JAVACFLAGS += -cp $(BES_LIBS)/\*:$$CLASSPATH
endif

javadir = $(datadir)/java
java_DATA = $(JARNAME)

JARNAME = 3g-bridge-plugin-$(PACKAGE_VERSION).jar

JAVA_CLASSES = $(noinst_JAVA:.java=.class)
JAVA_CLASSNAMES = $(subst /,.,$(JAVA_CLASSES:.class=))

CLEANFILES += $(JAVA_CLASSES) $(JARNAME)

$(JARNAME): classnoinst.stamp
	$(JAR) -cf $@ $(JAVA_CLASSES)

endif
