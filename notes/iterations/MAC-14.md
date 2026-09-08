# MAC-14: byte-exact text acceptance

## Hypothesis and scope

The native and Mac harnesses compared captured console output with strcmp.
A leading NUL followed by nonempty bytes therefore matched an empty expected
string. A NUL after an expected prefix similarly hid extra output. The Mac
capture also silently discarded bytes beyond its fixed capacity while reporting
successful writes, so a matching truncated prefix could pass.

## Red and green

First extracted the existing strcmp comparison into the shared acceptance helper
without changing its behavior. Added a focused test with four captured bytes
NUL, 0xff, A, B and an empty expected string. Compiling and executing the test
with the local C compiler and full project warnings failed with status 1:
`FAIL: hidden binary bytes accepted as empty output`.
This is an execution test of the extracted existing comparison, not a claimed
pre-change Mac guest failure.

Changed the shared helper to require exact captured byte length and content,
and reject an explicit truncation flag. Native combined and per-stream case
comparisons use their existing recorded byte lengths. Mac capture records any
discarded bytes, resets that flag for each case, and stops capture before
checking both output and status. Capture sizes and the expected transcript are
unchanged. Binary command probes still need internal byte-aware verification;
this text harness does not turn arbitrary binary output into text.

`make check-acceptance-output` passes with full warnings. Cases cover hidden
binary bytes, a NUL plus trailing suffix, a truncated matching prefix, short
and mismatched text, exact text, empty output and an unterminated exact buffer.
All 18 guest protocol tests pass. The focused executable is required by `make ci`.

## Remaining acceptance

Exact Woodpecker ci, mac68k and mac-automation, independent review and a fresh
complete 62-record Mac guest run are required before merge. No guest result is
claimed by this initial candidate.
