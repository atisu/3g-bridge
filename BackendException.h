#ifndef BACKENDEXCEPTION_H
#define BACKENDEXCEPTION_H

#include "QMException.h"


/**
 * Backend exception class. Can be used to throw exceptions in grid handling
 * plugins.
 */
class BackendException: public QMException
{
public:
	/// Create a new exception
	BackendException() {}

	/**
	 * Create a new exception using a string as reason.
	 * @param reason message of the exception
	 */
	BackendException(const std::string &reason):QMException(reason) {};

	/**
	 * Create a new exception using a printf-like format.
	 * @param fmt format string
	 * @param ... arguments to the format string
	 */
	BackendException(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

	/// Dispose the exception
	~BackendException() throw () {};

};

#endif /* BACKENDEXCEPTION_H */
