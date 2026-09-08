# BASENAME-01 integration

Integration based on accepted main `a7acb54bd569ef66d93884f9808cce66561e01d6`,
merging Claude's reviewed feature `234f395`. Worktree and branch:
`bsdinacan-BASENAME-01-integration`, `work/BASENAME-01-integration`.

The merge retains VFS-02 executable-node lifetime/error behavior, PROGNAME-01
startup identity, all dirent/dirname/locale/terminal operations and tests. The
basename accessor follows `closedir`; the mandatory libc API prefix is unchanged.
Native and Mac lists link and register both previously accepted probes and the
new ordinary basename probe. Shared acceptance is thirty-eight PASS records.
The full fixture remains at 64 registrations (12 base commands, 52 test probes),
with basename-specific probes in their scoped fixture. Existing registration
assertions remain intact.

The incoming basename symbol checks now use the existing draining `matches`
helper. This preserves the source-boundary failure semantics and avoids bringing
back the independently reproduced nm/ar SIGPIPE race fixed in main.

## Writable-result regression check

The feature's direct write/read/restore assertion could disappear under clang
`-O2`. The shared ordinary probe now accesses the returned byte through a
`volatile char *`, making the actual mutation and read observable.

A disposable host harness compiled the old exact `234f395` probe and the corrected
probe with `clang -O2 -std=c99`. Its fake basename returns a read-only `"bar"`
for `"/foo/bar"`; each check runs in a child with core dumps disabled. The old
probe wrongly returned success (child exit 0). The corrected probe attempted
the required write and the child received signal 10, so the harness correctly
rejected the read-only result. This is a regression demonstration, not a claim
that the initial implementation followed test-first development. The production
implementation subsequently passed the corrected ordinary probe in full CI and
Mac acceptance with its real writable per-task buffer.

## Verification and acceptance status

- `python3 tests/test_mac_guest.py`: all 18 protocol tests passed, including the
  thirty-eight-record union and preserved existing membership assertions.
- Shell syntax checks passed for the modified boundary/behavior scripts.
- Both imported basename files retain their recorded exact upstream hashes.
- `git diff --check`: passed.
- Full Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci` on the exact archived
  commit passed, including normal, sanitizer, build-mode and analyzer checks.
- Exact Woodpecker #219 passed ci, mac68k and mac-automation on merged runtime
  `5906b9c69551c6b7e3fd8fa14e777c204b5e1610`.

## Coordinator-owned Mac acceptance, 2026-09-08

Exact archive SHA256:
`51456b9d06aa1a7995abc0fbb03cf836f190e25d76ac43a87560fc944459f04b`.
Guest run `run-rt_33o6i` produced all 38 fresh PASS records. The coordinator
reviewed the screenshot and acceptance receipt, verified app closure and normal
guest shutdown with mounted disks closed, and released the serialized slot.

The first focus interruption occurred before output evidence was written. The
existing staged run was then resumed successfully in 12.92 seconds. This measures
the successful resumed portion, not a cold boot or an uninterrupted first attempt.
The imported worker note remains the historical feature report; this integration
record supplies the completed exact combined runtime and guest evidence.
