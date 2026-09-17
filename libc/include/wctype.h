#ifndef CANNEDBSD_WCTYPE_H
#define CANNEDBSD_WCTYPE_H

#include "cannedbsd/libc.h"
#include "wchar.h"

/* LS-02: single, immutable C locale (see wchar.h's own comment) -- a
   wide character is printable exactly when its one mapped byte is
   printable ASCII (0x20-0x7E). */
int cb_libc_iswprint(wint_t wc);
int cb_libc_wcwidth(wchar_t wc);

#define iswprint cb_libc_iswprint
#define wcwidth cb_libc_wcwidth

#endif
