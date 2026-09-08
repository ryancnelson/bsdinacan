# SOLARIS-01: Solaris 9 SPARC portability gate (source audit, in progress)

Base: freshly fetched `origin/main` (`a1c85ba`). Branch `work/SOLARIS-01`
claimed and pushed early (per `AGENTS.md`'s race-avoidance convention:
use the exact branch name so a second worker fails to claim the same
ID) before this substantive content existed.

**Status: bounded source audit and minimal, mostly-inert build
preparation. No native Solaris 9 execution has occurred.** Guest rig
coordinates were provided in this session's chat and access was
subsequently reasserted by the coordinator directly ("you have
exclusive rig access now"). Everything in this note up to that point
is local source work performed without touching that rig, exactly as
the backlog entry allows ("do useful local source work while
coordinator resolves rig access; no claims of native Solaris success").
Guest verification itself is recorded separately below once attempted.

## Historical reference: what to reconcile, not import

`work/SOLARIS-01-reference` (`a28f9ed`, "Qualify Solaris 9 SPARC runtime
and fix review regressions") is the prior local port, explicitly marked
by the coordinator as reference material only, not proof current `main`
passes, and not to be replaced wholesale. Reading its full diff:

- **Genuinely Solaris-specific** (reconciled into this branch, see
  below): the `makecontext` high-stack convention, `gethrtime()` for
  monotonic time, the `compat/solaris9/include/stdint.h` adapter, and
  the `-std=gnu99 -D__EXTENSIONS__`/`SHELL=/bin/ksh`/`-lrt` build
  requirements, all in what was then `src/host_linux.c` and its build
  invocation.
- **An unrelated file rename**: `src/host_linux.c` → `src/host_posix.c`,
  rippling into `src/main.c`, `src/internal.h`, and the Makefile's
  forbidden-symbol architecture check (`-g '!host_linux.c'`). Not
  carried forward -- the conditional adaptations do not require a
  rename, and a rename is exactly the kind of broad, non-essential
  churn the coordinator's "do not wholesale import" instruction warns
  against.
- **Unrelated review fixes bundled into the same historical commit**:
  ~110 changed lines in `src/vfs.c` (path resolution: `.`/`..`
  traversal, directory-type checks on intermediate components), a
  small `src/ramfs.c` fix (truncate a file's size only *after*
  `cb_open_file_create` succeeds, not before -- the original order
  destroyed data on an allocation failure), and a small `src/shell.c`
  fix (initialize every stage's redirection descriptors to `-1` in
  their own pass *before* attempting to open any of them, so a
  mid-pipeline failure doesn't leave later slots as uninitialized
  stack garbage during cleanup). **Checked directly against current
  `main`, not assumed: none of these three are present today.**
  `src/ramfs.c`'s `ramfs_open` still zeroes `node->size` *before*
  `cb_open_file_create`, and `src/shell.c`'s `run_pipeline` still
  combines descriptor initialization and `open_redirections` in one
  loop. An earlier draft of this note and of `SOLARIS9.md` claimed
  `VFS-02`/`VFS-03` had already superseded these -- that claim was
  never actually verified against the current source and was wrong;
  corrected here after reading `src/ramfs.c` and `src/shell.c` directly.
  These are real, still-open, pre-existing bugs, independent of
  Solaris portability. **Not imported here regardless** -- fixing them
  is legitimately out of scope for a portability audit branch, and
  bundling unrelated correctness fixes into this branch would itself
  be scope creep -- but the correct reason to exclude them is "out of
  scope," not a false claim that they no longer matter.
- **A large Makefile refactor** (~123 changed lines) adding a `HOST=`
  switch, and a `test`/`test-runtime`/`test-core` target split. Not
  reproduced as a Makefile change: the current Makefile already
  supports `CC ?=`/`CPPFLAGS ?=`/`CFLAGS ?=` overrides from the
  environment, and specific file targets (`build/bsdinacan`,
  `build/test_core`) can be built directly without invoking the
  aggregate `test` target at all. `tools/solaris9-build.sh` (below)
  uses exactly that: zero Makefile changes, and it does not invoke
  `gmake test`, reproducing only the *portable subset* of what the
  reference's own `test-runtime` target did (see below).

## What this branch actually changes

1. **`src/host_linux.c`** (no rename): `#ifdef CANNEDBSD_SOLARIS9`
   blocks for the `makecontext` stack-pointer convention and
   `gethrtime()`-based monotonic time; unconditionally added
   `uc_stack.ss_flags = 0` (explicit, harmless on every platform, from
   the reference).
2. **`compat/solaris9/include/stdint.h`** (new file, verbatim from the
   reference): wraps Solaris 9's `inttypes.h`-only exposure of
   fixed-width types and supplies the missing `SIZE_MAX`. Only reached
   when `-Icompat/solaris9/include` is passed explicitly. **Not
   independently re-verified against a real guest's actual header
   layout** -- flagged as an open item below, not assumed correct
   merely because it matches the historical reference.
3. **`src/core.c`** (`api_read`/`api_write`) and **`libc/cb_libc.c`**
   (`cb_libc_fread`): wrapped each's `count/request > INT64_MAX` check
   in `#if SIZE_MAX > INT64_MAX`, mirroring the guard already present
   at `cb_libc_fwrite`. This *is* a real, non-inert change on a 32-bit
   `size_t` host: it was directly verified to remove the exact
   `-Wtype-limits` "comparison is always false" warning previously
   observed, unguarded, in a real pinned-Retro68 Mac68k build of this
   same code (both call sites). Behavior is unchanged on every current
   platform (the check was already unreachable at those two call sites
   on any 32-bit `size_t` host, guard or no guard, since such a
   `size_t` can never numerically exceed `INT64_MAX`) -- but this
   *does* change Mac68k's compiled object code (the check is compiled
   out there now), which is why this is called out separately from the
   `host_linux.c` changes above, which are genuinely inert everywhere
   except under `-DCANNEDBSD_SOLARIS9`.
4. **`tests/test_core.c`** (`overflowprobe_main`): this existing test
   assumed a host where `SIZE_MAX > INT64_MAX` always holds. Found via
   direct trace, not assumed: on an ILP32 host, `count = SIZE_MAX` can
   never actually exceed `INT64_MAX` (true independent of item 3's
   guard -- the comparison already evaluates false there at runtime
   either way), so `api->read(descriptor, &byte, SIZE_MAX)` would fall
   through past the intended rejection and reach the real `read()`
   implementation. **Correction to an earlier draft of this note**: that
   draft additionally claimed the *write* call would then reach the
   real syscall with a huge count against this 1-byte buffer, calling it
   a "genuine out-of-bounds read" -- that claim was wrong, caught on
   review, because it never traced the actual control flow. The file is
   freshly truncated (empty), so the real `read()` call returns a clean
   `0` (nothing to transfer, regardless of the requested count); the
   test's own check is `result != -1`, and `0 != -1` is true, so the
   test returns `192` immediately -- **the `write()` call is never
   reached at all** on this exact test as originally written. The
   actual, only-reachable defect on ILP32 is an invalid, LP64-only test
   expectation (assuming `EINVAL` where the real, correct behavior is a
   clean `0`-byte read), not a host syscall buffer overrun. The fix
   below is kept regardless, as good practice that avoids ever
   depending on an untested, fragile write-side path -- but that is a
   design-hygiene reason, not a memory-safety one. Split into an
   `#if SIZE_MAX > INT64_MAX` (unchanged assertion, exercised
   identically on Linux today) / `#else` pair; the new ILP32 branch
   instead seeks to an offset at `SIZE_MAX` (safe and meaningful on any
   word size, since `cb_off_t` is a fixed 64-bit type) and confirms an
   ordinary, bounded, small read there cleanly returns `0` (EOF) -- a
   real, safe, still-meaningful assertion, not a dropped or weakened
   test.
5. **`tools/solaris9-build.sh`** (new): sets `CC`, `CPPFLAGS`
   (`-D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9
   -Icompat/solaris9/include -Iinclude -Isrc` -- deliberately *not*
   `-Ilibc/include`, see below), `CFLAGS` (`-std=gnu99 ... -pedantic`,
   not strict `-std=c99`, per the reference's own recorded finding that
   GCC 3.4.6 needs GNU C99 mode for the guest's own headers), and
   `LDLIBS=-lrt`; builds `build/bsdinacan build/test_core` directly
   with `SHELL=/bin/ksh`; runs `./build/test_core`, `test_launcher.sh`,
   and the same acceptance-output shell-pipeline assertions the
   reference's own `test-runtime` target used. Deliberately does *not*
   invoke `gmake test`: that aggregate target depends on
   `check-architecture` (uses `rg`, not assumed present in the guest)
   and `tests/test_one_process.sh` (reads Linux's `/proc`) -- neither
   applicable to a Solaris guest; those, and the source-provenance
   checks, are host-side checks already covered by Linux `make ci`.
6. **`SOLARIS9.md`** (new): honest status doc, corrected per the same
   review that caught the false VFS-superseded claim.

Confirmed scope: `git diff --stat origin/main` (after fetching current
`origin/main`, which has moved forward since this branch's own base)
shows exactly these six files plus this note changed or added, nothing
else.

## Why `-Ilibc/include` is not in the build script's global CPPFLAGS

`libc/include/` holds this project's own private NetBSD-import veneer
headers -- its own `string.h`, `stdio.h`, `stdlib.h`, `inttypes.h`,
`sys/types.h`, `unistd.h`, etc. -- already added by the Makefile's own
existing per-command-object rules, only for the specific upstream
NetBSD command sources built against that veneer (e.g. `commands/
wc.c`, `upstream/netbsd/usr.bin/yes/yes.c`). Adding `-Ilibc/include`
to a *global* `CPPFLAGS` override, as an earlier draft of this script
did, would redirect *every* compilation unit's ordinary standard-
header includes -- including `src/host_linux.c`'s own `#include
<time.h>`/`<ucontext.h>` and `src/core.c`'s `#include <stdio.h>` --
through this private veneer instead of the guest's real system
headers. That is a real, caught-in-review bug in the first draft of
this script, now fixed: the script's `CPPFLAGS` includes only
`-Icompat/solaris9/include` (which has no name collisions with the
per-command veneer -- it contains only the `stdint.h` adapter) plus the
ordinary `-Iinclude -Isrc`.

## Audit: the specific items the coordinator asked about

- **stdint/inttypes include cycle**: Solaris 9's own `<stdint.h>` does
  not exist as a standalone header at this vintage; fixed-width types
  and limits come through `<inttypes.h>`. `SIZE_MAX` specifically is
  absent and must be supplied. Adapter added at
  `compat/solaris9/include/stdint.h`, verbatim from the historical
  reference. **Open item, not resolved**: whether `-Icompat/solaris9/
  include`'s own `<inttypes.h>` reference inside that adapter actually
  resolves to the guest's real system `inttypes.h` (and not, say, some
  other private header earlier on the search path) depends on the
  guest's actual header layout and the exact include-path order used
  at build time -- this has not been independently verified against a
  real guest yet, and is not assumed safe merely because the file's
  own content is unchanged from the reference.
- **`gethrtime()`**: Solaris 9's `clock_gettime(CLOCK_MONOTONIC, ...)`
  support is not assumed reliable at this vintage; `gethrtime()`
  (nanosecond-resolution, monotonic, documented Solaris-native API) is
  the substituted source, requiring `librt` at link time. Reconciled
  as a conditional branch in `host_monotonic_millis()`; the existing
  `clock_gettime`-based `host_wall_clock_millis()` (wall clock, not
  monotonic) was not touched by the reference and is left as-is here
  too -- worth flagging as a real, unresolved question: is Solaris 9's
  `clock_gettime(CLOCK_REALTIME, ...)` reliable enough to leave
  unconditional? Not independently verified; carried over as an open
  item rather than guessed at.
- **Original Solaris 9 high-stack `makecontext` ABI**: Solaris 10
  changed `makecontext`'s stack-pointer convention; Solaris 9 (pre-10)
  takes the *high* stack address (`base + size - 8`), not the base, per
  Oracle's own `swapcontext(3C)` compatibility notes. The historical
  reference recorded a canary confirming the high-stack convention
  resumes correctly in-guest, and that the ordinary base-stack
  convention crashes under it -- carried forward as historical evidence,
  not re-independently verified here yet.
- **ILP32 offset bounds**: audited every `> INT64_MAX`/`SIZE_MAX`-
  adjacent comparison in `src/core.c`, `libc/cb_libc.c`, and
  `src/ramfs.c`. Found and fixed the two unguarded tautological checks
  (item 3 above) and the invalid LP64-only assumption in
  `overflowprobe_main` those checks' unreachability exposes on ILP32
  (item 4 above) -- the latter would have surfaced as a genuine test
  failure (an early `return 192`, not a crash -- see item 4's own
  correction) the first time this suite actually ran on a genuine ILP32
  target. `src/ramfs.c`'s own
  overflow check in its seek path (`base > INT64_MAX - offset`/`base <
  INT64_MIN - offset`) operates on `cb_off_t`, a fixed-width 64-bit
  offset type independent of the host's `size_t` width, so it is not in
  the same tautology class and needs no change. Not claimed as an
  exhaustive audit of the entire tree -- only of the code paths
  reachable from the reference's own changes plus a direct grep for the
  same comparison pattern.

## Preserved, unchanged

- `CB_MAX_PROGRAMS` (64) and every existing registration table.
- The WRITE runtime: `cb_libc_fwrite`/`cb_libc_fread`'s retry/error
  semantics are untouched; only a compile-time guard was added around
  one of `cb_libc_fread`'s existing checks (inert on Linux, real but
  behavior-preserving on Mac68k -- see item 3 above).
- The public ABI (`struct cb_api_v1`, `struct cb_host_ops_v1`, etc.):
  no field added, removed, or reordered.
- All existing tests: on the CI runner host `biggie`, via `rsync` (not
  a real clone, so precisely stated below rather than glossed as "make
  ci passed"): `make test` completes fully and shows identical results
  to `origin/main`. `make ci` runs through `check-linux-write`,
  `check-acceptance-output`, the Mac guest Python test suite, and (per
  its own recipe order) architecture/build-mode/clean-test/analyzer
  checks *before* reaching `check-publication`, which fails there --
  not from a real regression, but because the `rsync`'d copy used for
  this verification has no `.git` directory for `tests/test_publication.sh`
  to inspect. This is a real gap in *how this was verified*, not a
  claim that the complete `ci` gate passed end to end; this worktree's
  own `git status` is clean apart from the files listed above, and the
  actual git-based `check-publication` has not been separately
  re-confirmed after this round's edits (see the commit-scope check
  before pushing, which is the equivalent real check for this
  specific gate). The Mac68k build (`platform/mac68k/build.sh`) also
  still succeeds, and no longer emits the two `-Wtype-limits` warnings
  previously observed.

## Not claimed

No native Solaris 9 SPARC build, boot, or execution as of this section
of the note (see below for what was actually attempted once guest
access was asserted). `CB_MAX_PROGRAMS`'s 64-slot capacity, the WRITE
runtime, and the public ABI are unchanged. The three pre-existing,
non-Solaris-specific issues named above (`ramfs_open`, `run_pipeline`,
`src/vfs.c` path resolution) are real and still open on current `main`;
they are out of scope for this task and not fixed here. The historical
reference's own 2026-09-07 qualification result
(`SOLARIS9_CANNEDBSD_TEST=PASS` inside SunOS 5.9 `sun4m`) is carried
forward as historical evidence only, not promoted to acceptance of
current `main`. Solaris acceptance for this task remains pending; the
coordinator carries that gate forward per `notes/CI.md`'s transition
policy.
