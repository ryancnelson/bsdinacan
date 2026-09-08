# HEAD-02: bounded failure characterization of the accepted head import

Tests-only, based on `origin/main` (`e65e36f`, the accepted unchanged
`head` import). No upstream source, runtime, or ABI change; no new API;
no new registered program; `CB_MAX_PROGRAMS`'s 64-slot ceiling
untouched. All 21 existing `tests/head_probe.c` cases (the exact
`HEAD-01-testplan.md` matrix) are preserved unchanged. Current total:
**31 cases** (21 original + 10 new).

## Scope and technique

Traced the pinned `upstream/netbsd/usr.bin/head/head.c` directly before
writing anything: line-mode (`head`'s default) reads one byte at a time
via `getc(fp)`, echoing with `putchar`; byte mode (`-c N`) reads in
chunks via `fread`/`fwrite`. Traced the actual veneer implementations
these route through (`libc/cb_libc.c`): `read_input` collapses a real
`read()` failure and a clean EOF to the same "stop" signal
`cb_libc_fread`/`cb_libc_getc` return (`result <= 0`); `cb_libc_fwrite`
distinguishes negative (mark error, preserve injected `errno`, stop),
zero (mark error, force `errno = EIO`, stop), and short-but-positive
(retry) writes. `cb_libc_feof(stdout)` always returns `0` for an output
stream in this implementation, so `head.c`'s own
`if (feof(stdout)) errx(1, "EOF on stdout");` branch is dead code for
every case this fixture can produce -- confirmed by reading the code,
not asserted from POSIX expectation.

No new API, no modification to `head.c`, `cb_libc.c`, or `head_module.c`.
Fault injection reuses the existing `tests/head_probe.c` infrastructure
(`populate`/`verify_file`/`action`/`run_one`) with one small,
behavior-preserving change (`run_one` now spawns `test->args[0]` instead
of a hardcoded `"head"`, so existing cases, all of which already have
`args[0] == "head"`, are unaffected) plus fault-injection logic folded
into the *existing* `"headpipeproducer"` registration (`cb_head_pipe_program`),
not a new one -- see "capacity" below for why. Argument-parsed via
`argv[1]` as a single-digit mode selector (`'1'`..`'5'`); `argc < 2` or
no `argv[1]` keeps the original, unrelated pipe-producer behavior
(`argc == 1`, its only other caller). Each fault mode builds a
task-local `struct cb_api_v1` copy with `read`/`write`/`exit` overridden
and calls the exact, unchanged `cb_head_main` via `cb_libc_start` -- the
real, registered `"head"` program is never touched, and is spawned
normally by other cases in this same fixture to prove isolation.

## Capacity: reused an existing registration, not a new one

Checked directly, not assumed: `FIXTURE_FULL` (`tests/test_core.c`) is
at exactly 64/64 (`14` base programs via `cb_register_base_programs` +
`50` explicit registrations), confirming the coordinator's own
statement that HEAD-01 already had to relocate `cb_strcpy_probe_program`'s
registration to make room there. Fault dispatch was folded into the
existing `"headpipeproducer"` entry instead of adding a new one. Its
`requested_stack_size` was raised from its original `64 * 1024` to
`128 * 1024` (matching `"head"`'s own budget) since fault mode runs the
identical `cb_head_main` body, including its 65536-byte automatic
buffer -- a per-task stack allocation, not a change to the 64-program
registration table capacity.

## Restoration mechanism: why a post-`cb_libc_start` rebind cannot work here

The pinned `head.c`'s own `main()` (`upstream/netbsd/usr.bin/head/head.c`,
line 138: `exit(eval);`) is unconditional on **every** path, success
included -- it never returns a value for `cb_libc_start` to hand back.
An earlier version of this fixture rebound `cb_libc`'s internal API
binding in code placed immediately *after* the call to
`cb_libc_start(&copy, ...)`; that code is dead on every path, not just
the failure path, since `exit()` performs a non-local jump straight to
the scheduler and never returns control to the caller. That earlier
version's own claim that this restored the real API was false --
`bound_api` was left pointing at a task-local copy on a now-dead
child's stack.

Fixed by installing `copy.exit = fault_exit` (a task-local override of
the API's own `exit` function pointer) instead: `fault_exit` rebinds
`cb_libc`'s internal API binding to the real API via
`cb_libc_start(real, 0, NULL, head_fault_noop)`, records that the
restoration ran (`fault_restored = 1`), then calls the real `exit()`.
Since the pinned `main()` always calls `exit()` itself, this is the
*only* point in the call chain that reliably executes before task
termination, on both the success and the failure path.
`head_fault_dispatch` no longer tries to capture a return value from
`cb_libc_start(&copy, ...)` -- that call never returns.

`run_one()` asserts `fault_restored` is true after `waitpid` for every
fault-mode case (`args[0] == "headpipeproducer"` with a mode digit in
`args[1]`), on both the success and the failure path, closing the gap
the previous, false assertion left open.

## Bounded callback counts

`fault_read`/`fault_write` each count their own calls and force a
bounded failure (`-1`, `errno = EIO`, still routed through the real
`fault_exit` so the API stays correctly restored) past
`HEAD_FAULT_CALL_LIMIT` (64) -- a defensive cap, not expected to be hit
by any scenario here, so that a hypothetical regression causing
unbounded read/write retries fails this fixture with a bounded, real
exit instead of hanging the whole test run. `struct head_case` gained
an `expect_write_calls` field (0 = "not checked", matching every
existing/non-fault case via C's own zero-fill); the
`HEAD_FAULT_WRITE_PARTIAL` case asserts an exact count of 5 (10 bytes
at 2 bytes/call), not merely "eventually reaches 10 bytes".

## Characterized behavior (observed, not desired)

- **Input read failure, line mode, after a prefix**: a real `read()`
  failure partway through is indistinguishable from a clean, short EOF.
  `head` stops with the partial output already produced and **exits
  status 0** -- not an error, silently. Verified with an injected
  failure landing exactly after two output lines.
- **Input read failure, byte mode, after a prefix**: identical
  conflation via `fread`'s own `rv == 0` check. Verified with a failure
  landing exactly after 4 of a requested 20 bytes; exit status 0,
  exactly 4 bytes emitted.
- **Input read failure on the very first call, line and byte modes**:
  the boundary case distinct from the above -- zero bytes ever
  delivered. Output is completely empty; the same exit-0 limitation
  still holds. Added this round per reviewer request, alongside the
  existing prefix-then-fail cases (not instead of them).
- **Output write failure, negative return, line mode**: `putchar`
  returns `EOF`, `head` calls `err(1, "stdout")`, exit status 1, zero
  bytes ever reach the output (the very first `putchar` fails).
- **Output write failure, negative return, byte mode**: `fwrite`
  returns short, `cb_libc_feof(stdout)`'s always-0 return means
  `head.c`'s own conditional always resolves to
  `err(1, "failure writing to stdout")`, never `errx(1, "EOF on stdout")` --
  confirmed by tracing, not by assumption. Exit status 1, zero bytes.
- **Output write failure, zero progress**: same code path, but
  `cb_libc_fwrite` itself forces `errno = EIO` regardless of what the
  injected `write()` did to `errno` -- the only externally visible
  difference from the negative-return case is the error text.
- **Output write, positive partial retry**: `write()` always returns a
  short, non-zero count; `cb_libc_fwrite`'s own retry loop reassembles
  the complete, correct output across several short writes. This is
  **not a failure** -- `head` exits 0 with the full, correct bytes,
  proving the retry path is actually correct, not merely present. Also
  asserts the exact call count: 10 bytes at 2 bytes per `write()` is
  exactly 5 calls.
- **Repeated invocation after failure**: after each of the two
  write-failure cases above, the real, unmodified `"head"` program is
  spawned normally (ordinary input/output, no injected fault) and
  produces exactly correct output at exit status 0 -- the fault state
  above does not leak into a sibling task.

Diagnostic prefixes read `"headpipeproducer:"`, not `"head:"` --
**found by actually running this fixture, not assumed**: a task's
`getprogname()`/`err()` diagnostic identity is fixed at spawn time from
its own real `argv[0]` (`"headpipeproducer"`, the name it was actually
spawned under); reconstructing a synthetic `argv[0] = "head"` for the
*inner* `cb_head_main` call only affects what that inner call itself
sees, not the task's own identity. The pinned source never reads its
own `argv[0]` anyway, so this has no effect on `head`'s own behavior --
only on this fixture's own expected diagnostic strings, corrected to
match what is actually produced.

## Genuine bugs found via real execution (development-time, not a control)

Built and ran on the CI runner host's pinned Linux toolchain (the same
host Woodpecker's own `ci` check runs on):

1. **First round**: a real off-by-one -- the synthetic inner `argv`'s
   `NULL` terminator was written at `inner_argv[inner_argc - 1]`
   (overwriting the last real argument) instead of `inner_argv[inner_argc]`.
   `./build/test_core` failed exactly as expected, identifying the
   first new fault case by index. Fixed; rebuilt; the diagnostic-prefix
   mistake (above) surfaced next, fixed the same way.
2. **Second round**: the post-`cb_libc_start` rebind was dead code on
   every path (see "Restoration mechanism" above) -- this was caught by
   independent review re-reading `head.c`'s own unconditional
   `exit(eval);`, not by a failing test (the old, false assertion
   happened to pass, because it never actually checked anything real).
   Fixed by moving restoration into `copy.exit`.

These are genuine defects found during development, kept here separate
from the disposable negative control below, per the reviewer's
explicit instruction not to conflate the two categories.

## Disposable negative control (after-implementation, not red-first TDD)

Labeled honestly as a control performed *after* the fixture was
believed complete, to prove it actually detects regressions rather than
passing vacuously -- distinct from the genuine bugs above, which were
found *during* development of the fixture itself, not via a
deliberately-broken control:

- **First round**: temporarily disabled the negative-write fault (mode
  2 delegated to the real `write()` instead of injecting `-1`),
  rebuilt, reran -- `./build/test_core` failed exactly at the
  corresponding case. Restored; reran; green again.
- **Second round**: temporarily commented out `fault_restored = 1;`
  inside `fault_exit`, rebuilt, reran -- `./build/test_core` failed at
  the first fault-mode case (`headprobe` status 41, the new
  `fault_restored` assertion catching the disabled restoration flag).
  Restored; reran; green again (`all core tests passed`, exit 0).

## Verified end to end, real execution

1. `make test` on the pinned Linux CI toolchain on the CI runner host:
   `./build/test_core` exits `0`, `all core tests passed`, covering all
   31 `head_probe` cases and the new `fault_restored`/
   `expect_write_calls` assertions.
2. The real Mac68k build (`platform/mac68k/build.sh`, which already
   lists `tests/head_probe.c` in `platform/mac68k/CMakeLists.txt`) was
   rebuilt against the pinned Retro68 image: `cmake`/`cmake --build`
   both exit `0`, `CannedBSD.bin`/`.APPL` produced, `tests/head_probe.c`
   compiles cleanly for that target too. No guest run was attempted --
   that remains the coordinator's own action.

## Sequential-fixture-only safety, stated explicitly

The fault-mode globals (`fault_mode`, `fault_read_delivered`,
`fault_real_api`, `fault_read_calls`, `fault_write_calls`,
`fault_restored`) are safe only because this whole fixture spawns one
child, waits for it to fully exit, then moves to the next -- never two
fault-mode children concurrently, and nothing in this file yields back
to the scheduler while a copy is bound. This is sequential-fixture-only
safety, not a general task-isolation mechanism, and is stated as such
in the source comment, not left implicit.

## Not claimed

No change to `head.c`, `cb_libc.c`, `commands/head_module.c`, or any
other runtime file. No new registered program. No Mac guest run. No
claim that this exhausts every possible failure shape `head` could
encounter -- only the specific, bounded set the backlog asked for:
deterministic input read failure in both modes (both a prefix-then-fail
and a fail-on-first-call boundary), the three output-write failure
shapes with an exact retry-call-count assertion, repeated invocation
after failure, and a bounded callback-count safety cap with its own
restoration-boundary assertion.
