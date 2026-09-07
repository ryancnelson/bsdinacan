#ifndef CANNEDBSD_STRING_H
#define CANNEDBSD_STRING_H

#include "cannedbsd/libc.h"

#define strerror cb_libc_strerror
#define strlen cb_libc_strlen

#if defined(CANNEDBSD_BUILDING_LIBC_MEMCMP)
#if defined(__GNUC__) || defined(__clang__)
int memcmp(const void *left, const void *right, size_t count)
    __asm__("cb_libc_memcmp");
#else
#error "NetBSD memcmp import needs a compiler-specific link-name adapter"
#endif
#else
#define memcmp cb_libc_memcmp
#endif

#if defined(CANNEDBSD_BUILDING_LIBC_MEMCPY)
#if defined(__GNUC__) || defined(__clang__)
void *memcpy(void *destination, const void *source, size_t count)
    __asm__("cb_libc_memcpy");
#else
#error "NetBSD memcpy import needs a compiler-specific link-name adapter"
#endif
#else
#define memcpy cb_libc_memcpy
#endif

#if defined(CANNEDBSD_BUILDING_LIBC_MEMMOVE)
#if defined(__GNUC__) || defined(__clang__)
void *memmove(void *destination, const void *source, size_t count)
    __asm__("cb_libc_memmove");
#else
#error "NetBSD memmove import needs a compiler-specific link-name adapter"
#endif
#else
#define memmove cb_libc_memmove
#endif

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
