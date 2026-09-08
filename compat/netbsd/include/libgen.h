#ifndef CANNEDBSD_NETBSD_IMPORT_LIBGEN_H
#define CANNEDBSD_NETBSD_IMPORT_LIBGEN_H

/*
 * Import-only shim: the pinned NetBSD dirname.c includes <libgen.h> purely
 * for its own prototype's self-consistency; nothing in the file actually
 * needs a declaration from it. Do not expose this directory as cannedBSD's
 * public libc include path -- basename(3) and friends are not implemented.
 */

#endif
