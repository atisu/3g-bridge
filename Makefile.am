MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@

bin_PROGRAMS = injector CGManager

injector_SOURCES = \
		injector.cpp \
		CGAlgQueue.cpp \
		CGAlgQueue.h \
		CGJob.cpp \
		CGJob.h \
		DBHandler.cpp \
		DBHandler.h \
		Logging.cpp \
		Logging.h \
		QMConfig.cpp \
		QMConfig.h
injector_LDADD = -luuid $(MYSQL_LIBS)

BACKEND_SOURCES =
BACKEND_LIBS =
if HAVE_DCAPI
BACKEND_SOURCES += DCAPIHandler.cpp DCAPIHandler.h
BACKEND_LIBS += $(DCAPI_MASTER_LIBS)
endif
if HAVE_EGEE
BACKEND_SOURCES += EGEEHandler.cpp EGEEHandler.h
endif

CGManager_SOURCES = \
		common.h \
		BackendException.cpp \
		BackendException.h \
		CGAlgQueue.cpp \
		CGAlgQueue.h \
		CGJob.cpp \
		CGJob.h \
		CGManager.cpp \
		CGQueueManager.cpp \
		CGQueueManager.h \
		DBHandler.h \
		DBHandler.cpp \
		GridHandler.h \
		Logging.cpp \
		Logging.h \
		QMConfig.cpp \
		QMConfig.h \
		QMException.cpp \
		QMException.h \
		$(BACKEND_SOURCES)
CGManager_LDADD = $(BACKEND_LIBS) $(MYSQL_LIBS)

master.xml: master.xml.in
	$(SED) -e "s,@PACKAGE_VERSION\@,$(PACKAGE_VERSION)," \
		$< > $@

package: queuemanager.tar.gz

CLEANFILES = master.xml queuemanager.tar.gz

package_contents = CGManager master.xml dc-api.conf qm.conf.sample

queuemanager.tar.gz: $(package_contents)
	mkdir package-tmp
	cp -p $(package_contents) package-tmp/
	(cd package-tmp && $(AMTAR) cf - *) | GZIP=$(GZIP_ENV) gzip -c > $@
	rm -rf package-tmp
