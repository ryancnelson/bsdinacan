#ifndef CANNEDBSD_NETBSD_IMPORT_LIMITS_H
#define CANNEDBSD_NETBSD_IMPORT_LIMITS_H

/*
 * Import-only shim: the pinned NetBSD dirname.c uses only PATH_MAX, sized
 * to the exact same pre-existing constant cannedBSD's own runtime uses, so
 * there is one source of truth for the bound rather than two to keep in
 * sync. Do not expose this directory as cannedBSD's public libc include
 * path -- the real limits.h exposes far more than this project implements.
 */
#include "cannedbsd/abi.h"

#ifndef PATH_MAX
#define PATH_MAX CB_PATH_MAX
#endif

/*
 * Import-only shim, added for the pinned NetBSD memset.c: its word-fill
 * fast path replicates a byte pattern across a full `unsigned int` by
 * testing `#if UINT_MAX > 0xffff` (and > 0xffffffff) to decide how many
 * doubling steps to run. An undefined UINT_MAX evaluates to 0 in #if,
 * silently skipping those steps -- not a compile error, a silent wrong
 * answer in the upper bytes of every word-sized store. Confirmed by a
 * failing memset probe test before this was added, not assumed.
 */
#ifndef UINT_MAX
#define UINT_MAX 0xffffffffU
#endif

#endif
