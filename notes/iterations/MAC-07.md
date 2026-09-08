# MAC-07: run ordinary-source libc probes in the Mac guest

- Status: implemented; exact artifact validation in progress
- Base: `4f80e830a2d05376692fd6a5475094873f500522` (explicit coordinator override)
- Branch: `work/MAC-07`
- Worktree: sibling `bsdinacan-MAC-07`
- Hypothesis: running the same ordinary-source memory, getopt, and truncate
  probes on Linux and native 68K will expose target-specific failures that the
  previous seven shell commands and context check could not detect.

## Red

`python3 tests/test_mac_guest.py GuestTests.test_old_eight_pass_transcript_cannot_satisfy_expanded_suite`
failed with `AssertionError: Rejection not raised`. The old check accepted an
old eight-pass transcript without any memory, getopt, or truncate evidence.

## Implementation

`acceptance_cases.def` is the single command/output/status table included by
both the Mac application and the Linux test executable. The Mac registers three
acceptance-only programs using the existing libc module boundary. Their ordinary
C translation units receive the private libc headers, just as `wc` does.

- The new memory probe checks `strchr` first match, terminator, miss, and integer
  character conversion; both `memmove` overlap directions; unsigned `memcmp`
  ordering and zero length; and `memcpy` return values and boundary canaries.
- The existing getopt source/module are unchanged. Role E uses stdout as its
  report descriptor and stdin as its unused synchronization descriptor. It
  verifies `?`/`optopt == 'z'` with diagnostics suppressed; expected output is
  exactly `1` and exit status zero. This is basic target execution coverage,
  not the existing Linux suite's stronger interleaved parser-state proof.
- The existing truncate source/module are unchanged. They verify shrinking,
  zero-filled extension, readonly descriptor rejection, and negative size
  rejection using ordinary libc spellings.

There are now eleven named PASS records: one context check and ten command
cases. `guest.py` derives the exact ordered transcript from the same table and
stages `expected-result.txt`; the Hammerspoon driver reads that file instead of
using a hardcoded count. Host verification rejects missing, duplicated,
reordered, failed, or old probe records. Freshness checks, artifact checksum
binding, receipt creation, and slot ownership retain their existing behavior.
Old staged runs must be restaged for the new driver.

## Validation

- Expanded host protocol tests: 13 passed, including stale/empty/missing/failed
  result handling, checksum/commit/receipt binding, and duplicate-probe rejection.
- Lua matcher startup gate: passed without desktop actions.
- Linux focused `build/test_core --mac-acceptance`: passed all ten commands using
  the same ordinary-source probe code as the Mac application.
- Pinned Retro68 compile: passed. Exact Woodpecker artifact remains authoritative.
- Complete Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed on biggie
  in the existing Woodpecker agent image.
- Both exact Woodpecker workflows: pending.
- Exact-artifact Basilisk II acceptance: pending; coordinator owns the guest.

No portable runtime or libc implementation changes, no upstream source edits,
and no emulator control are part of this task. These probes run before the
interactive shell; failures still withhold ALL PASS and leave the window open.
