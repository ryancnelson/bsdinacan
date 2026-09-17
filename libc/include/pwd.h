#ifndef CANNEDBSD_PWD_H
#define CANNEDBSD_PWD_H

#include "sys/types.h"

/*
 * No passwd database exists on this backend at all -- there is no struct
 * passwd, no getpwuid/getpwnam. This header exists only to shadow the
 * host's real <pwd.h> (which both rm.c and mv.c include) with
 * cannedBSD's own, narrower surface, and to declare the one BSD
 * extension both utilities' interactive-confirmation heuristics use.
 */
const char *user_from_uid(uid_t uid, int nouser);
#define user_from_uid cb_libc_user_from_uid

#endif
