#ifndef CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H
#define CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H

/*
 * Import-only shim: the pinned NetBSD dirname.c uses only the MIN() macro
 * from <sys/param.h>. Do not expose this directory as cannedBSD's public
 * libc include path -- the real sys/param.h exposes page sizes, howmany(),
 * and other surface this project does not implement.
 */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#endif
