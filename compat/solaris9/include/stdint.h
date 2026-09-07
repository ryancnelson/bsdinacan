#ifndef CANNEDBSD_SOLARIS9_STDINT_H
#define CANNEDBSD_SOLARIS9_STDINT_H

/* Solaris 9 exposes its fixed-width types and constants through inttypes.h. */
#include <inttypes.h>
#include <limits.h>

#ifndef SIZE_MAX
#ifdef _LP64
#define SIZE_MAX ULONG_MAX
#else
#define SIZE_MAX UINT_MAX
#endif
#endif

#endif
