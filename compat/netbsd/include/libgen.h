#ifndef CANNEDBSD_NETBSD_IMPORT_LIBGEN_H
#define CANNEDBSD_NETBSD_IMPORT_LIBGEN_H

/*
 * Import-only shim: the pinned NetBSD dirname.c and basename.c each include
 * <libgen.h> purely for their own prototype's self-consistency; nothing in
 * either file actually needs a declaration from it. Do not expose this
 * directory as cannedBSD's public libc include path.
 */

#endif
