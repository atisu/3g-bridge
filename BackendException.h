#ifndef BACKENDEXCEPTION_H
#define BACKENDEXCEPTION_H

#include "QMException.h"

class BackendException: public QMException
{
public:
	/* Create a new exception */
	BackendException() {}
	BackendException(const std::string &reason):QMException(reason) {};
	BackendException(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

	/* Dispose the exception */
	~BackendException() throw () {};

};

#endif /* BACKENDEXCEPTION_H */
