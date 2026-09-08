# Iteration PORT-01: Host adapter conformance harness

- **Base SHA:** 78a1e5b3a83ee8d70d4b7cf568df2419415af5c5
- **Worktree:** bsdinacan-PORT-01
- **Branch:** work/PORT-01

## Hypothesis
A reusable mock-host suite can prove the version, size, capability, allocation, clock, console, and context contracts without a platform-specific implementation. By isolating the API validations from the backend testing, we ensure that new backends respect deterministic context swapping, allocation rules, and console contracts.

## Red Test
Modified the context conformance test to expect sequential bidirectional yielding. Commenting out the `context_switch` back to the root context causes the child context to run through to the second validation block out-of-order, verifying the test strictly tracks interleaved state preservation.
```
build/test_core
FAIL: Context resumed out of order
make: *** [Makefile:155: test] Error 1
```

## Solution
Implemented `run_mock_api_validation` to handle table size matching, version checks, and absent capability checks using a lightweight mock adapter. Implemented `test_real_conformance_contract` which receives the exact host adapter (e.g. Linux) and rigidly checks allocation, resize preservation, required console capabilities (poll, read, write), clock monotonicity, and strict bidirectional context suspensions with local-state and instruction-position validation. Removed the previous "fake successful context backend" and proved correctness directly against `cb_linux_host_ops`. Avoided any home directory exposure in this note.

## Risks & Blockers
None. Ready for review.
