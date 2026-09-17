#ifndef CANNEDBSD_SIGNAL_H
#define CANNEDBSD_SIGNAL_H

#include "sys/types.h"

#define SIGINT 2
#define SIGINFO 29

typedef void (*sig_t)(int);
sig_t signal(int sig, sig_t func);
#define signal cb_libc_signal

#endif
