#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "BackendException.h"

#include <stdarg.h>

BackendException::BackendException(const char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	msg_text = msg;
	free(msg);
}
