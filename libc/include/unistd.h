#ifndef CANNEDBSD_UNISTD_H
#define CANNEDBSD_UNISTD_H

#include "cannedbsd/libc.h"
#include "sys/types.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define isatty cb_libc_isatty
#define pipe cb_libc_pipe
#define read cb_libc_read
#define write cb_libc_write
#define close cb_libc_close
#define truncate cb_libc_truncate
#define ftruncate cb_libc_ftruncate
#define unlink cb_libc_unlink
#define rmdir cb_libc_rmdir

#define SEEK_SET CB_SEEK_SET
#define SEEK_CUR CB_SEEK_CUR
#define SEEK_END CB_SEEK_END
#define lseek cb_libc_lseek
#define fsync cb_libc_fsync
#define sync cb_libc_sync

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define access cb_libc_access

#define undelete cb_libc_undelete

#define environ (*cb_libc_environ_location())

#define optind (cb_libc_getopt_state_location()->optind)
#define opterr (cb_libc_getopt_state_location()->opterr)
#define optopt (cb_libc_getopt_state_location()->optopt)
#define optarg (cb_libc_getopt_state_location()->optarg)
#define getopt cb_libc_getopt

#endif
