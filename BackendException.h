#ifndef BACKENDEXCEPTION_H
#define BACKENDEXCEPTION_H

#include <exception>

class BackendException: public exception
{
public:
	/* Create a new exception */
	BackendException(const string reason)
	{
		this->msg_text = reason;
	}

	/* Dispose the exception */
	~BackendException() throw () {};

	/* Return the reason of the exception */
	inline const string reason()
	{
		return this->msg_text;
	}

private:
	/* The text of the reason */
	string msg_text;
};

#endif /* BACKENDEXCEPTION */
