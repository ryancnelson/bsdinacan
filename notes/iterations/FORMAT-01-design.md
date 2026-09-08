# FORMAT-01 Design Note

## Base Evidence & Scope
- **Base SHA:** `1f906a8`
- **Goal:** Design the smallest honest extension to `cannedBSD`'s internal `format_output` (in `libc/cb_libc.c`) to support NetBSD `uniq`'s specific count formatting.
- **Pinned `uniq` usage:** `uniq.c` (b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c) declares `static int repeats;` and uses exactly `fprintf(ofp, "%4d %s", repeats + 1, str);`.
- **Scope:** Documentation only. No runtime implementation, new ABI, or imported source in this task. Scheduled for a later utility milestone (post-Solaris / `tee` signals priority).

## Current Formatter State (`libc/cb_libc.c`)
- Supports exactly `%%` and `%s`.
- Any unsupported format specifier sets `CB_EINVAL` and returns `-1` (e.g. `tests/libc_stdio_source.c` currently tests `%d` yielding `EINVAL`).
- Return-count overflow is actively prevented: `add_output` fails if `length > INT_MAX - *total`.
- Sink errors (e.g., EPIPE, EBADF) return `-1` dynamically; `cb_libc_warn`/`err` retrieve and restore the caller's preserved `errno` correctly around formatting.

## Proposed Behavior: Signed Decimal & Minimum Width

1. **Smallest Honest Extension**
   - The formatter will parse an optional positive decimal width immediately following the `%`, terminating at `d`.
   - Examples: `%d`, `%4d`, `%10d` are supported.
   - The value is retrieved via `va_arg(arguments, int)`.
   
2. **ILP32 Bounds & `INT_MIN` Safe Conversion**
   - Target assumptions: ILP32 bounds (32-bit `int`). Range: `-2147483648` to `2147483647`.
   - **Proposed behavior:** The integer conversion must safely cast to `unsigned int` prior to magnitude derivation to prevent signed overflow when negating `INT_MIN` (`-2147483648`). The resulting maximum numeric string length is 11 characters (including the negative sign).

3. **Padding Logic**
   - The number string is generated in a local buffer.
   - If the requested width exceeds the string's length, the formatter will emit `(width - length)` spaces (`' '`) immediately prior to the string using `add_output()`.
   - If the width is less than or equal to the length, no padding is emitted and the string is never truncated.

4. **Exact Unsupported Syntax Policy**
   - **Rejections:** Any flag (`0`, `-`, `+`, ` `, `#`), any precision (`.`), any length modifier (`l`, `h`, `z`, `j`), or any other specifier (`x`, `u`, `f`, `p`, `c`) triggers immediate `CB_EINVAL` and returns `-1`.
   - **Strict String Match:** The format `%4s` (width applied to a string) is explicitly NOT required by `uniq` and will be rejected with `CB_EINVAL`. Only plain `%s` is supported for strings.
   - **Width Overflow:** Widths parsed beyond typical sane limits (e.g., overflowing an internal accumulator during parsing) trigger `CB_EINVAL`.

5. **Sink Partial / Error Behavior**
   - Padding and numeric buffers are emitted via the existing `add_output(..., length, &total)` mechanism.
   - **Proposed behavior:** If `add_output` fails due to a short write, `EBADF`, or `EIO` inside the underlying `write_all`, the formatter instantly aborts and bubbles up `-1` (`EOF`).
   - The existing `INT_MAX - *total` check dynamically protects the padding and string writes from overflowing the returned integer count.

6. **Meaningful Ordinary-Source Tests (`tests/libc_stdio_source.c`)**
   - Replace the existing `%d` -> `EINVAL` test with active format validations:
     - `INT_MIN` bounds check: `printf("%d", -2147483648)` correctly yields `"-2147483648"`.
     - Width padding check: `printf("%4d", 42)` yields `"  42"`.
     - Negative padding check: `printf("%4d", -42)` yields `" -42"`.
     - Width overflow check (no truncation): `printf("%4d", 12345)` yields `"12345"`.
   - Add new `EINVAL` assertions for specifically unsupported syntax: `%04d`, `%-4d`, `%4s`, `%x`.
   - Retain `EBADF` / `EPIPE` boundary tests to ensure format evaluation halts instantly without further string processing upon sink failure.
