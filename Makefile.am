MAINTAINERCLEANFILES = Makefile.in
bin_PROGRAMS = test injector
test_SOURCES = test.cpp CGAlg.h CGJob.cpp CGJob.h CGQueueManager.h CGQueueManager.cpp common.h CGAlgQueue.h CGAlgQueue.cpp
test_LDADD = -ldc-boinc -lsched -lboinc -lcrypto -lstdc++ -lmysqlclient -lz -lcrypt -lnsl -lm -lglib-2.0 -luuid -lmysqlpp
injector_SOURCES = injector.cpp
injector_LDADD = -lmysqlpp
