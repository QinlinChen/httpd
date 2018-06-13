#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

/* --------------------------------------------------
                    socket utilities
  --------------------------------------------------- */
typedef struct sockaddr SA;

#define	MAXLINE	 8192  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */
#define LISTENQ  1024  /* Second argument to listen() */

int open_listenfd(const char *port);

/*  
 * open_listenfd - Open and return a listening socket on port. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns: 
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
int open_listenfd(const char *port)  {
  struct addrinfo hints, *listp, *p;
  int listenfd, rc, optval = 1;

  /* Get a list of potential server addresses */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
  hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
  hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
  if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0) {
    fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port, gai_strerror(rc));
    return -2;
  }

  /* Walk the list for one that we can bind to */
  for (p = listp; p; p = p->ai_next) {
    /* Create a socket descriptor */
    if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */

    /* Eliminates "Address already in use" error from bind */
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /* Bind the descriptor to the address */
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
      break; /* Success */
    if (close(listenfd) < 0) { /* Bind failed, try the next */
      fprintf(stderr, "open_listenfd close failed: %s\n", strerror(errno));
      return -1;
    }
  }

  /* Clean up */
  freeaddrinfo(listp);
  if (!p) /* No address worked */
    return -1;

  /* Make it a listening socket ready to accept connection requests */
  if (listen(listenfd, LISTENQ) < 0) {
    close(listenfd);
    return -1;
  }
  return listenfd;
}

/* --------------------------------------------------
                      rio package
  --------------------------------------------------- */

#define RIO_BUFSIZE 8192
typedef struct {
  int rio_fd;                /* Descriptor for this internal buf */
  int rio_cnt;               /* Unread bytes in internal buf */
  char *rio_bufptr;          /* Next unread byte in internal buf */
  char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void rio_readinitb(rio_t *rp, int fd); 
ssize_t	rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t	rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

/*
 * rio_readn - Robustly read n bytes (unbuffered)
 */
ssize_t rio_readn(int fd, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nread = read(fd, bufp, nleft)) < 0) {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nread = 0;        /* and call read() again */
      else
        return -1; /* errno set by read() */
    }
    else if (nread == 0)
      break; /* EOF */
    nleft -= nread;
    bufp += nread;
  }
  return (n - nleft); /* Return >= 0 */
}

/*
 * rio_writen - Robustly write n bytes (unbuffered)
 */
ssize_t rio_writen(int fd, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nwritten = 0;     /* and call write() again */
      else
        return -1; /* errno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}

/* 
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n) {
  int cnt;

  while (rp->rio_cnt <= 0) { /* Refill if buf is empty */
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0) {
      if (errno != EINTR) /* Interrupted by sig handler return */
        return -1;
    }
    else if (rp->rio_cnt == 0) /* EOF */
      return 0;
    else
      rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
  }

  /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
  cnt = n;
  if (rp->rio_cnt < n)
    cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

/*
 * rio_readinitb - Associate a descriptor with a read buffer and reset buffer
 */
void rio_readinitb(rio_t *rp, int fd) {
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

/*
 * rio_readnb - Robustly read n bytes (buffered)
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nread = rio_read(rp, bufp, nleft)) < 0)
      return -1; /* errno set by read() */
    else if (nread == 0)
      break; /* EOF */
    nleft -= nread;
    bufp += nread;
  }
  return (n - nleft); /* return >= 0 */
}

/* 
 * rio_readlineb - Robustly read a text line (buffered)
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
  int n, rc;
  char c, *bufp = usrbuf;

  for (n = 1; n < maxlen; n++) {
    if ((rc = rio_read(rp, &c, 1)) == 1) {
      *bufp++ = c;
      if (c == '\n') {
        n++;
        break;
      }
    }
    else if (rc == 0) {
      if (n == 1)
        return 0; /* EOF, no data read */
      else
        break; /* EOF, some data was read */
    }
    else
      return -1; /* Error */
  }
  *bufp = 0;
  return n - 1;
}

/* --------------------------------------------------
                      error handle
  --------------------------------------------------- */

void unix_error(const char *msg);
void posix_error(int code, const char *msg);
void gai_error(int code, const char *msg);
void app_error(const char *format, ...);

 /* Unix-style error */
void unix_error(const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/* Posix-style error */
void posix_error(int code, const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(code));
  exit(1);
}

/* Getaddrinfo-style error */
void gai_error(int code, const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, gai_strerror(code));
  exit(1);
}

/* Application error */
void app_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  exit(1);
}

/* --------------------------------------------------
                        wrappers
  --------------------------------------------------- */

int Open_listenfd(const char *port) {
  int rc;

  if ((rc = open_listenfd(port)) < 0)
    unix_error("Open_listenfd error");
  return rc;
}

ssize_t Rio_readn(int fd, void *ptr, size_t nbytes) {
  ssize_t n;

  if ((n = rio_readn(fd, ptr, nbytes)) < 0)
    unix_error("Rio_readn error");
  return n;
}

void Rio_writen(int fd, void *usrbuf, size_t n) {
  if (rio_writen(fd, usrbuf, n) != n)
    unix_error("Rio_writen error");
}

void Rio_readinitb(rio_t *rp, int fd) {
  rio_readinitb(rp, fd);
}

ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
  ssize_t rc;

  if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    unix_error("Rio_readnb error");
  return rc;
}

ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
  ssize_t rc;

  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    unix_error("Rio_readlineb error");
  return rc;
}

int Accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
  int rc;

  if ((rc = accept(s, addr, addrlen)) < 0)
    unix_error("Accept error");
  return rc;
}

int Open(const char *pathname, int flags, mode_t mode) {
  int rc;

  if ((rc = open(pathname, flags, mode)) < 0)
    unix_error("Open error");
  return rc;
}

void Close(int fd) {
  int rc;

  if ((rc = close(fd)) < 0)
    unix_error("Close error");
}

void Getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, 
                 size_t hostlen, char *serv, size_t servlen, int flags) {
  int rc;

  if ((rc = getnameinfo(sa, salen, host, hostlen, serv,
                        servlen, flags)) != 0)
    gai_error(rc, "Getnameinfo error");
}

void *Mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
  void *ptr;

  if ((ptr = mmap(addr, len, prot, flags, fd, offset)) == ((void *)-1))
    unix_error("mmap error");
  return (ptr);
}

void Munmap(void *start, size_t length) {
  if (munmap(start, length) < 0)
    unix_error("munmap error");
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
  G.listenfd = 0;
  G.connfd = -1;
  
  /* initialize signal handle */
  signal(SIGINT, sigint_handle);

  /* process args */
  while (1) {
		static const char *optstring = "p:h";
		static const struct option longopts[] = {
			{ "port", required_argument, NULL, 'p' },
			{ "help", no_argument, NULL, 'h' },
			{ NULL, no_argument, NULL, 0 }
		};

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
    app_error("Expected argument after options\n");
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
  Close(G.connfd);
  Close(G.listenfd);
  free(G.port);
  free(G.site);
}