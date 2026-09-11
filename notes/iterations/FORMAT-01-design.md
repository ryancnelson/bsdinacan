# FORMAT-01 Design Note

## Base Evidence & Scope
- **Base SHA:** `1f906a876998567cbe686eeadc454eb31e24c3e9` (from `git merge-base work/FORMAT-01-design main`)
- **Goal:** Design an honest, minimal extension to `cannedBSD`'s internal `format_output` (in `libc/cb_libc.c`, lines 599-636) to support the exact signed-decimal formatting required by `uniq`.
- **Pinned `uniq` Dependency:** The `uniq.c` source is currently cached (NOT imported to `upstream/netbsd/`) at `work/NEXT-UTIL-02/scratch/uniq.c` relative to the canonical repository checkout (SHA-256: `78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`). It declares `static int numchars, numfields, repeats;` on line 57 and utilizes exactly `fprintf(ofp, "%4d %s", repeats + 1, str);` on line 195.
- **Out of Scope for `uniq`:** This formatter extension only solves the libc formatting boundary prerequisite. It explicitly does *not* fix `uniq.c`'s `repeats + 1` addition, which is source arithmetic outside scope without audited reachability. It also does not resolve `uniq`'s other missing API boundaries (e.g., `fgetln`, `asprintf`, `strtol`). Current `cb_libc_fprintf` accepts only the stdout and stderr singleton streams; formatting to an arbitrary output FILE is a separate prerequisite, not provided by this conversion extension.
- **Milestone Scope:** Documentation only. No runtime implementation, new ABI, or source code edits in this task. Assigned for a future utility milestone (post-Solaris / `tee` signals).

## Current Formatter State (`libc/cb_libc.c`)
- Supports strictly `%%` and `%s`.
- Sets `CB_EINVAL` and returns `-1` on any unsupported format specifier.
- Preempts return-count overflow by aborting `add_output` (line 589) if `length > (size_t)(INT_MAX - *total)`.
- The sink writer `write_all` (line 555-573) loops `while (length != 0)`: it guarantees positive short-write retries to completion. It aborts immediately upon a real negative descriptor error (`written < 0`) or 0-progress guard (`written == 0` -> `CB_EIO`), returning `-1`.
- Any output emitted *prior* to a failure is retained and actively transmitted to the underlying descriptor.
- `cb_libc_warn` and `cb_libc_err` explicitly save the caller's `errno` (lines 680, 701) before format evaluation for accurate diagnostics. `cb_libc_warn` restores `errno` (line 696) before returning, whereas `cb_libc_err` terminates execution immediately via `cb_libc_exit(eval)` (line 717) without restoring it. `cb_libc_errx` terminates similarly (line 674).

## Proposed Extension: Exact Bounded Width & Signed Decimal

1. **Strictly Bounded Widths (1-32)**
   - The formatter will parse an optional positive decimal width prior to `d` (`%d`, `%4d`).
   - **Exact Width Policy:** Supported widths are exclusively `1` through `32` (or a bare `%d` with no width). Any width $>32$, or any digit-accumulator parsing overflow, instantly triggers `CB_EINVAL` and aborts execution *before* any padding or number string is generated.
   - Width is a minimum total field length, including a minus sign. Pad on the left with spaces; never truncate digits or a sign when the representation is wider than the requested field. Zero converts to one digit, `0`.
   - Any literal characters correctly parsed and output *before* the invalid `%` format will remain emitted and preserved on the descriptor.

2. **Safe Buffer & Magnitude Conversion**
   - **Buffer Bound:** The conversion buffer size must be an exact compile-time limit expression (e.g., `char buf[(sizeof(int) * CHAR_BIT + 2) / 3 + 2]`), which mathematically bounds a 32-bit integer string. No dynamic allocation or contradictory magic numbers (like 64) are permitted.
   - **`INT_MIN` Safety:** Deriving the absolute value for conversion must explicitly cast the value to `unsigned int` (or execute mathematically equivalent bounds shifting) *before* magnitude derivation. A direct signed arithmetic negation `-(INT_MIN)` is undefined overflow in C and explicitly forbidden.

3. **Exact Unsupported Syntax Policy**
   - **Immediate `CB_EINVAL`:** Any flag (`-`, `+`, `0`, ` `, `#`), any precision period (`.`), length modifiers (`l`, `h`, `z`, `j`), or any alternative conversion specifier (`u`, `x`, `f`, `p`, `c`).
   - **Strict String Match:** The syntax `%4s` is explicitly not required by the `uniq` scope and will be actively rejected with `CB_EINVAL`. Only a bare `%s` remains valid.

4. **Sink Error Interruption & Retention**
   - If `add_output` fails due to a short-write loop abortion inside `write_all` (e.g., `EBADF`, `EPIPE`, `EIO`), the formatter exits immediately and returns `-1` (`EOF`).
   - No subsequent format parsing or buffer emission happens after a failure.

## Planned Tests: Ordinary Source and Private Runtime Fixtures

These are unexecuted acceptance requirements for the future implementation.
Keep ordinary source behind its existing private libc headers; runtime spies
and test-only access to static helpers belong in a separate runtime-side
fixture, not in `tests/libc_stdio_source.c` or the public ABI. Preserve the
existing normal/NULL-string/percent, stderr, closed-fd and EPIPE assertions.

- **Explicit Type `INT_MIN` Limit:** Test `INT_MIN` using an actual `int` expression from `<limits.h>` (e.g., `(int)(-2147483647 - 1)`) because a bare literal `-2147483648` acts as a `long` inside variadics and violates the `%d` contract.
- **Bounded Positive Padding:** `printf("%4d", 42)` yields `"  42"`.
- **Negative Padding:** `printf("%4d", -42)` yields `" -42"`.
- **Supported Boundaries:** Bare `%d` with zero produces `"0"` and returns 1. `%1d` with 42 produces `"42"` and returns 2; `%1d` with -42 produces `"-42"` and returns 3. `%32d` with 42 produces 30 spaces followed by `"42"` and returns 32; with -42 it produces 29 spaces followed by `"-42"`. Verify exact bytes and returned counts for `INT_MIN` and `INT_MAX`, including a width shorter than their representation.
- **Width Limit Enforcement:** `%33d` and `%999999d` both abort, preserve prefix, and set `EINVAL`.
- **Formatting Types:** `%04d`, `%-4d`, `%4s`, `%x` all yield `-1` and set `EINVAL`.
- **Preserve Unsupported-Format Coverage:** The existing `stdioprobe unsupported` case expects `printf("prefix:%d", 1)` to fail with prefix-only output. Once `%d` is supported, replace that deliberately invalid conversion with still-unsupported `%u` and a matching unsigned argument. Retain the same EINVAL, failure-return and prefix-only assertions in the ordinary probe and native harness; do not delete the case to make the new behavior pass.
- **Observe Stopping After Error:** A closed descriptor alone cannot prove the formatter stopped: later incorrect writes would fail too. Use a bounded runtime-side write spy with the real formatter. For `"a%db"`, let the first write fail with EBADF and make any later call capable of succeeding; require exactly one call, no captured bytes, return -1 and EBADF. A second plan accepts the literal prefix, then fails the first conversion write with EPIPE; require the prefix unchanged, no later sink calls, return -1 and EPIPE. An after-implementation control that continues after failure must fail these call-count assertions. Do not use invalid variadic arguments as a parsing detector.
- **Positive Short Writes and Errno:** Have the same bounded spy accept one byte per call for a padded negative integer followed by text. Require the exact complete output, exact total return count and preservation of the incoming errno. Record the accepted byte count and offered suffix on each call so repeated or skipped bytes are detected. Keep the established warn/err distinction: warn restores caller errno, err exits with its requested status; their surrounding diagnostic writes are not part of the formatter's stop-after-error rule.
- **Bounded Actual Return-Count Guard:** A separate runtime-side test build may expose a private test-only wrapper around the actual `add_output` helper. Pass a local accumulator through that wrapper; do not copy the guard into the test, add public ABI fields, or add a mutable production seed. Production `format_output` must continue to initialize its own count to zero and use that same helper. Starting at `INT_MAX - 1`, append one real byte: require success, exactly one sink call and total `INT_MAX`. Then append one more real byte: require -1/EINVAL, unchanged total and captured prefix, and no additional sink call. This executes the exact production guard with bounded storage and traffic. The spy rejects any unexpected second write before `add_output` can increment the accumulator, so an after-implementation control removing or shifting the real guard fails the call-count/errno assertions without executing signed overflow. No oversized pretend writes or signed-overflow-dependent expected result is permitted. The test-only wrapper must be absent from normal build exports.

## Design Review Follow-up

This note was corrected on `work/FORMAT-01-review` from the preserved original
commit `092566a655a9722714d24d912ba5deaeba615052`. Only the design note changes.
The base source/hash evidence above remains the original design's evidence;
these additional cases are specified, not executed runtime results. Future
implementation still requires actual red/green tests, full exact Woodpecker
checks, and native Solaris/Mac qualification under the project policy.
