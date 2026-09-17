#ifndef CANNEDBSD_NETBSD_SYS_TYPES_H
#define CANNEDBSD_NETBSD_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Import-only shim: the pinned NetBSD memset.c uses the legacy BSD u_char/
   u_int/u_long aliases internally. Do not expose this directory as
   cannedBSD's public libc include path -- the real sys/types.h exposes a
   much larger surface this project does not implement. */
typedef unsigned char u_char;
typedef unsigned int u_int;
typedef unsigned long u_long;
/* RM-01: rm.c's -P macro (outside the accepted matrix) uses this legacy
   BSD alias internally. */
typedef uint32_t u_int32_t;

#endif
