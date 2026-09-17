#ifndef CANNEDBSD_WCHAR_H
#define CANNEDBSD_WCHAR_H

#include "cannedbsd/libc.h"
#include <stddef.h>

#ifndef _MBSTATE_T_DECLARED
typedef struct {
    int __state;
} mbstate_t;
#define _MBSTATE_T_DECLARED
#endif

#ifndef _WINT_T_DECLARED
typedef unsigned int wint_t;
#define _WINT_T_DECLARED
#endif

#define mbrtowc cb_libc_mbrtowc
size_t cb_libc_mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);

#endif
