#ifndef CANNEDBSD_PWD_H
#define CANNEDBSD_PWD_H

#include "cannedbsd/libc.h"

/*
 * RM-01: no passwd database exists on this backend at all -- there is no
 * struct passwd, no getpwuid/getpwnam. This header exists only to shadow
 * the host's real <pwd.h> (which rm.c includes) with cannedBSD's own,
 * narrower surface, and to declare the one BSD extension check()'s
 * (unreachable, see notes/iterations/RM-01.md) auto-ask heuristic uses.
 */
#define user_from_uid cb_libc_user_from_uid

#endif
