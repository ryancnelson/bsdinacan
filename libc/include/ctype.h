#ifndef CANNEDBSD_CTYPE_H
#define CANNEDBSD_CTYPE_H

#include "cannedbsd/libc.h"

/* C/POSIX-locale only; no rune/locale table. Specified only for inputs
 * representable as unsigned char, or EOF, matching the standard's own
 * restriction. */
#define isdigit cb_libc_isdigit
#define isspace cb_libc_isspace
#define isascii cb_libc_isascii
#define toascii cb_libc_toascii
#define iscntrl cb_libc_iscntrl

int cb_libc_isascii(int character);
int cb_libc_toascii(int character);
int cb_libc_iscntrl(int character);

#endif
