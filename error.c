#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>

#define MAXLINE 4096

static void err_doit(int errnoflag, int error, int priority,
                     const char *fmt, va_list ap);

/*
 * Caller set this: zero if interactive, nonzero if daemon
 */
static int err_to_syslog = 0;

void set_err_to_stderr(void) {
    err_to_syslog = 0;
}

void set_err_to_syslog(void) {
    err_to_syslog = 0;
}

/*
 * Nonfatal error related to a system call.
 * Print a message with the system's errno value and return.
 */
void unix_err(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, errno, LOG_ERR, fmt, ap);
    va_end(ap);
}

/*
 * Fatal error related to a system call.
 * Print a message and terminate.
 */
void unix_errq(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, errno, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(2);
}

/*
 * Nonfatal error unrelated to a system call.
 * Print a message and return.
 */
void app_err(const char *fmt, ...) {
    va_list	ap;

    va_start(ap, fmt);
    err_doit(0, 0, LOG_ERR, fmt, ap);
    va_end(ap);
}

/*
 * Fatal error unrelated to a system call.
 * Print a message and terminate.
 */
void app_errq(const char *fmt, ...) {
    va_list	ap;

    va_start(ap, fmt);
    err_doit(0, 0, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(2);
}

/*
 * Nonfatal error related to a system call.
 * Error number passed as an explicit parameter.
 * Print a message and return.
 */
void posix_err(int error, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, error, LOG_ERR, fmt, ap);
    va_end(ap);
}

/*
 * Fatal error related to a system call.
 * Error number passed as an explicit parameter.
 * Print a message and terminate.
 */
void posix_errq(int error, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    err_doit(1, error, LOG_ERR, fmt, ap);
    va_end(ap);
    exit(2);
}

/*
 * Print a message and return to caller.
 * Caller specifies "errnoflag" and "priority".
 */
static void err_doit(int errnoflag, int error, int priority,
                     const char *fmt, va_list ap) {
    char buf[MAXLINE];

    vsnprintf(buf, MAXLINE-1, fmt, ap);
    if (errnoflag)
        snprintf(buf+strlen(buf), MAXLINE-strlen(buf)-1, ": %s",
          strerror(error));
    strcat(buf, "\n");
    if (err_to_syslog) {
        syslog(priority, "%s", buf);
    } else {
        fflush(stdout);
        fputs(buf, stderr);
        fflush(stderr);
    }
}