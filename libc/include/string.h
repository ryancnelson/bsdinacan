#ifndef CANNEDBSD_STRING_H
#define CANNEDBSD_STRING_H

#include "cannedbsd/libc.h"

#define strerror cb_libc_strerror
#define strlen cb_libc_strlen

#if defined(CANNEDBSD_BUILDING_LIBC_STRCMP)
#if defined(__GNUC__) || defined(__clang__)
int strcmp(const char *left, const char *right) __asm__("cb_libc_strcmp");
#else
#error "NetBSD strcmp import needs a compiler-specific link-name adapter"
#endif
#else
#define strcmp cb_libc_strcmp
#endif

#endif
