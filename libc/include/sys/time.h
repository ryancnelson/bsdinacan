#ifndef CANNEDBSD_SYS_TIME_H
#define CANNEDBSD_SYS_TIME_H

#include "sys/types.h"

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int utimes(const char *path, const struct timeval *times);
int futimes(int descriptor, const struct timeval *times);

#define utimes cb_libc_utimes
#define futimes cb_libc_futimes

#endif
