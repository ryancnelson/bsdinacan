#ifndef CANNEDBSD_LOCALE_H
#define CANNEDBSD_LOCALE_H

#include "cannedbsd/libc.h"

/* C/POSIX only. Successful results are borrowed and must not be modified.
 * Empty requests consult the current task environment, never the host. */
#define LC_ALL CB_LIBC_LC_ALL
#define setlocale cb_libc_setlocale

#endif
