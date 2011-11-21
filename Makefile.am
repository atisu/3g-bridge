ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@ -DRUNDIR='"${localstatedir}/run"' -DSYSCONFDIR='"${sysconfdir}"' -DPLUGINDIR='"${pkglibdir}"' @GSOAP_CFLAGS@

pkglibexecdir = $(libexecdir)/@PACKAGE@

bin_PROGRAMS = injector
sbin_PROGRAMS = 3g-bridge

CLEANFILES =
EXTRA_DIST =

if HAVE_JOBWRAPPER
pkglibexec_PROGRAMS = jobwrapper
endif

if HAVE_GSOAP
bin_PROGRAMS += wsclient wsmonitorclient
sbin_PROGRAMS += wssubmitter wsmonitor

SUBMITTER_SOAP_INTERFACE = \
		soap/3GBridgeSubmitter.h
SUBMITTER_SOAP_SOURCES_COMMON = \
		soap/Submitter.nsmap \
		soap/SubmitterC.cpp \
		soap/SubmitterH.h \
		soap/SubmitterStub.h
SUBMITTER_SOAP_SOURCES_CLIENT = \
		$(SUBMITTER_SOAP_SOURCES_COMMON) \
		soap/SubmitterClient.cpp
SUBMITTER_SOAP_SOURCES_SERVER = \
		$(SUBMITTER_SOAP_SOURCES_COMMON) \
		soap/SubmitterServer.cpp

MONITOR_SOAP_INTERFACE = \
		soap/3GBridgeMonitor.h
MONITOR_SOAP_SOURCES_COMMON = \
		soap/Monitor.nsmap \
		soap/MonitorC.cpp \
		soap/MonitorH.h \
		soap/MonitorStub.h
MONITOR_SOAP_SOURCES_CLIENT = \
		$(MONITOR_SOAP_SOURCES_COMMON) \
		soap/MonitorClient.cpp
MONITOR_SOAP_SOURCES_SERVER = \
		$(MONITOR_SOAP_SOURCES_COMMON) \
		soap/MonitorServer.cpp

$(SUBMITTER_SOAP_INTERFACE): 3GBridgeSubmitter.wsdl 3GBridgeSubmitter.typemap
	$(WSDL2H) -o $@ -t 3GBridgeSubmitter.typemap 3GBridgeSubmitter.wsdl

$(SUBMITTER_SOAP_SOURCES_COMMON): $(SUBMITTER_SOAP_INTERFACE)
	$(SOAPCPP2) -x -w -dsoap -n -I$(GSOAP_IMPORT) -pSubmitter $<

$(MONITOR_SOAP_INTERFACE): 3GBridgeMonitor.wsdl 3GBridgeMonitor.typemap
	$(WSDL2H) -o $@ -t 3GBridgeMonitor.typemap 3GBridgeMonitor.wsdl

$(MONITOR_SOAP_SOURCES_COMMON): $(MONITOR_SOAP_INTERFACE)
	$(SOAPCPP2) -x -w -dsoap -n -I$(GSOAP_IMPORT) -pMonitor $<

BUILT_SOURCES = \
		$(SUBMITTER_SOAP_INTERFACE) \
		$(SUBMITTER_SOAP_SOURCES_CLIENT) \
		$(SUBMITTER_SOAP_SOURCES_SERVER) \
		$(MONITOR_SOAP_INTERFACE) \
		$(MONITOR_SOAP_SOURCES_CLIENT) \
		$(MONITOR_SOAP_SOURCES_SERVER)
endif

SUBDIRS = doc plugins

noinst_HEADERS = \
		AlgQueue.h \
		BackendException.h \
		Conf.h \
		DBHandler.h \
		FileRef.h \
		plugins/DC-API_handler.h \
		plugins/DC-API-Single_handler.h \
		plugins/XTREMWEB_handler.h \
		plugins/Null_handler.h \
		plugins/EGEE_handler.h \
		plugins/Java_handler.h \
		DownloadManager.h \
		GridHandler.h \
		Job.h \
		QMException.h \
		DLException.h \
		Util.h \
		types.h \
		mkstr

injector_SOURCES = \
		injector.cpp \
		AlgQueue.cpp \
		DBHandler.cpp \
		Job.cpp \
		FileRef.cpp \
		QMException.cpp \
		Util.c
injector_LDADD = $(GLIB_LIBS) $(MYSQL_LIBS) -luuid

jobwrapper_SOURCES = \
		jobwrapper.cpp \
		Util.c
jobwrapper_CPPFLAGS = $(BOINC_CPPFLAGS) $(AM_CPPFLAGS)
jobwrapper_LDFLAGS = $(BOINC_LDFLAGS) $(AM_LDFLAGS)
jobwrapper_LDADD = -lboinc_api -lboinc $(GLIB_LIBS) $(MYSQL_LIBS) -luuid

3g_bridge_SOURCES = \
		AlgQueue.cpp \
		BackendException.cpp \
		DBHandler.cpp \
		GridHandler.cpp \
		Job.cpp \
		FileRef.cpp \
		QMException.cpp \
		DLException.cpp \
		QueueManager.cpp \
		Util.c \
		MonitorHandler.h \
		Conf.cpp Conf.h \
		LogMonMsg.cpp LogMonMsg.h

3g_bridge_LDADD =
if HAVE_EGEE
3g_bridge_LDADD += $(EGEE_LIBS)
endif
3g_bridge_LDADD += $(GLIB_LIBS) $(GTHREAD_LIBS) $(GMODULE_LIBS) $(MYSQL_LIBS) -luuid
3g_bridge_LDFLAGS = -export-dynamic

wssubmitter_CPPFALGS = $(GSOAP_CFLAGS) $(LIBCURL_CPPFLAGS)
nodist_wssubmitter_SOURCES = \
		$(SUBMITTER_SOAP_SOURCES_SERVER)
wssubmitter_SOURCES = \
		AlgQueue.cpp \
		DBHandler.cpp \
		DownloadManager.cpp \
		Job.cpp \
		FileRef.cpp \
		QMException.cpp \
		DLException.cpp \
		Util.c \
		WSSubmitter.cpp \
		Conf.cpp \
		LogMonMsg.cpp LogMonMsg.h
wssubmitter_LDADD = $(GLIB_LIBS) $(GTHREAD_LIBS) $(MYSQL_LIBS) $(GSOAP_LIBS) $(LIBCURL) -lcrypto -luuid

wsmonitor_CPPFALGS = $(GSOAP_CFLAGS) $(LIBCURL_CPPFLAGS)
nodist_wsmonitor_SOURCES = \
		$(MONITOR_SOAP_SOURCES_SERVER)
wsmonitor_SOURCES = \
		Util.c \
		QMException.cpp \
		BackendException.cpp \
		WSMonitor.cpp
wsmonitor_LDADD = $(GLIB_LIBS) $(GMODULE_LIBS) $(MYSQL_LIBS) $(GSOAP_LIBS) $(LIBCURL) -lcrypto
wsmonitor_LDFLAGS = -export-dynamic

wsclient_CPPFALGS = $(GSOAP_CFLAGS)
nodist_wsclient_SOURCES = \
		$(SUBMITTER_SOAP_SOURCES_CLIENT)
wsclient_SOURCES = \
		wsclient.cpp \
		Conf.cpp \
		QMException.cpp \
		Util.c
wsclient_LDADD = $(GLIB_LIBS) $(GSOAP_LIBS) -luuid

wsmonitorclient_CPPFALGS = $(GSOAP_CFLAGS)
nodist_wsmonitorclient_SOURCES = \
		$(MONITOR_SOAP_SOURCES_CLIENT)
wsmonitorclient_SOURCES = \
		wsmonitorclient.cpp
wsmonitorclient_LDADD = $(GLIB_LIBS) $(GSOAP_LIBS)

if HAVE_GSOAP
GSOAP_EDIT = -e '/^@@ WS-START @@/d' -e '/^@@ WS-END @@/d'
else
GSOAP_EDIT = -e '/^@@ WS-START @@/,/^@@ WS-END @@/d'
endif

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$(GSOAP_EDIT) \
		$< > $@

CLEANFILES += master.xml 3g-bridge-package.tar.gz soap/*
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST += \
		3GBridgeSubmitter.typemap \
		3GBridgeSubmitter.wsdl \
		3GBridgeMonitor.typemap \
		3GBridgeMonitor.wsdl \
		master.xml.in \
		3g-bridge.spec \
		etc/3g-bridge.* \
		doc/* \
		soap/.keepme

docdir = ${datadir}/doc/${PACKAGE}

dist_doc_DATA = \
		README \
		doc/README-EC2 \
		doc/README-BOINC2EGEE \
		master.xml
nobase_dist_doc_DATA = \
		db/schema.sql \
		db/schema-update-1.5.sql \
		db/schema-update-1.6.sql \
		db/purge.sql \
		examples/batch_pack \
		examples/batch_unpack \
		examples/dc-api.conf \
		examples/jobwrapper_client.xml \
		examples/resource-manager.sh \
		examples/hu/sztaki/lpds/G3Bridge/plugin/null/NullHandler.java

dist_sysconf_DATA = etc/3g-bridge.conf

SVNREV := $(shell svnversion "$(srcdir)")
dist-hook:
	if [ "$(SVNREV)" != "exported" ]; then \
		svn2cl --group-by-day --authors=AUTHORS --revision HEAD:2153 -o "$(distdir)/ChangeLog"; \
		for f in  "$(distdir)/3g-bridge.spec"{,.in}; do \
			$(SED) -i -e "s/^#define rev @REVISION@$$/%define rev ${SVNREV}/" "$$f"; \
		done; \
	fi

package: 3g-bridge-package.tar.gz

package_contents = \
		3g-bridge \
		etc/3g-bridge.conf \
		master.xml \
		examples/batch_pack \
		examples/batch_unpack \
		examples/dc-api.conf

if HAVE_GSOAP
package_contents += wssubmitter wsmonitor
endif

3g-bridge-package.tar.gz: $(package_contents)
	mkdir package-tmp
	cp -p $(package_contents) package-tmp/
	(cd package-tmp && $(AMTAR) cf - *) | GZIP=$(GZIP_ENV) gzip -c > $@
	rm -rf package-tmp
