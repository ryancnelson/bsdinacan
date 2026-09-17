#ifndef CANNEDBSD_SIGNAL_H
#define CANNEDBSD_SIGNAL_H

#include "cannedbsd/libc.h"

/*
 * PROVISIONAL PLACEHOLDER -- not a real implementation, by explicit
 * director instruction (see notes/iterations/RM-01.md). SIG-01 added a
 * real internal cooperative interrupt core, but its own note explicitly
 * excludes "the private signal.h veneer... and arbitrary handlers" from
 * that phase's scope -- there is no public signal()/handler-dispatch
 * surface to wire this to yet. Wiring SIGINFO's ^T progress reporting to
 * SIG-01's internal mechanism (or deciding not to) is a real design
 * question for whoever owns that decision, not something to resolve
 * incidentally under RM-01.
 *
 * cb_libc_signal here always returns SIG_ERR and never actually installs
 * anything -- rm.c discards signal()'s return value
 * (`(void)signal(SIGINFO, progress);`), so this is a silent, honest
 * no-op: -v/^T progress status reporting compiles but is inert, exactly
 * like -P and -W. It does not pretend to install a handler it cannot
 * actually call back into.
 */

typedef int sig_atomic_t;

#define SIG_ERR ((void (*)(int))-1)

/* Not a real signal number -- chosen only to be distinct from this
   project's own errno/status numbering; nothing on this backend can
   ever raise it. */
#define SIGINFO 29

/* Declared in cannedbsd/libc.h (implemented in libc/cb_libc.c); only the
   macro rename lives here, matching every other header under
   libc/include in this project. */
#define signal cb_libc_signal

#endif
