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

#define open cb_libc_open

#endif
