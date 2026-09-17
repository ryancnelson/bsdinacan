#ifndef CANNEDBSD_VIS_H
#define CANNEDBSD_VIS_H

#include "cannedbsd/libc.h"

/* LS-02: pinned ls/util.c's safe_print() (the -b/-B octal/C-style escape
   path) is this project's only strvis() consumer. Real implementation,
   not a stub: printable ASCII passes through unchanged; everything else
   is escaped, either as a C-style backslash sequence (VIS_CSTYLE, for
   the handful strvis(3) defines -- \n \t \r \b \a \f \v) or as a
   3-digit backslash-octal escape (VIS_OCTAL), matching real strvis(3)'s
   own choice when a byte has no C-style form. VIS_NL/VIS_WHITE select
   which whitespace also gets escaped rather than passed through -- the
   exact flag combination ls.c's own call site sets. */
#define VIS_NL     0x01
#define VIS_WHITE  0x02
#define VIS_OCTAL  0x04
#define VIS_CSTYLE 0x08

size_t cb_libc_strvis(char *dst, const char *src, int flags);
#define strvis cb_libc_strvis

#endif
