#ifndef CANNEDBSD_STDIO_H
#define CANNEDBSD_STDIO_H

#include "cannedbsd/libc.h"

#define EOF (-1)

int cb_libc_puts(const char *text);

#define puts cb_libc_puts

#endif
