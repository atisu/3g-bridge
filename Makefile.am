ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@ -DRUNDIR='"${localstatedir}/run"' @GSOAP_CFLAGS@

bin_PROGRAMS = injector
sbin_PROGRAMS = bridge
pkglibexecdir = $(libexecdir)/@PACKAGE@
pkglibexec_PROGRAMS = jobwrapper

if GSOAP
bin_PROGRAMS += wsclient
sbin_PROGRAMS += wssubmitter
endif

noinst_HEADERS = \
		AlgQueue.h \
		BackendException.h \
		Conf.h \
		DBHandler.h \
		DCAPIHandler.h \
		DownloadManager.h \
		EGEEHandler.h \
		GridHandler.h \
		Job.h \
		QMException.h \
		Util.h

injector_SOURCES = \
		injector.cpp \
		AlgQueue.cpp \
		DBHandler.cpp \
		Job.cpp \
		QMException.cpp \
		Util.c
injector_LDADD = -luuid $(GLIB_LIBS) $(MYSQL_LIBS)

jobwrapper_SOURCES = \
		jobwrapper.cpp \
		Util.c
jobwrapper_CPPFLAGS = $(BOINC_CPPFLAGS) $(AM_CPPFLAGS)
jobwrapper_LDFLAGS = $(BOINC_LDFLAGS) $(AM_LDFLAGS)
jobwrapper_LDADD = -luuid -lboinc_api -lboinc $(GLIB_LIBS) $(MYSQL_LIBS)

BACKEND_SOURCES =
BACKEND_LIBS =

if HAVE_DCAPI
BACKEND_SOURCES += DCAPIHandler.cpp
BACKEND_LIBS += $(DCAPI_MASTER_LIBS)
endif

if HAVE_EGEE
BACKEND_SOURCES += EGEEHandler.cpp
BACKEND_LIBS += $(EGEE_LIBS)
AM_CPPFLAGS += $(EGEE_CPPFLAGS)
endif

WSDL = 3GBridge.wsdl
TYPEMAP = 3GBridge.typemap

# gSOAP-related stuff
SOAP_HEADER = soap/3GBridge.h
SOAP_CPP_CORE = soap/soapC.cpp
SOAP_NS = soap/G3BridgeSubmitter.nsmap
SOAP_H_FILES = soap/soapH.h soap/soapStub.h
SOAP_CPP_SRC = $(SOAP_H_FILES) $(SOAP_NS) $(SOAP_CPP_CORE)

$(SOAP_HEADER): $(WSDL) $(TYPEMAP)
	$(WSDL2H) -o$(SOAP_HEADER) -t$(TYPEMAP) $(WSDL)

$(SOAP_CPP_SRC): $(SOAP_HEADER)
	$(SOAPCPP2) -x -w -dsoap -I$(GSOAP_IMPORT) $<

bridge_SOURCES = \
		AlgQueue.cpp \
		BackendException.cpp \
		DBHandler.cpp \
		Job.cpp \
		QMException.cpp \
		QueueManager.cpp \
		Util.c \
		$(BACKEND_SOURCES)
bridge_LDADD = $(GLIB_LIBS) $(BACKEND_LIBS) $(MYSQL_LIBS)

wssubmitter_CPPFALGS = $(GSOAP_CFLAGS) $(LIBCURL_CPPFLAGS)
wssubmitter_SOURCES = \
		$(SOAP_HEADER) \
		$(SOAP_CPP_SRC) \
		AlgQueue.cpp \
		DBHandler.cpp \
		DownloadManager.cpp \
		Job.cpp \
		QMException.cpp \
		Util.c \
		WSSubmitter.cpp \
		soap/soapServer.cpp
wssubmitter_LDADD = $(GLIB_LIBS) $(MYSQL_LIBS) $(GSOAP_LIBS) $(LIBCURL) -lcrypto -luuid

wsclient_CPPFALGS = $(GSOAP_CFLAGS)
wsclient_SOURCES = \
		$(SOAP_CPP_SRC) \
		soap/soapClient.cpp \
		wsclient.cpp
wsclient_LDADD = $(GLIB_LIBS) $(GSOAP_LIBS)

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$< > $@

CLEANFILES = master.xml queuemanager.tar.gz soap/*
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
		3GBridge.typemap \
		3GBridge.wsdl \
		master.xml.in \
		etc/3g-bridge.* \
		doc/*

nobase_dist_doc_DATA = \
			README-BOINC2EGEE \
			db/schema.sql \
			examples/batch_pack \
			examples/batch_unpack \
			master.xml

pkgconfdir = $(sysconfdir)/@PACKAGE@
dist_pkgconf_DATA = etc/bridge.conf.sample \
		    etc/dc-api.conf

package: queuemanager.tar.gz

package_contents = \
		   bridge \
		   bridge.conf.sample \
		   dc-api.conf \
		   master.xml \
		   examples/batch_pack \
		   examples/batch_unpack

queuemanager.tar.gz: $(package_contents)
	mkdir package-tmp
	cp -p $(package_contents) package-tmp/
	(cd package-tmp && $(AMTAR) cf - *) | GZIP=$(GZIP_ENV) gzip -c > $@
	rm -rf package-tmp
