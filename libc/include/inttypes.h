#ifndef CANNEDBSD_INTTYPES_H
#define CANNEDBSD_INTTYPES_H

#include "cannedbsd/libc.h"

/* intmax_t/INTMAX_MAX/INTMAX_MIN are pure compile-time type/constant
 * definitions with no callable API surface, so this project's established
 * fundamental-header exemption (stdbool.h, stddef.h, stdint.h) applies:
 * borrow them from the host's own <stdint.h> rather than reimplementing
 * them. strtoimax itself is the actual host-leak risk this header exists
 * to close -- declared and mapped to the private veneer below, never left
 * to resolve against any host declaration. */
#include <stdint.h>

intmax_t cb_libc_strtoimax(const char *restrict nptr, char **restrict endptr,
                          int base);
#define strtoimax cb_libc_strtoimax

#endif
