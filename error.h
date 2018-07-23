#ifndef _ERROR_H
#define _ERROR_H

void set_err_to_stderr(void);
void set_err_to_syslog(void);

void unix_err(const char *fmt, ...);
void unix_errq(const char *fmt, ...) __attribute__((noreturn));
void app_err(const char *fmt, ...);
void app_errq(const char *fmt, ...) __attribute__((noreturn));
void posix_err(int error, const char *fmt, ...);
void posix_errq(int error, const char *fmt, ...) __attribute__((noreturn));

#endif