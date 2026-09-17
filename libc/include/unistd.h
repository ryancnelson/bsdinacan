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
#define access cb_libc_access
#define unlink cb_libc_unlink
#define rmdir cb_libc_rmdir
#define strmode cb_libc_strmode
#define vfork cb_libc_vfork
#define execl cb_libc_execl
#define _exit cb_libc_exit

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define environ (*cb_libc_environ_location())

#define optind (cb_libc_getopt_state_location()->optind)
#define opterr (cb_libc_getopt_state_location()->opterr)
#define optopt (cb_libc_getopt_state_location()->optopt)
#define optarg (cb_libc_getopt_state_location()->optarg)
#define getopt cb_libc_getopt

int cb_libc_access(const char *path, int mode);
int cb_libc_unlink(const char *path);
int cb_libc_rmdir(const char *path);
void cb_libc_strmode(mode_t mode, char *p);
pid_t cb_libc_vfork(void);
int cb_libc_execl(const char *path, const char *arg0, ...);

#endif
