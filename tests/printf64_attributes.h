/* Compile-only audit of the pinned PRI64 consumers through the veneer.
   Attributes describe argument types, not support for every host format. */
#include "cannedbsd/libc.h"
int cb_libc_printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int cb_libc_fprintf(struct cb_libc_file *, const char *, ...)
    __attribute__((format(printf, 2, 3)));
int cb_libc_snprintf(char *, size_t, const char *, ...)
    __attribute__((format(printf, 3, 4)));
