#ifndef CANNEDBSD_SIGNAL_H
#define CANNEDBSD_SIGNAL_H

#include "sys/types.h"

/*
 * PROVISIONAL PLACEHOLDER -- not a real implementation, by explicit
 * director instruction (see notes/iterations/RM-01.md, applies equally
 * to mv's own use of this). SIG-01 added a real internal cooperative
 * interrupt core, but its own note explicitly excludes "the private
 * signal.h veneer... and arbitrary handlers" from that phase's scope --
 * there is no public signal()/handler-dispatch surface to wire this to
 * yet. Wiring SIGINFO's ^T progress reporting to SIG-01's internal
 * mechanism (or deciding not to) is a real design question for whoever
 * owns that decision, not something to resolve incidentally here:
 * decided to leave it outside the accepted matrix for both rm and mv,
 * same precedent as FCNTL-01's cat -l.
 *
 * cb_libc_signal always returns SIG_ERR and never actually installs
 * anything -- both rm.c and mv.c discard signal()'s return value, so
 * this is a silent, honest no-op: ^T progress status reporting compiles
 * but is inert. It does not pretend to install a handler it cannot
 * actually call back into.
 */

#define SIGINT 2
/* Not a real signal number -- chosen only to be distinct from this
   project's own errno/status numbering; nothing on this backend can
   ever raise it. */
#define SIGINFO 29

typedef void (*sig_t)(int);

#define SIG_ERR ((sig_t)-1)

sig_t signal(int sig, sig_t func);
#define signal cb_libc_signal

#endif
