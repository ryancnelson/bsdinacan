#ifndef CANNEDBSD_NETBSD_IMPORT_ASSERT_H
#define CANNEDBSD_NETBSD_IMPORT_ASSERT_H

/*
 * Import-only shim: the pinned NetBSD strlen.c includes <assert.h> but does
 * not use assert(). Do not expose this directory as cannedBSD's public libc
 * include path until assert semantics exist and are tested.
 *
 * _DIAGASSERT: pinned NetBSD common/lib/libc/stdlib/_strtol.h calls this
 * once. Real NetBSD assert.h defines it as a pure no-op unless the
 * internal _DIAGNOSTIC libc-build flag is set, which this project never
 * does; only the _DIAGNOSTIC branch calls a real diagnostic function.
 * Deliberately a no-op here too -- not wired to any host assert().
 */
#define _DIAGASSERT(e) ((void)0)

#endif
