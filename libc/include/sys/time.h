#ifndef CANNEDBSD_SYS_TIME_H
#define CANNEDBSD_SYS_TIME_H

#include "sys/types.h"

#ifndef _STRUCT_TIMEVAL_DECLARED
struct timeval {
    time_t tv_sec;
    long tv_usec;
};
#define _STRUCT_TIMEVAL_DECLARED
#endif

#ifndef _STRUCT_TIMESPEC_DECLARED
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#define _STRUCT_TIMESPEC_DECLARED
#endif

int utimes(const char *path, const struct timeval *times);
int futimes(int descriptor, const struct timeval *times);
int lutimens(const char *path, const struct timespec times[2]);

#define utimes cb_libc_utimes
#define futimes cb_libc_futimes
#define lutimens cb_libc_lutimens

#endif
