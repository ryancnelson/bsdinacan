# HEAD-02: bounded failure characterization of the accepted head import

Tests-only, based on `origin/main` (`e65e36f`, the accepted unchanged
`head` import). No upstream source, runtime, or ABI change; no new API;
no new registered program; `CB_MAX_PROGRAMS`'s 64-slot ceiling
untouched. All 21 existing `tests/head_probe.c` cases (the exact
`HEAD-01-testplan.md` matrix) are preserved unchanged.

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
`argv[1]` as a single-digit mode selector (`'1'`..`'4'`); `argc < 2` or
no `argv[1]` keeps the original, unrelated pipe-producer behavior
(`argc == 1`, its only other caller). Each fault mode builds a
task-local `struct cb_api_v1` copy with `read`/`write` overridden and
calls the exact, unchanged `cb_head_main` via `cb_libc_start` -- the
real, registered `"head"` program is never touched, and is spawned
normally by other cases in this same fixture to prove isolation.

## Capacity: reused an existing registration, not a new one

Checked directly, not assumed: `FIXTURE_FULL` (`tests/test_core.c`) is
at exactly 64/64 (`14` base programs via `cb_register_base_programs` +
`50` explicit registrations), confirming the coordinator's own
statement that HEAD-01 already had to relocate `cb_strcpy_probe_program`'s
registration to make room there. `"headprobe"`/`"headpipeproducer"`
themselves run under `FIXTURE_MAC`, a separate table (`45`/`64` before
this change), which had room -- but per direction, no new registration
was added regardless: fault dispatch was folded into the existing
`"headpipeproducer"` entry instead. Its `requested_stack_size` was
raised from its original `64 * 1024` to `128 * 1024` (matching `"head"`'s
own budget) since fault mode runs the identical `cb_head_main` body,
including its 65536-byte automatic buffer -- a per-task stack
allocation, not a change to the 64-program registration table capacity.

## Characterized behavior (observed, not desired)

- **Input read failure, line mode**: a real `read()` failure partway
  through is indistinguishable from a clean, short EOF. `head` stops
  with the partial output already produced and **exits status 0** --
  not an error, silently. Verified with an injected failure landing
  exactly after two output lines.
- **Input read failure, byte mode**: identical conflation via `fread`'s
  own `rv == 0` check. Verified with a failure landing exactly after 4
  of a requested 20 bytes; exit status 0, exactly 4 bytes emitted.
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
  proving the retry path is actually correct, not merely present.
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

## Genuine red/green, from real execution, not assumed

Built and ran on the CI runner host's pinned Linux toolchain (the same
host Woodpecker's own `ci` check runs on), not this worktree host's own
separate compiler:

1. First attempt had a real off-by-one: the synthetic inner `argv`'s
   `NULL` terminator was written at `inner_argv[inner_argc - 1]`
   (overwriting the last real argument) instead of `inner_argv[inner_argc]`.
   `./build/test_core` failed exactly as expected: `command: headprobe`,
   status `41` -- the first new fault case, cleanly identifying which
   case broke. Fixed; rebuilt; the *next* real bug (the diagnostic
   prefix above) surfaced at status `43`, fixed the same way. Both are
   genuine, observed compile-and-run failures, not hypothetical.
2. After both fixes: `./build/test_core` exits `0`, `all core tests
   passed`, all 28 `head_probe` cases (21 original + 7 new) pass.
3. **Disposable negative control, labeled honestly as an
   after-implementation control, not red-first TDD**: temporarily
   disabled the negative-write fault (made mode 2 delegate to the real
   `write()` instead of injecting `-1`), rebuilt, reran --
   `./build/test_core` failed exactly at the corresponding case (status
   `43`), confirming this fixture actually detects the behavior it
   claims to, not merely asserting a value that happens to hold.
   Restored the real fault; reran; green again.
4. The real Mac68k build (`platform/mac68k/CMakeLists.txt`, which
   already lists `tests/head_probe.c`) was rebuilt against the pinned
   Retro68 image: `cmake`/`cmake --build` both exit `0`,
   `CannedBSD.bin`/`.APPL` produced. No guest run was attempted --
   that remains the coordinator's own action.

## Sequential-fixture-only safety, stated explicitly

The fault-mode globals (`fault_mode`, `fault_read_delivered`,
`fault_real_api`) are safe only because this whole fixture spawns one
child, waits for it to fully exit, then moves to the next -- never two
fault-mode children concurrently, and nothing in this file yields back
to the scheduler while a copy is bound. This is sequential-fixture-only
safety, not a general task-isolation mechanism, and is stated as such
in the source comment, not left implicit. `cb_libc_start(api, 0, NULL,
noop)` explicitly rebinds `cb_libc`'s internal binding back to the real
API after each successful dispatch (the failure path's own task exits
through `err()`/`errx()`'s non-local jump and never returns here to
need it), mirroring the same rebind-after-mock idiom already
established in this project's other fault-injection tests.

## Not claimed

No change to `head.c`, `cb_libc.c`, `commands/head_module.c`, or any
other runtime file. No new registered program. No Mac guest run. No
claim that this exhausts every possible failure shape `head` could
encounter -- only the specific, bounded set the backlog asked for:
deterministic input read failure in both modes, the three output-write
failure shapes, and repeated invocation after failure.
