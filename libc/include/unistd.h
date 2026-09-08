#ifndef CANNEDBSD_UNISTD_H
#define CANNEDBSD_UNISTD_H

#include "cannedbsd/libc.h"

typedef cb_ssize_t ssize_t;
typedef cb_off_t off_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define read cb_libc_read
#define write cb_libc_write
#define close cb_libc_close
#define truncate cb_libc_truncate
#define ftruncate cb_libc_ftruncate

#define environ (*cb_libc_environ_location())

#define optind (cb_libc_getopt_state_location()->optind)
#define opterr (cb_libc_getopt_state_location()->opterr)
#define optopt (cb_libc_getopt_state_location()->optopt)
#define optarg (cb_libc_getopt_state_location()->optarg)
#define getopt cb_libc_getopt

#endif
