MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@

bin_PROGRAMS = injector CGManager

noinst_HEADERS = \
		common.h \
		BackendException.h \
		CGAlgQueue.h \
		CGJob.h \
		CGManager.h \
		CGQueueManager.h \
		DCAPIHandler.h \
		DBHandler.h \
		EGEEHandler.h \
		GridHandler.h \
		Logging.h \
		QMException.h

injector_SOURCES = \
		injector.cpp \
		CGAlgQueue.cpp \
		CGJob.cpp \
		DBHandler.cpp \
		Logging.cpp \
		QMException.cpp
injector_LDADD = -luuid $(MYSQL_LIBS) $(GLIB_LIBS)

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

CGManager_SOURCES = \
		BackendException.cpp \
		CGAlgQueue.cpp \
		CGJob.cpp \
		CGManager.cpp \
		CGQueueManager.cpp \
		DBHandler.cpp \
		Logging.cpp \
		QMException.cpp \
		$(BACKEND_SOURCES)
CGManager_LDADD = $(BACKEND_LIBS) $(MYSQL_LIBS) $(GLIB_LIBS)

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$< > $@

package: queuemanager.tar.gz

CLEANFILES = master.xml queuemanager.tar.gz

package_contents = \
		   CGManager \
		   master.xml \
		   dc-api.conf \
		   qm.conf.sample \
		   examples/batch_pack \
		   examples/batch_unpack

queuemanager.tar.gz: $(package_contents)
	mkdir package-tmp
	cp -p $(package_contents) package-tmp/
	(cd package-tmp && $(AMTAR) cf - *) | GZIP=$(GZIP_ENV) gzip -c > $@
	rm -rf package-tmp
