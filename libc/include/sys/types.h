#ifndef CANNEDBSD_SYS_TYPES_H
#define CANNEDBSD_SYS_TYPES_H

#include "cannedbsd/abi.h"
#include <stddef.h>
#include <stdint.h>

typedef uint64_t ino_t;
typedef uint32_t mode_t;
typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef cb_off_t off_t;
typedef cb_ssize_t ssize_t;
typedef int32_t blksize_t;
typedef int64_t blkcnt_t;
typedef uint32_t dev_t;

#endif
