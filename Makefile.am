MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@

bin_PROGRAMS = injector CGManager

injector_SOURCES = injector.cpp
injector_LDADD = -lmysqlpp -luuid

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
		Logging.h \
		QMConfig.cpp \
		QMConfig.h \
		$(BACKEND_SOURCES)
CGManager_LDADD = $(BACKEND_LIBS) -lmysqlpp
