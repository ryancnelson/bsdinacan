# FORMAT-01 Design Note

## Base Evidence & Scope
- **Base SHA:** `1f906a8`
- **Goal:** Design the smallest honest extension to `cannedBSD`'s internal `format_output` (in `libc/cb_libc.c`, lines 599-636) to support NetBSD `uniq`'s specific count formatting.
- **Pinned `uniq` Dependency:** `upstream/netbsd/usr.bin/uniq/uniq.c` (`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`) declares `static int repeats;` (line 64) and utilizes exactly `fprintf(ofp, "%4d %s", repeats + 1, str);` (line 120). 
- **Out of Scope for `uniq`:** This formatter extension only solves the libc boundary prerequisite. It does *not* fix `uniq.c`'s internal integer overflow vulnerability (`repeats + 1`) nor does it resolve its other missing libc/API boundaries (e.g. `ctype`, `errx`).
- **Milestone Scope:** Documentation only. No runtime implementation, new ABI, or imported source in this task. Assigned for a future utility milestone (post-Solaris / `tee` signals).

## Current Formatter State (`libc/cb_libc.c`)
- Supports strictly `%%` and `%s`.
- Sets `CB_EINVAL` and returns `-1` on any unsupported format specifier (currently verified by `tests/libc_stdio_source.c`'s `%d` -> `EINVAL` test).
- Preempts return-count overflow by aborting `add_output` (line 589) if `length > INT_MAX - *total`.
- The sink writer `write_all` (line 555-573) iterates to completion over positive short writes (`written > 0` but `< length`). It aborts immediately upon a real error (`written < 0`) or 0-progress guard (`written == 0` -> `CB_EIO`), returning `-1`.
- Any output emitted *prior* to a failure is correctly retained and transmitted to the underlying descriptor.
- `cb_libc_warn` and `cb_libc_err` explicitly save and restore the caller's `errno` (lines 680, 696) around format evaluation to protect accurate diagnostics.

## Proposed Extension: Exact Bounded Width & Signed Decimal

1. **Strictly Bounded Widths (1-32)**
   - The formatter will parse an optional positive decimal width prior to `d` (`%d`, `%4d`). 
   - **Width bounds:** The parsed width must be exactly $\le 32$. Widths $>32$, or any digit-accumulator parsing overflow, instantly trigger `CB_EINVAL` and abort execution *before* any padding or number string is generated.
   - Any literal characters correctly parsed and output *before* the invalid `%` format will remain emitted and preserved.

2. **Safe Buffer & Magnitude Conversion**
   - **Buffer Bound:** The conversion buffer size must be derived dynamically from type limits rather than magic integers. For a signed 32-bit `int`, the required text bytes will not exceed `(sizeof(int) * CHAR_BIT + 2) / 3 + 2` (approx. 13 chars), safely absorbing the maximum numeric width. With a strict width cap of `32`, a single 64-byte stack buffer trivially holds the padded result.
   - **`INT_MIN` Safety:** Deriving the absolute value for conversion must cast the value to `unsigned int` (or mathematically equivalent bounds shifting) *before* magnitude derivation. A direct signed negation `-(INT_MIN)` constitutes undefined overflow in C. 

3. **Exact Unsupported Syntax Policy**
   - **Immediate `CB_EINVAL`:** Any flag (`-`, `+`, `0`, ` `, `#`), any precision period (`.`), length modifiers (`l`, `h`, `z`, `j`), or any alternative conversion specifier (`u`, `x`, `f`, `p`, `c`). 
   - **Strict String Match:** The syntax `%4s` is explicitly not required by the `uniq` scope and will be rejected with `CB_EINVAL`. Only a bare `%s` remains valid.

4. **Sink Error Interruption & Retention**
   - If `add_output` fails due to a short-write loop abortion inside `write_all` (e.g., `EBADF`, `EPIPE`, `EIO`), the formatter exits immediately and returns `-1` (`EOF`).
   - No subsequent format parsing or buffer emission happens after a failure.

## Meaningful Ordinary-Source Tests (`tests/libc_stdio_source.c`)

- **Explicit Type `INT_MIN` Limit:** `printf("%d", (int)(-2147483647 - 1))` yields `"-2147483648"`. *(Note: Bare `-2147483648` evaluates as `long` in C90 variadic arguments and is illegal for `%d`.)*
- **Bounded Positive Padding:** `printf("%4d", 42)` yields `"  42"`.
- **Negative Padding:** `printf("%4d", -42)` yields `" -42"`.
- **Width Limit Enforcement:** `%33d` and `%999999d` both yield `-1` and set `EINVAL`.
- **Formatting Types:** `%04d`, `%-4d`, `%4s`, `%x` all yield `-1` and set `EINVAL`.
- **No-Output-After-Error:** Ensure `EBADF` injection during a multi-format string (e.g. `printf("a%db", ...)` on a closed fd) halts mid-stream, verifying the `b` is never evaluated/emitted to a repaired descriptor.
- **Dynamic `INT_MAX` Return-Count Check:** Test a sequence of writes that artificially pushes the `*total` accumulator above `INT_MAX`, verifying the bounds check in `add_output` successfully traps and aborts.
