#ifndef CANNEDBSD_UNISTD_H
#define CANNEDBSD_UNISTD_H

#include "cannedbsd/libc.h"

typedef cb_ssize_t ssize_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define read cb_libc_read
#define write cb_libc_write
#define close cb_libc_close

#define environ (*cb_libc_environ_location())

#endif
