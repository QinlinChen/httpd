#ifndef _HTTP_UTILS_H
#define _HTTP_UTILS_H

#define LISTENQ  1024  /* Second argument to listen() */

int open_listenfd(const char *port);
int open_clientfd(char *hostname, char *port);

#endif