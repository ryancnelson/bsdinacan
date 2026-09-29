#ifndef CANNEDBSD_UTIL_H
#define CANNEDBSD_UTIL_H

#include "cannedbsd/libc.h"

/* humanize_number(3) and its HN_* flags live in stdlib.h, as on NetBSD. */
#include "stdlib.h"

/* LS-02: real, not stubbed -- getbsize's actual default path (no
   BLOCKSIZE environment override, which this runtime has no use for
   since there is no real disk block size to report) is exactly
   "512 bytes per block", the same default real getbsize(3) falls back
   to. flags_to_string is honestly bounded: st_flags is always 0 here
   (no BSD file-flags concept, matching st_uid/st_gid's own fixed-0
   reasoning -- see notes/iterations/FS-STAT-01.md), so the only case
   that can ever actually execute is "return the caller's default
   string for zero flags", which is exactly what real flags_to_string
   does too. */
char *cb_libc_getbsize(int *headerlenp, long *blocksizep);
char *cb_libc_flags_to_string(unsigned long flags, const char *def);
#define getbsize cb_libc_getbsize
#define flags_to_string cb_libc_flags_to_string

#endif
