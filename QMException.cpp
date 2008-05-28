#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "QMException.h"

#include <stdarg.h>

QMException::QMException(const char *fmt, va_list ap)
{
	char *msg;

	vasprintf(&msg, fmt, ap);
	msg_text = msg;
	free(msg);
}

QMException::QMException(const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	msg_text = msg;
	free(msg);
}

const char *QMException::what() const throw()
{
	return msg_text.c_str();
}
