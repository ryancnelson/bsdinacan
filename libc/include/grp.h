#ifndef CANNEDBSD_GRP_H
#define CANNEDBSD_GRP_H

#include "cannedbsd/libc.h"

/*
 * RM-01: no group database exists on this backend at all -- there is no
 * struct group, no getgrgid/getgrnam. This header exists only to shadow
 * the host's real <grp.h> (which rm.c includes) with cannedBSD's own,
 * narrower surface, and to declare the one BSD extension check()'s
 * (unreachable, see notes/iterations/RM-01.md) auto-ask heuristic uses.
 */
#define group_from_gid cb_libc_group_from_gid

#endif
