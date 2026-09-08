# MAC-01: repeatable Basilisk II artifact acceptance

- Status: host implementation and live protocol validated; latest Woodpecker checks pending
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
- Native follow-up red: the placeholder test failed with FileNotFoundError,
  establishing that staging did not precreate the result needed for native extfs.
  It now creates an empty file before the staging timestamp; tests prove this
  cannot pass until a fresh complete guest result replaces its contents.

## Green

- Focused command: `python3 tests/test_mac_guest.py` — ten tests passed.
- Full command: `make LDLIBS=-lucontext SANITIZE_CC=clang ci` in Alpine 3.22,
  with the documented build dependencies — passed (optimized, ASan/UBSan,
  build-mode regression, analyzer, architecture, and publication checks).
- Linux Woodpecker: pending feature push; coordinator must inspect exact commit.
- mac68k Woodpecker: pending feature push; coordinator must inspect exact commit.
- Guest acceptance: coordinator tested this host protocol against the exact
  PENV-04 Woodpecker artifact `ce34f2cb4bd94e2bd132b7fbeefdb5b4b98ecffb`.
  Archive SHA-256: `31e0fdc27ddbfb49e4342cf89ce5a669824a7c452b6a96c535f28b10dd8bd044`.
  Fresh result SHA-256: `e1daee5c7eac553b701f74c2d8130d0d0923043c26882fcce6ab67bcc3372739`.
  Receipt reported `ALL PASS`; coordinator also observed it on screen and the
  successful Unix evidence message. An absent file failed native creation;
  an empty pre-existing file was successfully rewritten. This is evidence for
  the staging/evidence protocol using that named artifact, not a claim that
  the subsequently built MAC-01 artifact was executed. No guest C source changes.

## Change and review

- Implementation: `guest.py stage/check/status/release` verifies a full expected
  commit and archive checksum, extracts only the regular HFS disk into a new run,
  creates a new shared directory and empty result placeholder, optionally copies a clean boot seed, and emits
  a receipt only after fresh complete `ALL PASS` evidence. Optional native
  template/ROM arguments generate a config with only this run's disks and export. The slot survives
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
