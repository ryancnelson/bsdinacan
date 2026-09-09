# FORMAT-01 Design Note

## Base Evidence & Scope
- **Base SHA:** `1f906a88b5e28a5ff255b0a39a738dc667ebf09f` (from `git log -1`)
- **Goal:** Design an honest, minimal extension to `cannedBSD`'s internal `format_output` (in `libc/cb_libc.c`, lines 599-636) to support the exact signed-decimal formatting required by `uniq`.
- **Pinned `uniq` Dependency:** The `uniq.c` source is currently cached (NOT imported to `upstream/netbsd/`) at `/Users/ryan/devel/bsdinacan/work/NEXT-UTIL-02/scratch/uniq.c` (SHA-256: `78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`). It declares `static int repeats;` on line 57 and utilizes exactly `fprintf(ofp, "%4d %s", repeats + 1, str);` on line 195.
- **Out of Scope for `uniq`:** This formatter extension only solves the libc formatting boundary prerequisite. It explicitly does *not* fix `uniq.c`'s internal integer arithmetic overflow vulnerability (`repeats + 1`), which is outside the formatter's scope. It also does not resolve `uniq`'s other missing API boundaries (e.g., `fgetln`, `asprintf`, `strtol`).
- **Milestone Scope:** Documentation only. No runtime implementation, new ABI, or source code edits in this task. Assigned for a future utility milestone (post-Solaris / `tee` signals).

## Current Formatter State (`libc/cb_libc.c`)
- Supports strictly `%%` and `%s`.
- Sets `CB_EINVAL` and returns `-1` on any unsupported format specifier.
- Preempts return-count overflow by aborting `add_output` (line 589) if `length > (size_t)(INT_MAX - *total)`.
- The sink writer `write_all` (line 555-573) loops `while (length != 0)`: it guarantees positive short-write retries to completion. It aborts immediately upon a real negative descriptor error (`written < 0`) or 0-progress guard (`written == 0` -> `CB_EIO`), returning `-1`.
- Any output emitted *prior* to a failure is retained and actively transmitted to the underlying descriptor.
- `cb_libc_warn` and `cb_libc_err` explicitly save and restore the caller's `errno` (lines 680, 696) around format evaluation to protect accurate diagnostics. `cb_libc_errx` and `cb_libc_err` both accurately terminate execution via `cb_libc_exit(eval)` (lines 674, 717).

## Proposed Extension: Exact Bounded Width & Signed Decimal

1. **Strictly Bounded Widths (1-32)**
   - The formatter will parse an optional positive decimal width prior to `d` (`%d`, `%4d`). 
   - **Exact Width Policy:** Supported widths are exclusively `1` through `32` (or a bare `%d` with no width). Any width $>32$, or any digit-accumulator parsing overflow, instantly triggers `CB_EINVAL` and aborts execution *before* any padding or number string is generated.
   - Any literal characters correctly parsed and output *before* the invalid `%` format will remain emitted and preserved on the descriptor.

2. **Safe Buffer & Magnitude Conversion**
   - **Buffer Bound:** The conversion buffer size must be derived dynamically as a compile-time limit expression (e.g., `char buf[(sizeof(int) * CHAR_BIT + 2) / 3 + 2]`), which mathematically bounds a 32-bit integer string. No dynamic allocation or contradictory magic numbers (like 64) are permitted.
   - **`INT_MIN` Safety:** Deriving the absolute value for conversion must explicitly cast the value to `unsigned int` (or execute mathematically equivalent bounds shifting) *before* magnitude derivation. A direct signed arithmetic negation `-(INT_MIN)` is undefined overflow in C and explicitly forbidden.

3. **Exact Unsupported Syntax Policy**
   - **Immediate `CB_EINVAL`:** Any flag (`-`, `+`, `0`, ` `, `#`), any precision period (`.`), length modifiers (`l`, `h`, `z`, `j`), or any alternative conversion specifier (`u`, `x`, `f`, `p`, `c`). 
   - **Strict String Match:** The syntax `%4s` is explicitly not required by the `uniq` scope and will be actively rejected with `CB_EINVAL`. Only a bare `%s` remains valid.

4. **Sink Error Interruption & Retention**
   - If `add_output` fails due to a short-write loop abortion inside `write_all` (e.g., `EBADF`, `EPIPE`, `EIO`), the formatter exits immediately and returns `-1` (`EOF`).
   - No subsequent format parsing or buffer emission happens after a failure.

## Meaningful Ordinary-Source Tests (`tests/libc_stdio_source.c`)

- **Explicit Type `INT_MIN` Limit:** Test `INT_MIN` using an actual `int` expression from `<limits.h>` (e.g., `(int)(-2147483647 - 1)`) because a bare literal `-2147483648` acts as a `long` inside variadics and violates the `%d` contract.
- **Bounded Positive Padding:** `printf("%4d", 42)` yields `"  42"`.
- **Negative Padding:** `printf("%4d", -42)` yields `" -42"`.
- **Width Limit Enforcement:** `%33d` and `%999999d` both abort, preserve prefix, and set `EINVAL`.
- **Formatting Types:** `%04d`, `%-4d`, `%4s`, `%x` all yield `-1` and set `EINVAL`.
- **No-Output-After-Error:** Ensure an `EBADF` fault halts execution immediately during a multi-format string (e.g. `printf("a%db", ...)` on a closed fd), verifying the `b` is never evaluated or emitted, and that the warning `errno` preservation remains perfectly intact.
- **Dynamic `INT_MAX` Return-Count Check:** Introduce a bounded test seam to seed the runtime-side accumulator near `INT_MAX`. A format string appending beyond `INT_MAX` must explicitly trip the `length > (size_t)(INT_MAX - *total)` guard, rejecting the format *before* signed integer overflow and returning `CB_EINVAL` (without relying on gigabytes of fake oversized writes).
