# VFS-03: directory iteration and libc `dirent`

- Status: done
- Base SHA: `c93e2ba` (`origin/main`, freshly fetched — VFS-01 and FS-01 are
  both already integrated here, confirmed directly: `truncate` and
  `mounts[4]` are present in `src/internal.h` before any change in this
  branch)
- Branch: `work/VFS-03`
- Design: `notes/iterations/VFS-03-design.md` (`work/VFS-03-design`), as
  corrected after review — see that document's own "Correction from an
  earlier draft" notes for the three fixes carried into this implementation
  (task-owned directory-descriptor table instead of relying on
  `task_release_allocations`; both a duplicate-on-insert and a
  skip-on-removal explicitly tested, not a false "never duplicated" claim;
  `d_name` sized to the already-existing `CB_PATH_MAX`, not an invented
  smaller bound that would have required a `ramfs_create` regression).

## Honesty note on what was and was not red-first

Being precise about this rather than presenting a uniform "TDD" narrative
that didn't actually happen uniformly: the ABI operation
(`opendir`/`readdir`/`closedir` on `cb_api_v1`), the `cb_vfs_node_ops`
extension (`child_at`), the task-owned `directories[CB_MAX_DIRS]` table,
`src/vfs.c`'s dispatch helpers, and RAMFS's `child_at` implementation were
all written directly, without a preceding failing test of their own — the
same "ABI/runtime as prerequisite infrastructure" treatment `PENV-02`/
`PENV-03`/`PENV-05` gave their own ABI additions. What genuinely was
red-first: the ordinary-source probe (`tests/libc_dirent_probe.c`) and its
`opendir`/`readdir`/`closedir` calls were written and compiled *before*
`libc/cb_libc.c`'s three wrapper functions existed — concretely, by stashing
just that file's new definitions (`git stash push -- libc/cb_libc.c`,
keeping everything else already in place) and rebuilding.

## Red

- Command: `make LDLIBS=-lucontext build/test_core` (Alpine 3.22 container
  matching the Woodpecker agent image), with `libc/cb_libc.c`'s
  `cb_libc_opendir`/`cb_libc_readdir`/`cb_libc_closedir` definitions and
  their three `api_is_usable` checks temporarily removed (stashed).
- Expected failure: an ordinary opendir/readdir/closedir probe fails to
  link (matching this backlog item's literal "fails to compile" wording in
  the same spirit `PENV-02`/`PENV-03`/`PENV-05`'s actual "fails to link"
  reds did — the interface was already fully declared, only the
  definitions were missing).
- Observed failure: linking `build/test_core` failed with `undefined
  reference to 'cb_libc_opendir'`, `'cb_libc_readdir'`, and
  `'cb_libc_closedir'` from `build/direntprobe_command.o`
  (`tests/libc_dirent_probe.c`). Every other new piece (the ABI struct
  extension, `cb_vfs_node_ops.child_at`, RAMFS's implementation, the three
  `api_*` dispatch functions in `src/core.c`, `dirent.h`, `sys/types.h`'s
  `ino_t`) compiled cleanly in the same build, confirming the stash isolated
  exactly the missing piece and nothing else was accidentally broken.

## Green

- Focused command: `make LDLIBS=-lucontext test` — passed after fixing a
  real crash (below) and the review's four implementation findings.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` — passed
  clean, including the Clang ASan/UBSan build (exactly the build that
  would have caught the crash below, had it still been present).
- All three exact-commit Woodpecker statuses: pending push

## Second review pass: four implementation findings, and a real crash
found while fixing them

1. **`api_readdir` cleared `errno` on clean end-of-directory**,
   contradicting the design's own stated rule (`notes/iterations/
   VFS-03-design.md` §2/§5: "must not touch errno on clean EOF"). Fixed by
   removing the `cb_task_set_error(task, 0)` call entirely on that path.
   My own test hadn't caught this because it primed `errno` to `0` before
   the call, which cannot distinguish "preserved" from "cleared to 0" —
   fixed to prime a nonzero sentinel (`CB_EPERM`) first.
2. **The three new `cb_api_v1` operations were hard-required in
   `api_is_usable`**, unlike every other appended `cb_api_v1` operation's
   *stated* intent to be an optional tail. Concretely, `api_is_usable`'s
   base check was `api->struct_size >= sizeof(*api)` — always the full
   *current* size, which would already have rejected an older, smaller
   struct regardless of the explicit per-field `!= NULL` checks. Fixed:
   the base check now uses `offsetof(struct cb_api_v1, opendir)` (through
   `getprogname`, mandatory) instead of the whole struct's size, and
   `opendir`/`readdir`/`closedir` are no longer required there at all.
   Each of `cb_libc_opendir`/`cb_libc_readdir`/`cb_libc_closedir` now
   checks a shared `dirent_api_available()` helper (struct large enough
   to include `closedir`, the last of the three contiguous fields, and
   all three non-`NULL`) before ever dereferencing `bound_api->opendir`
   etc., returning `ENOSYS` otherwise — the same pattern `cb_vfs_node_ops`
   already uses for `truncate`/`child_at`, just one layer up.
3. **Ordinary-source old-size/null-tail coverage, and the crash it
   surfaced**: added `tests/libc_dirent_oldtable_probe.c` (ordinary,
   calls `opendir()` and checks `errno == ENOSYS`) plus a raw-ABI
   orchestrator, `direntoldtableprobe`. My first version of the
   orchestrator called `cb_libc_start` with a truncated `cb_api_v1` copy
   from a bare top-level test function *outside any running task* —
   `active_kernel->current` was never valid, and the very first
   `errno`-touching statement in the probe (`api_errno_location`
   dereferencing a null `active_kernel->current`) crashed with a real
   SIGSEGV, confirmed via a from-scratch ASan/UBSan debug build
   (`src/core.c:1275`, `member access within null pointer of type 'struct
   cb_task'`). Fixed by moving the truncated-copy dance *inside* a real,
   already-scheduled task's own `.start` function (so
   `active_kernel->current` is valid throughout) and explicitly rebinding
   `bound_api` back to the task's own stable `api` pointer afterward
   (`cb_libc_start(api, 0, NULL, dirent_noop_main)`) — otherwise
   `bound_api` is left pointing at a stack frame (the truncated copy)
   that becomes dangling the moment the orchestrator returns, silently
   corrupting every later ordinary-source probe in the same test binary
   run. This was a real, load-bearing bug the review's ask surfaced, not
   a hypothetical one.
4. **`api_readdir` silently truncated a caller's too-small name buffer**,
   returning a different (truncated) name as if it were correct and
   advancing the cursor past the real entry. Fixed to return `-1`/
   `CB_ENAMETOOLONG` without writing to `name_out` or advancing the
   cursor, so a caller with a larger buffer can still observe the exact
   same entry on retry. Tested directly (a 3-byte buffer against an
   8-character name, then the same call again with a full-size buffer
   returning that same name).

## Third review pass: mutation-test scenario corrected

The skip-on-removal case (`direntmutationprobe`, §7 item 4a) originally
unlinked the *next not-yet-returned* entry after reading the first one —
which only reflects the directory shrinking, proving nothing about a
skip. Corrected to unlink the *already-returned* entry instead: with
`[A, B, C]`, read `A`, unlink `A` (not `B`), and the next read must
return `C`, skipping `B` — that is the actual claim under test. The
duplicate-on-insertion case (`4b`) was designed correctly from the start.

## Design corrections carried from review, applied here

1. **Ownership**: `struct cb_dir_handle { struct cb_vfs_node *node; size_t
   index; int in_use; }` plus `directories[CB_MAX_DIRS]` on `struct
   cb_task` (`src/internal.h`), mirroring `descriptors[CB_MAX_FDS]`. A new
   `dir_close_all(task)` (`src/core.c`) releases every `in_use` node and is
   called from **all three** places a task's resources can be reclaimed:
   `api_exit` (normal exit), `task_finish_exec` (successful exec — every
   open directory closes unconditionally, no close-on-exec concept for
   directories), and **`task_destroy`** — added after a second review pass
   caught that `cb_kernel_destroy` calls `task_destroy` directly for any
   task still live or blocked at kernel teardown (`src/core.c`, confirmed
   by reading the call site: `cb_kernel_destroy` walks every remaining task
   and calls `task_destroy`, independent of whether that task ever reached
   `api_exit`). `dir_close_all` is idempotent (skips any slot already
   `!in_use`), so calling it from more than one of these paths for the same
   task (e.g. `api_exit` then later `task_destroy` when a zombie is finally
   reaped) is always safe.
2. **Mutation policy tests, corrected scenario**: the duplicate-on-insert
   and skip-on-removal tests both use `[A, B, C]`. Critically, the
   skip-on-removal case unlinks the *already-returned* entry (`A`, after
   `readdir` has already consumed it), not the next not-yet-returned one —
   unlinking the immediate next entry and observing the following one
   merely reflects the directory shrinking, which proves nothing about a
   skip. Unlinking the already-returned `A` shifts `B` into the
   already-consumed index `0`, so the very next `readdir()` returns `C`,
   *skipping* `B` — that is the actual claim being tested.
3. See `notes/iterations/VFS-03-design.md` for the two design-level
   corrections (task-owned handle table instead of relying on
   `task_release_allocations`; `d_name` sized to `CB_PATH_MAX`) that were
   made before this implementation began, not during it.

## Change and review

- Implementation: one new `cb_vfs_node_ops` entry, `child_at` (ordinal
  index, re-derived from `directory->children` on every call — see the
  design doc for why this specific shape was chosen over a retained-node
  cursor or an opaque mount-supplied token); one new task-owned
  `directories[CB_MAX_DIRS]` table (`struct cb_dir_handle`) mirroring
  `descriptors[CB_MAX_FDS]`; three new `cb_api_v1` operations
  (`opendir`/`readdir`/`closedir`), append-only, genuinely optional (see
  above); `libc/include/dirent.h` (opaque `DIR`, `struct dirent` sized to
  the pre-existing `CB_PATH_MAX`) modeled directly on `stdio.h`'s `FILE`
  pattern; `struct dirent` itself is defined in `include/cannedbsd/libc.h`
  rather than the ordinary-facing header, since `cb_libc.c` (which must
  write its fields) is not compiled with `-Ilibc/include` and `struct
  dirent` carries no runtime-private pointers, so defining it once at that
  shared layer avoids two definitions to keep in sync.
- ABI, ownership, and cleanup review: append-only throughout, no
  reordering. `dir_close_all` is called from all three places a task's
  resources can be reclaimed — `api_exit`, `task_finish_exec` (directories
  have no close-on-exec flag; all close unconditionally across `exec`),
  and `task_destroy` (added after review caught that `cb_kernel_destroy`
  calls `task_destroy` directly for any task still live or blocked at
  kernel teardown, independent of whether it ever reached `api_exit`) —
  and is idempotent, so being called from more than one of these for the
  same task is always safe. Fixed one real, pre-existing test fragility
  this change exposed: `test_truncate_vfs_contract`'s "partial truncate
  callback must not be read" case used `sizeof(copy) - 1`, which only ever
  meant "one byte short of fully including `truncate`" because `truncate`
  happened to be the struct's last field — appending `child_at` after it
  silently made that test stop testing what it claimed to, since
  `sizeof(copy) - 1` now excluded one byte of `child_at` instead. Corrected
  to `offsetof(..., truncate) + sizeof(...truncate) - 1`, which is robust
  to whatever gets appended after it in the future, and added the parallel
  "partial/absent `child_at`" case for this iteration's own field, reusing
  the same mock-table infrastructure.
- Documentation: `LIBC.md` gains a `dirent.h` bullet (task-owned handle,
  `CB_PATH_MAX`-sized `struct dirent`, EOF/`ENAMETOOLONG`/`ENOSYS` behavior).
  `SPEC.md` §4.5 gains `child_at` in the node-operation summary plus a note
  on task-owned directory handles and old-table `ENOSYS` fallback; §7 gains
  a directory-iteration bullet covering the ordinal enumeration, the
  documented duplicate/skip looseness under concurrent mutation, the
  errno-preserving EOF rule, and the `ENAMETOOLONG`/unchanged-cursor rule.
## Fourth review pass: acceptance-evidence gaps closed

An independent review found the second-pass fixes sound but the test
coverage claiming them incomplete: every existing dirent probe explicitly
closes what it opens, so none of them actually exercised the reclaim paths
(`api_exit`, `task_finish_exec`, `task_destroy`) the second pass touched;
the old-table coverage only shrank `struct_size`, never testing a
full-size table with the callbacks themselves left `NULL`; and
`cb_libc_opendir`'s allocation-failure branch (already correct in the
code, releasing the acquired runtime descriptor before returning `NULL`)
had no test proving it, as opposed to merely reading correct.

- **`cb_libc_opendir` allocation-failure release**: added
  `tests/libc_dirent_allocfail_probe.c` (ordinary, expects `ENOMEM`) plus
  `direntlibcallocfailprobe`, a raw-ABI orchestrator that fails the very
  next `allocate()` call (the `struct cb_libc_dir` allocation is the only
  one this probe triggers -- `opendir()` dispatch itself never allocates)
  and then, as real evidence the acquired descriptor was released and not
  leaked, successfully opens all `CB_MAX_DIRS` directories from scratch.
- **Full-size table, `NULL` directory callbacks**: added
  `direntnulltableprobe`, distinct from the existing `direntoldtableprobe`
  (which only shrinks `struct_size`). This is the case that would catch a
  regression back to "trust `struct_size` alone" if the per-field `NULL`
  checks in `cb_libc_opendir`/`readdir`/`closedir` were ever dropped.
- **Reclaim paths actually exercised, with a real assertion, not just "did
  not crash"**: added a whitebox-hook, `cb_test_ramfs_node_references`
  (mirrors the existing `cb_test_task_allocation_count`-style test-only
  introspection already in this codebase), exposing the otherwise-opaque
  RAMFS refcount. `test_dir_reclaim_contract` runs three real, fully
  scheduled kernels, each with a task that opens `/tmp` and deliberately
  never closes it, then inspects `/tmp`'s reference count afterward:
  1. Exit without `closedir()`: confirms `api_exit`'s `dir_close_all`
     released the retain (count returns to baseline).
  2. Successful `exec()` without `closedir()` first: confirms
     `task_finish_exec`'s unconditional close released it too.
  3. Kernel teardown while a spawned, never-`waitpid`'d child is still
     blocked (reading from a pipe whose both ends it holds itself, so it
     blocks forever regardless of the parent's own exit): confirms the
     node is genuinely still retained at that moment (count is elevated,
     not 1 -- proving the scenario is real, not vacuous) and exercises
     `task_destroy`'s `dir_close_all` call under ASan/UBSan. This one
     deliberately does **not** re-inspect the node after
     `cb_kernel_destroy`: the node may already be freed by then, and
     reading it back would itself be a use-after-free bug in the test,
     not a valid check. Being explicit about this limitation rather than
     quietly claiming full coverage: the destroy-path test verifies the
     setup is real and that the reclaim code runs cleanly under the
     sanitizer, not that the node was independently re-confirmed released
     afterward.

All of these, plus the existing suite, pass `make LDLIBS=-lucontext test`
and the full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` gate.

## Fifth review pass: tests that could mask the cleanup they claim to prove

A further independent review found the fourth pass's runtime still sound,
but three specific ways the *tests themselves* could pass even if the
cleanup they claim to verify were broken:

1. **Allocation-failure loop only covered `fail_at == 0`.** `bound_api
   ->allocate` (`api_allocate`) makes two underlying `cb_allocate` calls
   per logical allocation: one for the payload, one for its own
   bookkeeping node (the task-allocation-tracking entry). Testing only
   `fail_at == 0` exercises "the payload allocation itself fails" but
   never reaches the separate branch where the payload succeeds and the
   *bookkeeping* allocation fails, forcing `api_allocate` to release the
   payload it had already acquired. `direntlibcallocfailprobe` now loops
   `fail_at` over `{0, 1}`, independently verifying both.
2. **The exit and exec reclaim checks were both only observable from
   *outside*, after `cb_kernel_run` had already returned -- and a shell
   booted via `cb_kernel_boot` always spawns its single command and
   blocks in `waitpid()` for it immediately.** For the exit scenario this
   meant the shell's own reap (`task_destroy`, which also calls
   `dir_close_all`) would run immediately after the target task's own
   `api_exit`, with no way to tell whether `api_exit`'s cleanup had
   actually run or the shell's reap was silently covering for a missing
   one. Fixed by adding `direntreapexitboot`, an orchestrator the shell
   spawns instead: it spawns the real target itself, `yield()`s once (the
   target runs to completion -- open, self-check, exit -- entirely within
   that turn), and only *then* checks the node's reference count and
   `waitpid()`s to reap it -- a window that exists only because this
   orchestrator, not the shell, is the target's actual parent. This
   isolates `api_exit`'s own cleanup from `task_destroy`'s. For the exec
   scenario, the fix was simpler: `direntreapexecpeer` (the program
   exec'd into) now records the reference count as the very first thing
   it does, before anything else could have released the old task's
   retain -- including its own eventual exit -- which is what actually
   attributes the release to `task_finish_exec` specifically rather than
   to whatever runs whenever the process eventually exits regardless.
3. **The null-tail coverage set all three directory callbacks `NULL`
   together, then only ever called `opendir()`.** This could never prove
   that `cb_libc_readdir`/`cb_libc_closedir` check their *own* fields --
   only that `cb_libc_opendir` does, transitively making the other two
   unreachable. Testing this way also papered over a real bundling bug:
   `dirent_api_available()` required all three fields non-`NULL`
   *together*, so a table missing only `readdir` would have incorrectly
   also rejected `opendir()` and `closedir()`, even though their own
   fields were present and usable. Fixed the implementation first --
   `dirent_api_available()` is now three independent per-field checks,
   `opendir_api_available()`/`readdir_api_available()`/
   `closedir_api_available()`, matching `cb_vfs_node_ops`'s existing
   per-operation independence (this also required guarding
   `cb_libc_opendir`'s own error-path call to `closedir()`, which would
   otherwise dereference a `NULL` function pointer for a table that
   provides `opendir` without `closedir`) -- then split the one bundled
   test into three: `direntopendirnulltableprobe` (only `opendir` `NULL`),
   `direntreaddirnulltableprobe` (only `readdir` `NULL`, `opendir`/
   `closedir` must still succeed), and, **as of this pass**,
   `direntclosedirnulltableprobe` (only `closedir` `NULL`). At this point
   in the review sequence `opendir()`/`readdir()` were still expected to
   succeed here too -- the sixth review pass below found that this
   specific case is not actually safe (closedir is the only thing that
   can release what opendir acquires) and changed `cb_libc_opendir`,
   and this test's expected outcome, accordingly. Recorded here as it
   genuinely happened, rather than silently rewritten to look correct
   from the start.

**Verification that these tests are real, not vacuous**: each of the
three `dir_close_all` call sites (`api_exit`, `task_finish_exec`,
`task_destroy`) was temporarily disabled, one at a time, and
`test_dir_reclaim_contract` was re-run: disabling `api_exit`'s call
produced `FAIL: api_exit did not release an unclosed directory's node
retain`; disabling `task_finish_exec`'s call produced `FAIL:
task_finish_exec did not release a directory held across exec`. (The
implementation was restored immediately after each check; the third
call site, `task_destroy`'s, is exercised by the kernel-teardown scenario
but -- per that scenario's own documented limitation above -- is not
independently verifiable this way without introducing a genuine
use-after-free into the test itself, so it was not sabotage-tested; the
existing pre-teardown "genuinely held" assertion plus a clean run under
ASan/UBSan remains the honest scope of that one scenario.) All fixes
re-verified against a full `make LDLIBS=-lucontext test` and the complete
`make LDLIBS=-lucontext SANITIZE_CC=clang ci` gate.

## Sixth review pass: a real descriptor leak in `cb_libc_opendir`

An independent review of `f9c3214` found a genuine bug the fifth pass's
per-field independence introduced: `cb_libc_opendir` only required
`opendir` itself to be usable, not `closedir`. `opendir` is the only
thing that ever acquires the raw runtime descriptor, and `closedir` is
the only thing that can ever release it (`readdir` cannot). A table
providing `opendir` without `closedir` -- fully legitimate under the
per-field-independence model just established -- would let
`cb_libc_opendir` acquire a descriptor with no way to ever release it:
not on a later allocation failure (the existing cleanup call would be a
`NULL` dereference, previously guarded defensively rather than
prevented), and not even on a normal, successful `closedir()` call
(which would just report `ENOSYS` forever, leaking for the rest of the
task's lifetime).

Fixed by requiring `closedir_api_available()` in addition to
`opendir_api_available()` before `cb_libc_opendir` ever calls
`bound_api->opendir()` -- the leak path is now unreachable by
construction, not merely handled defensively. `readdir` is still not
required: its absence alone never prevents a successful `open`/`close`
pair.

This changed what `direntclosedirnulltableprobe` (closedir `NULL` from
the start) actually proves: `opendir()` itself must now fail with
`ENOSYS` immediately, not "opendir/readdir succeed, only closedir
fails" -- it now reuses the same ordinary probe as the old-table case
(`cb_direntoldtable_main`). The scenario that used to be tested there
(closedir's own per-call guard against an *already-open* handle) is
preserved, not dropped, by a new `direntclosedirrebindprobe`: it opens
and reads a real handle under the full, working table first, then
rebinds `bound_api` to a copy with only `closedir` `NULL` to exercise
`closedir()`'s guard against that already-open handle, then restores the
full table (the degraded copy has no way to ever close it) before
actually closing it for real. `direntopendirnulltableprobe` and
`direntreaddirnulltableprobe` (the other two independent per-field
scenarios) are unaffected and unchanged -- neither one's table is
missing `closedir`; the opendir-NULL case rejects acquisition, while
the readdir-NULL case still opens and closes successfully.

Verified the fix is real: temporarily reverted the guard to check only
`opendir_api_available()`, rebuilt, and confirmed
`direntclosedirnulltableprobe` fails (`cb_direntoldtable_main` observing
`opendir()` unexpectedly succeed); restored the fix and reconfirmed
green. That intermediate commit also raised the production capacity
`CB_MAX_PROGRAMS` from 64 to 96. Review rejected that unrelated change;
`f2974f5` restored 64 and moved the dirent probes into a scoped fixture.

`make LDLIBS=-lucontext test` and the full
`make LDLIBS=-lucontext SANITIZE_CC=clang ci` gate both pass clean.

- Remaining risk or follow-up: `rewinddir`/`seekdir`/`telldir`,
  `scandir`, and `readdir_r` are explicitly not claimed (design doc §9).
  `IO-01` (public descriptor polling) is a separate, concurrently-worked
  backlog item touching adjacent descriptor-table territory; per this
  task's instructions, reconciling any overlap is left to integration, not
  attempted here. Guest acceptance under AGENTS.md step 7 is outstanding,
  coordinator-owned.

## Integration against current main

The integration starts at `8a4eb94` and preserves its libc, command and
terminal work. The directory operations append after
`dirname_buffer_location`, following the existing poll and terminal fields.
Libc startup still accepts the mandatory prefix ending before `poll`.
The new `FIXTURE_DIRENT` is separate from the existing base, full, Mac and
dirname fixtures; the production program limit remains 64. Every existing
probe and lifecycle case is retained. The exit cleanup orchestrator now
asserts the waited child's status as well as the returned PID.

The ordinary dirent probe is compiled and registered in both the shared
Linux fixture and the Mac application. It now distinguishes a readdir
error from clean EOF instead of accepting either once tmp was observed.
Its direct shared acceptance case makes 29 expected records including
contexts. Host protocol, complete CI and exact-artifact guest acceptance
remain pending for this candidate; the coordinator owns the guest slot.

## Exact integration acceptance, 2026-09-08

The coordinator verified integrated commit `6e83f00` with all three
Woodpecker #182 workflows green (ci, mac68k, mac-automation). The exact
CannedBSD archive SHA256 is
`f6cc4b48b3a0cf21d6adb2c752e9c7455cfc2d0cbf43edb5406726ad65e8453c`.
Fresh acceptance `run-26u3wozf` produced 29 PASS records including the direct
ordinary dirent case. The screenshot was reviewed; the application closed,
normal guest shutdown completed, mounted disks were verified closed and the
serialized guest slot was released. This completes the integration acceptance
that was pending in the preceding historical sections.

Two focus interruptions preceded output creation. The coordinator moved the
emulator window left and resumed the same staged run. The successful resumed
automation took 15.02 seconds; it is not evidence of a 15.02-second cold boot.
