#ifndef CANNEDBSD_NETBSD_IMPORT_ASSERT_H
#define CANNEDBSD_NETBSD_IMPORT_ASSERT_H

/*
 * Import-only shim: the pinned NetBSD strlen.c includes <assert.h> but does
 * not use assert(). Do not expose this directory as cannedBSD's public libc
 * include path until assert semantics exist and are tested.
 */

#endif
