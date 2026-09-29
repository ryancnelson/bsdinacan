#ifndef CANNEDBSD_TIME_H
#define CANNEDBSD_TIME_H

#include "cannedbsd/libc.h"

/*
 * LS-02: private shim, matching the established pattern for limits.h/
 * sys/types.h/stdio.h -- the system's own <time.h> (reachable
 * transitively via <stddef.h>/<stdint.h>) declares a conflicting
 * time_t/struct tm for this project's own 32-bit epoch-seconds model,
 * so it must never become visible to pinned source. time_t is spelled
 * uint32_t, matching st_atimespec.tv_sec's existing type in
 * cannedbsd/libc.h's struct stat.
 */
#ifndef _TIME_T_DECLARED
typedef uint32_t time_t;
#define _TIME_T_DECLARED
#endif

uint32_t cb_libc_time(uint32_t *out);
#define time cb_libc_time

char *cb_libc_ctime(const uint32_t *timer);
#define ctime cb_libc_ctime

#endif
