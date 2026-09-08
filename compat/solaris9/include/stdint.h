#ifndef CANNEDBSD_SOLARIS9_STDINT_H
#define CANNEDBSD_SOLARIS9_STDINT_H

/* Solaris 9 exposes its fixed-width types and constants through inttypes.h,
   not a standalone stdint.h. This adapter is added to the include path only
   when building with -DCANNEDBSD_SOLARIS9 (see tools/solaris9-build.sh); it
   is otherwise never on the search path and has no effect on any other
   build. Reconciled from work/SOLARIS-01-reference (a28f9ed) verbatim --
   this file itself is unchanged from that historical port. */
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
