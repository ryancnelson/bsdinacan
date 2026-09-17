#ifndef CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H
#define CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H

/*
 * Import-only shim: the pinned NetBSD dirname.c uses only the MIN() macro
 * from <sys/param.h>. Do not expose this directory as cannedBSD's public
 * libc include path -- the real sys/param.h exposes page sizes, howmany(),
 * and other surface this project does not implement.
 */
#include "sys/types.h"
#include "signal.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef MAXBSIZE
#define MAXBSIZE 65536
#endif

#endif
