#ifndef CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H
#define CANNEDBSD_NETBSD_IMPORT_SYS_PARAM_H

/*
 * Import-only shim: the pinned NetBSD dirname.c uses only the MIN() macro
 * from <sys/param.h>. Do not expose this directory as cannedBSD's public
 * libc include path -- the real sys/param.h exposes far more surface than
 * this project implements.
 */
#include "cannedbsd/abi.h"
#include "sys/types.h"
#include "signal.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* LS-02: pinned ls.c uses howmany() for block-count arithmetic, and
   major()/minor() to decode st_rdev for character/block device display
   -- reachable only via S_ISCHR/S_ISBLK, which RAMFS never produces (no
   device-node primitive), so these compile for a path that cannot
   execute here, same shape as FTS_W/S_ISWHT elsewhere in this project. */
#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif
#define major(x) ((int)(((x) >> 8) & 0xff))
#define minor(x) ((int)((x) & 0xff))

#ifndef MAXPATHLEN
#define MAXPATHLEN CB_PATH_MAX
#endif

/* Single source of truth: PATH_MAX derives from CB_PATH_MAX across limits.h and sys/param.h */
#ifndef PATH_MAX
#define PATH_MAX CB_PATH_MAX
#endif

#ifndef MAXBSIZE
#define MAXBSIZE 65536
#endif

#endif
