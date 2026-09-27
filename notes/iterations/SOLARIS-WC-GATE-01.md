# SOLARIS-WC-GATE-01: match the accepted NetBSD wc output

Base: freshly fetched `6cd71fefc50167e1161c590a2f8f515bfb861e1a`.
Branch: `work/SOLARIS-WC-GATE-01`, in an isolated sibling worktree.
The base's exact Woodpecker #480 passed ci, mac68k and mac-automation.

WC-02 replaced the bootstrap wc implementation with the pinned NetBSD command.
The accepted shared case `echo -n hello | wc -c` expects seven spaces followed
by `5` and a newline. The native Solaris wrapper still compared its captured
output to unpadded `5`, so correct current output could not reach its final
PASS marker. Only that comparison changes: it now requires seven spaces and
`5`. Existing command-substitution newline handling and command-status checks
remain unchanged; no whitespace normalization or weaker numeric comparison is
introduced.

## Red and green

Before changing the wrapper, the new offline command
`python3 -B tests/test_solaris9_build_acceptance.py` ran four tests and exited 1
with two behavioral failures:

- Correct current output was rejected with
  `libc acceptance output: <       5>` and status 1.
- Obsolete unpadded `5` incorrectly reached the final PASS and status 0.

After the one-line comparison fix, the same four tests passed. They execute
the checked-in wrapper's actual acceptance tail, including preceding pipeline
and exit-status assertions and its final PASS. They substitute command outputs,
file(1), and ksh's print builtin in unique temporary directories; they do not
reimplement the predicates. Negative inputs cover empty output, obsolete and
incorrect padding, a tab, wrong count, suffix and extra nonempty line. Correct
text accompanied by command status 7 must retain status 7 and cannot reach PASS.
Failures of either preceding assertion also prevent PASS.

These tests are wired into `make ci`. Shell syntax, publication hygiene and
`git diff --check` passed locally. Full exact feature Woodpecker results are
reported in the handoff after push. No Docker, UI or shared rig is required
for these offline tests. They are not native Solaris execution evidence.

## Ownership and remaining qualification

No shared libc/runtime symbols, headers, imports, feature implementations or
acceptance records changed. The assigned path is `tools/solaris9-build.sh`;
the historical SOLARIS-02 runner work changes a separate driver and is not
merged by this task. Shared rollups are left to the coordinator.

Current main's combined native Solaris qualification remains outstanding and
must use its exact source archive and full guest transcript. This correction
removes the known stale assertion; it does not prove the compiler, guest or
other assertions pass. Mac runtime execution is not required for this native
Solaris gate-only change; the project's separate current-main Mac acceptance
remains pending. No main merge or live guest operation is performed here.
