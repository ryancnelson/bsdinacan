#ifndef CANNEDBSD_SYS_WAIT_H
#define CANNEDBSD_SYS_WAIT_H

#include "sys/types.h"

#define WIFEXITED(status) 1
#define WEXITSTATUS(status) ((status) & 0377)

pid_t waitpid(pid_t pid, int *status, int options);
#define waitpid cb_libc_waitpid

#endif
