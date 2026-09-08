#ifndef CB_LIBC_POLL_H
#define CB_LIBC_POLL_H

#include <stddef.h>
#include <sys/cdefs.h>
#include <cannedbsd/abi.h>
#include <cannedbsd/libc.h>

#define POLLIN      CB_POLLIN
#define POLLPRI     CB_POLLPRI
#define POLLOUT     CB_POLLOUT
#define POLLERR     CB_POLLERR
#define POLLHUP     CB_POLLHUP
#define POLLNVAL    CB_POLLNVAL

typedef size_t nfds_t;

#define pollfd cb_pollfd

#define poll cb_libc_poll

#endif
