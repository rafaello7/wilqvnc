#include "vnclog.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>


static int gLogLevel = 0;

void log_setLevel(int level)
{
    gLogLevel = level;
}

static void dolog(int level, const char *fmt, va_list args, int errNum)
{
    if( level <= gLogLevel ) {
        if( gLogLevel >= 2 ) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct tm *tmp = localtime(&tv.tv_sec);
            printf("%02d:%02d:%02d.%03ld ", tmp->tm_hour, tmp->tm_min,
                    tmp->tm_sec, tv.tv_usec / 1000);
        }
        if( level < 0 )
            printf("error: ");
        else if( level == 0 )
            printf("warn: ");
        vfprintf(stdout, fmt, args);
        if( errNum != 0 )
            printf(": %s", strerror(errNum));
        printf("\n");
        fflush(stdout);
    }
    if( level < -1 )
        exit(1);
}

void log_fatal(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dolog(-2, fmt, args, 0);
    va_end(args);
}

void log_fatal_errno(const char *fmt, ...)
{
    va_list args;
    int errNum = errno;

    va_start(args, fmt);
    dolog(-2, fmt, args, errNum);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dolog(-1, fmt, args, 0);
    va_end(args);
}

void log_error_errno(const char *fmt, ...)
{
    va_list args;
    int errNum = errno;

    va_start(args, fmt);
    dolog(-1, fmt, args, errNum);
    va_end(args);
}

void log_warn(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dolog(0, fmt, args, 0);
    va_end(args);
}

void log_info(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dolog(1, fmt, args, 0);
    va_end(args);
}

void log_debug(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dolog(2, fmt, args, 0);
    va_end(args);
}

