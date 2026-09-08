# MAC-08: image-matched manual guest window close

- Base: `4f80e83`, freshly fetched `origin/main`.
- Branch/worktree: `work/MAC-08`, sibling `bsdinacan-MAC-08`.
- Assigned boundary: close the cannedBSD application after a coordinator has
  inspected a MAC-03 failure; never terminate or shut down the emulator.
- Hypothesis: an app-specific title-bar crop plus a calibrated hotspot identifies
  the go-away box after window movement while rejecting ambiguous candidates.

## Red and green

`python tests/test_mac_image_match.py
MatchTests.test_cannedbsd_close_crop_tracks_hotspot_after_translation` initially
failed: matching the crop returned its center `(470.5,89)` rather than the close
hotspot `(274,89)`. The matcher now recognizes this crop's hotspot. The test
passes after translation and rejects a duplicate title-bar target. All ten
matcher tests pass; Lua syntax and the existing startup tests pass. These are
image-processing checks, not mock claims about actual UI delivery.

Offline matching against the coordinator's two saved guest-only failure
screens found the title crop with score 1.0 (rounded), second peak 0.498 and
screen position equal to the observed close box: window frame plus `(43,63)`.
Only the 842×36 title crop is stored in the repository. No full private screen,
ROM, disk, or guest result was committed.

## Implementation and lifecycle

The separate `close-window.lua` takes local config plus an explicit inspected
PID, checks active-slot preferences in that process's command, requires the main
runner inactive and the guest frontmost, waits for matcher readiness, saves a
fresh before screenshot, and posts mouseMoved/down/up only after an unambiguous
match and unchanged window geometry. It saves an after screenshot and action
record in a new directory. It does not touch evidence exports, release the slot,
launch/shut down/terminate Basilisk, or alter the default happy path.

The action deliberately records click delivery rather than asserting successful
closure: the coordinator must inspect after.png for Finder or a dialog before
any further operation. A stalled helper is bounded by a timeout; only that owned
helper may be terminated. Local storage remains required to avoid cloud-file
hydration blocking Hammerspoon itself.

## Remaining gates

- Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: pass in Alpine 3.22.
- Exact feature commit Woodpecker workflows: pending push.
- Coordinator live close verification: pass on the inspected MAC-03 setup
  failure. Match score 0.99935 at `(251,101)`; the retained after.png showed Finder
  and no cannedBSD window. Evidence is in the local-cache staged run
  `run-eueh8lpk/recovery-close-20260907-182732`. The coordinator subsequently used
  ordinary Special-menu shutdown as a separate action. No worker UI actions.
  This verifies this host action against that failure; it is not acceptance of
  the new feature commit's guest artifact.
- Exact feature artifact guest acceptance and main merge remain coordinator-owned.
