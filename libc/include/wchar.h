#ifndef CANNEDBSD_WCHAR_H
#define CANNEDBSD_WCHAR_H

#include "cannedbsd/libc.h"
#include <stddef.h>

/* WC-02 and LS-02: one immutable C locale (LOCALE-01), so wide characters
   map 1:1 to bytes and mbstate_t carries no real state. wchar_t is the
   compiler's own builtin from <stddef.h>. mbstate_t is declared once, in
   cannedbsd/libc.h, alongside the functions that take it. */
#ifndef _WINT_T_DECLARED
typedef unsigned int wint_t;
#define _WINT_T_DECLARED
#endif

#define WEOF ((wint_t)-1)

#define mbrtowc cb_libc_mbrtowc
#define wcrtomb cb_libc_wcrtomb

#endif
