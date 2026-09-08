#ifndef CANNEDBSD_SOLARIS9_STDINT_H
#define CANNEDBSD_SOLARIS9_STDINT_H

/* Solaris 9 exposes its fixed-width types and constants through inttypes.h,
   not a standalone stdint.h. This adapter is added to the include path only
   when building with -DCANNEDBSD_SOLARIS9 (see tools/solaris9-build.sh).

   Found via a real guest build, not assumed: an angle-bracket
   `#include <inttypes.h>` here is not safe. The Makefile's own
   per-command-object rules add `-Ilibc/include` (this project's
   private NetBSD-import veneer, which has its own inttypes.h) for any
   source built against that veneer (e.g. commands/wc.c), and that
   directory sits on the search path before the guest's real system
   headers regardless of where -Icompat/solaris9/include itself is
   listed. An angle-bracket include from here re-enters the search from
   the top and finds the private veneer's inttypes.h first -- confirmed
   by a real GCC 3.4.6 build on the guest, which failed inside
   libc/include/inttypes.h with "syntax error before cb_libc_strtoimax"
   (that veneer header assumes types this adapter is the one trying to
   supply). Including the guest's real system header by its verified
   absolute path sidesteps the search-path ambiguity entirely; checked
   directly against the actual guest (SunOS 5.9 sun4m), not assumed:
   this is exactly where it lives, guarded by its own _INTTYPES_H. */
#include "/usr/include/inttypes.h"
#include <limits.h>

#ifndef SIZE_MAX
#ifdef _LP64
#define SIZE_MAX ULONG_MAX
#else
#define SIZE_MAX UINT_MAX
#endif
#endif

#endif
