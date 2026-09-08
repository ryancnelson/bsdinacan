# PROGNAME-01: startup identity and private public veneer

Base: `0423c31`. Work: `work/PROGNAME-01`.

Hypothesis: replacing argv[0] in ordinary main must not replace the name saved
at startup. The first regression invokes a scheduled probe, replaces only the
vector pointer, compares the original identity, restores the vector before
teardown, and asserts the actual probe exit status. It uses its own registration
fixture without increasing production capacity. No red result is claimed yet;
this test-only commit is submitted to Woodpecker to measure the baseline.

Implementation and Mac acceptance remain pending.
