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

## First real guest attempt: diagnosis, not acceptance evidence

With the coordinator's assigned guest slot, staged this exact commit's
source onto the guest (via a Rock-Ridge ISO built from a `ustar`-format
tarball -- a plain macOS `tar`'s default PaxHeader extended-attribute
entries are not understood by this guest's own `tar`/`mkisofs` and
corrupt the extraction; `COPYFILE_DISABLE=1 tar --format=ustar` avoids
them) and ran `tools/solaris9-build.sh` for real. **This surfaced two
further genuine build blockers this branch had not yet encountered**,
found by reading actual GCC 3.4.6 diagnostics, not by inspection:

1. **The private NetBSD-import veneer's own `libc/include/inttypes.h`
   shadowed the real system header.** The Makefile's own per-command-
   object rules add `-Ilibc/include` for sources built against that
   veneer (e.g. `commands/wc.c`); since that directory sits on the
   include path before the guest's real system headers regardless of
   where `-Icompat/solaris9/include` itself is listed, an angle-bracket
   `#include <inttypes.h>` inside `compat/solaris9/include/stdint.h`
   re-entered the search from the top and found the private veneer's
   own `inttypes.h` first, which failed to compile in this context
   ("syntax error before cb_libc_strtoimax"). This is exactly the
   "cycles into our private inttypes before fundamental types" concern
   raised in review before any guest access existed -- now confirmed
   for real, and fixed for real: `compat/solaris9/include/stdint.h` now
   includes the guest's real system header by its verified absolute
   path (`/usr/include/inttypes.h`, confirmed present and correctly
   guarded on the actual guest), sidestepping the search-path ambiguity
   entirely rather than guessing at include-order fixes.
2. **The guest's own system headers trigger a GCC 3.4.6 warning that
   `-Werror` turns fatal, unrelated to this project's own source.**
   `/usr/include/inttypes.h` itself contains a Sun-specific `#pragma
   ident "..."` that GCC 3.4.6 does not recognize
   ("warning: ignoring #pragma ident"); under `-Werror` this aborted
   the build. Added `-Wno-unknown-pragmas` to `tools/solaris9-build.sh`'s
   `CFLAGS` only, narrowly scoped to this one class of vendor-header
   warning -- every warning this project's own source can trigger stays
   fatal; this is not a general weakening of the shared `-Werror` gate.
3. **A genuine, GCC-3.4.6-specific warning in this project's own test
   source, still fixed at the source level, not suppressed.**
   `tests/libc_fwrite_probe.c`'s `invalid_streams` case deliberately
   synthesizes a garbage, non-NULL `FILE *` (`(FILE *)&argc`) to
   exercise `fwrite`'s own validity check -- never dereferenced as a
   real `FILE`, so there is no actual aliasing violation. GCC 3.4.6
   nonetheless warns "type-punning to incomplete type might break
   strict-aliasing rules" for a direct cast to a pointer-to-incomplete
   type (`FILE` is opaque in this project's own headers); no other
   pinned toolchain (Linux, Retro68 Mac68k) warns on this line. Fixed
   by routing through an intermediate `(FILE *)(void *)&argc` cast --
   semantically identical, and confirmed via a real, isolated guest
   recompile of the patched line before committing it, not merely
   reasoned about.

Progress as of this note: `build/bsdinacan` linked successfully and
compilation reached deep into the test-object list before hitting
finding 3 above; that fix has not yet been re-verified with a full,
uninterrupted guest run reaching `SOLARIS9_CANNEDBSD_TEST=PASS`. A
partial, patched-guest compile succeeding past a given point is
diagnosis of what still needs fixing, not exact-source acceptance
evidence -- that requires a complete, fresh run producing the actual
final marker, recorded here once it exists. Also noted per the
coordinator: `origin/main` has since advanced past this branch's own
base (now including `STAT-01` and the full, corrected `HEAD-02`); final
qualification must be re-run after merging fresh `origin/main` into
this branch, not asserted against the older base alone.

All three fixes above were re-verified on the pinned Linux CI toolchain
(`make test` unchanged) and the pinned Retro68 Mac68k build (still
succeeds) before being committed, exactly as with every other change in
this branch.

## Two further guest-found fixes, then a genuine, fresh PASS

Continuing past the point above surfaced two more real GCC 3.4.6
issues, both in `tests/test_core.c`, fixed and re-verified the same way
(guest diagnostic first, fix committed after Linux/Mac68k
re-verification, then re-tested on the guest):

- `yesprobe_main`/`stdioepipeprobe_main` zero-initialize
  `struct cb_spawn_action_v1` arrays. Neither `= {{0}}` ("missing
  initializer" for the nested struct's fields) nor `= {0}` ("missing
  braces around initializer") satisfies GCC 3.4.6's `-Wextra` for this
  pattern -- confirmed by trying both on the real guest. Replaced with
  plain declarations plus `memset(..., 0, sizeof(...))`, unambiguous on
  every pinned toolchain.
- `truncateprobe_main` already split ILP32 vs. LP64 behavior with a
  *runtime* `if (sizeof(size_t) < sizeof(cb_off_t))`, but a runtime
  `if`/`else` still requires the compiler to fully type-check both
  branches regardless of which is reachable: the LP64-only branch's
  `(uint64_t)resize_request_size >= (uint64_t)INT64_MAX` is still a
  compile-time tautology GCC 3.4.6 warns on for a 32-bit `size_t`
  variable, cast or not. Converted to a preprocessor
  `#if SIZE_MAX > INT64_MAX`/`#else`, which actually excludes the
  unreachable branch from compilation, matching the pattern already
  used in `src/core.c`/`libc/cb_libc.c`.

With those fixed (commit `9aff546`), a complete, uninterrupted guest
run produced this exact, fresh output (not historical, captured
directly from `/var/tmp/sol01fix7.log` on the guest immediately after
the run):

```
make: warning:  Clock skew detected.  Your build may be incomplete.
Orequired getopt tests passed
argv ownership tests passed
fread tests passed
file ownership tests passed
stdin tests passed
clockloss test passed
runnable timeout test passed
all core tests passed
launcher test passed
build/bsdinacan:        ELF 32-bit MSB executable SPARC Version 1, dynamically linked, not stripped
SOLARIS9_CANNEDBSD_TEST=PASS
```

`build/test_core` exists on the guest (`930336` bytes, real ELF, dated
by the guest's own clock). The three acceptance-output assertions in
`tools/solaris9-build.sh` (`HELLO` pipe, exit-status `1`, `wc -c` = `5`)
are each gated by `set -eu` plus an explicit `test ... || exit 1`; since
the script ran all the way to its own final `SOLARIS9_CANNEDBSD_TEST=PASS`
print, all three passed silently (a failure there would have stopped
the script before that line). The guest's own clock reads `2026-09-05`
(behind real time; this is the same clock skew the build already warns
about, and it does not affect source identity, only the displayed
timestamp) -- the commit actually tested is `9aff546`, confirmed by
this being the exact content staged onto the guest in this run.

**This is real, fresh, exact-commit guest evidence -- not a promoted
historical result.** It is still not yet "final Solaris qualification"
by the coordinator's own stated bar: `origin/main` has advanced past
this branch's base (now including `STAT-01` and the corrected
`HEAD-02`, with its full `31`-case/read-count/`memset`-storage/behavioral-probe
suite), and the coordinator explicitly authorized merging fresh
`origin/main` into this branch before that qualification is asserted.
That merge and re-test follow next, recorded separately below.

## Final qualification: merged `origin/main`, genuine fresh PASS

Merged fresh `origin/main` (`eaff869`, bringing in `STAT-01`, the
corrected `HEAD-02` with its full `31`-case suite, `tee_state_probe.c`,
and other work landed since this branch's own base) into
`work/SOLARIS-01` as a real merge commit (`698541f`, `git merge
origin/main --no-edit`, auto-merged cleanly -- no conflicts in
`Makefile` or `tests/test_core.c`). Re-verified before pushing: `make
test` on the pinned Linux CI toolchain unchanged; `bash
tests/test_publication.sh` (the real, git-based check, not the
`.git`-less rsync-copy artifact noted earlier) passes cleanly; the
pinned Retro68 Mac68k build still succeeds. Pushed; Woodpecker's `ci`,
`mac-automation`, and `mac68k` all report `success` on `698541f`.

Staged this exact merged commit onto the guest the same way as every
round before (`ustar` tarball -> Rock-Ridge ISO -> `locking:off` media
swap -> guest-side `mount`/`cpio` -> `tools/solaris9-build.sh`). Every
source file newly brought in by the merge (`tests/tee_state_probe.c`,
the rewritten `tests/head_probe.c`, `libc/include/sys/stat.h`, etc.)
compiled cleanly on GCC 3.4.6 with no new warnings or errors -- the
five fixes already made in this branch were sufficient; no sixth
guest-specific bug surfaced from the newly merged content. A complete,
uninterrupted run produced this exact, fresh output, captured directly
from `/var/tmp/sol01merged.log` on the guest immediately after the run
(paths confirmed absolute, not relative to a stale shell `cwd`, since
this persistent console session's working directory had drifted from
an earlier round):

```
make: warning:  Clock skew detected.  Your build may be incomplete.
Orequired getopt tests passed
argv ownership tests passed
fread tests passed
file ownership tests passed
stdin tests passed
clockloss test passed
runnable timeout test passed
all core tests passed
launcher test passed
build/bsdinacan:        ELF 32-bit MSB executable SPARC Version 1, dynamically linked, not stripped
SOLARIS9_CANNEDBSD_TEST=PASS
```

Confirmed via absolute-path `ls` on the guest (not the drifted relative
`cwd`): `/var/tmp/sol01merged/build/test_core` (`960028` bytes) and
`/var/tmp/sol01merged/build/bsdinacan` (`308368` bytes) both exist,
both freshly dated to this run. Guest: `SunOS solaris 5.9 Generic sun4m
sparc SUNW,SPARCstation-5`; guest clock read `2026-09-05` at capture
time (the same pre-existing clock skew noted throughout this branch --
it affects only the displayed timestamp, not source identity, which is
confirmed by commit `698541f` being the exact content staged for this
specific run).

**This is the final Solaris qualification evidence for this task**:
exact commit (`698541f`), exact guest (the coordinator's assigned
Solaris 9 sun4m rig), exact fresh transcript above, `SOLARIS9_CANNEDBSD_TEST=PASS`,
covering the current merged source -- not the older pre-merge base, not
a historical result promoted forward. Per `notes/CI.md`'s own
transition policy, this is manual/serialized acceptance ahead of
SOLARIS-02's future automated CI; the coordinator owns the actual
integration decision and any handoff/runbook publication from here.

## Coordinator evidence verification and Mac acceptance

Native-tested commit: `698541f1ce96df7c600b463875548af6281f446f`.
The retained staged gzip archive has SHA256
`fd612db0c1c05cf6fd2df66d8b55adcc207af28a7704f0ece2d359281c184325`.
The coordinator compared all 326 archived regular files byte-for-byte against
that commit: zero missing, changed or extra files. The remote staged archive
has the same digest. Staged ISO SHA256:
`c0e5198b33a03ae9d959966c4fe51f5893c05024a02206551d589fac27a8bbb1`.

Retained full raw console capture SHA256:
`05c7e0d383480ac1edef0ef3fba30aa857c5990d61b83ad40b03d8c60db9f54b`.
Cleaned derivative SHA256:
`4ffdba5d00ba05db787f4a9921b5e885845d086f2cf91f5bc5b21a24fbaf483d`.
Both are preserved with the source archive, toolchain capture and invocation
record under the coordinator's SOLARIS-02 worktree in
`evidence/SOLARIS-01-698541f/`. Hash verification passed. The full log contains
GCC 3.4.6 and GNU Make 3.81 identity, clean rebuild commands, all core tests,
launcher tests, and the final Solaris PASS. The successful environment used
`PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin`, `CC=gcc`, `MAKE=make`;
both compiler and GNU make resolve under `/usr/local/bin`. Guest command:
`/bin/ksh tools/solaris9-build.sh` from the fresh extracted source directory.
The script's three output assertions precede its final PASS under `set -eu`.

Independent review of `4060ab01e4cf960eee70b20512867b50e4b26073` found no
runtime blocker or weakened coverage. #394 (native parent) and #395 (note-only
child) each passed ci, mac68k and mac-automation. Fresh exact #395 Mac artifact
passed all 67 records plus ALL PASS in `run-a_8sjg00`; screenshot inspected,
normal application and guest shutdown verified, disks closed and slot released.
Elapsed 21.68 seconds. Archive SHA256:
`c31de39713ec5629a76e9c0eed53be72976c4d603be72675a6984b749e9f6877`.

The subsequent runbook correction changes documentation and source comments
only; an independent lexical comparison confirmed executable C tokens and
non-comment shell lines unchanged. Main integration remains subject to the
coordinator's final exact integration CI and Mac artifact gates.

## Final main integration accepted

Main `adf62f122a675706f0df7681b6cfb0ee2f039062` passed final independent review
and exact #398 ci, mac68k and mac-automation. Its source-token comparison
preserved the qualified native runtime; subsequent changes were documentation
and comments. Fresh Mac `run-rcnizgak` passed all 67 records plus ALL PASS,
screenshot inspected, normal shutdown and closed disks independently checked,
slot released, 22.16 seconds. Exact archive SHA256:
`177166a17d439e09db7cdcfff752f0953efbc529d73be8b41fd3c32bfe5aec13`.
The coordinator pushed main and fast-forwarded the canonical checkout,
preserving its unrelated untracked worker directories. SOLARIS-02 automated
CI remains a separate unfinished task.
