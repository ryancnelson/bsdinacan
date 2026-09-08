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

**A flag alone does not prove the rebind actually happened** -- a
review round-3 finding: `fault_restored` is only bookkeeping set by
`fault_exit` itself, so any bug that reaches the flag-set statement
without actually completing the rebind (or a bug in `fault_exit`'s own
call) would still leave the flag true. `run_one()` therefore also runs
a behavioral probe after every fault-mode case: it snapshots
`fault_write_calls`, calls `cb_libc_write(1, NULL, 0)` (a zero-length,
side-effect-free write through `cb_libc`'s own internal, otherwise
opaque API binding), and asserts the counter is unchanged. If the real
API were not actually rebound, `cb_libc`'s internal binding would still
hold the exited child's task-local copy -- whose function pointers are
valid code addresses, not stack garbage, so the call would silently
route through `fault_write` and bump the counter, rather than erroring
or crashing. This is the authoritative check; `fault_restored` remains
a cheap secondary sanity check alongside it, not a replacement.
`fault_restored` is now set only *after* `cb_libc_start(real, ...)`
returns, not before, so it reflects the rebind call having actually run.

## Bounded callback counts

`fault_read`/`fault_write` each count their own calls. Past
`HEAD_FAULT_CALL_LIMIT` (64) they now call `fault_exit(HEAD_FAULT_BUDGET_EXIT_STATUS)`
directly (a dedicated, distinct exit status, 97, never used by any real
head-invocation case) instead of merely returning `-1` -- a bare `-1`
only stops one call; a hypothetical regression in cb_libc's own retry
loop that keeps calling anyway would not be stopped by that. Routing
through `fault_exit` guarantees a bounded, real process exit with the
API binding correctly restored first, on this path exactly as on a
genuine head exit. Not expected to be hit by any scenario here -- this
is a defensive cap for a regression that does not exist today, not a
behavior this fixture claims `head` or `cb_libc` actually has.

`fault_write`'s cap counts **every** invocation, any destination fd
(`fault_write_total_calls`) -- not just fd==1/stdout. A round-4 review
found the previous version returned early for `fd != 1` *before*
incrementing or checking anything, so writes to fd 2 (stderr, what
`err()`/`warn()` actually write through when a fault case's own stdout
write fails) were completely uncounted and unbounded, contradicting
this file's own "hard, defensive cap ... may be called" claim. The
existing `fault_write_calls` counter (fd==1 only, what
`expect_write_calls` asserts) is unchanged in meaning.

`struct head_case` gained three fields: `expect_write_calls` (existing;
`0` = "not checked", matching every existing/non-fault case via C's own
zero-fill), and new `expect_read_calls`/`expect_read_delivered`, which
use `HEAD_FAULT_UNCHECKED` (`-1`), not `0`, as their "not checked"
sentinel -- because a real, assertable value of exactly `0` delivered
bytes is the entire point of the `HEAD_FAULT_READ_FIRST` cases, and
colliding it with a "skip this check" sentinel would silently stop
checking the one thing those cases exist to prove. The
`HEAD_FAULT_WRITE_PARTIAL` case asserts an exact write-call count of 5
(10 bytes at 2 bytes/call); the four read-fault cases assert exact
`fault_read_calls`/`fault_read_delivered` pairs, derived from real
execution, not by inspection alone (see "Genuine bugs" below --
inspection got the byte-mode prefix-then-fail count wrong the first
time).

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
3. **Third round**: the first hand-derived `expect_read_calls` for the
   byte-mode prefix-then-fail case (`"headpipeproducer" "1" -c 20`,
   4-byte budget) was wrong -- assumed 2 calls, actual is 3.
   `./build/test_core` failed exactly at that case with the wrong
   expectation. Re-tracing `head.c`'s own byte-mode loop explains why:
   its `while (bytecnt)` outer loop only stops on a *zero* `fread`
   return, not a short one, so a first `fread(..., 20)` that returns 4
   (not 0) causes a **second** outer-loop iteration requesting the
   remaining 16 bytes -- which is what actually produces the third
   `fault_read` call (the second one, inside the first `fread`, already
   fails with the budget exhausted; the third, in the second outer
   iteration, fails again and this time yields `fread`'s own `rv == 0`,
   which is what actually stops `head`). Corrected to 3; verified green.
4. **Fourth round**, both caught by independent review, not by a failing
   test (as with round 2's dead-code finding):
   - The task-local API override (`copy` in `head_fault_dispatch`) was a
     stack-local variable. An omitted-rebind regression would leave
     `cb_libc`'s internal binding pointing into that stack frame *after*
     the child was reaped by `waitpid` -- reading it back from
     `run_one`'s `cb_libc_write()` probe was undefined behavior; that the
     round-3 control (below) observed the expected failure was
     happenstance, not a guaranteed outcome. Fixed by giving the override
     fixture-owned, static storage (`fault_api_copy`) that legitimately
     outlives the reap, for the lifetime of the whole (sequential-only)
     fixture. Re-ran the round-3 control against the corrected code and
     it still fails as expected -- now for a well-defined reason.
   - `fault_write`'s early return for `fd != 1` happened *before* the
     call-count cap check, so writes to fd 2 (stderr -- what `err()`/
     `warn()` actually write through) were completely uncounted and
     unbounded, despite this file's own claim of a "hard, defensive cap
     on how many times fault_read()/fault_write() may be called". Fixed
     by splitting into two counters: `fault_write_total_calls` (every
     invocation, any fd -- what the cap now checks) and the existing
     `fault_write_calls` (fd==1/stdout only -- what `expect_write_calls`
     asserts, unchanged in meaning).

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
- **Third round**: per the reviewer's explicit instruction that a
  control must omit the *actual rebind call*, not merely the bookkeeping
  flag -- replaced `cb_libc_start(real, 0, NULL, head_fault_noop)`
  inside `fault_exit` with a direct, non-rebinding call to
  `head_fault_noop(0, NULL)` (keeps the function referenced, avoiding an
  unrelated unused-function warning, while genuinely never touching
  `cb_libc`'s internal binding), leaving `fault_restored = 1` completely
  untouched. Rebuilt, reran -- `./build/test_core` failed at the first
  fault-mode case (status 41), confirming the **new `cb_libc_write`
  behavioral probe** (not the flag, which was still true and would have
  passed) is what actually catches this class of regression. Restored
  the real rebind call; reran; green again.
- **Fourth round**: re-ran the round-3 omitted-rebind control
  unchanged, against the corrected, static-storage `fault_api_copy` --
  `./build/test_core` still fails at the first fault-mode case (status
  41). This confirms the storage fix did not weaken the control; the
  detection is now a defined outcome of well-defined memory, not a
  previously-happenstance read of a freed stack frame. Restored; reran;
  green. Separately, to check the stderr/cap fix rather than just read
  the diff: temporarily set `HEAD_FAULT_CALL_LIMIT` to `1`, rebuilt,
  reran -- `./build/test_core` failed immediately at the first
  fault-mode case (status 41, `fault_write_total_calls` exceeding the
  now-tiny cap), confirming the cap is actually enforced end to end.
  Restored `HEAD_FAULT_CALL_LIMIT` to `64`; reran; green again. This
  smoke check confirms the cap fires; it does not by itself isolate
  stderr specifically from stdout (the failing case's own stdout writes
  alone already exceed a cap of 1) -- the stderr-inclusiveness itself is
  a direct, small code change (moving the fd-dispatch after the counter
  increment) verified by reading the corrected code, not by a dedicated
  stderr-only reproduction.

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

## Solaris 9 SPARC portability status

`origin/main` (`a1c85ba`) has since added a Solaris 9 SPARC portability
requirement (`AGENTS.md`, `notes/CI.md`) for runtime, libc, VFS, shell,
command, shared ABI, and host-adapter changes -- and this applies to
shared test/probe surface too, including `tests/head_probe.c`, not only
to `head.c`/`cb_libc.c` themselves; a "tests-only" change is not
automatically exempt merely for being tests-only. Per the transition
policy (SOLARIS-01 pending), **Solaris acceptance for this branch is
reported as pending integration**, not as not-required. This branch
does not import the Solaris reference (`work/SOLARIS-01-reference`) and
does not touch the shared Solaris rig; that work is tracked separately
under SOLARIS-01.

## Sequential-fixture-only safety, stated explicitly

The fault-mode globals (`fault_mode`, `fault_read_delivered`,
`fault_real_api`, `fault_read_calls`, `fault_write_calls`,
`fault_write_total_calls`, `fault_restored`, and now the API override
itself, `fault_api_copy`) are safe only because this whole fixture
spawns one child, waits for it to fully exit, then moves to the next --
never two fault-mode children concurrently, and nothing in this file
yields back to the scheduler while `fault_api_copy` is bound. This is
sequential-fixture-only safety, not a general task-isolation mechanism,
and is stated as such in the source comment, not left implicit. Giving
`fault_api_copy` static storage (round 4, see "Genuine bugs") relies on
this exact same guarantee -- a second concurrent fault-mode child would
corrupt it, which is precisely why this fixture never runs one.

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
