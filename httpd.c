#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

#include "rio.h"
#include "error.h"
#include "http-utils.h"

#define	MAXLINE	 8192  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */

/* --------------------------------------------------
                        wrappers
  --------------------------------------------------- */

int Open_listenfd(const char *port) {
  int rc;

  if ((rc = open_listenfd(port)) < 0)
    unix_errq("Open_listenfd error");
  return rc;
}

ssize_t Rio_readn(int fd, void *ptr, size_t nbytes) {
  ssize_t n;

  if ((n = rio_readn(fd, ptr, nbytes)) < 0)
    unix_errq("Rio_readn error");
  return n;
}

void Rio_writen(int fd, void *usrbuf, size_t n) {
  if (rio_writen(fd, usrbuf, n) != n)
    unix_errq("Rio_writen error");
}

void Rio_readinitb(rio_t *rp, int fd) {
  rio_readinitb(rp, fd);
}

ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
  ssize_t rc;

  if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    unix_errq("Rio_readnb error");
  return rc;
}

ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
  ssize_t rc;

  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    unix_errq("Rio_readlineb error");
  return rc;
}

int Accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
  int rc;

  if ((rc = accept(s, addr, addrlen)) < 0)
    unix_errq("Accept error");
  return rc;
}

int Open(const char *pathname, int flags, mode_t mode) {
  int rc;

  if ((rc = open(pathname, flags, mode)) < 0)
    unix_errq("Open error");
  return rc;
}

void Close(int fd) {
  int rc;

  if ((rc = close(fd)) < 0)
    unix_errq("Close error");
}

void Getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, 
                 size_t hostlen, char *serv, size_t servlen, int flags) {
  int rc;

  if ((rc = getnameinfo(sa, salen, host, hostlen, serv,
                        servlen, flags)) != 0)
    unix_errq("Getnameinfo error: %s", gai_strerror(rc));
}

void *Mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
  void *ptr;

  if ((ptr = mmap(addr, len, prot, flags, fd, offset)) == ((void *)-1))
    unix_errq("mmap error");
  return (ptr);
}

void Munmap(void *start, size_t length) {
  if (munmap(start, length) < 0)
    unix_errq("munmap error");
}

/* --------------------------------------------------
                        httpd
  --------------------------------------------------- */

#ifdef LOG
  #define log(format, ...) \
    do { \
      printf(format, ## __VA_ARGS__); \
    } while (0)
#else 
  #define log(format, ...) ((void)0)
#endif

struct {
  char *port;
  char *site;
  int listenfd;
  int connfd;
} G;

void show_usage(const char *name);
void normalize_dir(char *dir);
void doit(int fd);
void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename);
void get_filetype(char *filename, char *filetype);
void serve_static(int fd, char *filename, int filesize);
void release_resource();

void sigint_handle(int signum) {
  assert(signum == SIGINT);
  release_resource();
  printf("Httpd is shut down\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  int opt;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_in clientaddr;
  socklen_t clientlen;

  /* initialize global */
  G.port = NULL;
  G.site = NULL;
  G.listenfd = -1;
  G.connfd = -1;

  /* initialize signal handle */
  signal(SIGINT, sigint_handle);

  /* process args */
  while (1) {
    static const char *optstring = "p:h";
    static const struct option longopts[] = {
        {"port", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {NULL, no_argument, NULL, 0}};

    opt = getopt_long(argc, argv, optstring, longopts, NULL);
    if (opt == -1)
      break;

    switch (opt) {
    case 'p': G.port = strdup(optarg); break;
    case 'h':
    /* 0, ?, etc. */
    default: show_usage(argv[0]);
    }
  }

  /* handle illegal input */
  if (G.port == NULL)
    show_usage(argv[0]);
  if (optind >= argc)
    app_errq("Expected argument after options");
  G.site = strdup(argv[optind]);
  normalize_dir(G.site);

  /* run */
  printf("Httpd is starting. (port=%s, site=%s)\n", G.port, G.site);
  G.listenfd = Open_listenfd(G.port);
  while (1) {
    clientlen = sizeof(clientaddr);
    G.connfd = Accept(G.listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    log("Accepted connection from (%s, %s)\n", hostname, port);
    doit(G.connfd);
    Close(G.connfd);
  }

  return 0;
}

void show_usage(const char *name) {
  printf("Usage: %s [-h, --help] -p, --port PORT DIR\n", name);
  exit(1);
}

void normalize_dir(char *dir) {
  int len = strlen(dir);
  if (len != 1 && dir[len - 1] == '/')
    dir[len - 1] = '\0';
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE];
  struct stat sbuf;
  rio_t rio;

  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE))
    return;
  log("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "We haven't implemented this method");
    return;
  }
  read_requesthdrs(&rio);

  if (parse_uri(uri, filename) != 0) {
    clienterror(fd, filename, "404", "Not Found",
                "We couldn't find this file");
    return;
  }

  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not Found",
                "We couldn't find this file");
    return;
  }
    
  if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
    clienterror(fd, filename, "403", "Forbidden",
                "We couldn't read the file");
    return;
  }
  serve_static(fd, filename, sbuf.st_size);
}

void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // Build the HTTP response body
  sprintf(body, "<html><title>Error</title>");
  sprintf(body, "%s<body bgcolor=ffffff>\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Naive Http Server</em>\r\n", body);

  // Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  log("%s", buf);
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  log("%s", buf);
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  log("%s", buf);
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  // skip request headers
  Rio_readlineb(rp, buf, MAXLINE);
  log("%s", buf);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    log("%s", buf);
  }
}

int parse_uri(char *uri, char *filename) {
  struct stat sbuf;

  if (strcmp(G.site, "/") == 0)
    strcpy(filename, "");
  else
    strcpy(filename, G.site);
  strcat(filename, uri);
  
  if (uri[strlen(uri) - 1] == '/')
    strcat(filename, "index.html");
  else {
    if (stat(filename, &sbuf) < 0)
      return -1;
    if (S_ISDIR(sbuf.st_mode))
      strcat(filename, "/index.html");
  }
  return 0;
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".css"))
    strcpy(filetype, "text/css");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".jpeg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".ico"))
    strcpy(filetype, "image/ico");
  else if (strstr(filename, ".js"))
    strcpy(filetype, "application/js");
  else if (strstr(filename, ".json"))
    strcpy(filetype, "application/json");
  else
    strcpy(filetype, "text/plain");
}

void serve_static(int fd, char *filename, int filesize) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // Send response headers to client
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Naive Http Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  log("Response headers:\n");
  log("%s", buf);

  // Send response body to client
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

void release_resource() {
  close(G.connfd);
  close(G.listenfd);
  free(G.port);
  free(G.site);
}