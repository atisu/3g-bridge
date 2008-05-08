MAINTAINERCLEANFILES = Makefile.in

ACLOCAL_AMFLAGS = -I cf

AM_CPPFLAGS = @MYSQL_CPPFLAGS@

bin_PROGRAMS = injector CGManager

injector_SOURCES = injector.cpp
injector_LDADD = -lmysqlpp -luuid

if HAVE_DCAPI

CGManager_SOURCES = \
		common.h \
		CGAlg.h \
		CGAlgQueue.cpp \
		CGAlgQueue.h \
		CGJob.cpp \
		CGJob.h \
		CGManager.cpp \
		CGQueueManager.cpp \
		CGQueueManager.h \
		JobDB.h \
		JobDB.cpp \
		GridHandler.h \
		DCAPIHandler.h \
		DCAPIHandler.cpp
CGManager_LDADD = $(DCAPI_MASTER_LIBS) -lmysqlpp -luuid

endif

if HAVE_EGEE

CGManager_SOURCES = \
		common.h \
		CGAlg.h \
		CGAlgQueue.cpp \
		CGAlgQueue.h \
		CGJob.cpp \
		CGJob.h \
		CGManager.cpp \
		CGQueueManager.cpp \
		CGQueueManager.h \
		JobDB.h \
		JobDB.cpp \
		GridHandler.h \
		EGEEHandler.cpp \
		EGEEHandler.h
CGManager_LDADD = -lmysqlpp -luuid

endif
