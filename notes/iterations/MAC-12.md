# MAC-12: optional window and cursor calibration

Base `00bf923`. The coordinator observed focus interruptions and a pointer
inside the trash template on failed runs, then successful manually calibrated
resumes. These observations motivated a bounded optional pre-match configuration;
no claim is made that it can prevent every external focus change.

Implemented pure finite-coordinate/bounds planning, pre-launch configuration
validation, and post-PID-check window placement with exact frame/focus verification.
The cursor moves without clicking to a configured interior neutral point. Existing
lock, focus, staged evidence, matcher and shutdown safeguards remain intact.

Validation: geometry tests cover disabled, valid, negative-screen origins,
nonfinite values, missing fields, edge/outside cursor fractions and off-screen
window rejection. Existing startup/preflight and autorun sequencing tests pass.
The first new test invocation failed because the helper did not exist; that is
setup failure, not claimed behavioral TDD evidence. Exact CI and guest acceptance
remain pending. No fresh timing claim yet.

## Coordinator acceptance, 2026-09-08

Combined commit `894b75316eea9965438aa45b12e5875c53f5136a` passed all
three exact Woodpecker #230 workflows and is merged to main. Archive SHA256
`ca21a340b41340796078a4236ac81fa3123cd31fa93de85d6c258932a31dcd1c`.
Fresh guest `run-mraq4k0o` produced all 42 expected PASS records and ALL PASS.
The decoded screenshot was visually inspected; the receipt confirms app closure
and closed guest disks, and the slot was released after normal shutdown.
Configured calibration applied before matching. The complete cold automated
cycle took 16.09 seconds; no manual mouse correction or resume was needed.
Actual-driver mock tests also validate identity/calibration/matching order,
disabled configuration, and rejection paths that retain the guest and slot.
Regression negative controls removing calibration, moving it before identity,
or omitting frame verification each fail; these are not claimed test-first red.
