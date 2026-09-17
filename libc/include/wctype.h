#ifndef CANNEDBSD_WCTYPE_H
#define CANNEDBSD_WCTYPE_H

#include "cannedbsd/libc.h"
#include <stddef.h>

#ifndef _WINT_T_DECLARED
typedef unsigned int wint_t;
#define _WINT_T_DECLARED
#endif

#define iswspace cb_libc_iswspace
int cb_libc_iswspace(wint_t wc);

#endif
