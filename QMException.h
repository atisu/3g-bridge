#ifndef QMEXCEPTION_H
#define QMEXCEPTION_H

#include <exception>
#include <string>
#include <stdarg.h>


/**
 * Queue Manager Exception class.
 */
class QMException: public std::exception
{
public:
	/// Create a new exception
	QMException() { msg_text = ""; };

	/**
	 * Create a new exception using a string
	 * @param reason reason string
	 */
	QMException(const std::string &reason):msg_text(reason) {};

	/**
	 * Create a new exception using variable argument list
	 * @param fmt format string
	 * @param ap variable argument list
	 */
	QMException(const char *fmt, va_list ap);

	/**
	 * Create a new exception using printf-like format
	 * @param fmt format string
	 * @param ... fmt arguments
	 */
	QMException(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

	/// Dispose the exception
	~QMException() throw () {};

	/**
	 * Return the reason of the exception
	 * @return reason message
	 */
	const char *what() const throw ();

protected:
	/// The text of the reason
	std::string msg_text;
};

#endif /* QMEXCEPTION_H */
