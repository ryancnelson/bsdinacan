#ifndef CANNEDBSD_ERR_H
#define CANNEDBSD_ERR_H

#include "cannedbsd/libc.h"
#include "sys/cdefs.h"

void cb_libc_errx(int eval, const char *fmt, ...) __dead;
#define errx cb_libc_errx

#endif
