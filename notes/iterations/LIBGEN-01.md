# LIBGEN-01: `dirname(3)` libc implementation

- Status: done.
- Base SHA: `592ae41` (`origin/main`, freshly fetched -- confirmed directly:
  `cb_api_v1`'s actual current tail is `tcsetattr` (terminal work, not yet
  present when the design was first drafted), not `getprogname`/`closedir`
  as an earlier draft assumed. The new `dirname_buffer_location` accessor
  is appended after that actual tail, per the approved, since-corrected
  design (`notes/iterations/LIBGEN-01-design.md`).
- Branch: `work/LIBGEN-01`.
- Scope: the libc `dirname(3)` contract only. The `usr.bin/dirname`
  command, `basename(3)`, and any other `libgen.h` surface remain
  explicitly out of scope (design doc §1, §9) -- per direction, the real
  command is a separate, later step once this passes.

## Honesty note on what was and was not red-first

The ABI accessor (`dirname_buffer_location` on `cb_api_v1`), the
`dirname_buffer` field on `struct cb_task`, its `api_dirname_buffer_
location` implementation, and the three new compat shim headers
(`sys/param.h`, `limits.h`, `libgen.h`) were all written directly,
without a preceding failing test of their own -- the same "ABI/runtime
as prerequisite infrastructure" treatment every prior libc addition in
this project has given its own ABI additions. `libc/cb_libc.c`'s
`cb_libc_dirname` definition was written before any of this note's
"regression check" below was performed -- so, unlike `PENV-02`'s or
`PENV-03`'s genuine link-fails-before-the-definition-exists reds, none
of this iteration's tests were actually written or run against a
missing implementation first. The check described below was performed
*after* the full implementation already existed, by temporarily
deleting an already-working definition and confirming the test suite
notices. That is real, meaningful evidence -- it is not a "this test
probably works" assumption, and it did catch a mismatch once already
(the `struct_size`/`register_test_programs` fixture issues below) -- but
it is regression evidence, not red-first TDD evidence, and calling it
red-first would overclaim what was actually done. Recorded plainly
rather than reused as if it were the same thing.

## Regression check (not red-first: performed after the implementation existed)

- Command: temporarily removed `cb_libc_dirname`'s definition from
  `libc/cb_libc.c` (the declaration in `include/cannedbsd/libc.h` and
  every caller stayed in place), then `make LDLIBS=-lucontext build/
  test_core`.
- Observed failure: link failure, `undefined reference to
  'cb_libc_dirname'`, from `libc_dirname_probe.c`'s `check`/
  `check_boundary` functions and from the two raw-ABI isolation
  orchestrators in `tests/test_core.c` -- exactly the call sites that
  should fail, and nothing else (every other new piece -- the ABI field,
  the task-owned buffer, the imported `cb_libc_dirname_upstream` object,
  the three compat shims -- compiled cleanly in the same build). This
  demonstrates the test suite would catch cb_libc_dirname going missing
  again later; it does not demonstrate the tests were written before the
  implementation, because they were not.
- Restored the real definition immediately afterward; re-verified green.

## Green

- `make LDLIBS=-lucontext test`: passed (`all core tests passed`).
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed clean,
  including the Clang ASan/UBSan build.
- `platform/mac68k/build.sh` (the exact pinned Retro68 cross-compiler
  image): built clean. `platform/mac68k/CMakeLists.txt` needed a new
  `cb_dirname` object library (mirroring `cb_strlen`'s shape, with its
  own `-Ddirname=cb_libc_dirname_upstream` rename and source path) added
  to the `CannedBSD` target's sources -- `libc/cb_libc.c` is unconditionally
  part of that target and now references `cb_libc_dirname_upstream`, so
  the mac68k link would otherwise fail with a missing symbol (the exact
  failure class PENV-06 hit for the same reason). A real Mac-side test
  case was also added, not just the link fix: `cb_dirname_probe` (the
  same `tests/libc_dirname_probe.c` used natively) is now built into the
  `CannedBSD` target and exercised through
  `platform/mac68k/acceptance_cases.def`'s `libcdirnameprobe` case, run
  by both `test_mac_acceptance()` natively and the real Mac guest
  acceptance harness (`test_mac_guest.py`) -- successful linking alone
  does not exercise the runtime behavior, so this was added rather than
  left as a documentation-only claim.
- `tests/test_netbsd_libc_source.sh` now pins `dirname`'s source hash
  and provenance and checks its private link name and host-symbol
  exclusion, mirroring every other pinned import there (the earlier
  commit's `UPSTREAM.md` entry claimed this coverage before it actually
  existed -- corrected now, not just described).
- `tests/test_libc_source.sh` now checks `tests/libc_dirname_probe.c`
  for cannedBSD-specific names, requires it to call `dirname()`, and
  requires its compiled object to import the private `cb_libc_dirname`
  veneer rather than a host-facing `dirname` symbol -- the same
  ordinary-source boundary check every other libc addition here gets.
- `tests/libc_dirname_probe.c`'s boundary case now targets the actual
  exact-fit boundary (a 1023-byte result, the true maximum a
  `PATH_MAX == CB_PATH_MAX == 1024` buffer can hold) rather than one
  byte short of it, and a new overflow case confirms a dirname computed
  from a path long enough that upstream's own `xdirname_r` truncates it
  first (to longer than `PATH_MAX` can hold) is reproduced by the veneer
  exactly as upstream already truncated it, not truncated a second time
  or read past.
- All three exact-commit Woodpecker statuses: pending push.

## Design decisions carried from the approved design, as implemented

1. **Task-local output lifetime** (design §4): imported `lib/libc/gen/
   dirname.c` byte-for-byte under the private link name
   `cb_libc_dirname_upstream` (a plain `-D` rename, confirmed safe by
   reading the file -- no `#undef`/macro interference). `cb_libc_dirname`
   copies its result out of that shared, process-wide static into a new
   task-owned `dirname_buffer[CB_PATH_MAX]` field (reached via the new
   `dirname_buffer_location` accessor) before ever returning, so one
   task's call cannot be silently overwritten by another's unrelated
   call. Verified directly: `dirnameisolationprobe`/`dirnameisolationpeer`
   force a parent and a freshly spawned child to interleave (parent calls
   `dirname()`, spawns the child, `yield()`s so the child runs its own
   `dirname()` call to completion with a completely different path, then
   the parent re-checks its own earlier result is unchanged).
2. **Per-field optional-tail guard** (design §4, matching this project's
   now-established `cb_vfs_node_ops`/VFS-03-style discipline, adapted to
   this base's existing inline-check style used for `poll`/`isatty`/
   `tcgetattr`/`tcsetattr` rather than a separate helper function):
   `cb_libc_dirname` checks `struct_size` reaches `dirname_buffer_location`
   and that the field itself is non-`NULL` before ever calling it,
   returning `ENOSYS` otherwise. Covered by two independent cases,
   matching VFS-03's fourth-review-pass lesson about not collapsing
   old-table and null-field coverage into one: `dirnameoldtableprobe`
   (`struct_size` shrunk to stop before the field) and
   `dirnamenulltableprobe` (full `struct_size`, field itself `NULL`).
3. **New compat shims, exactly as the design anticipated** (design §5):
   `sys/param.h` (`MIN` only), `limits.h` (`PATH_MAX` defined from the
   pre-existing `CB_PATH_MAX`, one source of truth rather than a second
   pinned constant), `libgen.h` (empty import-only shim). The genuinely
   ordinary-facing `libc/include/libgen.h` is a separate file, matching
   the existing `dirent.h`/`err.h` precedent.
4. **No locale dependency** (design §6): confirmed by inspection and
   unaffected by implementation -- the imported function never touches
   locale state.
5. **Allocation/exec/exit lifetime** (design §7): no code needed at
   `task_finish_exec`/`task_destroy`/`api_exit` -- the buffer is inline
   storage on `struct cb_task`, dying with the task automatically, exactly
   as designed.

## Test capacity note

`FIXTURE_FULL` (the general native test-program registration set) was
already at `CB_MAX_PROGRAMS`'s 64-slot ceiling before this change. Rather
than raising that production struct's capacity for test-only registration
growth, this iteration adds a new, separately scoped `FIXTURE_DIRNAME`
(mirroring the existing `FIXTURE_MAC` pattern, which already keeps the
Mac-acceptance probe set independent of the full native fixture for the
same reason) covering only the five new dirname-specific programs.
`CB_MAX_PROGRAMS` itself is untouched.

## Change and review

- Implementation: one new `cb_api_v1` field, `dirname_buffer_location`
  (append-only, after `tcsetattr`, the actual current tail); one new
  `struct cb_task` field, `dirname_buffer[CB_PATH_MAX]` (inline, no
  separate allocation or cleanup); `cb_libc_dirname` in `libc/cb_libc.c`;
  the pinned NetBSD import and its three new compat shims; the genuinely
  ordinary-facing `libc/include/libgen.h`.
- Documentation: `LIBC.md` gains a `libgen.h` bullet. `UPSTREAM.md` gains
  the `dirname` provenance entry (distinct two-clause NetBSD Foundation
  license, not the three-clause Regents license every other pinned
  import here carries). `SPEC.md` is not touched -- consistent with this
  project's existing practice of not documenting individual small libc
  additions (`getopt`, `errx`, `poll`/`isatty`/`termios`) there; it is
  reserved for architectural-level contracts.
- Remaining risk or follow-up: `basename(3)`, `usr.bin/dirname` (the
  command), `realpath(3)`, and any other `libgen.h` surface are
  explicitly not claimed (design doc §9) -- deliberately left for later,
  separate backlog items per current direction. Guest acceptance under
  AGENTS.md step 7 is outstanding, coordinator-owned.

## Integration wiring correction

The integration candidate combines the locale and dirname probes without
reordering the ABI: `dirname_buffer_location` follows `tcsetattr`. It also
adds the missing checked registration of `cb_dirname_probe_program` in the
Mac driver. The shared transcript contains 22 records including contexts
before the separate dirname command integration. Exact combined CI and guest
acceptance remain pending; the coordinator owns the guest slot.
