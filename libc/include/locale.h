#ifndef CANNEDBSD_LOCALE_H
#define CANNEDBSD_LOCALE_H

#include "cannedbsd/libc.h"

/* C/POSIX only. Successful results are borrowed and must not be modified.
 * Empty requests consult the current task environment, never the host. */
#define LC_ALL CB_LIBC_LC_ALL
/* STATICS-CACHE-02: localeconv(3) for the one C locale. Only the numeric
   members are provided (C-locale values "." and ""); a caller naming any
   other struct lconv member fails to compile rather than reading made-up
   data. Pinned humanize_number.c reads decimal_point. */
#define lconv cb_libc_lconv
#define localeconv cb_libc_localeconv
#ifndef setlocale
#define setlocale cb_libc_setlocale
#endif

#endif
