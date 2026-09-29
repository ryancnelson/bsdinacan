#ifndef CANNEDBSD_SIGNAL_H
#define CANNEDBSD_SIGNAL_H

#include "sys/types.h"

/* SIG-02: only cooperative SIGINT default/ignore are supported. Default
   delivery terminates the internal task with status 130 at a safe boundary.
   No host signal, arbitrary handler, or SIGINFO progress callback is installed. */
#define SIGINT 2
#define SIGINFO 29

typedef void (*sig_t)(int);
/* Function identities, compared but never invoked. No integer-to-function
   pointer casts, including on the 32-bit Mac and Solaris targets. */
void cb_libc_sig_ignore(int sig);
void cb_libc_sig_error(int sig);
#define SIG_DFL ((sig_t)0)
#define SIG_IGN cb_libc_sig_ignore
#define SIG_ERR cb_libc_sig_error

sig_t cb_libc_signal(int sig, sig_t func);
#define signal cb_libc_signal

#endif
