# CONV-01-design: numeric and small string conversion for unchanged head

Design and measurement only, based on main `c296deb`. No API implementation,
no `main`/UI/guest change, and no backlog rollup edit is claimed here; this
document proposes the exact conversion contract for the coordinator's own
backlog to accept or revise. It follows `notes/iterations/HEAD-01-plan.md`
item 3 (`CONV-01`) and the pinned-source diagnostics already recorded in
`notes/iterations/utility-roadmap-20260908.md`.

## Exact call sites (from the pinned source itself, not assumed)

The pinned `usr.bin/head/head.c` (revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`,
SHA-256 `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`,
matching the hash already recorded in the roadmap) was fetched again and
re-hashed directly for this design, not trusted from memory. Its three
relevant call sites, read directly from the source:

```c
case 'c':
        errno = 0;
        bytecnt = strtoimax(optarg, &ep, 10);
        if ((bytecnt == INTMAX_MAX && errno == ERANGE) ||
            *ep || bytecnt <= 0)
                errx(1, "illegal byte count -- %s", optarg);
        break;
case 'n':
        errno = 0;
        linecnt = strtoimax(optarg, &ep, 10);
        if ((linecnt == INTMAX_MAX && errno == ERANGE) ||
            *ep || linecnt <= 0)
                errx(1, "illegal line count -- %s", optarg);
        break;
```
```c
static void
obsolete(char *argv[])
{
        char *ap;
        while ((ap = *++argv)) {
                if (ap[0] != '-' || ap[1] == '-' ||
                    !isdigit((unsigned char)ap[1]))
                        return;
                if ((ap = malloc(strlen(*argv) + 2)) == NULL)
                        err(1, NULL);
                ap[0] = '-';
                ap[1] = 'n';
                (void)strcpy(ap + 2, *argv + 1);
                *argv = ap;
        }
}
```

Three concrete, falsifiable facts follow directly from this, not from the
general standard:

- `strtoimax` is called with a **literal base of `10` only**, never `0` or
  any other base, and always with a non-`NULL` `endptr`. Nothing in the
  pinned source exercises base autodetection (leading `0x`/`0`), a `NULL`
  `endptr`, or any base other than 10.
- Overflow detection is the exact idiom `result == INTMAX_MAX && errno ==
  ERANGE`; underflow (a negative `intmax_t` result) is never distinguished
  from "just a small number," because the immediately following `bytecnt
  <= 0` / `linecnt <= 0` check rejects zero and negative values identically
  as "illegal count," regardless of whether `strtoimax` itself set `ERANGE`.
  A conforming `strtoimax` that saturates to `INTMAX_MIN` on underflow (per
  `_strtol.h`, fetched and read below) is already sufficient; this call site
  does not need `errno` to distinguish that case at all.
- `isdigit` is called exactly once, only on `(unsigned char)ap[1]`, and
  `strcpy` is called exactly once, into a buffer `obsolete()` itself just
  sized as `strlen(*argv) + 2` bytes -- i.e. `strcpy`'s destination is never
  under-sized relative to its source at this call site (`*argv + 1` is one
  byte shorter than the `malloc`'d buffer minus the two-byte `"-n"` prefix
  `obsolete` writes before calling it). This is head's own responsibility,
  not something `strcpy` itself protects; the design below does not weaken
  or add bounds-checking behavior no standard `strcpy` has.

## What the standard interfaces actually require here

- `strtoimax(const char *nptr, char **endptr, int base)`, declared in
  `<inttypes.h>`, requiring `intmax_t` and `INTMAX_MAX`/`INTMAX_MIN` from the
  same header (C99 7.8.1, 7.18.1.5). Skips leading whitespace, an optional
  sign, and a base-appropriate digit sequence; sets `*endptr` to the first
  unconverted character (or `nptr` itself if nothing converted); on overflow
  returns `INTMAX_MAX`/`INTMAX_MIN` and sets `errno` to `ERANGE`; base `10`
  never needs the `0x`/`0` leading-sequence detection base `0` requires. The
  full standard interface accepts base `0` or `2..36`; head.c only ever asks
  for base `10`, but nothing about that restricts what the *interface*
  itself must support without becoming a different, narrower contract than
  the name `strtoimax` promises to any other future caller.
- `isdigit(int c)`, declared in `<ctype.h>` (C99 7.4.1.5): true for the
  ten decimal digit characters in the "C" locale, for any `c` representable
  as `unsigned char` or equal to `EOF`; undefined for any other `int` value,
  which is exactly why head.c casts through `unsigned char` before calling
  it.
- `strcpy(char *restrict dst, const char *restrict src)`, declared in
  `<string.h>` (C99 7.21.2.3): copies `src` through and including its
  terminating NUL into `dst`; undefined if the regions overlap or `dst` is
  too small. No bounds-checking variant is part of this contract, and this
  design does not invent one.
- `ERANGE`, declared in `<errno.h>`, needed only as the sentinel `strtoimax`
  sets on overflow/underflow.

None of these three call sites, nor the transitive dependency traced below,
touch `cb_api_v1` (the versioned runtime/libc-veneer boundary struct) at all:
`intmax_t` is an ordinary C typedef head.c uses for its own local variables,
never a parameter or return type crossing the runtime ABI. This is exactly
like `puts`/`printf`/`strlen` today -- an ordinary-facing header and a small
number of `cb_libc_*` functions, not a new runtime accessor, not a new
`cb_api_v1` field, and therefore nothing to append at any ABI tail.

## A transitive dependency not visible from head.c's own source: `isspace`

Fetching and reading the actual pinned-revision implementation this design
proposes to import (below) shows it internally skips leading whitespace with
`isspace(c)`. Head.c itself never calls `isspace`, but importing `strtoimax`
unchanged means `<ctype.h>` must also supply a working `isspace`, or the
imported file will not compile. This was found by tracing the actual
implementation's own calls, not assumed from the standard's general
requirements or added speculatively: `isdigit` and `isspace` are the only
two `<ctype.h>` functions this task needs, and are the only two proposed.

## Proposed contract: do not import NetBSD's real `ctype`/locale machinery

NetBSD's actual `isdigit`/`isspace` (`lib/libc/gen/isctype.c`,
`sha256:33839149cfbd9c0783d1c1955fd666983213b067277dce2c0fcf46155f5721d5`
at the pinned revision, fetched and read for this design) are table-driven
through `_RuneLocale`, `runetype_local.h`, and `setlocale_local.h` -- the
same per-locale rune-table machinery `LOCALE-01`/`setlocale` deliberately
does not import wholesale. Head.c's own single `isdigit` call only ever
needs the C/POSIX-locale ASCII digit test (it casts through
`(unsigned char)`, never routes through a task's current locale). Importing
NetBSD's real implementation would require importing that whole rune-table
subsystem for one digit check nothing in this task's actual scope uses
locale-sensitively. Proposed instead: two minimal, cannedBSD-owned functions
(`cb_libc_isdigit`, `cb_libc_isspace`) implementing exactly the C-locale
ASCII contract (`'0'`..`'9'`; and the C-locale whitespace set space, `\t`,
`\n`, `\v`, `\f`, `\r`), each a single-line range/set check, each accepting
any `int` and specified only for inputs representable as `unsigned char` or
`EOF`, matching the standard's own restriction. Both are exhaustively
testable over all 257 relevant input values, unlike a locale-aware table.
This is a smaller, not larger, libc surface than importing the real NetBSD
source would be -- consistent with "smallest independent missing interface,"
not an exception to it.

## Proposed contract: import `strtoimax` unchanged, in NetBSD's own already-simplified mode

The pinned revision's actual `strtoimax` is a two-file macro template, both
fetched, read in full, and hashed for this design (URLs given so the
coordinator can refetch and re-verify independently, exactly as
`utility-roadmap-20260908.md` already does for head.c/echo.c):

| File | Path | SHA-256 | License |
| --- | --- | --- | --- |
| [strtoimax.c](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/common/lib/libc/stdlib/strtoimax.c) | `common/lib/libc/stdlib/strtoimax.c` | `c2476abb39e6ab8dd1fe2d745aeef66309d6cc90adb10f4beee64ae38c1f1ae5` | Two-clause (DragonFly/Citrus, 2003/2005) |
| [_strtol.h](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/common/lib/libc/stdlib/_strtol.h) | `common/lib/libc/stdlib/_strtol.h` | `f6ad43531aab239f6bb1c669e01b9df9ebc0c3e0a035cc27a89b74d404dbb74c` | Three-clause Regents (1990, 1993) |

`strtoimax.c` is a thin driver: it defines `_FUNCNAME strtoimax`, `__INT
intmax_t`, `__INT_MIN INTMAX_MIN`, `__INT_MAX INTMAX_MAX`, then `#include
"_strtol.h"`, which contains the actual base-2..36 parsing loop, overflow
cutoff arithmetic, and `errno`/`endptr` handling (this is the same shared
template `strtol`/`strtoll` use with different `#define`s -- importing it
once for `intmax_t` does not pull in those other functions, since the
template only emits code for whichever `_FUNCNAME` the including file
defines).

The critical, verified finding: `_strtol.h` branches its whole body on
`defined(_KERNEL) || defined(_STANDALONE) || defined(HAVE_NBTOOL_CONFIG_H)
|| defined(BCS_ONLY)`. When any of those is defined, it emits the plain
three-argument `intmax_t strtoimax(const char *, char **, int)` signature
using plain `isspace(c)` -- **not** the locale-aware `isspace_l` and
`locale_t`-threaded static wrapper the `#else` branch builds (which needs
NetBSD's own `setlocale_local.h`/`_current_locale()`, not something
cannedBSD implements or should). Separately, and importantly: the
overflow/underflow `errno = ERANGE` assignment and the invalid-base `errno =
EINVAL` assignment are each guarded only by `!defined(_KERNEL) &&
!defined(_STANDALONE)` -- a narrower exclusion that does **not** also
exclude `HAVE_NBTOOL_CONFIG_H`. So defining `HAVE_NBTOOL_CONFIG_H` selects
the plain non-locale-aware signature *and* still gets ordinary userland
`errno` semantics, not the kernel `panic()` path. This is NetBSD's own
already-established "build this generic template without the full locale
subsystem" mode (used by their host-side `nbtool` cross-build support) --
this design proposes reusing it exactly as NetBSD intends it, rather than
inventing a new cannedBSD-specific carve-out of this file.

`strtoimax.c` itself does `#if HAVE_NBTOOL_CONFIG_H #include
"nbtool_config.h" #endif`, so defining that macro also requires a header at
that name on the include path. NetBSD's real `nbtool_config.h` defines
various host-tool `HAVE_*` feature macros unrelated to this file's own
`#if`s; nothing else in `strtoimax.c`/`_strtol.h` inspects its contents, so
the proposed private header is empty (one include guard, no macros) --
exactly the same "import-only, satisfies an `#include` line and nothing
more" pattern already established for this project's empty `libgen.h`.

Compiled this way, at `-Ilibc/include` (private headers only, matching
every other imported command/library source in this project), the only
externally-visible names `strtoimax.c` needs are: `isspace`, `errno`,
`ERANGE`, `EINVAL`, `INTMAX_MAX`, `INTMAX_MIN`, `intmax_t`, and its own
`nbtool_config.h`. Renaming its actual definition to a private link name is
proposed as a single Makefile-level `-Dstrtoimax=cb_libc_strtoimax` (the
same one-step rename `strlen.c`/`strchr.c` already use -- this file needs no
separate "upstream" wrapper layer the way `dirname`/`basename` do, because
it has no per-task result buffer of its own to own; `errno` is already
task-owned today, transparently, through the existing `errno.h` mapping, so
the imported file's own `errno = ERANGE;` line already writes into the
current task's own error cell with zero new veneer code). `<inttypes.h>`
then declares `intmax_t`/`INTMAX_MAX`/`INTMAX_MIN` and maps
`#define strtoimax cb_libc_strtoimax`, mirroring `stdio.h`'s existing
`#define puts cb_libc_puts` pattern exactly.

## `_DIAGASSERT`: an explicit, empty, import-only policy -- no host `assert`

`_strtol.h` (in the branch this design selects) calls `_DIAGASSERT(nptr !=
NULL);` once; `strcpy.c` and `strtoimax.c` both `#include <assert.h>` in
their own non-kernel/non-standalone branch even where they never call any
assert-style macro themselves. Fetching NetBSD's real
`include/assert.h` at the pinned revision shows exactly what `_DIAGASSERT`
is: `#undef _DIAGASSERT` then, unless the internal `_DIAGNOSTIC` build flag
is defined (a NetBSD libc-build-time-only flag this project never sets),
`#define _DIAGASSERT(e) ((void)0)` -- a pure no-op. Only the `_DIAGNOSTIC`
branch (not proposed here) calls a real `__diagassert13` diagnostic
function. So the correct, minimal, no-host-dependency policy is a private,
import-only `<assert.h>` (on the same footing as this project's existing
empty `libgen.h`) that defines `_DIAGASSERT(e)` as `((void)0)`
unconditionally and does **not** implement `_DIAGNOSTIC` mode or route to
any host `assert()`. This must be stated as an explicit, deliberate policy
in the implementing task -- not left for whoever writes the header to
improvise -- because the alternative (a private `assert.h` that actually
maps to the host's own `<assert.h>`) would be exactly the kind of host
leak `check-architecture`'s existing header-leak check is designed to
catch, and because a *silently* empty shim invites a future edit to
"complete" it by wiring in a real assert without re-deriving why it was
deliberately a no-op.

## Proposed contract: import `strcpy` unchanged -- corrected to the `strcmp`/`memcpy` link-name pattern, not `strlen`'s

The pinned revision's `common/lib/libc/string/strcpy.c`
(`sha256:36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad`,
three-clause Regents 1988/1993, fetched and read for this design) is a
seven-line, dependency-free loop -- no macro template, no locale, nothing
transitive beyond the standard's own undefined-on-overlap/under-size
contract already noted above.

**This design's first draft wrongly proposed importing it like `strlen.c`
(a single Makefile `-Dstrcpy=cb_libc_strcpy` rename); independent review
caught the error before implementation.** The pinned source itself does
`#undef strcpy` immediately before its own definition (`#undef strcpy` /
`char * strcpy(char *to, const char *from) { ... }`), exactly like
`strcmp.c`/`memcpy.c`/`memmove.c` already do, and for the same reason:
a plain `-D` command-line macro definition is textually cancelled by that
file's own `#undef` before the definition is reached, so the rename would
silently not happen and the object would export a plain `strcpy` symbol.
This project already solved exactly this problem for `strcmp`/`memcpy`/
`memmove` with the GCC/Clang assembler-name link adapter in `string.h`
(`int strcmp(...) __asm__("cb_libc_strcmp");` guarded by
`CANNEDBSD_BUILDING_LIBC_STRCMP`, immune to `#undef` because it renames the
emitted symbol at the declaration level, not by text substitution).
`strcpy` needs the identical treatment: a
`CANNEDBSD_BUILDING_LIBC_STRCPY`-guarded
`char *strcpy(char *to, const char *from) __asm__("cb_libc_strcpy");`
declaration in `<string.h>`, defined only when compiling
`strcpy.c` itself with `-DCANNEDBSD_BUILDING_LIBC_STRCPY`; ordinary callers
still see the plain `#define strcpy cb_libc_strcpy` macro. Archived into
`libcannedbsd.a` exactly like `strcmp.c`/`memcpy.c` already are. No wrapper,
no task-owned state, no ABI involvement.

`strcpy.c` also does `#include <assert.h>` (present in the pinned source
even though its own seven-line body never actually calls any assert-style
macro) -- see the `_DIAGASSERT` section below, which this file shares with
`_strtol.h`.

## Measured: Retro68 (ILP32) against this host's LP64 toolchain

The first version of this design flagged the Retro68 side as unmeasured
because Docker was down on the design-worktree host. The coordinator
pointed out Docker there is irrelevant: the CI runner host itself has both
Docker and the exact pinned Retro68 image already present, reachable over
SSH. This section records what was actually run there, over SSH, against
that exact pinned image -- distinguishing **compile evidence** (the
compiler accepted or rejected the code; this is real, deterministic
signal) from **execution evidence** (a compiled binary was actually run
and observed; this design does none of that, per the coordinator's
explicit instruction not to touch runtime or the guest).

Confirmed the exact pinned image is present and matches
`.woodpecker/mac68k.yml`'s pin byte-for-byte (`docker inspect` on the
digest reference returns that same digest in `RepoDigests`, not merely a
same-named tag that could silently drift):

```
$ docker inspect ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019 \
    --format '{{.RepoDigests}}'
[ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019]
```

### Compile evidence: type/limit `_Static_assert`s, actually compiled on the pinned cross-compiler

Wrote the exact probe this design's first draft only proposed (not
executed, only *compiled to an object file* -- `-c`, no link, no run):

```c
_Static_assert(sizeof(intmax_t) == 8, "intmax_t must be 8 bytes");
_Static_assert(sizeof(int) == 4, "int must be 4 bytes (ILP32)");
_Static_assert(sizeof(long) == 4, "long must be 4 bytes (ILP32)");
_Static_assert(sizeof(void *) == 4, "pointer must be 4 bytes (ILP32)");
_Static_assert(INTMAX_MAX == 9223372036854775807LL, "INTMAX_MAX must be 2^63-1");
_Static_assert(INTMAX_MIN == (-INTMAX_MAX - 1), "INTMAX_MIN must be -2^63");
```

Compiled inside the pinned container with the exact toolchain binary the
real Mac build resolves to (`m68k-apple-macos-gcc`, confirmed as GCC
`16.1.0`, not assumed from any general Retro68 version knowledge):

```
$ docker run --rm -v <probe.c>:/work/probe.c \
    ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019 \
    /Retro68-build/toolchain/bin/m68k-apple-macos-gcc \
    -std=c99 -Wall -Wextra -Wpedantic -c /work/probe.c -o /tmp/probe.o
```

Result: exit status `0`. The only diagnostics were six `-Wpedantic`
warnings that C99 (as opposed to C11) doesn't standardize
`_Static_assert` -- warnings, not errors, and irrelevant to what is being
measured; every `_Static_assert` itself passed, because a failing one is a
hard compile error in GCC 16, not a warning. **This is the actual measured
fact this design's first draft could only expect**: on the exact pinned
Retro68 cross-compiler, `intmax_t` is 8 bytes, `int`/`long`/`void *` are
each 4 bytes (ILP32, confirmed, not assumed), and `INTMAX_MAX`/`INTMAX_MIN`
hold the exact standard 64-bit values.

### Compile+link evidence: the libgcc 64-bit arithmetic helpers actually resolve

`nm -u` on that same object file:

```
$ /Retro68-build/toolchain/bin/m68k-apple-macos-nm -u /tmp/probe.o
         U __divdi3
         U __muldi3
         U printf
```

Confirms directly (not inferred from ABI convention alone) that ordinary
`intmax_t` multiply/divide on this target -- literally the same
`acc *= base; acc += digit;` and cutoff-division arithmetic
`_strtol.h`'s accumulation loop performs, reproduced in the probe as
`accumulate()`/`divide_step()` -- compiles down to calls on `__muldi3`/
`__divdi3`, exactly as the m68k ABI predicts for a 64-bit type on a CPU
with no native 64-bit multiply/divide.

Confirmed those symbols are actually defined (not just referenced) in the
toolchain's own default `libgcc.a`, and link successfully with no other
flags added:

```
$ m68k-apple-macos-gcc -print-libgcc-file-name
/Retro68-build/toolchain/lib/gcc/m68k-apple-macos/16.1.0/libgcc.a
$ m68k-apple-macos-nm /Retro68-build/toolchain/lib/gcc/m68k-apple-macos/16.1.0/libgcc.a | grep -E '__muldi3|__divdi3'
00000000 T __muldi3
00000000 T __divdi3
$ m68k-apple-macos-gcc /tmp/probe.o -o /tmp/probe.elf
$ echo $?
0
```

The link step succeeded (exit `0`); `__muldi3`/`__divdi3`/`printf` all
resolved automatically from the toolchain's own default libraries, with no
special flags. This confirms the earlier architectural claim ("no private
64-bit-arithmetic helper needed") as a measured fact on the exact pinned
image, not just an inference from the absence of `-nostdlib` in this
project's own build scripts.

**Explicitly not done, and not claimed**: `probe.elf` (produced above) was
never executed -- not on real hardware, not under an emulator, not in the
Basilisk II guest. It is also not a real Retro68/Mac application (no CRT,
no resource fork, no `CMAKE_TOOLCHAIN_FILE`); it exists solely to answer
the compile- and link-time questions above. Nothing here is Mac guest
acceptance evidence, and this section must not be read as such.

## No private 64-bit-arithmetic compiler helper is needed (measured, not just inferred)

`_strtol.h`'s accumulation loop (`acc *= base; acc += i;` and the symmetric
negative-side subtraction) performs ordinary `intmax_t` multiply/add. As
measured directly above, this compiles to real `__muldi3`/`__divdi3` calls
on the pinned Retro68 cross-compiler, and those calls resolve and link
successfully against that toolchain's own default `libgcc.a` with no
extra flags. `libgcc` is the C compiler's own runtime support library,
linked automatically by an ordinary (non-freestanding) build; separately
checked that neither `platform/mac68k/CMakeLists.txt` nor
`platform/mac68k/ci-build.sh` passes `-nostdlib`, `-nodefaultlibs`, or any
other flag that would exclude it, so the existing Mac build already links
it normally today too. No new "private compiler helper" is proposed.

## Proposed additions, scoped exactly to this evidence

- `libc/include/ctype.h` (new): `isdigit`, `isspace` only. Not `isalpha`,
  `isupper`, `isspace_l`, or any other `ctype` function nothing in this
  task's traced dependency chain calls.
- `libc/include/inttypes.h` (new): `intmax_t`, `INTMAX_MAX`, `INTMAX_MIN`,
  `strtoimax`. Not `strtoumax`, not the `PRId64`-style format-macro family
  head.c never uses.
- `libc/include/errno.h`: add `#define ERANGE CB_ERANGE`, and add
  `CB_ERANGE` to `enum cb_error` in `include/cannedbsd/abi.h`. **Corrected
  by review**: the numeric value `34` is NetBSD's (and Linux's) own
  implementation choice, not a value POSIX mandates -- POSIX only requires
  `ERANGE` be a distinct positive macro, not any particular number. This
  design still proposes `34` for familiarity with those two systems, but
  states plainly that cannedBSD is free to assign any value that does not
  collide with its existing `enum cb_error` entries; nothing about this
  task depends on matching NetBSD's number. Placed near the existing
  `EPIPE = 32` entry for readability -- exact placement is the
  implementing task's call. This is a plain named-constant addition to an
  enumeration of independent error codes, not a versioned struct field, so
  no ABI-tail positional concern applies to it the way it does for
  `cb_api_v1`'s function-pointer table. **Also added by review**:
  `cb_libc_strerror`'s existing table (`src/core.c`'s `api_strerror`,
  which already has one `case` per existing `enum cb_error` value) needs
  its own `case CB_ERANGE: return "...";` entry and a probe asserting it --
  this was missing from the first draft entirely, not merely under-scoped.
- `libc/include/string.h`: add the `CANNEDBSD_BUILDING_LIBC_STRCPY`-guarded
  `__asm__`-link-name declaration plus the ordinary `#define strcpy
  cb_libc_strcpy` mapping, per the corrected import section above -- not
  a bare `-D` rename.
- A private, import-only `<assert.h>` (`libc/include/assert.h`, new)
  defining `_DIAGASSERT(e)` as `((void)0)` unconditionally, per the
  dedicated section above. Not a general-purpose `assert()` -- head.c
  itself never calls `assert`, and nothing here proposes one.
- `upstream/netbsd/common/lib/libc/stdlib/strtoimax.c`,
  `upstream/netbsd/common/lib/libc/stdlib/_strtol.h`,
  `upstream/netbsd/common/lib/libc/string/strcpy.c`: vendored unchanged,
  each with its own `UPSTREAM.md` entry recording its own license (two of
  these three files carry different licenses from each other, and both
  differ from `strcpy.c`'s -- each needs its own accurate entry, not a
  merged one).
- A private, empty `nbtool_config.h` on the same import-only include path
  as the existing empty `libgen.h`, solely to satisfy `strtoimax.c`'s own
  `#include` line.
- Two small cannedBSD-owned functions, `cb_libc_isdigit`/`cb_libc_isspace`,
  in `libc/cb_libc.c` (or a new small file, implementer's choice) --
  genuinely new code, not an import, and the only genuinely new logic this
  design proposes beyond renaming/wiring three unchanged upstream files.

Deliberately not proposed: `strtoumax`, `strtol`/`strtoll` (head.c calls
neither directly, and importing `strtoimax` alone does not require them),
`isalpha`/other `ctype` functions, any bounds-checked string-copy variant,
or a `strtoi`/`strtou` range-checked wrapper layer (`_strtoi.h`, also
fetched and read for this design) -- head.c does its own `<= 0` bound check
inline and never calls that wrapper.

## Proposed executable test matrix (for the implementing task, not run here)

Numeric (`strtoimax`). Head.c only ever calls this with a literal base of
`10`, but the *function*'s own declared contract covers base `0`
autodetection and bases `2..36`, and review correctly rejected narrowing
the test matrix to only what one caller happens to exercise. Every row's
`errno` assertion is checked against a **nonzero sentinel value seeded
into `errno` immediately before the call** (e.g. an arbitrary fixed value
distinct from every real error code this project defines), not against
literal `0` -- so "unchanged" means "still bit-for-bit the sentinel
afterward," not merely "happens to read as zero," which would not
actually distinguish "never touched" from "touched and reset to zero."

| Input | Base | Expected result | Expected `*endptr` | Expected `errno` |
| --- | --- | --- | --- | --- |
| `"10"` | 10 | `10` | terminating NUL | sentinel (unchanged) |
| `"-10"` | 10 | `-10` | terminating NUL | sentinel |
| `"  42"` (leading whitespace) | 10 | `42` | terminating NUL | sentinel |
| `"+5"` | 10 | `5` | terminating NUL | sentinel |
| `"9223372036854775807"` (`INTMAX_MAX`) | 10 | `INTMAX_MAX` | terminating NUL | sentinel |
| `"9223372036854775808"` (`INTMAX_MAX + 1`) | 10 | `INTMAX_MAX` | terminating NUL | `ERANGE` |
| `"-9223372036854775808"` (`INTMAX_MIN`) | 10 | `INTMAX_MIN` | terminating NUL | sentinel |
| `"-9223372036854775809"` (`INTMAX_MIN - 1`) | 10 | `INTMAX_MIN` | terminating NUL | `ERANGE` |
| `"12abc"` | 10 | `12` | points at `'a'` | sentinel |
| `"abc"` (no digits) | 10 | `0` | equals `nptr` (points at `'a'`) | sentinel |
| `""` (empty) | 10 | `0` | equals `nptr` | sentinel |
| `"0"` | 10 | `0` | terminating NUL | sentinel |
| `"018"` (leading zero, base 10 -- not octal) | 10 | `18` | terminating NUL | sentinel |
| `"-"` (sign only, no digits) | 10 | `0` | equals `nptr` (endptr does not even advance past the sign, since no digit was consumed) | sentinel |
| `"017"` (leading zero, base `0` autodetect) | 0 | `15` (octal) | terminating NUL | sentinel |
| `"0x1F"` (base `0` autodetect) | 0 | `31` (hex) | terminating NUL | sentinel |
| `"42"` (no prefix, base `0` autodetect) | 0 | `42` (decimal) | terminating NUL | sentinel |
| `"1010"` | 2 | `10` | terminating NUL | sentinel |
| `"1f"` (lowercase hex digit) | 16 | `31` | terminating NUL | sentinel |
| `"1F"` (uppercase hex digit) | 16 | `31` | terminating NUL | sentinel |
| `"z"` (lowercase, highest base-36 digit) | 36 | `35` | terminating NUL | sentinel |
| `"Z"` (uppercase, highest base-36 digit) | 36 | `35` | terminating NUL | sentinel |
| `"5"` | 1 (invalid: below 2, and not the autodetect value 0) | `0` | equals `nptr` (nothing consumed) | `EINVAL` |
| `"5"` | 37 (invalid: above 36) | `0` | equals `nptr` | `EINVAL` |
| `"5"` | 10 | `5`, called with `endptr = NULL` | (no crash; nothing to inspect) | sentinel |
| overflowing digit string with more valid digits *after* the overflow point, e.g. `"99999999999999999999999999"` followed immediately by more digits then a non-digit, all base 10 | 10 | `INTMAX_MAX` | scans past **every** digit character, not just up to the point overflow was first detected (`_strtol.h`'s `if (any < 0) continue;` keeps consuming recognized digits without accumulating once overflow is latched) | `ERANGE` |

String (`strcpy`, bounds probes with canaries, matching this project's
existing exact-byte-boundary probe style):

| Case | Assertion |
| --- | --- |
| Ordinary copy into an exactly-sized destination | destination equals source through its NUL; return value equals destination pointer |
| Empty source (`""`) | destination's first byte becomes NUL; no bytes beyond it are touched |
| Destination has a trailing canary byte one past where the NUL lands | canary byte is provably unmodified |
| Destination has a leading canary byte one before the copy starts | canary byte is provably unmodified (this function never writes before `dst`) |

`ERANGE` `strerror` mapping (added by review, missing from the first
draft):

| Case | Assertion |
| --- | --- |
| `cb_libc_strerror(CB_ERANGE)` | returns a non-`NULL`, non-empty string distinct from every other existing `cb_error` message |

String (`strcpy`, bounds probes with canaries, matching this project's
existing exact-byte-boundary probe style):

| Case | Assertion |
| --- | --- |
| Ordinary copy into an exactly-sized destination | destination equals source through its NUL; return value equals destination pointer |
| Empty source (`""`) | destination's first byte becomes NUL; no bytes beyond it are touched |
| Destination has a trailing canary byte one past where the NUL lands | canary byte is provably unmodified |
| Destination has a leading canary byte one before the copy starts | canary byte is provably unmodified (this function never writes before `dst`) |

`isdigit`/`isspace` (exhaustive over all 257 relevant inputs, not a sample):

| Case | Assertion |
| --- | --- |
| Every `unsigned char` value `0..255` | `isdigit` true only for `'0'..'9'`; `isspace` true only for the C-locale whitespace set |
| `EOF` (`-1`) | both return false, not undefined behavior (this is the one negative `int` value the standard requires support for) |

## Independent worker steps and acceptance

1. `CONV-01`: implement exactly the surface proposed above (three unchanged
   upstream imports, one import-only `assert.h` and one empty
   `nbtool_config.h`, two small new functions, one new error code plus its
   `strerror` mapping). Temporarily removing a symbol (e.g.
   `cb_libc_isdigit`/`cb_libc_isspace` or the `strtoimax` link-name
   declaration) and confirming a link/compile failure is a **source-boundary
   negative control** -- it proves the build genuinely depends on that
   symbol, not that a behavior was captured by a test before the behavior
   existed. Label it as exactly that, not as "red-first," per this
   project's own established distinction (see `BASENAME-01`'s notes for
   the same care). Genuine behavioral red-first evidence is the numeric/
   string/`ctype` test matrix above: write it to fail against a stub or
   missing implementation first, then implement. This design's own
   `_Static_assert(sizeof(intmax_t) == 8, ...)` compile check has already
   been run on the pinned Retro68 image (see above); the implementing task
   should still compile it again itself, on both targets, as part of its
   own red/green record, rather than importing this design's result by
   reference only. Exact CI on all three Woodpecker checks. Runtime Mac
   guest acceptance is a libc/runtime behavior change and **must never be
   marked "not required"** merely because the guest slot is unavailable --
   record it as **pending** until the coordinator actually runs the exact
   built artifact in Basilisk II, exactly like every other runtime-affecting
   task in this project's backlog.
2. `HEAD-01` (deferred, per the existing plan): once `CONV-01` and this
   plan's other still-open prerequisites (`GETOPT-02`, `ERR-02`, `STDIN-01`,
   `FWRITE-01`, `ARGV-01`) are all actually merged, assemble the unchanged
   `head.c` import itself, including its own elevated
   `requested_stack_size` for the pinned source's `65536`-byte automatic
   buffer. Not started or claimed here.
