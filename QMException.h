#ifndef QMEXCEPTION_H
#define QMEXCEPTION_H

#include <exception>
#include <string>
#include <stdarg.h>

class QMException: public std::exception
{
public:
	/* Create a new exception */
	QMException() { msg_text = ""; };
	QMException(const std::string &reason):msg_text(reason) {};
	QMException(const char *fmt, va_list ap);
	QMException(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

	/* Dispose the exception */
	~QMException() throw () {};

	/* Return the reason of the exception */
	const char *what() const throw ();

protected:
	/* The text of the reason */
	std::string msg_text;
};

#endif /* QMEXCEPTION_H */
