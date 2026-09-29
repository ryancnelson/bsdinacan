# TEST-ENTRY-01 — extract the existing fclose probe callback correction

- Status: merged as `7a8b0fb`; exact #502 all-three CI and independent review passed.
- Base: `9d13844fd298b198f77f4f4b1ed966e0b1421564`.
- Branch: `work/TEST-ENTRY-01`.
- Hypothesis: the fclose probe defines `main(void)` but its module declares and
  invokes an `int (int, char **)` callback; calling through that incompatible
  function pointer is undefined behavior.

## Red

Exact main-derived Woodpecker #499 reports `libc/cb_libc.c:85:12: runtime
error: call to function (unknown) through pointer to incorrect function type`
in the sanitizer run. Its workflow still succeeds because UBSan is recoverable;
workflow success must not be described as a clean sanitizer log.

Independently compile the unchanged probe with its module's existing prototype
in a temporary header:

```sh
printf '%s\n' 'extern int cb_fclose_stdout_probe_main(int argc, char *argv[]);' > entry.h
cc -std=c99 -Wall -Wextra -Werror -Wpedantic -Iinclude -Ilibc/include \
  -Dmain=cb_fclose_stdout_probe_main -include ./entry.h -fsyntax-only \
  tests/libc_fclose_stdout_probe.c
```

Observed exit 1: conflicting types for `cb_fclose_stdout_probe_main`, pointing
to `int main(void)` and the actual module prototype. This is a focused type
check, not a new behavioral test or a production change.

## Change and focused green

The exact existing file correction from Claude's `b2d003b` is extracted without
modification; its unrelated ls test and unaccepted runtime are not included.
No second implementation is introduced. The corrected two-argument definition
explicitly ignores its arguments. The same strict syntax command exits zero.
Existing `fclosestdoutprobe` runtime assertions still require both successful
initial writes, both closes, and rejected later writes/repeated closes.

No ABI, production runtime, allocator, host adapter, or upstream source changes.
The source is linked into `build/test_core` only and is absent from the Mac
application build/registration. Fresh Mac artifact execution is not required
for this test-only file correction; exact mac68k and mac-automation workflows
remain required. This does not qualify current-runtime Solaris or waive its
outstanding production gate.

## Remaining validation

- Exact Woodpecker #502 ci/mac68k/mac-automation: success.
- Independent review: clean.
- Full Linux log inspected: no incorrect-function-type or other UBSan runtime
  diagnostic remained. Known ASan context-switch warnings are a separate issue.
- Follow-up: make unexpected UBSan diagnostics fail the Linux gate, with a
  negative control; do not simply suppress the diagnostic or broaden exceptions.
