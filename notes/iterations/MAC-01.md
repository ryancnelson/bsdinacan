# MAC-01: repeatable Basilisk II artifact acceptance

- Status: implementation complete; exact-commit Woodpecker checks pending
- Base SHA: `78a1e5b`
- Branch: `work/MAC-01`
- Hypothesis: a host runner can preserve exact CI provenance and reject stale,
  missing, failed, or incomplete guest output while reserving the shared slot.

## Red

- Command: `python3 tests/test_mac_guest.py`
- Expected failure: no exact-artifact staging/evidence runner exists.
- Observed failure: `FAIL: missing exact-artifact guest runner` (exit 1).
  The focused tests then exercise invalid checksum, wrong commit, stale output,
  failed/missing output, slot conflicts, open disks, and preservation of seeds.

## Green

- Focused command: `python3 tests/test_mac_guest.py` — eight tests passed.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in Alpine 3.22,
  with the documented build dependencies — passed (optimized, ASan/UBSan,
  build-mode regression, analyzer, architecture, and publication checks).
- Linux Woodpecker: pending feature push; coordinator must inspect exact commit.
- mac68k Woodpecker: pending feature push; coordinator must inspect exact commit.
- Guest acceptance: runner integration remains to be exercised by the coordinator
  in its serialized guest slot. This change does not modify guest application
  sources or runtime behavior. Unit tests are not guest-execution evidence.

## Change and review

- Implementation: `guest.py stage/check/status/release` verifies a full expected
  commit and archive checksum, extracts only the regular HFS disk into a new run,
  creates a new shared directory, optionally copies a clean boot seed, and emits
  a receipt only after fresh complete `ALL PASS` evidence. The slot survives
  checking until explicit release verifies disks closed with `lsof`.
- ABI, ownership, and cleanup review: no ABI/runtime changes. Artifacts, disks,
  and evidence remain in unique per-run directories; no existing disk is
  overwritten. Failure during staging drops only its own unlaunched slot.
- Documentation: platform README documents HFS transfer, shared-folder lifetime,
  verifying shutdown confirmation, freshness limits, and all commands. CI.md
  documents the focused protocol tests, now part of `make ci`.
- Remaining risk or follow-up: freshness is host mtime plus a unique export,
  not a guest nonce. The operator must mount the staged application and export
  the matching directory. Extfs files must not be moved/deleted during a live
  guest session. `lsof` only observes local processes; use one coordinator and
  state root, and do not configure a remotely hosted guest against these paths.
