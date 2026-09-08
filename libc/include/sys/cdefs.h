#ifndef CANNEDBSD_SYS_CDEFS_H
#define CANNEDBSD_SYS_CDEFS_H

#define __COPYRIGHT(message) \
    typedef char cb_copyright_notice[(sizeof(message) > 0) ? 1 : -1]
#define __RCSID(message) \
    typedef char cb_rcsid_notice[(sizeof(message) > 0) ? 1 : -1]
#define __strong_alias(alias, symbol)
#define __UNCONST(pointer) ((void *)(uintptr_t)(const void *)(pointer))

#if defined(__GNUC__) || defined(__clang__)
#define __dead __attribute__((__noreturn__))
#else
#define __dead
#endif

#endif
