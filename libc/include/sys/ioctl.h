#ifndef CANNEDBSD_SYS_IOCTL_H
#define CANNEDBSD_SYS_IOCTL_H

#include "cannedbsd/libc.h"

/* LS-02: pinned ls.c's only ioctl() use is TIOCGWINSZ to query real
   terminal geometry for column-width layout. This runtime has no real
   terminal geometry to report (no pty, no host tty passthrough), so
   cb_libc_ioctl honestly fails every request rather than fabricate a
   window size -- ls.c's own fallback (termwidth's compiled-in default
   of 80) then takes over, which is real NetBSD behavior on any output
   that isn't a real window-aware terminal. Not a general ioctl
   implementation: only the declaration and TIOCGWINSZ constant this
   one consumer needs. */
#define TIOCGWINSZ 0x40087468

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int cb_libc_ioctl(int fd, unsigned long request, ...);
#define ioctl cb_libc_ioctl

#endif
