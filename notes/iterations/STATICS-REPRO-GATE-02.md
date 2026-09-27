# STATICS-REPRO-GATE-02 — reject false-success regression results

Base: `b2d003b6820d6c19b47885986e8271f5b2d049fe`, explicitly assigned.
Branch: `work/STATICS-REPRO-GATE-02`.

## Hypothesis and genuine red

The inherited statics regression script checked only that selected diagnostic
substrings were absent. It printed, but did not require, command status and
never required positive fixture output.

Before editing the script, two new tests in
`python3 -B tests/test_statics_repro_gate.py` executed the actual script with
`PROGRAM_PATH` set to the host's `false` and `true` executables. Both assertions
failed: the script returned zero and reported `5 fixed, 0 still broken` for
both a failed command and a successful command producing no output. The test
runner returned 1 with two failures. These were actual pre-fix failures, not
mutated expected output or a deliberately reverted implementation.

## Bounded fix

Every case requires process status zero and byte-exact stdout and stderr files.
Status markers observe each command under test before a following command could
hide its failure. The fixtures require both cat outputs, both default-column
ls listings, the expected failed-then-successful rm statuses and first error,
verbose-then-quiet cp plus destination contents, and both mv statuses plus final
contents. Comparison uses `cmp`, preserving trailing newlines and embedded NULs.

The existing known three-line ASan context-switch warning is collected separately
and accepted only in its exact established shape; additional sanitizer diagnostics
fail even if the target returned zero. Unique temporary directories and exit traps
keep concurrent checks separate and remove capture files on success/failure.

Five host test methods include thirty per-case fault subtests. They verify valid
fixture acceptance exactly once per case, rejection of false/true, process failures,
missing output, missing final newline, extra NUL bytes, unexpected stderr, nonzero
inner statuses, and rejection of a sanitizer error appended to the allowed warning.
The host controls run from `make ci`; the actual five binary sessions retain their
existing `make test` registration and therefore run under all existing build modes.

## Observed validation

- Before fix: two host controls failed as described above.
- After fix: `python3 -B tests/test_statics_repro_gate.py` passed all five methods,
  including thirty per-case subtests and sanitizer-report controls.
- A disposable Linux export built the actual base runtime with the repaired
  script. All five real sessions passed with complete matching fixture bytes.
- `sh -n tests/test_statics_repro.sh`, `git diff --check`, and
  `make check-publication` passed.
- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed in the pinned
  Linux build image
  `sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
  using an isolated source export. This includes ordinary/sanitizer runs,
  build-mode isolation, source boundaries, publication and static analysis.
- An initial full-gate attempt directly on the glibc host stopped in the
  existing dirname sanitizer check on its additional makecontext warning.
  No test allowance or runtime code was changed to bypass that failure; the
  passing full gate used the CI image and its libucontext configuration.
- Exact Woodpecker results are reported in the handoff; no earlier candidate
  or other branch's status substitutes for them.

## Limits and ownership

Only the regression script, its host control, CI registration and this note change.
No runtime, ABI, shared symbol, static-reset implementation or imported source is
modified. No Mac or Solaris guest execution is claimed. This host-test correction
introduces no guest behavior change; the inherited runtime branch still requires
its own repaired implementation and exact guest qualification before integration.

Passing these five cases does not prove complete command-state isolation. The
cat case keeps the same 2048-byte buffer request; it does not cover the separately
reported resize, kernel-recreation or interleaving defects. The cp case exercises
verbose-option leakage, not every option. The mv case tests same-mount renames;
its cross-mount `fastcopy` allocation path remains unexercised. The summary now
reports passing cases rather than claiming five proven fixes. Do not merge this
entire prerequisite branch into main on the strength of this detector repair.
