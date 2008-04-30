MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = @MYSQL_CPPFLAGS@

bin_PROGRAMS = test injector CGManager

injector_SOURCES = injector.cpp
injector_LDADD = -lmysqlpp

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
		CGQueueManager.h
CGManager_LDADD = $(DCAPI_MASTER_LIBS) -lmysqlpp

test_SOURCES = test.cpp \
		common.h \
		CGAlg.h \
		CGAlgQueue.cpp \
		CGAlgQueue.h \
		CGJob.cpp \
		CGJob.h \
		CGQueueManager.cpp \
		CGQueueManager.h
test_LDADD = $(DCAPI_MASTER_LIBS) -lmysqlpp

endif
