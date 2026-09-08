# MAC-04: calibrated Hammerspoon guest test driver

- Status: project integration implemented; exact-commit CI and guest run pending
- Base SHA: `5c013c50846a3af56f8b023cd5a0c794ddacbecb`, explicitly assigned by the
  user/coordinator to build on MAC-01 without merging unrelated iterations.
- Branch: `work/MAC-04`
- Hypothesis: preserving calibrated mouse delivery and persistent image matching
  can automate a short guest test cycle while failing closed on ambiguous UI.

## Red

- Initial command: `python tests/test_mac_image_match.py`
- Observed failure: `FAIL: missing persistent image matcher` (exit 1).
- The earlier prospective matcher was superseded by the coordinator's working
  Hammerspoon/OpenCV driver. Its raw normalized-correlation algorithm had no
  rejection for flat templates (OpenCV can return misleading perfect matches).
  The project matcher explicitly rejects flat templates; tests also exercise
  duplicate targets, low-confidence images, missing/oversized input, malformed
  frames, cache replacement, Retina normalization, and persistent JSON errors.

## Green

- Focused command: virtual-environment Python `tests/test_mac_image_match.py` —
  eight tests passed on arm64 macOS with the pinned requirements.
- CI-container command: Python 3.14-slim with pinned requirements and
  `python tests/test_mac_image_match.py` — eight tests passed.
- Syntax command: `luac -p platform/mac68k/automation/run.lua` — passed.
- Full gate: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in documented Alpine
  environment — passed, including optimized, sanitizer, build-mode, analyzer,
  architecture, and publication checks.
- Woodpecker Linux `ci`, `mac68k`, and `mac-automation`: pending feature push.
- Guest evidence: the coordinator's source driver completed one observed cycle
  in 12.6956949234 seconds on 2026-09-07: cold boot, applet launch, eight PASS and
  ALL PASS, guest screenshot, exit, menu selection, and emulator disappearance.
  That local trial preceded this staged-artifact integration and did not certify
  a CI commit. The project version requires its own exact-artifact trial; do not
  present the timing as evidence that this feature commit ran in the guest.

## Change and review

- Implementation: preserve explicit mouseMoved/down/up and held menu drag;
  normalize screenshots to 2x template scale and reject ambiguous second peaks.
  Read machine paths from external JSON config. Derive disks and export from
  MAC-01's slot, refuse existing/reused/mismatched guests, pin the launched PID,
  require `guest.py check`, and release only after process/disk closure.
- Runtime/ABI: no guest C code changes, no portable-core or libc modifications.
- Evidence: only guest-window captures go in the local staged run; the repository
  contains small calibrated template crops, no full host screenshots or ROMs.
- CI: separate Docker workflow installs pinned host matching packages and runs
  synthetic tests; it does not drive a desktop or alter the existing Linux gate.
- Documentation: automation README, external config example, pinned requirements,
  and a link from the platform README. Setup needs Hammerspoon permissions and a
  boot seed with the compiled `zzz-run-tests` applet targeting the staged volume.
- Bounds: one timed source trial, not a reliability series; layout/font/menu
  changes need explicit recalibration. Unknown dialogs time out for inspection.
  Tests do not simulate Hammerspoon event delivery or prove guest UI behavior.


## Coordinator runtime validation

The committed project driver `13c0061` completed a staged MAC-03 artifact run
in **15.4431 seconds**, including PID/preferences verification, exact artifact
acceptance, screenshot, normal guest shutdown, and slot release. Artifact:
`86c6754e211a849db57938b25b5652f8d13b2283`; archive SHA-256:
`386030c623aa2df9f1e639385a8f48c73a5e4e2c6e8e62d82e04228b8548e8bc`.
The retained MAC-01 receipt is `run-evhesz6f/acceptance.json`; its automation
subdirectory holds the screenshot and timestamped action record. Default
interactive mode was used; optional guest-owned PICT mode was not exercised.
This validates the driver against that artifact, not the merged integration
artifact, which must receive its own CI build and guest run.
