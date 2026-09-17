#ifndef CANNEDBSD_GRP_H
#define CANNEDBSD_GRP_H

#include "sys/types.h"

/*
 * No group database exists on this backend at all -- there is no struct
 * group, no getgrgid/getgrnam. This header exists only to shadow the
 * host's real <grp.h> (which both rm.c and mv.c include) with
 * cannedBSD's own, narrower surface, and to declare the one BSD
 * extension both utilities' interactive-confirmation heuristics use.
 */
const char *group_from_gid(gid_t gid, int nogroup);
#define group_from_gid cb_libc_group_from_gid

#endif
