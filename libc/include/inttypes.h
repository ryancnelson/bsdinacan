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

/* Format strings must match the caller's typedef, not merely its width.
   The build probes the target's stdint.h with compatible declarations;
   the formatter already consumes long for l and long long for ll.
   Do not import the host inttypes.h's callable API to obtain these macros. */
#undef PRIu64
#undef PRId64
#if CB_PRIu64_KIND == 1
#define PRIu64 "lu"
#elif CB_PRIu64_KIND == 2
#define PRIu64 "llu"
#else
#error "Build must probe the actual uint64_t type"
#endif
#if CB_PRId64_KIND == 1
#define PRId64 "ld"
#elif CB_PRId64_KIND == 2
#define PRId64 "lld"
#else
#error "Build must probe the actual int64_t type"
#endif

#endif
