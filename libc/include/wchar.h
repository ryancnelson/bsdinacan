#ifndef CANNEDBSD_WCHAR_H
#define CANNEDBSD_WCHAR_H

#include "cannedbsd/libc.h"
#include <stddef.h>

/* LS-02: pinned ls/util.c's default (non-"-w") path treats every
   filename byte as one wide character to decide whether it needs
   escaping. LOCALE-01 already committed this whole runtime to a single,
   immutable C locale (no locale database, no multibyte encodings) --
   this is that same commitment applied to wide characters: a 1:1
   byte<->wchar_t mapping, genuinely stateless (mbstate_t carries no
   real state, since there is no real multibyte encoding to track state
   across), not a fabricated multibyte decoder. wchar_t itself is not
   redefined here -- <stddef.h> (transitively included via
   cannedbsd/abi.h) already provides the host compiler's own builtin
   wchar_t, the same fundamental-type exemption inttypes.h already
   applies to intmax_t. */
typedef int wint_t;
typedef struct { int unused; } mbstate_t;

#define WEOF ((wint_t)-1)

/* ps is spelled void * here, not mbstate_t *, to be textually identical
   to cannedbsd/libc.h's declaration of the same function (a real
   conflicting-types error otherwise, since mbstate_t isn't visible in
   that internal header) -- callers passing a mbstate_t * still convert
   implicitly at each call site. */
size_t cb_libc_mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps);
size_t cb_libc_wcrtomb(char *s, wchar_t wc, void *ps);

#define mbrtowc cb_libc_mbrtowc
#define wcrtomb cb_libc_wcrtomb

#endif
