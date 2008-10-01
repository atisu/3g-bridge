#ifndef WSINTERFACE_H
#define WSINTERFACE_H

#include <glib.h>
#include <string>

class WSInterface {
public:
	WSInterface(GKeyFile *config);
	~WSInterface();
	void run();
	void shutdown();
private:
	pid_t child;
	char *port;
	char *workdir;
	char *wsbinary;
};

#endif  /* WSINTERFACE_H */
