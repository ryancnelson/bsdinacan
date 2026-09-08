# MAC-09: supported guest autorun staging and host driver

- Status: merged at `ca0cd19`; exact integration CI and guest acceptance passed
- Base: `c93e2ba73f62484cced5a8cd31651e56ed4e6187` (explicit integration base)
- Branch: `work/MAC-09`; worktree: sibling `bsdinacan-MAC-09`
- Hypothesis: the integrated guest's durable result/PICT/done protocol can drive
  normal shutdown without matching an interactive shell or typing exit, while
  withholding acceptance on partial evidence or incomplete shutdown.

## Red

The new `stage(..., autorun=True)` test failed because the staging API did not
support autorun. A second baseline check, using the exact base's `guest.py`,
set `autorun: true` in a staged manifest and supplied only a fresh full result.
It failed the required rejection with `AssertionError: baseline accepted
 autorun receipt without done, screenshot, or closure`: the old check ignored
this mode and published acceptance without its additional evidence.

## Change

- `stage --autorun` records the mode and creates all four shared files before
  its timestamp. Default interactive staging is unchanged.
- `inspect` validates evidence without a receipt. Autorun additionally requires
  fresh regular done/PICT files, exact PASS completion, extended PICT header and
  bounded dimensions, successful macOS sips decode, matching pixel dimensions,
  and enough dark/light pixels to reject blank images. The decoded PNG is kept.
- The driver checks the full expected transcript and completion, inspects the
  screenshot, observes Finder and the CannedBSD title's absence, requests normal
  shutdown, and waits for emulator exit. Only then does `check --app-closed`
  revalidate evidence and closed disk handles, publish the hash-bound receipt,
  and allow slot release. Autorun never types shell exit.
- Failed checks stop the controller, preserve evidence/failure screenshot, and
  do not force emulator termination. Empty completion or an app that remains
  open waits until the existing bounded timeout. A callback sequence test proves
  that receipt and slot release cannot precede shutdown completion.
- The actual title/close-square template is reused from coordinator-verified
  MAC-08. There are no guest C/Toolbox changes and no UI actions by this worker.

## Validation

- Host protocol suite: 18 tests passed, including stale/empty/missing inputs,
  bad completion, decoder failure, closure/closed-disk gates, and default mode.
- Lua startup and autorun sequencing tests: passed without desktop operations.
- Existing image matcher suite: 9 tests passed. Decoded-pixel suite: 2 passed.
- Real local sips/OpenCV diagnostic: a direct-color PICT containing previously
  captured guest pixels decoded successfully; a legacy bitmap diagnostic that
  rendered white was rejected. These diagnostic containers are not claimed as
  a fresh guest test of this branch.
- Complete Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed in
  the existing Linux Woodpecker agent image on biggie.
- Exact Woodpecker workflows and artifact: pending.
- Exact-artifact autorun through normal shutdown: pending; coordinator owns UI.

The protocol tests inject only the image decoder/disk-owner result for isolated
file-protocol cases. They do not claim to simulate Toolbox capture or durability.
The actual guest run remains required to validate readable PICT content and the
observed Finder/app/shutdown transitions.

## Exact feature acceptance

Feature `9febfe1f7be9bfdb5d8dc821cc1e5761ddccb6f9` passed all three
Woodpecker #81 workflows. Archive SHA256:
`b008e0d07f2b5427bd6fb01c8f843e0a744d2fb186286a03d3c27eb7371f407d`.
Fresh autorun `run-9hmu7vz8` completed fifteen named checks, wrote PASS
completion and a 39,784-byte PICT, and produced a decoded screenshot visually
verified by the coordinator. Finder/app closure preceded normal shutdown;
receipt publication followed shutdown, then slot release. Full cold cycle:
14.08 seconds. No shell matching or exit typing was used.

## Integrated acceptance

Integration `ca0cd199e82c17463114ab01ac97a42a89ea6c08` passed all three
Woodpecker #87 workflows and was fast-forwarded to main. Exact archive SHA256:
`507044bd961258f6f754be6eb161535e5698b0a66d8407116f27b0596c59aa6e`.
Run `run-o95ac49k` produced the full fresh fifteen-check transcript and a decoded
PICT visually inspected by the coordinator. App closure and normal shutdown
preceded receipt publication and slot release. Full cold cycle: 13.76 seconds.
