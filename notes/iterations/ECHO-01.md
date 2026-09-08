# ECHO-01

## Real integration (this pass)

- Status: implementation, not preparation. `PROGNAME-01`/`STDOUT-01` are
  merged on `origin/main` at `00bf923` and `work/STDOUT-01-integration`
  (`7e7f28a`, independently confirmed as Woodpecker pipeline #226, all
  three checks `success`, before merging). Both were fetched and merged
  into this branch from fresh, per the coordinator's explicit direction,
  rather than assumed from memory.
- `netbsdecho` now builds as its own command object (`netbsd_echo.o`,
  private link name `cb_netbsdecho_main`), following the exact
  `dirname`/`basename`/`yes` pattern: `commands/echo_module.c` (new),
  a `CB_LIBC_PROGRAM` registration under the distinct name `netbsdecho`,
  wired into `src/programs.c`'s `cb_register_base_programs` (the shell
  builtin `echo` is untouched -- both now coexist under different
  names), the Makefile, and `platform/mac68k/CMakeLists.txt` (mirroring
  `cb_yes`/`cb_printenv`'s shape, since echo needs no separate NetBSD
  "gen" library object the way dirname/basename do).
- The pinned source is unchanged (still hash
  `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04`,
  revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`); its own build
  rule carries a documented `-Wno-unused-parameter` (only that one file)
  because `main` never reads `argc`, historically marked `/* ARGSUSED */`
  -- a lint-only annotation that has no effect on GCC/Clang warnings.
  `tests/test_netbsd_source.sh` now pins echo's hash/provenance and
  asserts its object imports only `cb_libc_*` veneers
  (`cb_libc_setprogname`, `cb_libc_setlocale`, `cb_libc_strcmp`,
  `cb_libc_printf`, `cb_libc_putchar`, `cb_libc_fflush`,
  `cb_libc_ferror`, `cb_libc_err`), never the host-facing names.

### Executable exact-output/status tests (from the design's own matrix)

`tests/test_echo_behavior.sh` runs the prepared matrix from this note's
earlier preparation pass against the real command through the shell,
exactly like `test_dirname_behavior.sh`/`test_basename_behavior.sh`:
no arguments, an empty argument, a single-space argument, `-n`
suppressing the trailing newline, `--` and `-e` as ordinary operands
(never special), multiple space-joined operands, and a literal
backslash-`n` operand proven NOT to be interpreted (only this test's
own trailing `\n` becomes a real newline; the two source characters
backslash-n inside the operand reach argv, and echo's output, completely
literally). That last case's shell-quoting was independently verified
byte-for-byte with a standalone `printf`/`xxd` check before being
written into the script, since this project's own inner shell escapes
backslashes even inside double quotes (unlike POSIX), which is easy to
get wrong silently.

### Write-failure / task-isolation coverage (needed STDOUT-01; now exists)

The one row the original preparation pass explicitly could not write --
"a forced write failure on one task's stdout, followed by a second,
independent task's success" -- is now covered natively in
`tests/test_echo_state.c`. The first version of this test used two
separate top-level kernels (one per case), which only proves each case
individually works, not that the two tasks' error state is actually
isolated from one another -- two independent kernels wouldn't share
state regardless. Reworked, per review, into a single parent task
(`parent_main`, mirroring `test_stdio_state.c`'s own `lifecycle_main`)
that `spawn`s the *same* registered `netbsdechofault` program twice
within *one* kernel, `waitpid`s on each in turn, and only changes the
injected-write behavior (`fail_stdout_write`) between the two spawns:
the first spawn, with the fault armed, makes the pinned source's own
`if (ferror(stdout) != 0) err(1, "write error");` path fire with exact
status `1` and exact `err(3)`-formatted stderr
(`netbsdechofault: write error: broken pipe\n`); the second, unarmed
spawn of the identical program afterward succeeds completely normally
(`hello world\n`, status `0`, no stderr). This proves the error state
left behind by the first child does not leak into its sibling, within
one shared kernel, rather than merely showing two unrelated runs both
work in isolation.

**A second, real correctness bug found in review, before any CI run:**
`cb_libc_start` sets `libc/cb_libc.c`'s internal `bound_api` global and
never restores it -- ordinary programs get away with this because
their own `CB_LIBC_PROGRAM` wrapper always calls `cb_libc_start` fresh
at the start of their own dispatch, rebinding it themselves regardless
of what a previous task left behind. This test's fault child instead
binds a *stack-local* `copy` (to install the injected `write()`), and
its failure path exits through `err()` -> `cb_libc_exit()` ->
`bound_api->exit()`, which never returns control to that child's own
wrapper -- so nothing in that child ever gets a chance to restore
anything, and `bound_api` is left pointing at a struct that lived on
that now-terminated task's own stack. `parent_main` now explicitly
rebinds `bound_api` back to the kernel's real `api` (via
`cb_libc_start(api, 0, NULL, noop_main)`, the same rebind-after-mock
idiom `test_stdio_state.c`'s own `injected_main` already uses) right
after each `waitpid`, before doing anything else -- covering both the
child that exited abnormally and, defensively, the one that returned
normally too.

### A real capacity conflict, found and fixed, not worked around

Adding `netbsdecho` as a 13th command to `cb_register_base_programs`
(shared, unconditional, by every `test_core.c` fixture) would have
pushed `FIXTURE_FULL` -- already documented as sitting exactly at
`CB_MAX_PROGRAMS`'s 64-slot ceiling (12 base programs before this
change, plus its own 52 explicit registrations) -- to 65, overflowing
the production capacity constant on the 65th `cb_kernel_register` call.
This was caught by actually counting both lists directly against the
merged source, not assumed from the design or prior notes. Per this
project's own established pattern (`FIXTURE_DIRNAME`/
`FIXTURE_DIRENT`/`FIXTURE_BASENAME`), the fix scopes `yesreader_program`
and `yesprobe_program` out of `FIXTURE_FULL` into a new, minimal
`FIXTURE_YES` (mirroring `register_basename_probes`), freeing exactly
the one slot this change costs. `FIXTURE_FULL` is now at 63/64 (13 base
+ 50 explicit). `CB_MAX_PROGRAMS` itself was not touched.

**This was incomplete on the first push (`e0802a6`) and caught for real,
not just in review.** There were actually *two* external call sites
referencing the literal fixture value `1` for these programs, not one:
`run_case("yesprobe", "ok\n", 0, 1)` (updated in the first pass) and a
second, separate `run_case("yes ok | yesreader", "ok\n", 0, 1)` a few
lines later, spawning `yesreader` through an ordinary shell pipeline.
The second one was missed initially. Woodpecker's own Linux `ci` log on
`e0802a6` reported it precisely: expected status/output `0`/`ok`,
actual `127`/`sh: yesreader: no such file or directory` -- `yesreader`
was no longer registered in `FIXTURE_FULL` after the scoping change,
and this call site still asked for it there. `mac-automation` and
`mac68k` were both still `success` on that same commit; only `ci`
caught this. This is genuine red evidence from actual execution, not
regression theater -- it is recorded here exactly because it is real,
not because it is flattering. Fixed by updating this second call site
to `FIXTURE_YES` too, in the same follow-up commit as the fixes below.

### Mac registration and the guest acceptance-case count

`netbsdecho` is registered exactly like `dirname`/`basename`: through
`cb_register_base_programs` (shared by the real Mac app and the Linux
native tests), so no separate `platform/mac68k/main.c` change was
needed for the command itself (that file only lists native *probe*
programs, not base commands). `platform/mac68k/acceptance_cases.def`
gets four new plain shell-invocable cases covering no-arguments,
`-n`, `--` as an ordinary operand, and multiple operands -- the
subset of the design's matrix reachable without STDOUT-01's
fault-injection surface (the write-failure/task-isolation case stays
Linux-native-only, exactly like `test_stdio_state.c`'s own
`stdioinject`/`stdiolifecycle` cases, which also have no Mac
equivalent in this file). `platform/mac68k/guest.py`'s
`expected_result()` derives one `PASS <command>` line per
`acceptance_cases.def` entry plus a fixed `PASS contexts` line, so
`tests/test_mac_guest.py`'s hardcoded expected total moved from 42
(the count already accepted at `894b753`, per the coordinator's
status) to 46. Verified directly with `python3 tests/test_mac_guest.py`
(all 18 cases pass on this host, no Docker or Mac toolchain needed for
this particular check) rather than assumed from the arithmetic alone.
Running the actual case list against a real Mac guest is the
coordinator's, tracked in MAC-12.

### Coordinator status incorporated into this base

Re-fetched `origin/main` after the coordinator reported `STDOUT-01`/
`MAC-12` accepted and merged (`894b753`, 42-PASS cold guest run,
`16bfaea` docs) and merged it into this branch (clean, no conflicts) on
top of the earlier merge of `origin/main` at `00bf923` and
`origin/work/STDOUT-01-integration` at `7e7f28a`. Per the coordinator's
explicit correction, this branch's uncommitted `BACKLOG.md` edit was
reverted -- rollup edits to that file are the coordinator's, not a
worker's; this note is the sole record of ECHO-01's own evidence.

The coordinator separately reported `origin/main` advancing again to
`1bb7c9e` (`ARGV-01` accepted, 43 Mac records, plus a `head` plan doc)
with explicit "no dependency on your ECHO code" and that the
coordinator will merge that integration later. This branch has
deliberately *not* fetched/merged that commit: it is unrelated to
ECHO-01's own dependency chain, and the coordinator asked for it to
stay that way for now. Once it does land on `main` and gets merged
here, `platform/mac68k/acceptance_cases.def`'s guest-run total becomes
47 (43 + this branch's 4), not 46 -- the 46 recorded above is exactly
this branch's own current merge-base, honestly scoped to what has
actually been fetched and merged into it so far, not a prediction of
where `main` will be later.

### Honest verification status

Docker Desktop on this host is still down (the outage first hit during
`BASENAME-01`), and this project's runtime backend needs `ucontext(3)`,
which is deprecated/removed on this host's macOS SDK -- so a full local
`make ci` is not possible here, exactly as in every prior iteration this
session. Verified locally instead: `clang -fsyntax-only` with each
file's exact real build flags (echo's object, `commands/echo_module.c`,
`src/programs.c`, `tests/test_core.c`, `tests/test_echo_state.c`, all
clean), `bash -n` on both shell scripts, and a standalone `printf`/`xxd`
byte-level check of the trickiest test case's shell-quoting. Neither
`clang -fsyntax-only` nor any other local check available on this host
can execute code, so no genuine local red/green cycle was possible for
`tests/test_echo_state.c` specifically -- that gap is real, not glossed
over.

Woodpecker on the first pushed commit (`e0802a6`) is exactly what
caught the missed `FIXTURE_YES` call site above: `mac-automation` and
`mac68k` were `success`, `ci` was not, with an exact, specific failure
(`yesreader`: expected `0`/`ok`, actual `127`/command not found) that
would have been invisible to any of this session's local checks. That
is genuine execution-based red evidence, obtained from real CI, not
fabricated or assumed. This note will keep recording the exact result
of each subsequent push honestly -- this is not being claimed green
before Woodpecker actually says so on the exact pushed commit.

**Outcome**: `1d27c1e` (the commit fixing the missed `FIXTURE_YES` call
site and the `bound_api` rebind) came back `success` on all three
Woodpecker checks -- `ci`, `mac-automation`, and `mac68k`. This is the
exact candidate commit for the coordinator's review.

## Preparation pass (superseded by the above; kept for history)

The section below is the original preparation-only record from before
`PROGNAME-01`/`STDOUT-01` were merged. It is kept verbatim for history;
the "Remaining steps" and "not-yet-executable" framing it describes has
now been carried out, as recorded above.

- Status: preparation, explicitly not implementation. `PROGNAME-01`
  (`work/PROGNAME-01`, two commits, not yet merged) and `STDOUT-01`
  (not yet started as of this note) are unmerged prerequisites this
  design (`notes/iterations/ECHO-01-design.md`) requires before the
  imported command can even compile, let alone pass a falsifiable red
  test or a real CI gate. This note records what was prepared now and
  what remains, without claiming any of the remaining work is done.
- Base SHA: `0423c31` (`origin/main`, freshly fetched).
- Branch: `work/ECHO-01`.
- Authorization: coordinator-authorized preparation while prerequisites
  integrate; explicitly no fake stubs and no success claims ahead of
  `PROGNAME-01`/`STDOUT-01` landing.

## What was actually done

1. Vendored `bin/echo/echo.c` unchanged at the pinned revision
   (`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`) to
   `upstream/netbsd/bin/echo/echo.c`. Hash verified directly against a
   fresh download: `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04`,
   matching both the roadmap's own measured hash and the file as
   fetched here -- not merely trusted from the roadmap document.
2. Added the `UPSTREAM.md` provenance entry, explicitly marked
   "preparation only, not yet compiled or linked" so the record cannot
   be misread later as claiming a working import.
3. **Deliberately not done, and why**: no Makefile rule, no module
   file, no `commands/echo_module.c`-equivalent, and no registration
   anywhere. The vendored source calls `setprogname`, `putchar`,
   `fflush`, and `ferror` -- none of which this base declares or
   implements as private veneers (confirmed by reading
   `include/cannedbsd/libc.h` and `libc/cb_libc.c` directly in this
   worktree, not just trusting the roadmap's own diagnostic). Adding a
   compile rule now would either reproduce that exact failure (useless
   as evidence -- it was already measured) or require inventing stub
   declarations for `setprogname`/`putchar`/`fflush`/`ferror` ahead of
   `PROGNAME-01`/`STDOUT-01`'s own real design, which the coordinator's
   authorization explicitly forbids ("no fake stubs or success claims").

## Prepared, not-yet-executable test matrix

These are the exact command-level cases this iteration's eventual
`tests/test_echo_behavior.sh` (or equivalent) must assert once the
command actually builds, derived directly from reading the pinned
source's control flow (not guessed): it does not call `getopt`,
recognizes only a literal first `-n` argument, never interprets `--`,
`-e`, or backslash sequences as special, and joins operands with a
single space.

| Command | Expected stdout | Expected stderr | Status |
| --- | --- | --- | --- |
| `netbsdecho` (no args) | `\n` | (none) | 0 |
| `netbsdecho ""` | `\n` | (none) | 0 |
| `netbsdecho " "` | ` \n` | (none) | 0 |
| `netbsdecho -n foo bar` | `foo bar` (no trailing newline) | (none) | 0 |
| `netbsdecho -- foo` | `-- foo\n` (`--` is an ordinary operand) | (none) | 0 |
| `netbsdecho -e foo` | `-e foo\n` (`-e` is an ordinary operand, not an escape flag) | (none) | 0 |
| `netbsdecho 'a\nb'` | `a\nb` + a real trailing newline (the two source characters backslash-`n` are printed literally, not interpreted) | (none) | 0 |

The remaining case the design and roadmap both call for --
"earlier output failure and a subsequent independent task's success"
-- cannot be written as a concrete command line yet: it requires
`STDOUT-01`'s task-owned output-error state and a way to force a real
write failure (mirroring this project's existing fault-injection
patterns, e.g. the pipe-capacity and allocation-failure probes used by
earlier iterations), neither of which exists on this base. Once
`STDOUT-01` lands, this case should: force a write failure on one
task's stdout (so `ferror(stdout)` is nonzero and the pinned source's
own `if (ferror(stdout) != 0) err(1, "write error");` fires, producing
exact stderr matching `err(3)`'s format and status `1`), then run a
second, independent task's `netbsdecho` to completion and confirm it
succeeds with completely normal output -- proving the error state is
genuinely per-task, not a shared/global flag that would incorrectly
contaminate the second task.

## Remaining steps (not started here)

1. `PROGNAME-01` and `STDOUT-01` merge to `main` (both coordinator/other
   workers' responsibility, tracked in their own branches and design
   doc; not touched by this preparation).
2. Once both are available on a fresh `main`, rebase this branch (or
   re-fetch into a fresh worktree, matching this session's established
   practice when a dependency's base moves mid-task) and add the actual
   compile rule, distinct object name, `commands/echo_module.c`-style
   registration under the name `netbsdecho` (per the design's item 3 --
   deliberately not replacing the existing shell builtin `echo`), and
   wire the prepared test matrix above into a real, executable
   `tests/test_echo_behavior.sh`, plus the write-failure/independent-task
   case once `STDOUT-01`'s fault-injection surface exists.
3. A genuine falsifiable red (the command fails to link without its
   own module/registration, exactly as the other imported commands in
   this project have done), full `make ci`, and a fresh Mac guest
   artifact run (coordinator-owned) before this item is considered
   done.

## Constraints preserved

- `CB_MAX_PROGRAMS` (64) is untouched; no registration was added at
  all in this preparation pass, so there is nothing to scope into a
  fixture yet. The eventual implementation should follow the existing
  `FIXTURE_DIRNAME`/`FIXTURE_DIRENT`/`FIXTURE_BASENAME` scoped-fixture
  pattern for its own native test coverage rather than growing
  `FIXTURE_FULL`.
- No guest/Basilisk II acceptance attempted; that remains
  coordinator-owned per this task's direction.
- No merge to `main` attempted or implied.

## CI verification of the preparation commit

The preparation commit (`1c9f28e`, pure vendoring plus documentation --
no Makefile, build, or source-of-truth file touched) first reported
Woodpecker `ci` as `failure`, with `mac-automation` and `mac68k` both
`success`. Fetching the actual pipeline log (via the Woodpecker API,
not just the GitHub status summary) showed the concrete failure:

```
FAIL: cannedBSD dirname veneer does not use the imported dirname
```

from `tests/test_netbsd_libc_source.sh`'s
`nm -u $BUILD_PATH/cb_libc.o | grep cb_libc_dirname_upstream` check,
raised inside `make check-build-modes`'s own internal
`make clean; make sanitize; make test` sequence (`tests/test_build_modes.sh`).

This is very unlikely to be a real regression from this commit:

- The identical check, against the identical source tree, had already
  passed twice earlier in the same pipeline run -- once for the main
  `make clean test` (`BUILD_PATH=build`) and once for the main
  `make sanitize` (`BUILD_PATH=build/sanitize`) -- both logging
  `pinned unmodified NetBSD libc source boundary passed`. Only the
  third, redundant rebuild triggered by `check-build-modes` failed.
- This commit changed zero files the check depends on: not
  `libc/cb_libc.c`, not `src/core.c`, not any dirname Makefile rule,
  not `tests/test_netbsd_libc_source.sh` itself.

The leading hypothesis is CI-runner-level nondeterminism (e.g.
resource pressure or a shared/racing build directory on the
Woodpecker host) rather than a genuine bug, since three consecutive,
byte-for-byte-identical rebuild-and-check cycles should otherwise be
fully deterministic. This note records that a fresh commit was pushed
specifically to test that hypothesis (no direct pipeline rerun was
available: the Woodpecker API's rerun endpoint returned `401`
unauthorized), and the honest outcome will be recorded here once that
run completes -- this is not being reported as green until it is
actually confirmed green on the exact pushed commit.

**Outcome**: the follow-up commit (`efcaaa4`) came back `success` on
all three Woodpecker checks (`ci`, `mac-automation`, `mac68k`) --
`nm -u build/sanitize/cb_libc.o | grep cb_libc_dirname_upstream`
passed cleanly through all three rebuild-and-check cycles this time,
including the `check-build-modes` cycle that failed on `1c9f28e`. The
failure did not reproduce against an unchanged source tree, so this
confirms transient CI-runner nondeterminism on `1c9f28e`, not a real
bug in this preparation commit or in the dirname veneer itself.
ECHO-01 preparation is therefore green as of `efcaaa4`, with the
scope explicitly limited to what this note already describes:
vendoring plus documentation only, no compile rule, no registration,
pending `PROGNAME-01`/`STDOUT-01`.
