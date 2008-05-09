#ifndef BACKENDEXCEPTION_H
#define BACKENDEXCEPTION_H

#include <exception>

class BackendException: public exception
{
public:
	/* Create a new exception */
	BackendException() { msg_text = ""; };
	BackendException(const string reason):msg_text(reason) {};

	/* Dispose the exception */
	~BackendException() throw () {};

	/* Return the reason of the exception */
	inline const string reason()
	{
		return msg_text;
	}

	/* Set the reason of the exception */
	inline void set_reason(const string reason)
	{
		msg_text = reason;
	}

private:
	/* The text of the reason */
	string msg_text;
};

#endif /* BACKENDEXCEPTION */
