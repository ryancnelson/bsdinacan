# MAC-MILESTONE-TEST-01 implementation

- Assigned base: `7f96b10a30a26267ed6fa33dafdd9b25af70ab89`, the sprint
  integration candidate, whose guest qualification remains pending.
- Consumed coordinator build correction `461504d` as `0b4b06e` before testing:
  retain the signal probe object in the integrated Linux test link.
- Branch: `work/MAC-MILESTONE-TEST-01-implementation`, in a new sibling
  worktree. The original proposal and its documentation-only worktree were
  preserved. Building atop this candidate was explicitly authorized; it does
  not mean the dependencies have merged or passed their remaining gates.
- Hypothesis: running ten independent file-operation fixtures through the
  shared Mac/Linux table detects missing copy contents, retained move sources,
  and retained deleted files, while preserving the existing acceptance suite.

## Cases and expected transcript

Appended the proposal's ten create/list/copy/move/inspect/delete cases to
`platform/mac68k/acceptance_cases.def`. The original 64 table rows remain
byte-for-byte unchanged; with five startup probes, the suite grows from 69
PASS records to 79. Every table case still creates and destroys its own kernel,
so no case depends on another case's filesystem state.

The proposal's `b\na\n` listing was obsolete: integrated upstream ls sorts
names and uses columns on a console. The two multi-file observation cases use
`ls -1 d` and assert `a\nb\n`. They test both the populated directory and the
empty directory after deletion. All other proposal commands and expectations
are retained, including the missing move-source diagnostic and status 1.

`guest.py` already generates the expected transcript directly from the shared
table. It now produces 2,034 bytes (2,035 including the terminating C NUL),
322 bytes more than the previous 1,712-byte transcript. The proposal's 313-byte
addition was not reused. A new C99 constant expression in
`acceptance_transcript.h` derives the guest result buffer size from the header,
five startup records, every shared command, and the longest footer. It compiles
to 2,035 bytes on Linux. FAIL and PASS are equal-length prefixes; failure stops
the loop and uses the shorter FAILED footer, so the complete success result
bounds every current failure result. This avoids relying on a shrinking margin
in the old 2,048-byte buffer. No runtime allocation or pointer-width assumption
is introduced. The capacity header is a dependency of its Linux compile check.

The protocol test requires 79 records and rejects a reconstructed old 69-record
transcript even when that old transcript ends in ALL PASS.

## Observed verification and failure controls

Pinned Linux container `tribblix-woodpecker-agent:3.18.0`, image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
network disabled, disposable source directory:

- Baseline after the build correction: build/test_core --mac-acceptance
  passed before adding the cases.
- Expanded table: `make LDLIBS=-lucontext -j4 build/test_core
  check-acceptance-output`, then `./build/test_core --mac-acceptance` passed,
  exit 0. The capacity compile check reported 2,035 bytes including NUL.
- `python3 tests/test_mac_guest.py`: 19 tests passed.
- No-op deletion control: only in the disposable copy, replaced `rm d/a d/b`
  in the final lifecycle case with `true`, leaving expected bytes unchanged.
  The shared runner exited 1: expected `a\nb\n`, actual `a\nb\na\nb\n`,
  both status 0. Thus surviving files are observed before kernel teardown.
- Move-without-unlink control: only in that disposable copy, replaced `mv f m`
  in the source-removal case with `cp f m`. The shared runner exited 1:
  expected status 1 and `ls: f: no such file or directory\n`; actual status 0
  and `f\n`. These are explicit command-substitution failure controls after
  the implementation, not test-first runtime defects or a claim that missing
  test registration is behavioral red evidence.
- Restored the unmodified table after controls and reran the shared suite.
- Full local CI and exact pushed-commit ci/mac68k/mac-automation results are
  reported in the handoff; local results do not substitute for those workflows.

## Scope and outstanding qualification

Changes are confined to the shared case table, transcript sizing, its build
check, protocol tests, and this note. No imported source, public runtime ABI,
libc symbol, command implementation, or host adapter was modified.

Fresh Mac execution of the exact artifact and native Solaris qualification are
still coordinator gates. This worker did not launch a guest, drive Hammerspoon,
merge to main, or claim the expanded Mac milestone has run in System 7.
