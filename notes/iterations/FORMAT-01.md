# FORMAT-01: bounded signed decimal formatting

- Status: behavioral red established; implementation pending
- Base SHA: `7b1a152` (fresh `origin/main`)
- Branch: `work/FORMAT-01`
- Hypothesis: the existing ordinary-source stdio probe can emit a left-space
  padded signed integer through the private formatter, returning the exact
  count and preserving incoming errno, without expanding the public ABI.
- Scope: accepted FORMAT-01-design; no uniq import or other stdio expansion.

## Red

Validation uses an isolated Ubuntu 24.04 ARM64 Linux container with GCC and
Clang, mounted only to this feature checkout. This is a local Linux result,
not Woodpecker or native guest evidence.

- Clean baseline: `make clean test` passed, including the full core suite,
  source boundaries and Linux one-process check.
- Command: `make build/test_core && build/test_core --format`.
- Ordinary source calls `printf("%4d", -42)` with errno set to ENOENT.
- Expected: status 0, exact output ` -42`, return count 4, preserved errno.
- Observed: build succeeded; test exited 1, reporting
  `actual status/output: 17 <>` against `expected status/output: 0 < -42>`.
- The production formatter is unchanged at this checkpoint. This is a
  pre-implementation behavioral failure, not a missing-declaration failure.

## Green

Pending: implementation, full design matrix, full `make ci`, independent review,
exact Woodpecker checks and serialized Mac/Solaris qualification.

## Change and review

The existing ordinary unsupported `%d` test and the separate stdio-state `%d`
rejection must be changed to `%u` with an unsigned argument when implementing
the conversion, preserving their failure/errno/prefix and stream-state checks.
The shared formatter's real count guard requires a private test-only wrapper;
normal exports and the program API must remain unchanged.
