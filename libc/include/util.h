#ifndef CANNEDBSD_UTIL_H
#define CANNEDBSD_UTIL_H

#include "cannedbsd/libc.h"

/* LS-02: pinned ls/print.c's only util.h use is humanize_number(), for
   -h (SI-scaled sizes). Genuinely out of accepted scope for this ID --
   same precedent as cat -l/rm -P/-W: an honest, real declaration so the
   pinned source compiles unmodified, and a real failure at the one
   call site rather than a fabricated scaled number. cat.c's own -l and
   this share the same shape: err(1, "humanize_number") fires and ls -h
   fails loudly and honestly; every other flag is unaffected. */
int cb_libc_humanize_number(char *buffer, size_t length, int64_t quantity,
                            const char *suffix, int scale, int flags);
#define humanize_number cb_libc_humanize_number

#define HN_DECIMAL      0x01
#define HN_NOSPACE      0x02
#define HN_B            0x04
#define HN_AUTOSCALE    0

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
