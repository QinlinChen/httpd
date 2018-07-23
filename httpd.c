#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "rio.h"
#include "error.h"
#include "http-utils.h"

#ifdef LOG
    #define log(format, ...) \
        do { \
            printf(format, ## __VA_ARGS__); \
        } while (0)
#else 
    #define log(format, ...) ((void)0)
#endif

#define	MAXLINE	 4096  /* Max text line length */
#define MAXBUF   8192  /* Max I/O buffer size */
#define MAXEVENTS 1024

static const char *httpd_name = "The Naive HTTP Server";
static sig_atomic_t termflag = 0;
static char *site;
static int listenfd;
static int connfd;

void show_usage(const char *name);
void normalize_dir(char *dir);
void run(int listenfd);
int doit(int connfd);
void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename);
void get_filetype(char *filename, char *filetype);
void serve_static(int fd, char *filename, int filesize);
void release_resource();

typedef void (*sigfunc_t)(int);
sigfunc_t signal_intr(int signo, sigfunc_t func);
void sigint_handle(int signum);

int main(int argc, char *argv[]) {
    int opt;
    char *port;
    
    /* Initialize variables. */
    port = NULL;
    site = NULL;
    listenfd = -1;
    connfd = -1;

    /* Initialize signal handle. */
    if (signal_intr(SIGINT, sigint_handle) == SIG_ERR)
        unix_errq("signal_intr error");

    /* Process args. */
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
        case 'p': port = optarg; break;
        case 'h':
        /* 0, ?, etc. */
        default: show_usage(argv[0]);
        }
    }

    /* Handle illegal input. */
    if (port == NULL)
        show_usage(argv[0]);
    if (optind >= argc)
        app_errq("Expected argument after options");
    site = strdup(argv[optind]);
    normalize_dir(site);

    /* open socket and listen */
    if ((listenfd = open_listenfd(port)) < 0)
        unix_errq("open_listenfd error");

    /* Run! */
    printf("Httpd is running. (port=%s, site=%s)\n", port, site);
    run(listenfd);
    
    close(listenfd);
    printf("\nHttpd is shut down\n");
    return 0;
}

sigfunc_t signal_intr(int signo, sigfunc_t func) {
	struct sigaction act, oact;

	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
#ifdef	SA_INTERRUPT
	act.sa_flags |= SA_INTERRUPT;
#endif
	if (sigaction(signo, &act, &oact) < 0)
		return SIG_ERR;
	return oact.sa_handler;
}

void sigint_handle(int signum) {
    assert(signum == SIGINT);
    termflag = 1;
}

void show_usage(const char *name) {
    printf("Usage: %s [-h, --help] <-p, --port PORT> DIR\n", name);
    exit(1);
}

void normalize_dir(char *dir) {
    int len = strlen(dir);
    if (len != 1 && dir[len - 1] == '/')
        dir[len - 1] = '\0';
}

void run(int listenfd) {
    int rc, epollfd, nfds, i;
    char cli_hostname[MAXLINE], cli_port[MAXLINE];
    struct sockaddr_in cli_addr;
    socklen_t cli_len;
    struct epoll_event ev, events[MAXEVENTS];

    if ((epollfd = epoll_create1(0)) == -1)
        unix_errq("epoll_create1 error");
    
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev) == -1)
        unix_errq("epoll_ctl error");
    
    while (!termflag) {
        if ((nfds = epoll_wait(epollfd, events, MAXEVENTS, -1)) == -1) {
            if (errno == EINTR) {
                printf("interrupted from epoll wait\n");
                break;
            }
            unix_errq("epoll_wait error");
        }
        
        for (i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listenfd) {
                cli_len = sizeof(cli_addr);
                if ((connfd = accept(listenfd, 
                              (struct sockaddr *)&cli_addr, &cli_len)) < 0)
                    unix_errq("accept error");
                
                if ((rc = getnameinfo((struct sockaddr *)&cli_addr, cli_len, 
                          cli_hostname, MAXLINE, cli_port, MAXLINE, 0)) != 0)
                    unix_errq("getnameinfo error: %s", gai_strerror(rc));

                log("Accepted connection from (%s, %s)\n", cli_hostname, cli_port);
                printf("connfd: %d\n\n", connfd);
                ev.events = EPOLLIN;
                ev.data.fd = connfd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) == -1)
                    unix_errq("epoll_ctl error");
            }
            else if (events[i].events & EPOLLIN) {
                doit(events[i].data.fd);
                printf("close connfd %d\n\n", events[i].data.fd);
                close(events[i].data.fd);
            }
            else {  /* Should not reach here. */
                assert(0);
            }
        }
    }
    
    if (close(epollfd) != 0)
        unix_errq("epoll close error");
}

int doit(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE];
    struct stat sbuf;
    rio_t rio;
    ssize_t nread;

    rio_readinitb(&rio, connfd);
    /* Read method, uri, version. */
    if ((nread = rio_readlineb(&rio, buf, MAXLINE)) <= 0) {
        if (nread == 0)
            return -1;
        unix_errq("rio_readlineb error");
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    log("%s", buf);

    /* Check method. */
    if (strcasecmp(method, "GET")) {    /* We only support GET method*/
        clienterror(connfd, method, "501", "Not Implemented",
                    "We haven't implemented this method");
        return 0;
    }

    /* Read request headers and discard them. */
    read_requesthdrs(&rio);

    /* Parse uri to local filename. */
    if (parse_uri(uri, filename) != 0) {
        clienterror(connfd, filename, "404", "Not Found",
                    "We couldn't find this file");
        return 0;
    }

    /* Check existence. */
    if (stat(filename, &sbuf) < 0) {
        clienterror(connfd, filename, "404", "Not Found",
                    "We couldn't find this file");
        return 0;
    }
    
    /* Check permission. */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
        clienterror(connfd, filename, "403", "Forbidden",
                    "We couldn't read the file");
        return 0;
    }

    serve_static(connfd, filename, sbuf.st_size);
    return 0;
}

void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    size_t body_len;

    /* Build the HTTP response body. */
    sprintf(body, "<html><title>Error</title>");
    sprintf(body, "%s<body bgcolor=ffffff>\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>%s</em>\r\n", body, httpd_name);
    body_len = strlen(body);

    /* Send the HTTP response. */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf, "%sContent-type: text/html\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)body_len);
    if (rio_writen(fd, buf, strlen(buf)) < 0)
        unix_errq("rio_writen error");
    if (rio_writen(fd, body, body_len) < 0)
        unix_errq("rio_writen error");
    log("%s", buf);
}

void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    /* Skip request headers. */
    if (rio_readlineb(rp, buf, MAXLINE) < 0)
        unix_errq("rio_readlineb error");
    log("%s", buf);
    while (strcmp(buf, "\r\n")) {
        if (rio_readlineb(rp, buf, MAXLINE) < 0)
            unix_errq("rio_readlineb error");
        log("%s", buf);
    }
}

int parse_uri(char *uri, char *filename) {
    struct stat sbuf;

    if (strcmp(site, "/") == 0)
        strcpy(filename, "");
    else
        strcpy(filename, site);
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

    /* Send response headers to client. */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: %s\r\n", buf, httpd_name);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    if (rio_writen(fd, buf, strlen(buf)) < 0)
        unix_errq("rio_writen error");
    log("Response headers:\n%s", buf);

    /* Send response body to client. */
    if ((srcfd = open(filename, O_RDONLY, 0)) < 0)
        unix_errq("open error");
    if ((srcp = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0)) == MAP_FAILED)
        unix_errq("mmap error");
    if (close(srcfd) != 0)
        unix_errq("close error");
    if (rio_writen(fd, srcp, filesize) < 0)
        unix_errq("rio_writen error");
    if (munmap(srcp, filesize) != 0)
        unix_errq("munmap error");
}

void release_resource() {
    close(connfd);
    close(listenfd);
    free(site);
}