# FTS-CORE-01: fts_open/fts_read/fts_close/fts_set(FTS_SKIP)

**Scope authority:** `notes/iterations/FTS-01-design.md` (design, `work/FTS-01` @ `2bdf4dd`)
and `notes/iterations/FTS-01-design-review.md` (independent review, ACCEPTED &
AUTHORIZED, five mandatory invariants).

**Branch:** `work/FTS-CORE-01`, based on `main` @ `a9ba8d7` (VFS-04, LS-01,
TERM-03, SIG-01 already merged).

## What shipped

- `libc/include/fts.h`: `FTS`/`FTSENT`, `FTS_D/DP/DNR/ERR/NS/DC/W/DEFAULT`,
  `FTS_PHYSICAL/LOGICAL/NOCHDIR/COMFOLLOW/XDEV/NOSTAT/SEEDOT/WHITEOUT`,
  `FTS_SKIP`. Only what the design note's grep of the pinned NetBSD
  `b890038f` sources actually requires -- no `FTS_F`/`FTS_SL`/`FTS_DEFAULT`
  distinctions beyond the single catch-all, no `fts_cycle`, no
  `fts_children`/`fts_link`/`fts_parent` (ls-only, FTS-CHILDREN-01's scope).
- `libc/cb_fts.c`: `fts_open`/`fts_read`/`fts_close`/`fts_set`. Iterative
  (explicit frame stack, no native recursion), built entirely on the
  already-public `cb_libc_opendir`/`readdir`/`closedir` and the one new
  thin pass-through `cb_libc_stat` (added to `cb_libc.c`/`cannedbsd/libc.h`
  alongside the existing `cb_libc_truncate`-style wrappers). Zero new
  kernel ABI, zero `cb_stat_v1` change, matching the review's claim.
- `tests/fts_core_walk.c`, `fts_skip_walk.c`, `fts_close_walk.c`,
  `fts_cycle_walk.c`, `fts_allocfail_walk.c`: standalone libc-consumer
  mains (`#include <fts.h>`), each driven via `cb_libc_start` from a raw
  `cb_program_v1` wrapper in `tests/test_core.c` that builds the tree
  first through the api_v1 layer. New `FIXTURE_FTS` fixture,
  `register_fts_probes`, `test_fts()`, wired into `main()`.
- Makefile: `FTS_OBJECT` (archived into `libcannedbsd.a`, same treatment
  as `cb_libc.o`), the five walk objects linked into `$(TEST_PROGRAM)`
  (dirent-probe pattern: separately compiled with `-Ilibc/include`, not
  part of `$(TEST_SOURCES)`), and all six new files added to the
  `analyze` target's explicit per-file `-fanalyzer` list.

## Architectural findings made while implementing (not previously nailed down)

1. **`bound_api` is `static`, private to `cb_libc.c`'s translation unit.**
   `cb_fts.c` cannot touch it directly. This settles a question the design
   note left open: FTS must be built as an ordinary *consumer* of the
   existing public `cb_libc_opendir`/`readdir`/`closedir` wrappers, not as
   a second privileged accessor. This turned out to make the "zero new
   ABI" claim stronger, not weaker: nothing about FTS's placement outside
   `src/` needed relaxing to make it work.
2. **Cycle detection uses inode numbers, not raw `struct cb_vfs_node*`
   pointer identity**, contrary to the design/review's literal phrasing
   ("Retaining ancestor nodes (`cb_vfs_node_retain`)... pointer identity").
   `cb_fts.c` lives outside `src/` and has no access to `cb_vfs_node` at
   all -- confirmed by grepping `src/internal.h`'s visibility boundary.
   Directory inode numbers, obtained through the same public `stat()`
   surface already used for `fts_statp`, are drawn from a single
   per-kernel counter (`node_create`'s `++kernel->next_inode`) and are
   therefore just as globally unique as a pointer would have been -- the
   design note and the review both already establish this fact, just in
   service of a different claim. Because the values held in the ancestor
   array are copied integers, not pointers into the VFS, invariant 4's
   underlying concern (a stale/dangling ancestor reference) does not
   apply here: an inode number cannot dangle. There is nothing to
   retain/release, which is why `cb_fts.c` does neither.
3. **`fts_statp` is typed `struct cb_stat_v1 *`, not `struct stat *`.**
   `libc/include/sys/stat.h` is still a stub with no public POSIX `struct
   stat` (that is CAT-01's open decision). This matches the design note's
   own stated approach and does not preempt CAT-01; whoever eventually
   ports the real `rm.c`/`cp.c` unchanged will need to reconcile this
   field's type, not this ID.

## FTS_DC: verified unreachable against the current RAMFS backend

While designing the cycle test, I first tried to construct a genuine,
reachable-from-root graph cycle by calling the raw `node_ops->rename`
directly (bypassing `cb_vfs_rename_paths`'s cycle guard from VFS-04's
post-acceptance fix on purpose, to test FTS's own independent defense) --
splicing the mount root under one of its own descendants.

This failed with `EPERM`: `ramfs_rename` (`src/ramfs.c`) refuses to move
any node whose `parent` is `NULL`, which includes every mount's root,
unconditionally, beneath the cycle-guard layer.

Retrying with a non-root node instead does not avoid the problem, it just
relocates it: renaming any node `X` into one of `X`'s own descendants
only ever reassigns `X`'s single parent pointer. That reassignment
necessarily severs the one edge that made the entire `X`-rooted subtree
reachable from root in the first place, since nothing else references it
-- `ramfs_node_ops`'s full operation list (`retain/release/lookup/
create/unlink/open/stat/parent/name/truncate/child_at/rmdir/rename`) has
no hardlink-style "add a second reference to an existing node" primitive
at any layer, public or raw. I verified this by reading the full op list,
not just reasoning about it.

**Conclusion, reached by attempting it and observing the failure, not by
assumption:** a true, reachable-from-root cycle is not merely untested
against this backend, it is currently unconstructible by any combination
of available operations. `FTS_DC`'s classification code exists (needed
for forward compatibility with any future backend that *can* produce
cycles -- real hardlinked directories, or a future symlink subsystem
following `FTS_LOGICAL`) and is exercised for the *no-false-positive*
case (`fts_cycle_walk.c`, an 8-level-deep ordinary tree), but its
true-positive path is currently dead code, honestly and for a specific,
checked reason.

## Invariant-by-invariant status

1. Multi-root `path_argv` -- yes (`fts_core_walk.c`, two independent roots).
2. `fts_accpath == fts_path` pointer identity under `FTS_NOCHDIR` -- yes,
   by construction (the only mode this fts implements) and asserted on
   every entry in `fts_core_walk.c`.
3. `FTS_SKIP` suppresses both descent and the `FTS_DP` visit -- yes
   (`fts_skip_walk.c`).
4. Ancestor lifetime -- see the cycle-detection finding above: satisfied
   by construction (copied integers, not retained pointers) rather than
   by an explicit retain/release pair.
5. Symmetric cleanup on early `fts_close` -- yes (`fts_close_walk.c`,
   closes mid-walk with 3 frames open; wrapper proves no descriptor leak
   by exhausting and reusing all of `CB_MAX_DIRS` afterward).

Also covered: `fts_open` argument validation (empty `path_argv` ->
`EINVAL`; `FTS_XDEV` rejected at `fts_open()` time with `ENOSYS`, per the
design note, not silently ignored); allocation-failure injection across
`fail_at` 0..7 (`fts_allocfail_walk.c` + `ftsallocfailprobe_main`, same
technique and accounting as `direntlibcallocfailprobe_main`), each
iteration re-confirming no directory descriptor leak.

## Evidence

`make LDLIBS=-lucontext test` and `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
both green in the `alpine:3.22` container (build-base, clang20,
compiler-rt, libucontext-dev), including the sanitize rebuild and
`check-build-modes`. Mac68k guest acceptance: **PENDING**, not run this
iteration (per standing direction deferring Mac68k acceptance runs).

## Follow-on work already identified, not in this scope

- `FTS-CHILDREN-01`: `fts_children`/`fts_link`/`fts_parent`, `FTS_SEEDOT`
  synthesis of `.`/`..` (ls-only, per the design note's own grep).
- `FTS-XDEV-01`: mount-crossing detection once `FS-STAT-01`'s unified
  `cb_stat_v1` append lands (the review's decision to coalesce the
  `device` field with CAT-01's timestamp/nlink/uid/gid append).
- `RM-01`: unblocked now -- `rm`/`rm -r`/`rm -f` need only
  `fts_open`/`fts_read`/`fts_close`/`fts_set(FTS_SKIP)`, all present.
