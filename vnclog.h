#ifndef VNCLOG_H
#define VNCLOG_H


void log_setLevel(int);
void log_fatal(const char *fmt, ...);
void log_fatal_errno(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_error_errno(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);


#endif /* VNCLOG_H */
