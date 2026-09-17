#ifndef CANNEDBSD_SYS_TYPES_H
#define CANNEDBSD_SYS_TYPES_H

#include "cannedbsd/abi.h"
#include <stddef.h>
#include <stdint.h>

#ifndef _INO_T_DECLARED
typedef uint64_t ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
typedef uint32_t mode_t;
#define _MODE_T_DECLARED
#endif

#ifndef _NLINK_T_DECLARED
typedef uint32_t nlink_t;
#define _NLINK_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef uint32_t uid_t;
#define _UID_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef uint32_t gid_t;
#define _GID_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef cb_off_t off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef cb_ssize_t ssize_t;
#define _SSIZE_T_DECLARED
#endif

#ifndef _BLKSIZE_T_DECLARED
typedef int32_t blksize_t;
#define _BLKSIZE_T_DECLARED
#endif

#ifndef _BLKCNT_T_DECLARED
typedef int64_t blkcnt_t;
#define _BLKCNT_T_DECLARED
#endif

#ifndef _DEV_T_DECLARED
typedef uint32_t dev_t;
#define _DEV_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
typedef int32_t pid_t;
#define _PID_T_DECLARED
#endif

#ifndef _SIG_ATOMIC_T_DECLARED
typedef int sig_atomic_t;
#define _SIG_ATOMIC_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef uint32_t time_t;
#define _TIME_T_DECLARED
#endif

#endif
