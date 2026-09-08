# BASENAME-01 integration

Candidate based on accepted main `a7acb54bd569ef66d93884f9808cce66561e01d6`,
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
implementation must still pass the corrected ordinary probe in full CI and Mac
acceptance with its real writable per-task buffer.

## Verification and acceptance status

- `python3 tests/test_mac_guest.py`: all 18 protocol tests passed, including the
  thirty-eight-record union and preserved existing membership assertions.
- Shell syntax checks passed for the modified boundary/behavior scripts.
- Both imported basename files retain their recorded exact upstream hashes.
- `git diff --check`: passed.
- Full Linux CI and exact Woodpecker checks are pending for this candidate.
- Fresh Basilisk II acceptance is coordinator-owned and pending. The imported
  feature note's historical "done" wording does not constitute guest acceptance
  of this combined candidate. No emulator/UI operation was performed here.
