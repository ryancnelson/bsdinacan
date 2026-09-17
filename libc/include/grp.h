#ifndef CANNEDBSD_GRP_H
#define CANNEDBSD_GRP_H

#include "sys/types.h"

const char *group_from_gid(gid_t gid, int nogroup);
#define group_from_gid cb_libc_group_from_gid

#endif
