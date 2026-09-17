#ifndef CANNEDBSD_STRING_H
#define CANNEDBSD_STRING_H

#include "cannedbsd/libc.h"

#define strerror cb_libc_strerror
#define strlen cb_libc_strlen

#if defined(CANNEDBSD_BUILDING_LIBC_STRCPY)
#if defined(__GNUC__) || defined(__clang__)
char *strcpy(char *dest, const char *src) __asm__("cb_libc_strcpy");
#else
#error "NetBSD strcpy import needs a compiler-specific link-name adapter"
#endif
#else
#define strcpy cb_libc_strcpy
#endif
#define strchr cb_libc_strchr
#define strrchr cb_libc_strrchr
#define strmode cb_libc_strmode

#if defined(CANNEDBSD_BUILDING_LIBC_MEMSET)
#if defined(__GNUC__) || defined(__clang__)
void *memset(void *destination, int character, size_t count)
    __asm__("cb_libc_memset");
#else
#error "NetBSD memset import needs a compiler-specific link-name adapter"
#endif
#else
#define memset cb_libc_memset
#endif

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

#define strlcpy cb_libc_strlcpy
#define strrchr cb_libc_strrchr
#define strncat cb_libc_strncat
#define strspn cb_libc_strspn
#define strcspn cb_libc_strcspn

size_t cb_libc_strlcpy(char *dst, const char *src, size_t siz);
char *cb_libc_strrchr(const char *text, int character);
char *cb_libc_strncat(char *s1, const char *s2, size_t n);
size_t cb_libc_strspn(const char *s, const char *charset);
size_t cb_libc_strcspn(const char *s, const char *charset);

#endif
