# CI-SANITIZER-02 — make undefined behavior fail the Linux gate

Base: `897efc71a3dce8d30b43131959af4d38c4b95ab9` (includes TEST-ENTRY-01).
Scope: sanitizer build policy and its independent executable control; no
production source, Mac CMake flags, Solaris flags, public ABI or guest changes.

The existing UBSan configuration recovers after diagnostics, allowing a green
workflow despite undefined behavior. A volatile signed-overflow control that
explicitly returns zero after the operation reproduces that condition. Unlike
a control returning the overflowed value, its status does not accidentally
make the old configuration appear correct.

Observed red on host Clang: with `-fsanitize=address,undefined`, the new control
prints a signed-integer-overflow UBSan diagnostic and exits zero; policy test
fails with 'undefined behavior was reported but did not fail the process'.
Adding `-fno-sanitize-recover=undefined` to the flags shared by sanitizer compile
and link makes `make SANITIZE_CC=clang check-sanitizer-policy` pass. The same
binary's nonoverflowing path succeeds without diagnostics. The negative path
must report the actual overflow, exit nonzero, and never reach its success text.
Explicit `UBSAN_OPTIONS=halt_on_error=0` proves the compile policy remains fatal
even if the environment requests recovery.

`sanitize` requires this check before compiling the full suite, so ordinary
`ci` and the build-mode isolation test both use it. Address sanitizer and all
existing tests remain enabled. Existing ASan context-switch/no-return warnings
are not suppressed and are not equivalent to sanitizer errors.

Exact commit `03176ef` passed Woodpecker #520 ci/mac68k/mac-automation and
independent review, then merged. Full CI log inspection found three core PASS
markers and zero UBSan runtime-error / ASan ERROR markers. Mac guest execution is not required for this build-only
Linux policy; no runtime portability or current Solaris qualification is claimed.
