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

#endif
