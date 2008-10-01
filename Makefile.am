MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@ $(GSOAP_CPPFLAGS) $(GSOAP_SSL_CPPFLAGS)

bin_PROGRAMS = injector bridge wsbridge wsclient

noinst_HEADERS = \
		AlgQueue.h \
		BackendException.h \
		Bridge.h \
		DBHandler.h \
		DCAPIHandler.h \
		EGEEHandler.h \
		GridHandler.h \
		Job.h \
		Logging.h \
		QMException.h \
		QueueManager.h

injector_SOURCES = \
		injector.cpp \
		AlgQueue.cpp \
		DBHandler.cpp \
		Job.cpp \
		Logging.cpp \
		QMException.cpp
injector_LDADD = -luuid $(GLIB_LIBS) $(MYSQL_LIBS)

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

# gSOAP-related stuff
SOAP_HEADER = WSBridge.h
SOAP_CPP_CORE = soap/soapC.cpp
SOAP_NS = soap/gggbridge.nsmap soap/gggbridge.wsdl
SOAP_H_FILES = soap/soapH.h soap/soapStub.h
SOAP_CPP_SRC = $(SOAP_H_FILES) $(SOAP_NS) $(SOAP_CPP_CORE)

$(SOAP_CPP_SRC) : $(SOAP_HEADER)
	$(SOAP) -dsoap $(SOAP_FLAGS) $<

bridge_SOURCES = \
		AlgQueue.cpp \
		BackendException.cpp \
		Bridge.cpp \
		DBHandler.cpp \
		Job.cpp \
		Logging.cpp \
		QMException.cpp \
		QueueManager.cpp \
		$(BACKEND_SOURCES)
bridge_LDADD = $(GLIB_LIBS) $(BACKEND_LIBS) $(MYSQL_LIBS)

wsbridge_CPPFALGS = $(GSOAP_SSL_CPPFLAGS)
wsbridge_SOURCES = \
		$(SOAP_HEADER) \
		$(SOAP_CPP_SRC) \
		wsbridge.cpp \
		DBHandler.cpp \
		Logging.cpp \
		Job.cpp \
		AlgQueue.cpp \
		QMException.cpp \
		soap/soapServer.cpp
wsbridge_LDADD = $(GLIB_LIBS) $(MYSQL_LIBS) $(GSOAP_SSL_LDFLAGS) -lpthread -luuid -lssl

wsclient_CPPFALGS = $(GSOAP_SSL_CPPFLAGS)
wsclient_SOURCES = \
		wsclient.cpp \
		$(SOAP_HEADER) \
		$(SOAP_CPP_CORE) \
		soap/soapClient.cpp
wsclient_LDADD = $(GSOAP_SSL_LDFLAGS) -lssl

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$< > $@

package: queuemanager.tar.gz

CLEANFILES = master.xml queuemanager.tar.gz

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
