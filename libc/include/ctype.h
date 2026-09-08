#ifndef CANNEDBSD_CTYPE_H
#define CANNEDBSD_CTYPE_H

#include "cannedbsd/libc.h"

/* C/POSIX-locale only; no rune/locale table. Specified only for inputs
 * representable as unsigned char, or EOF, matching the standard's own
 * restriction. */
#define isdigit cb_libc_isdigit
#define isspace cb_libc_isspace

#endif
