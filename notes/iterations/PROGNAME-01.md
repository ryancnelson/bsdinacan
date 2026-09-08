# PROGNAME-01: startup identity and private public veneer

Base: `0423c31`. Work: `work/PROGNAME-01`.

Hypothesis: replacing argv[0] in ordinary main must not replace the name saved
at startup. The first regression invokes a scheduled probe, replaces only the
vector pointer, compares the original identity, restores the vector before
teardown, and asserts the actual probe exit status. It uses its own registration
fixture without increasing production capacity. Woodpecker #198 on test-only `7b4051f` compiled and then failed in
`build/test_core`: `FAIL: argv vector replacement renamed startup identity`.
This is the intended behavioral red before the runtime implementation.

Implementation and Mac acceptance are complete; exact evidence is recorded below.

The implementation saves a borrowed reference to the original task-owned argv[0]
string at creation and successful exec. The existing raw runtime getter returns
that reference; the public private-header veneer selects its final pathname
component. setprogname preserves the name established by startup. No ABI tail,
new allocation, host call, or modification of argv is introduced. Tests restore
vector mutation before teardown; ownership of caller-modified argv is not expanded.

## Accepted integration, 2026-09-08

Merged runtime: `a7acb54bd569ef66d93884f9808cce66561e01d6`.
Exact Woodpecker #213 passed ci, mac68k and mac-automation. Archive SHA256:
`4332ea8f1b4abfb52dd31146d767234d007ce049523704ef6b33b34c11d0c87f`.
The coordinator accepted guest run `run-3h5wky79` with all 31 fresh PASS records,
reviewed its screenshot and receipt, and verified app closure and normal guest
shutdown with mounted disks closed and the serialized slot released.

Multiple prelaunch focus/mouse interruptions left the staged output evidence
empty. After those interruptions, the existing staged run was resumed and the
successful portion took 12.42 seconds. This is not a cold-boot measurement;
no uninterrupted first attempt is claimed. These observations motivate MAC-12's
optional window/cursor calibration without relaxing the focus checks.
