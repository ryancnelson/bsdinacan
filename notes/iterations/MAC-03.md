# MAC-03: guest autorun with durable result, screenshot, and completion

- Status: implementation complete; validation in progress
- Base SHA: `9a3e4db6314e7115e7f1ac37ac68faa800a9c9da` (explicit MAC-02 dependency)
- Branch: `work/MAC-03`
- Worktree: sibling `bsdinacan-MAC-03`
- Hypothesis: an explicit autorun marker can turn the existing guest acceptance
  suite into an unattended run, while withholding completion and keeping the
  app open whenever evidence cannot be written.

## Red

- Command: `tests/test_mac_autorun.sh`
- Against the prior result-only behavior extracted into the orchestration
  helper, the test failed `step == 3`, exit 134: screenshot and completion
  publication never happened.

## Green

- Focused command: `tests/test_mac_autorun.sh` — passed. The deterministic test
  verifies result → screenshot → completion ordering, honest PASS and FAIL
  tokens, and early failure at each of the three operations.
- Full Linux gate: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` — passed in the
  existing Linux Woodpecker agent image on biggie (optimized, sanitizer,
  build-mode, architecture, publication, and static-analyzer gates).
- Linux Woodpecker: pending.
- mac68k Woodpecker: pending.
- Exact-artifact guest acceptance: pending, coordinator owns emulator slot.

## Change and review

- Marker `Unix:cannedbsd-autorun.txt` opts in. Missing marker or missing Unix
  volume preserves interactive behavior. Autorun clears the done file before
  starting tests. Setup errors keep the app open with an error.
- After tests, the app writes the existing detailed result, redraws its actual
  window, records the window's pixels using `OpenPicture` + `CopyBits`, and
  writes a PICT file with the standard 512-byte header. Evidence operations
  check exact writes, close, and volume flush before success.
- Completion is exactly `PASS` or `FAIL` followed by a newline, reflecting test
  outcome. The app exits normally only after all evidence operations succeed.
  On any evidence failure it displays an error and remains open. A failed done
  write attempts to clear the token again.
- The controller must require both the token and disappearance of the CannedBSD
  process before asking Finder to shut down. This prevents a visible token from
  a failed final flush from becoming permission to shut down.
- All evidence and screenshot Toolbox work runs on the original application
  stack. There is no guest shutdown call in CannedBSD; the external host
  controller owns that final step. Public ABI and kernel behavior are unchanged.

## Guest preparation and primary format evidence

Before boot, precreate empty writable `cannedbsd-result.txt`,
`cannedbsd-screen.pict`, and `cannedbsd-done.txt` in the shared host directory.
Create `cannedbsd-autorun.txt` only for unattended runs. Native Basilisk II's
shared-folder backend can open preexisting files but does not reliably create
absent ones. Each host run must use fresh/emptied outputs to avoid stale evidence.

Apple's [PICT document format guidance](https://developer.apple.com/documentation/appkit/nspictimagerep/pictrepresentation)
requires a 512-byte header before picture data. Apple's
[QuickDraw picture opcode documentation](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-461.html)
defines the bitmap copy records used here. The screenshot records actual window
pixels after a redraw, not a generated image or a recording of text commands.

Remaining acceptance work: verify screenshot pixels and dimensions, completion
ordering, app exit, clean Finder shutdown, and accurate host PASS/FAIL reporting
from the exact Woodpecker artifact. No guest execution claimed by this note.
