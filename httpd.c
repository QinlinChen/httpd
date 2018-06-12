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

/* --------------------------------------------------
                    socket utilities
  --------------------------------------------------- */
typedef struct sockaddr SA;

#define	MAXLINE	 8192  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */
#define LISTENQ  1024  /* Second argument to listen() */

int open_listenfd(char *port);

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


/* --------------------------------------------------
                        httpd
  --------------------------------------------------- */

struct {
  char *port;
  char *site;
} G;

void show_usage(const char *name);
void app_error(const char *format, ...);
void normalize_dir(char *dir);
void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void get_filetype(char *filename, char *filetype);
void serve_static(int fd, char *filename, int filesize);
void serve_dynamic(int fd, char *filename, char *cgiargs);

int main(int argc, char *argv[]) {
  int opt;

  /* initialize global */
  G.port = NULL;
  G.site = NULL;

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
			default: show_usage(argv[0]); exit(1);
		}	
	} 

  /* handle illegal input */
  if (G.port == NULL) {
    show_usage(argv[0]);
    exit(1);
  }
  if (optind >= argc) {
    app_error("Expected argument after options\n");
    exit(1);
  }
  G.site = strdup(argv[optind]);
  normalize_dir(G.site);

  /* run */
  printf("Httpd is running. (port=%s, site=%s)\n", G.port, G.site);

  // int listenfd, connfd;
  // char hostname[MAXLINE], port[MAXLINE];
  // struct sockaddr_in clientaddr;
  // socklen_t clientlen;

  // listenfd = Open_listenfd(argv[1]);
  // while (1) {
  //   clientlen = sizeof(clientaddr);
  //   connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
  //   Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
  //   printf("Accepted connection from (%s, %s)\n", hostname, port);
  //   if (Fork() == 0) {
  //     Close(listenfd);
  //     doit(connfd);
  //     Close(connfd);
  //     exit(0);
  //   }
  //   Close(connfd);
  // }

  return 0;
}

int open_listenfd(char *port) 
{
    struct addrinfo hints, *listp, *p;
    int listenfd, rc, optval=1;

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
            continue;  /* Socket failed, try the next */

        /* Eliminates "Address already in use" error from bind */
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,    //line:netp:csapp:setsockopt
                   (const void *)&optval , sizeof(int));

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

void show_usage(const char *name) {
  printf("Usage: %s [-h, --help] -p, --port PORT DIR\n", name);
}

void app_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}

void normalize_dir(char *dir) {
  int len = strlen(dir);
  if (len != 1 && dir[len - 1] == '/')
    dir[len - 1] = '\0';
}

// void doit(int fd)
// {
//     int is_static;
//     char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
//     char filename[MAXLINE], cgiargs[MAXLINE];
//     struct stat sbuf;
//     rio_t rio;
    
//     Rio_readinitb(&rio, fd);
//     if (!Rio_readlineb(&rio, buf, MAXLINE))
//         return;
//     printf("%s", buf);
//     sscanf(buf, "%s %s %s", method, uri, version);
//     if (strcasecmp(method, "GET")) {
//         clienterror(fd, method, "501", "Not Implemented",
//                     "We haven't implemented this method");
//         return;
//     }
//     read_requesthdrs(&rio);
    
//     is_static = parse_uri(uri, filename, cgiargs);
//     if (stat(filename, &sbuf) < 0) {
//         clienterror(fd, filename, "404", "Not Found",
//                     "We couldn't find this file");
//         return;
//     }
    
//     if (is_static) {
//         if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
//             clienterror(fd, filename, "403", "Forbidden",
//                         "We couldn't read the file");
//             return;
//         }
//         serve_static(fd, filename, sbuf.st_size);
//     }
//     else {
//         if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
//             clienterror(fd, filename, "403", "Forbidden",
//                         "We couldn't run the CGI program");
//             return;
//         }
//         serve_dynamic(fd, filename, cgiargs);
//     }
// }

// void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
// {
//     char buf[MAXLINE], body[MAXBUF];

//     // Build the HTTP response body
//     sprintf(body, "<html><title>Error</title>");
//     sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
//     sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
//     sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
//     sprintf(body, "%s<hr><em>The Naive Web Server</em>\r\n", body);

//     // Print the HTTP response
//     sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
//     Rio_writen(fd, buf, strlen(buf));
//     printf("%s", buf);
//     sprintf(buf, "Content-type: text/html\r\n");
//     Rio_writen(fd, buf, strlen(buf));
//     printf("%s", buf);
//     sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
//     Rio_writen(fd, buf, strlen(buf));
//     printf("%s", buf);
//     Rio_writen(fd, body, strlen(body));
// }

// void read_requesthdrs(rio_t *rp)
// {
//     char buf[MAXLINE];
    
//     // skip request headers
//     Rio_readlineb(rp, buf, MAXLINE);
//     printf("%s", buf);
//     while (strcmp(buf, "\r\n")) {
//         Rio_readlineb(rp, buf, MAXLINE);
//         printf("%s", buf);
//     }
// }

// int parse_uri(char *uri, char *filename, char *cgiargs)
// {
//     char *ptr;
    
//     // static content
//     if (!strstr(uri, "cgi-bin")) {
//         strcpy(cgiargs, "");
//         strcpy(filename, workdir);
//         strcat(filename, uri);
//         if (uri[strlen(uri) - 1] == '/')
//             strcat(filename, "index.html");
//         return 1;
//     }
//     // dynamic content
//     else {
//         ptr = index(uri, '?');
//         if (ptr) {
//             strcpy(cgiargs, ptr + 1);
//             *ptr = '\0';
//         }
//         else
//             strcpy(cgiargs, "");
//         strcpy(filename, workdir);
//         strcat(filename, uri);
//         return 0;
//     }
// }

// void get_filetype(char *filename, char *filetype) 
// {
//     if (strstr(filename, ".html"))
// 	    strcpy(filetype, "text/html");
//     else if (strstr(filename, ".css"))
// 	    strcpy(filetype, "text/css");
//     else if (strstr(filename, ".gif"))
// 	    strcpy(filetype, "image/gif");
//     else if (strstr(filename, ".png"))
// 	    strcpy(filetype, "image/png");
//     else if (strstr(filename, ".jpg"))
//         strcpy(filetype, "image/jpeg");
//     else if (strstr(filename, ".jpeg"))
//         strcpy(filetype, "image/jpeg");
//     else if (strstr(filename, ".ico"))
//         strcpy(filetype, "image/ico");
//     else if (strstr(filename, ".js"))
//         strcpy(filetype, "application/js");
//     else if (strstr(filename, ".json"))
//         strcpy(filetype, "application/json");
//     else
//         strcpy(filetype, "text/plain");
// }  

// void serve_static(int fd, char *filename, int filesize) 
// {
//     int srcfd;
//     char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
//     // Send response headers to client 
//     get_filetype(filename, filetype);
//     sprintf(buf, "HTTP/1.0 200 OK\r\n");
//     sprintf(buf, "%sServer: Naive Web Server\r\n", buf);
//     sprintf(buf, "%sConnection: close\r\n", buf);
//     sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//     sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
//     Rio_writen(fd, buf, strlen(buf));
//     printf("Response headers:\n");
//     printf("%s", buf);

//     // Send response body to client
//     srcfd = Open(filename, O_RDONLY, 0);
//     srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
//     Close(srcfd);
//     Rio_writen(fd, srcp, filesize);
//     Munmap(srcp, filesize);
// }

// void serve_dynamic(int fd, char *filename, char *cgiargs) 
// {
//     char buf[MAXLINE], *emptylist[] = { NULL };

//     // Return first part of HTTP response
//     sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
//     Rio_writen(fd, buf, strlen(buf));
//     sprintf(buf, "Server: Naive Web Server\r\n");
//     Rio_writen(fd, buf, strlen(buf));
  
//     if (Fork() == 0) { 
//         // Child
// 	    // Real server would set all CGI vars here
// 	    setenv("QUERY_STRING", cgiargs, 1);
// 	    Dup2(fd, STDOUT_FILENO);                // Redirect stdout to client
// 	    Execve(filename, emptylist, environ);   // Run CGI program
//     }
//     Wait(NULL); // Parent waits for and reaps child
// }