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

## Proposed contract: import `strcpy` unchanged, exactly like `strlen`/`strchr`

The pinned revision's `common/lib/libc/string/strcpy.c`
(`sha256:36754cc692e0df72390e24cfd585a1fb9343257ae6edc4052771b1e5a47c9fad`,
three-clause Regents 1988/1993, fetched and read for this design) is a
seven-line, dependency-free loop -- no macro template, no locale, nothing
transitive beyond the standard's own undefined-on-overlap/under-size
contract already noted above. It is proposed for import exactly like
`strlen.c`: a single Makefile `-Dstrcpy=cb_libc_strcpy` rename, archived into
`libcannedbsd.a`, with `<string.h>` adding `#define strcpy cb_libc_strcpy`
alongside the existing `strlen`/`strchr`/`strerror` mappings. No wrapper,
no task-owned state, no ABI involvement -- the same size of change as the
existing `strlen`/`strchr` entries.

## Verified vs. still-to-measure: Retro68 (ILP32) against this host's LP64 toolchain

Measured directly on this design's own host (Apple Clang 17, arm64 Darwin,
a real LP64 target -- not the exact Woodpecker Alpine/GCC/x86_64 image, but
architecturally the same class for this specific question: is `long` 64
bits?):

```
sizeof(int)=4 sizeof(long)=8 sizeof(long long)=8 sizeof(void*)=8
sizeof(intmax_t)=8, INTMAX_MAX=9223372036854775807, INTMAX_MIN=-9223372036854775808
__INTMAX_TYPE__ = long int
```

This confirms, on a real measured LP64 compiler, `intmax_t` is spelled
`long` and is 64 bits, matching what glibc/musl also do on 64-bit Linux (the
actual Woodpecker `ci` target) -- `long` is already 64 bits there, so no
64-bit-arithmetic-on-a-32-bit-register concern exists on that side at all.

**Not measured here, and explicitly flagged as such rather than assumed**:
the pinned Retro68 image (`ghcr.io/autc04/retro68@sha256:459dd3ea9856262162615527021be7b64f198631dc59cca8cedbf197b7656019`,
read from `.woodpecker/mac68k.yml`) requires Docker, which remains down on
this host (the same outage that blocked local execution for `BASENAME-01`
and `ECHO-01`). What is known with high confidence from the m68k target's
own well-established GCC ABI, without needing to run anything: `int`/`long`
are 32 bits and pointers are 32 bits (ILP32) on classic m68k, but `long
long` has always been a real, GCC-supported 64-bit type there too, so C99's
own requirement ("`intmax_t` is the widest signed integer type the
implementation supports") forces the *spelling* to differ: `intmax_t` must
be `long long` on Retro68's target, not `long`. Code must therefore never
assume the two platforms' `intmax_t` share an underlying spelling -- always
route through `<inttypes.h>`'s `intmax_t`/`INTMAX_MAX`/`INTMAX_MIN`, never
`long`/`LONG_MAX` -- which is exactly what the proposed import already does,
and is precisely the kind of assumption a design should make impossible to
get away with silently, not just avoid making itself.

The one thing this measurement gap must not become is a silent assumption
inside the implementation: the bounded next step (not claimed done here) is
a `_Static_assert(sizeof(intmax_t) == 8 && INTMAX_MAX ==
9223372036854775807LL, ...)` compiled on **both** targets as part of that
task's own red/green evidence, the same "measure, do not assume" discipline
`utility-roadmap-20260908.md` already applied to head/echo's own missing
declarations. Until that assertion has actually been compiled on the
pinned Retro68 image, "Retro68's `intmax_t` is 64 bits" remains a
well-founded expectation from general m68k GCC ABI knowledge, not a
measured cannedBSD fact.

## No private 64-bit-arithmetic compiler helper is needed

`_strtol.h`'s accumulation loop (`acc *= base; acc += i;` and the symmetric
negative-side subtraction) performs ordinary `intmax_t` multiply/add. On
Retro68's 32-bit m68k target, since `intmax_t` there is a 64-bit `long long`
on a CPU with no native 64-bit multiply/divide instruction, GCC will emit
calls to its own `libgcc` runtime routines (`__muldi3`, `__divdi3`, and
similar) exactly as it already must for *any* `long long` arithmetic
anywhere in this project's Mac target, head.c's own conversion code
included. This is not something cannedBSD needs to implement: `libgcc` is
the C compiler's own runtime support library, linked automatically by an
ordinary (non-freestanding) build. Checked directly for this design: neither
`platform/mac68k/CMakeLists.txt` nor `platform/mac68k/ci-build.sh` passes
`-nostdlib`, `-nodefaultlibs`, or any other flag that would exclude it, so
the existing Mac build already links `libgcc` normally today. No new
"private compiler helper" is proposed; the bounded next step's own
`_Static_assert`-style smoke build (above) is also the natural place to
confirm this by simply linking successfully, not by writing new code.

## Proposed additions, scoped exactly to this evidence

- `libc/include/ctype.h` (new): `isdigit`, `isspace` only. Not `isalpha`,
  `isupper`, `isspace_l`, or any other `ctype` function nothing in this
  task's traced dependency chain calls.
- `libc/include/inttypes.h` (new): `intmax_t`, `INTMAX_MAX`, `INTMAX_MIN`,
  `strtoimax`. Not `strtoumax`, not the `PRId64`-style format-macro family
  head.c never uses.
- `libc/include/errno.h`: add `#define ERANGE CB_ERANGE`, and add
  `CB_ERANGE` to `enum cb_error` in `include/cannedbsd/abi.h` (matching
  POSIX/NetBSD's own numeric value `34`, placed near the existing `EPIPE =
  32` entry for readability -- exact placement is the implementing task's
  call, not fixed here). This is a plain named-constant addition to an
  enumeration of independent error codes, not a versioned struct field, so
  no ABI-tail positional concern applies to it the way it does for
  `cb_api_v1`'s function-pointer table.
- `libc/include/string.h`: add `#define strcpy cb_libc_strcpy`.
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

Numeric (`strtoimax`, base 10 only, matching head.c's own exclusive usage,
but testing the full declared interface's contract since nothing about a
single caller's habits should leave the *function* itself narrower than its
name promises):

| Input | Expected result | Expected `*endptr` | Expected `errno` |
| --- | --- | --- | --- |
| `"10"` | `10` | points at the terminating NUL | unchanged (not set) |
| `"-10"` | `-10` | points at the terminating NUL | unchanged |
| `"  42"` (leading whitespace) | `42` | points at the terminating NUL | unchanged |
| `"+5"` | `5` | points at the terminating NUL | unchanged |
| `"9223372036854775807"` (`INTMAX_MAX`) | `INTMAX_MAX` | terminating NUL | unchanged |
| `"9223372036854775808"` (`INTMAX_MAX + 1`) | `INTMAX_MAX` | terminating NUL | `ERANGE` |
| `"-9223372036854775808"` (`INTMAX_MIN`) | `INTMAX_MIN` | terminating NUL | unchanged |
| `"-9223372036854775809"` (`INTMAX_MIN - 1`) | `INTMAX_MIN` | terminating NUL | `ERANGE` |
| `"12abc"` | `12` | points at `'a'` | unchanged |
| `"abc"` (no digits) | `0` | equals `nptr` (points at `'a'`) | unchanged |
| `""` (empty) | `0` | equals `nptr` | unchanged |
| `"0"` | `0` | terminating NUL | unchanged |
| `"018"` (leading zero, base 10 -- not octal) | `18` | terminating NUL | unchanged |

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
   upstream imports, one empty import-only header, two small new functions,
   one new error code). Genuinely red-first: temporarily omit
   `cb_libc_isdigit`/`cb_libc_isspace` or the `strtoimax` rename and confirm
   an actual link/compile failure before restoring them, per this project's
   standard practice. Compile the `_Static_assert(sizeof(intmax_t) == 8,
   ...)` smoke check on both the Linux `ci` target and the pinned Retro68
   image as this task's own first falsifiable step, not assumed from this
   design. Exact CI on all three Woodpecker checks; an ordinary Mac
   acceptance probe if the coordinator's guest-acceptance slot allows it,
   documented as not required otherwise with a reason, exactly as
   Documentation-only tasks already do.
2. `HEAD-01` (deferred, per the existing plan): once `CONV-01` and this
   plan's other still-open prerequisites (`GETOPT-02`, `ERR-02`, `STDIN-01`,
   `FWRITE-01`, `ARGV-01`) are all actually merged, assemble the unchanged
   `head.c` import itself, including its own elevated
   `requested_stack_size` for the pinned source's `65536`-byte automatic
   buffer. Not started or claimed here.
