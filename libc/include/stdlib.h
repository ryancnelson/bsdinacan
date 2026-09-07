#ifndef CANNEDBSD_STDLIB_H
#define CANNEDBSD_STDLIB_H

#include "cannedbsd/libc.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define malloc cb_libc_malloc
#define free cb_libc_free

#endif
