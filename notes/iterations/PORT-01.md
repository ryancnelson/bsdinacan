# Iteration PORT-01: Host adapter conformance harness

- **Base SHA:** $(git rev-parse origin/main)
- **Worktree:** /Users/ryan/devel/bsdinacan-PORT-01
- **Branch:** work/PORT-01

## Hypothesis
A reusable mock-host suite can prove the version, size, capability, allocation, clock, console, and context contracts without a platform-specific implementation.

## Red Test
Intentionally disabling context switching in the new mock host adapter fails the test correctly:
```
build/test_core
FAIL: Host context did not run or return
make: *** [Makefile:155: test] Error 1
```

## Solution
Implemented `run_host_conformance_harness` which explicitly validates arbitrary host adapters (checking undersized/oversized tables, absent capabilities, clock monotonicity, and context switches). Created a platform-independent `mock_host_ops` in `tests/test_core.c` and passed both it and `cb_linux_host_ops()` through the harness to prove the core interface is correctly validated without depending on Linux behaviors.

## Risks & Blockers
None. The harness cleanly validates adapter behavior without breaking existing Linux tests.
