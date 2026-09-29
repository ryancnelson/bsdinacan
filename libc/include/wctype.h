#ifndef CANNEDBSD_WCTYPE_H
#define CANNEDBSD_WCTYPE_H

#include "cannedbsd/libc.h"
#include "wchar.h"

/* Single, immutable C locale (see wchar.h): a wide character is printable
   exactly when its one mapped byte is printable ASCII (0x20-0x7E). */
#define iswprint cb_libc_iswprint
#define iswspace cb_libc_iswspace
#define wcwidth cb_libc_wcwidth

#endif
