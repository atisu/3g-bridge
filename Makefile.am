MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@ @GLIB_CFLAGS@

bin_PROGRAMS = injector Manager

noinst_HEADERS = \
		common.h \
		AlgQueue.h \
		BackendException.h \
		DBHandler.h \
		DCAPIHandler.h \
		EGEEHandler.h \
		GridHandler.h \
		Job.h \
		Logging.h \
		Manager.h \
		QMException.h \
		QueueManager.h

injector_SOURCES = \
		injector.cpp \
		AlgQueue.cpp \
		DBHandler.cpp \
		Job.cpp \
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

Manager_SOURCES = \
		AlgQueue.cpp \
		BackendException.cpp \
		DBHandler.cpp \
		Job.cpp \
		Logging.cpp \
		Manager.cpp \
		QMException.cpp \
		QueueManager.cpp \
		$(BACKEND_SOURCES)
Manager_LDADD = $(BACKEND_LIBS) $(MYSQL_LIBS) $(GLIB_LIBS)

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$< > $@

package: queuemanager.tar.gz

CLEANFILES = master.xml queuemanager.tar.gz

package_contents = \
		   Manager \
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
