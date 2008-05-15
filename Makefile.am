MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@

bin_PROGRAMS = injector CGManager

injector_SOURCES = injector.cpp
injector_LDADD = -lmysqlpp -luuid

if HAVE_DCAPI

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
		DCAPIHandler.h \
		DCAPIHandler.cpp \
		Logging.h
CGManager_LDADD = $(DCAPI_MASTER_LIBS) -lmysqlpp -luuid

endif

if HAVE_EGEE

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
		EGEEHandler.cpp \
		EGEEHandler.h \
		Logging.h
CGManager_LDADD = -lmysqlpp -luuid

endif
