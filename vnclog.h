#ifndef VNCLOG_H
#define VNCLOG_H


void vnclog_setLevel(int);
void vnclog_fatal(const char *fmt, ...);
void vnclog_fatal_errno(const char *fmt, ...);
void vnclog_error(const char *fmt, ...);
void vnclog_error_errno(const char *fmt, ...);
void vnclog_info(const char *fmt, ...);
void vnclog_debug(const char *fmt, ...);


#endif /* VNCLOG_H */
