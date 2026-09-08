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
- Initial Linux Woodpecker: #44 passed for `86c6754e211a849db57938b25b5652f8d13b2283`.
- Initial mac68k Woodpecker: #44 passed for the same exact SHA.
- Exact-artifact guest acceptance: pending, coordinator owns emulator slot.

## Change and review

- Marker `Unix:cannedbsd-autorun.txt` opts in. Missing marker or missing Unix
  volume preserves interactive behavior. Autorun clears the done file before
  starting tests. Setup errors keep the app open with an error.
- After tests, the app writes the existing detailed result, redraws its actual
  window, copies its pixels into a 32-bit offscreen graphics world, records them using
  `OpenCPicture` + `CopyBits`, and
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

## Screenshot regression found in the real guest

The coordinator tested exact build #44 in a fresh guest. Autorun wrote PASS and
exited, but macOS `sips` decoded the 4017-byte PICT as an all-white 630×400 image.
That is a failed screenshot acceptance, despite a nonempty file and passing tests.
Decoding that file's actual PackBits rows showed the expected test text and ALL
PASS. The legacy monochrome PICT recording therefore contained pixels, but did
not produce usable evidence through the host image decoder. Bounded clip and
explicit foreground/background metadata alone did not repair decoding. A
diagnostic direct-color PICT made from exactly those decoded pixels displayed
correctly in `sips`; this was diagnosis, not a replacement acceptance image.

The fix snapshots the displayed window into a locked 32-bit `GWorld`, then
records that pixel map in an extended version-2 PICT at 72 dpi with an explicit
frame clip. It does not synthesize text or substitute a host-generated image.
The additional pixel buffer is roughly 1 MiB for this window and is released on
every path. Allocation, pixel locking, and QuickDraw errors withhold completion.
The prior graphics port/device is restored before disposing the buffer.

Apple recommends [`OpenCPicture` for new picture recordings](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-333.html).
Its [offscreen graphics documentation](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-302.html)
describes System 7 graphics worlds and pixel copies. The
[`CopyBits` contract](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-166.html)
permits a color port coerced through `GrafPtr` and requires the respective local
coordinate systems; the window and offscreen frame here use the same rectangle.
Black foreground and white background avoid recoloring the source pixels.

The deterministic orchestration test still verifies that a capture failure
prevents completion and exit. It cannot prove Toolbox pixel capture. Acceptance
of this correction requires decoding and visually inspecting the newly written
PICT from the corrected exact Woodpecker artifact; that remains pending.

Correction checks before push: `tests/test_mac_autorun.sh` passed; pinned
Retro68 build and single-CODE resource check passed; complete Linux
`make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed on biggie. Exact correction
Woodpecker checks and guest screenshot inspection remain pending.

## Coordinator guest verification

Exact `62bbc1fb42362b6dcd9bfd9dd626dfdd885a128d`, Woodpecker #60 all green,
archive SHA256 `a7faa48a1e50f914f62e3b39779edf02581a6518b0fe7d277f9c86b0c9362a18`:

- `run-25j70ghw`: fresh ALL PASS, PASS completion, and 37374-byte PICT that
  sips decoded with visible test text; application exited automatically.
  Normal Finder shutdown was verified. The first automation attempt stalled
  on iCloud hydration; it is not a speed measurement or uninterrupted run.
- `run-wi9z5qw7`: screenshot path was a directory before boot. Tests wrote a
  fresh result, completion stayed empty, and the application displayed
  "Autorun evidence failed; completion withheld." and remained open.
- `run-eueh8lpk`: completion path was a directory before boot. The application
  displayed setup failure; result and screenshot stayed empty. It remained
  open until the image-matched go-away action was explicitly invoked.
- Both injected failures were closed normally, then Finder Shut Down was
  selected; slot release verified the emulator and mounted files were gone.

These are observed guest failure cases, separate from mock callback tests.
