# BASENAME-01: `basename(3)` libc and command

- Status: done. Final commit `fd42d36` is green on Woodpecker's exact
  `ci`, `mac68k`, and `mac-automation` checks (see "Woodpecker results"
  below). Local `make ci` was not run (host Docker outage; see "Local
  verification limitation") -- Woodpecker's real Alpine runner caught
  two genuine bugs host `clang -fsyntax-only` could not (a test logic
  error, then a missing shell/Mac-runner registration), each fixed in
  its own commit; see the two "Woodpecker attempt found..." sections
  below for exactly what each caught and how it was fixed.
- Base SHA: `6e83f00` (`origin/main`, freshly fetched -- confirmed
  directly: `cb_api_v1`'s actual current tail is `closedir`
  (VFS-03's directory-iteration work, merged after `dirname` but before
  this task started), not `dirname_buffer_location` as the original
  bounded plan assumed against an earlier snapshot. `FIXTURE_DIRENT = 4`
  already exists; this work adds `FIXTURE_BASENAME = 5`.
- Branch: `work/BASENAME-01`.
- Plan: `notes/iterations/BASENAME-01-plan.md` (`work/basename-milestone`),
  an inventory-only audit with pinned hashes and diagnostic compile
  evidence, not implementation.

## Rebase note

Implementation was first written against `96d5936` (the base named in
the original assignment). Mid-task, the coordinator reported VFS-03 had
merged to `main` as `6e83f00`, moving the actual ABI tail from
`dirname_buffer_location` to `closedir` and taking fixture slot 4 for
`FIXTURE_DIRENT`. Rather than patch a stale base, the worktree was reset
to fresh `origin/main` (`git reset --hard origin/main`, after backing up
every purely-additive new file first) and every shared-file edit --
`abi.h`'s append point, `struct cb_task`'s new field's neighbor,
`core.c`'s accessor wiring, `test_core.c`'s fixture enum and raw-ABI
orchestrators, all six boundary-test scripts, and the Mac coordinator
files -- was redone directly against the real current state rather than
reused from the stale draft.

## Local verification limitation

Partway through this task the host's Docker Desktop became unresponsive
following a disk-space exhaustion event (independently observed and
reported by the coordinator as already addressed: "~3GiB cleared from
closed disposable boot copies"). Docker did not recover after repeated
restart attempts (`open -a Docker`, quit/relaunch, waiting several
minutes). Per the coordinator's explicit fallback instruction ("Prefer
biggie Woodpecker if local Docker setup fails"), this iteration was
pushed without a local `make ci` run. What *was* verified locally,
directly on the host, without Docker:

- `clang -fsyntax-only` (matching each file's real build flags: `-Dmain=
  cb_basename_main` for the command, `-Dbasename=cb_libc_basename_upstream`
  for the pinned libc import, etc.) on every new or modified C
  translation unit: `upstream/netbsd/usr.bin/basename/basename.c`,
  `upstream/netbsd/lib/libc/gen/basename.c`, `tests/libc_basename_probe.c`,
  `tests/libc_basename_oldtable_probe.c`, `commands/basename_module.c`,
  `tests/libc_basename_probe_module.c`, `libc/cb_libc.c`, `src/core.c`,
  and the full `tests/test_core.c`. All compiled clean under
  `-std=c99 -Wall -Wextra -Werror -Wpedantic`.
- `bash -n` on every modified shell test script.
- The pinned source hashes, verified directly against freshly
  downloaded copies from the exact pinned NetBSD revision (matches the
  bounded plan's own audit hashes exactly).
- `python3 tests/test_mac_guest.py` (pure Python, no emulator or Docker
  needed): all 18 tests pass, including the updated 36-`PASS`-line
  transcript assertion and the new `libcbasenameprobe`/`basename ...`
  acceptance-case membership checks.

This is real evidence that the code is syntactically correct and that
the Mac guest-acceptance *protocol* logic (counting, staging, transcript
comparison) is internally consistent with the new cases -- it is
explicitly **not** evidence of a successful link, a passing runtime
test, or a clean sanitizer run. Those require the exact Woodpecker `ci`
and `mac68k` checks on the pushed commit, reported separately below once
observed.

## Exact source inputs (matches the bounded plan's own audit exactly)

- Command: `usr.bin/basename/basename.c`, SHA256
  `717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd`;
  three-clause Regents license, retained in the file.
- Libc: `lib/libc/gen/basename.c`, SHA256
  `f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87`;
  two-clause NetBSD Foundation license, retained in the file.
- Both fetched fresh from
  `https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/...`
  and hash-verified to match the plan's own audit before vendoring.

## Design, exactly as directed by the plan

1. **Separate, writable, per-task buffer**: `struct cb_task
   .basename_buffer[CB_PATH_MAX]`, reached via a new append-only
   `cb_api_v1` accessor `basename_buffer_location`, appended after the
   *actual* current tail (`closedir`). `cb_libc_basename` copies
   upstream's shared static result into this buffer immediately, with
   no yield in between, exactly mirroring `cb_libc_dirname`. Does not
   reuse or repurpose the existing `dirname_buffer`/`dirname_buffer_
   location` -- confirmed by a direct same-task test
   (`check_retains_dirname` in `tests/libc_basename_probe.c`) that calls
   `dirname()` then `basename()` in the same task and asserts the
   `dirname()` result is still intact afterward.
2. **Distinct build object identities**: `basename_command.o` /
   `cb_basename_main` for the command (separate from `dirname_command.o`);
   `netbsd_basename.o` / `cb_libc_basename_upstream` for the imported
   libc function (separate from `netbsd_dirname.o`); `basenameprobe_
   command.o` / `cb_basename_probe_main` for the ordinary direct probe.
3. **No speculative surface**: only the one accessor, one private
   `libgen.h` declaration/mapping pair (ordinary-facing and import-only
   compat shim), the task buffer, and tests were added. No locale
   expansion (`setlocale` was already supplied by the prior `LOCALE-01`
   integration), no broader `libgen.h` API (`realpath`, etc.), no
   filesystem work, and no `CB_MAX_PROGRAMS` capacity increase --
   `FIXTURE_BASENAME`, a new scoped native test fixture (mirroring the
   existing `FIXTURE_DIRNAME`/`FIXTURE_DIRENT` pattern), keeps the five
   new native probes out of the already-full `FIXTURE_FULL` set.
4. **Registered in the actual Mac main**, not merely its CMake link
   list: `cb_basename_probe_program` is in `register_mac_probes()`
   (`tests/test_core.c`), and `libcbasenameprobe`/`basename ...` cases
   are in `platform/mac68k/acceptance_cases.def`, consumed by both the
   native `test_mac_acceptance()` and the real Mac guest harness.

## Falsifiable acceptance, as specified by the plan

- Libc (`tests/libc_basename_probe.c`): `NULL`/empty/plain/root/repeated-
  slash/trailing-slash inputs; input left unmodified; the returned
  buffer is genuinely writable (implicit in the command's own `p[off] =
  '\0'` truncation, exercised by the command behavioral test below); a
  1023-byte exact-fit result and a 1100-byte longer-component input that
  exercises upstream's own truncation, reproduced faithfully rather than
  truncated a second time.
- Same-task repeated calls (`check_repeated_calls`), forced cross-task
  interleaving (`basenameisolationprobe`/`basenameisolationpeer`, mirroring
  `dirname`'s isolation pair exactly), and retained `dirname` alongside a
  `basename` call in the same task (`check_retains_dirname`). Old-sized
  API (`basenameoldtableprobe`, shrunk `struct_size`) and independently
  `NULL` accessor (`basenamenulltableprobe`, full `struct_size`, field
  `NULL`) both reject with `ENOSYS` before ever touching the upstream
  static/veneer buffer.
- Command (`tests/test_basename_behavior.sh`): ordinary path with a
  matching suffix, root, trailing slash, empty path (prints just a
  newline and exits `0` *before* calling libc `basename`, matching the
  plan's documented distinction from `basename("")` returning `"."`),
  zero/too-many operands, invalid option, `--` dash-leading operand,
  matching/nonmatching/equal-length/longer/empty suffix (the empty-suffix
  case is a no-op because `off` already points at the existing `NUL`,
  and `strcmp("", "")` matches trivially), repeated invocations, and a
  pipeline. Exact examples from the plan: `basename /tmp/example.txt
  .txt` -> `example\n`; `basename foo foo` -> `foo\n` (equal length,
  unchanged); `basename ''` -> `\n`; all exit status `0` except the
  three usage/option-error cases (status `1`).
- Provenance (`tests/test_netbsd_source.sh` for the command,
  `tests/test_netbsd_libc_source.sh` for the libc import): pinned hash,
  revision, and license entries in `UPSTREAM.md`; private link name
  present; host-facing `basename`/`setlocale`/`getopt`/`strlen`/`strcmp`/
  etc. symbols absent from the compiled object. Ordinary-source boundary
  (`tests/test_libc_source.sh`): the direct probe uses no cannedBSD-
  specific names and its compiled object imports the private
  `cb_libc_basename` veneer, not a host-facing `basename`.

## Documentation

- `LIBC.md`'s `libgen.h` bullet now covers both `dirname` and `basename`,
  noting the separate per-function task-owned buffers.
- `UPSTREAM.md` gains two new entries -- `NetBSD basename` (the libc
  import) and `NetBSD basename (command)` -- mirroring `dirname`'s own
  two-entry shape exactly, including the same two-clause NetBSD
  Foundation license distinction for the libc file versus the
  three-clause Regents license on the command.

## First Woodpecker attempt found a real bug

Commit `4e97b9f`'s `ci` check failed: `libcbasenameprobe` exited `12`,
meaning `check_repeated_calls` in `tests/libc_basename_probe.c` failed
on real hardware even though host `clang -fsyntax-only` passed it (a
logic error has no syntax to catch). The bug: the test asserted
`basename("/second/different/name")` equals `"different"`; the correct
last path component is `"name"`. Fixed the assertion to `"name"`.
Nothing else in the probe's sequence (return codes `2`-`11`, covering
every edge case, the boundary/overflow cases, and cross-task isolation)
failed, so this was the only defect the real gate found in this
iteration. `mac-automation` and `mac68k` were already `success` on
`4e97b9f` and are unaffected by this ordinary-libc-only fix.

## Second Woodpecker attempt found a second real bug

Commit `0f473c9` (the `check_repeated_calls` fix)'s `ci` check failed
differently: `sh: basename: no such file or directory` on the very
first command test case. Root cause: `commands/basename_module.c`
defines `cb_basename_program`, but nothing ever registered it with the
shell -- `src/programs.c`'s `cb_register_base_programs` (the actual
base-program table every shell instance, native and Mac, is built from)
was never updated to include it, unlike `cb_dirname_program`, which
*is* in that table. The `mac68k` and `mac-automation` checks stayed
green through both failures because they only build/compile
(`mac68k`) or check the image-matcher tooling (`mac-automation`); this
class of registration bug is a boot-time behavior gap, invisible to a
compile-only check. While fixing this, also found and fixed the same
gap for the *test probe* (not just the command): `platform/mac68k/
main.c`'s acceptance-case runner registers `cb_dirname_probe_program`
for every case but never registered `cb_basename_probe_program`, so the
Mac guest's own `libcbasenameprobe` case would have failed identically
whenever guest acceptance eventually runs. Fixed both: `cb_basename_
program` added to `cb_register_base_programs`'s table, and
`cb_basename_probe_program` added to `main.c`'s per-case registration
list, in both cases at the exact same position as their `dirname`
counterparts. Verified by exhaustive grep: every file referencing
`cb_dirname_program`/`cb_dirname_probe_program` now has an exact
`cb_basename_program`/`cb_basename_probe_program` counterpart, and vice
versa -- no remaining asymmetry between the two commands' wiring.

## Woodpecker results

- `4e97b9f`: `ci` failed (`libcbasenameprobe` exit `12`, a wrong
  expected value in this iteration's own test); `mac-automation` and
  `mac68k` both `success`.
- `0f473c9`: `ci` failed differently (`sh: basename: no such file or
  directory` -- `cb_basename_program` was never registered with the
  shell); `mac-automation` and `mac68k` both `success` again (a
  boot-time registration gap is invisible to a compile-only check).
- `fd42d36` (the shell/Mac-runner registration fix): `ci`,
  `mac-automation`, and `mac68k` all `success`. This is the reported
  final commit for this iteration.

## Remaining risk or follow-up

- Full `make ci` was never run locally (host Docker outage throughout
  this task); confirmed instead via Woodpecker's real Alpine runner on
  the final commit `fd42d36` (`ci`, `mac68k`, and `mac-automation` all
  `success`).
- Guest acceptance (AGENTS.md step 7) is explicitly coordinator-owned
  per this task's direction; not attempted here.
- `realpath(3)` and any other `libgen.h` surface remain out of scope,
  matching the bounded plan's explicit "no broader libgen API" limit.
