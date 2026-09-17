#ifndef CANNEDBSD_FCNTL_H
#define CANNEDBSD_FCNTL_H

#include "cannedbsd/libc.h"

#define O_RDONLY CB_LIBC_O_RDONLY
#define O_WRONLY CB_LIBC_O_WRONLY
#define O_RDWR CB_LIBC_O_RDWR
#define O_ACCMODE CB_LIBC_O_ACCMODE
#define O_APPEND CB_LIBC_O_APPEND
#define O_CREAT CB_LIBC_O_CREAT
#define O_TRUNC CB_LIBC_O_TRUNC

/*
 * O_SYNC/O_RSYNC/O_NOFOLLOW: RM-01. Values chosen distinct from every
 * CB_LIBC_O_* bit cb_libc_open's translate_open_flags() recognizes, so
 * passing any of them is REJECTED with EINVAL rather than silently
 * accepted -- correct, since none of the three is actually implemented.
 * rm.c's only call site (rm -P) is outside the accepted matrix; this
 * rejection is exactly the "fails honestly, no stub that claims success"
 * treatment FCNTL-01 already established for cat -l.
 */
#define O_SYNC 0x1000
#define O_RSYNC 0x2000
#define O_NOFOLLOW 0x4000

#define open cb_libc_open

#endif
