#ifndef CANNEDBSD_STDLIB_H
#define CANNEDBSD_STDLIB_H

#include "cannedbsd/libc.h"
#include "sys/cdefs.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define malloc cb_libc_malloc
#define calloc cb_libc_calloc
#define realloc cb_libc_realloc
#define free cb_libc_free

void cb_libc_exit(int status) __dead;
#define exit cb_libc_exit

const char *cb_libc_getprogname(void);
void cb_libc_setprogname(const char *name);
#define getprogname cb_libc_getprogname
#define setprogname cb_libc_setprogname

#define arc4random cb_libc_arc4random

long cb_libc_strtol(const char *nptr, char **endptr, int base);
#define strtol cb_libc_strtol

const char *cb_libc_getenv(const char *name);
#define getenv cb_libc_getenv

int cb_libc_atoi(const char *nptr);
#define atoi cb_libc_atoi

/* LS-02: single, immutable C locale (LOCALE-01) -- genuinely 1 here,
   not an approximation. */
#define MB_CUR_MAX 1

#endif
