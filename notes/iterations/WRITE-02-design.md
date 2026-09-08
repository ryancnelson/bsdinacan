# WRITE-02-design: finite console writes before tee

Source audit at accepted `e65e36fc0444193445b8a304f6e818d2768d5b62`.
This is a proposed bounded contract and test plan, not an implemented fix or
an observed behavioral regression. The motivating unchanged NetBSD tee source
is pinned in the NEXT-UTIL-02 audit; tee is not imported or accepted yet.

## Observed paths

- `libc/cb_libc.c`, `cb_libc_write`, returns the runtime write result unchanged.
- `src/core.c`, `api_write`, checks descriptor, representable count and non-NULL
  nonempty buffer, then dispatches to the descriptor operation.
- Its `console_write` converts negative host results to task errno, but forwards
  zero and positive results without checking progress or the requested bound.
- `src/host_linux.c`, `host_console_write`, loops until the requested count is
  written. It retries EINTR, maps another failure to EIO, and increments by the
  host write result. A zero result for a nonempty request would repeat forever
  inside the adapter, before any portable-core validation could run. A failure
  after a positive prefix currently returns an error instead of that prefix.
- `platform/mac68k/host_mac.c`, `console_write`, returns the requested count;
  normal console output is synchronous. Acceptance capture truncation is a
  separate flag and already rejects an incomplete captured transcript.
- RAMFS nonempty writes return the requested count or an error. Pipe writes
  block when full, and return a positive prefix or EPIPE when readers close.
  Their existing zero-count handling must remain intact.
- Pinned tee's raw write loop advances by the returned count. A zero result
  leaves its loop unchanged; an oversized result would advance/subtract beyond
  the remaining bytes. Existing stdio checks protect fwrite/printf callers,
  but do not protect this raw-write caller.

These are source deductions. No claim is made that the actual Linux terminal
has produced zero progress, or that this failure was reproduced in a guest.

## Proposed bounded implementation

Keep this separate from tee signals and per-task module state. Do not add a
signal stub, modify upstream tee or introduce an ABI extension.

1. At the portable console boundary, accept a positive count only when it is
   at most the requested count. Map zero progress on a nonempty request and
   positive over-return to EIO/-1. Preserve valid positive short writes and
   documented negative host errors. Validate wide values before narrowing;
   handling malformed negative values must avoid negating INT64_MIN.
2. Keep descriptor validation before zero-count handling. A valid console
   zero-count write returns zero without touching the host callback or buffer.
   State the errno policy explicitly and preserve existing zero-count tests.
3. Fix the Linux adapter's own finite-progress loop. A zero or hard error after
   a positive prefix returns that prefix; with no prefix it reports EIO.
   Retain EINTR retry and exact pointer/count advancement. This is required
   even if the portable boundary is hardened, because it cannot interrupt an
   adapter that never returns. Do not claim that an arbitrary malicious host
   callback can be preempted by this change.
4. Document the narrowed blocking console contract. General VFS plugins and
   future nonblocking descriptors need their own contracts; this change does
   not establish full POSIX signal, terminal or nonblocking-write semantics.

## Falsifiable acceptance

Use deterministic callbacks, never an unbounded hanging test. First demonstrate
that a checked real task receives zero for a nonempty console write with the
current fake host. After the fix it must receive -1/EIO after exactly one call.
Also cover exact success, positive short count, over-return (including a value
above UINT32_MAX on ILP32), negative EPIPE, unusable zero-count buffer with no
callback, and a later successful call. Assert actual task exit and cleanup.

For the Linux loop, use a narrowly scoped injected host-write seam or a test
translation unit with the real adapter function, with bounded call counters:
short+success, short+zero, short+error, first-call zero/error and EINTR+success.
Check returned count, emitted prefix, callback offsets and request remainders.
The old zero-progress loop must fail a bounded control rather than stall CI.
Do not implement a second copy of the loop as the test subject.

The portable console cases should run through the same helper on Linux and Mac;
keep the fixed program capacity and current 65 acceptance records by using an
existing checked probe or an internal subcase. Linux-only syscall injection is
separate evidence. Require independent review, exact Woodpecker ci/mac68k/
mac-automation and fresh complete Mac acceptance before runtime integration.
