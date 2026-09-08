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
