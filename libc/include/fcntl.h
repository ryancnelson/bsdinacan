#ifndef CANNEDBSD_FCNTL_H
#define CANNEDBSD_FCNTL_H

#include "cannedbsd/libc.h"

#include "sys/types.h"

#define O_RDONLY CB_LIBC_O_RDONLY
#define O_WRONLY CB_LIBC_O_WRONLY
#define O_RDWR CB_LIBC_O_RDWR
#define O_ACCMODE CB_LIBC_O_ACCMODE
#define O_NONBLOCK 0x0004
#define O_APPEND CB_LIBC_O_APPEND
#define O_CREAT CB_LIBC_O_CREAT
#define O_TRUNC CB_LIBC_O_TRUNC

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define F_GETLK  7
#define F_SETLK  8
#define F_SETLKW 9

#define F_RDLCK 1
#define F_UNLCK 2
#define F_WRLCK 3

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

#ifndef _STRUCT_FLOCK_DECLARED
struct flock {
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
    short l_type;
    short l_whence;
};
#define _STRUCT_FLOCK_DECLARED
#endif

#if defined(__GNUC__) || defined(__clang__)
int fcntl(int fd, int cmd, ...) __asm__("cb_libc_fcntl");
#else
int fcntl(int fd, int cmd, ...);
#define fcntl cb_libc_fcntl
#endif

#define open cb_libc_open

#endif
