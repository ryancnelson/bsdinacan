#ifndef CANNEDBSD_PWD_H
#define CANNEDBSD_PWD_H

#include "sys/types.h"

const char *user_from_uid(uid_t uid, int nouser);
#define user_from_uid cb_libc_user_from_uid

#endif
