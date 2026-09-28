# MAC-FILEPROBE-01: allow the documented zero wall-clock adapter

Base: `b98b708ee94dd59362ba904daab6428942debdfb`.
Branch: work/MAC-FILEPROBE-01, isolated sibling worktree.

## Observed Mac failure

Exact Woodpecker #491 passed ci/mac68k/mac-automation. Its archive SHA256 was
`178ea1a4397e6b8cdb015229b1d9530eb46dc900767fffca19317cb80f01074f`.
Fresh autorun `run-bheud83l` produced 61 PASS records, then `FAIL fileprobe`
and `FAILED`; no acceptance receipt was created. The coordinator inspected
its transcript and screenshot. The app returned to Finder; a separate
Hammerspoon recovery using the checked-in matcher and held Special-menu drag
observed normal guest exit in 6.62 seconds. guest.py release independently
confirmed disks closed and released the slot. All failed-run evidence remains.

The hs command initially timed out while the asynchronous runner continued;
that IPC result was not the guest result. The later run.json reported the
actual guest test failure after 35.09 seconds. No emulator was terminated.

## Hypothesis and genuine red

The classic Mac host explicitly returns zero from wall_clock_millis because
its local civil clock has no reliable UTC offset. FS-STAT-01 changed the
portable cp-stub-probe to require nonzero root timestamps, which rejects this
existing adapter contract even though zero metadata is translated correctly.

Before changing the ordinary probe, tests/test_file.c was extended to run the
existing complete fileprobe with an injected zero wall clock. In a disposable
Linux source export, `make LDLIBS=-lucontext build/test_core` then
`./build/test_core` exited 1 with:

    fileprobe mode11 failure7 status57
    FAIL: file ownership ordinary/lifecycle status

The pinned container image was
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
with /bin/sh explicitly selected as entrypoint. An earlier entrypoint setup
error is excluded from red evidence.

## Change and verification

No runtime, adapter, ABI, shared libc symbol or pinned source changes.
The portable smoke probe keeps stat success but no longer assumes UTC exists.
Exact timestamp checks move into ordinary-source modes exercised through the
real libc boundary with injected host times 0 and 1234567 milliseconds.
They assert all three seconds fields, all three nanosecond fields, and the
public timestamp macros against known values. The full fileprobe also runs
under both clocks; monotonic scheduling remains on its original host clock.
This replaces a host-dependent nonzero predicate with exact zero/nonzero
translation coverage, rather than manufacturing a date on the Mac.

The focused core binary passed after the change, including file ownership and
all existing core tests. Publication hygiene and diff checks passed. Full
exact-commit Woodpecker and fresh Mac acceptance are pending at this commit;
Linux success alone is not a guest pass. Native Solaris qualification remains
outstanding for the combined current runtime and these shared probe changes.
Independent review is requested before integration. Later acceptance evidence
must identify its exact candidate and archive, not reuse the failed run above.
