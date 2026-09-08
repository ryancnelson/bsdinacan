#ifndef CANNEDBSD_LIBGEN_H
#define CANNEDBSD_LIBGEN_H

#include "cannedbsd/libc.h"

char *cb_libc_dirname(char *path);
#define dirname cb_libc_dirname

#endif
