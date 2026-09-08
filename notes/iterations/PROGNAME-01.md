# PROGNAME-01: startup identity and private public veneer

Base: `0423c31`. Work: `work/PROGNAME-01`.

Hypothesis: replacing argv[0] in ordinary main must not replace the name saved
at startup. The first regression invokes a scheduled probe, replaces only the
vector pointer, compares the original identity, restores the vector before
teardown, and asserts the actual probe exit status. It uses its own registration
fixture without increasing production capacity. Woodpecker #198 on test-only `7b4051f` compiled and then failed in
`build/test_core`: `FAIL: argv vector replacement renamed startup identity`.
This is the intended behavioral red before the runtime implementation.

Implementation and Mac acceptance remain pending.

The implementation saves a borrowed reference to the original task-owned argv[0]
string at creation and successful exec. The existing raw runtime getter returns
that reference; the public private-header veneer selects its final pathname
component. setprogname preserves the name established by startup. No ABI tail,
new allocation, host call, or modification of argv is introduced. Tests restore
vector mutation before teardown; ownership of caller-modified argv is not expanded.
