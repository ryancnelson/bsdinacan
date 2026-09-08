#ifndef CANNEDBSD_NETBSD_IMPORT_NBTOOL_CONFIG_H
#define CANNEDBSD_NETBSD_IMPORT_NBTOOL_CONFIG_H

/*
 * Import-only shim: pinned NetBSD strtoimax.c includes "nbtool_config.h"
 * only under its own HAVE_NBTOOL_CONFIG_H branch, which this project
 * defines to select that file's plain, non-locale-aware signature. Nothing
 * else in strtoimax.c/_strtol.h inspects this file's contents, so it stays
 * empty.
 */

#endif
