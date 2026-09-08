# TERM-05: bounded host output service proposal

Status: design under review, no runtime or ABI change. Base `b4946d8`.
This resolves the output-service prerequisite in TERM-01; it does not authorize
implementing all terminal stages together.

## Observed gap

At this base, `src/host_linux.c:host_console_poll` waits only on stdin.
`host_console_write` loops until the entire request completes and does not
handle zero progress; read/write errors are flattened to EIO. Core console
output readiness always reports writable. The scheduler's idle wait also uses
input-only polling. Therefore neither a root echo suffix nor a blocked task
writer can wait for output progress without blocking the process or spinning.
The Mac writer updates its UI and reports the entire count; its event pump and
legacy canonical input stay unchanged until a separate Mac raw-adapter task.

## Proposed optional boundary

Add one optional host-tail wait operation when the scheduler stage is assigned.
It accepts an interest mask for console input, stdout, and stderr, and a timeout
in milliseconds (-1 indefinite, 0 probe, positive finite). It returns the subset
of ready interests, zero on timeout, or a negative project error. Exact C names
and bit values are implementation work; this note advertises no shipped API.
The actual current host table tail and unchanged mandatory prefix are binding.

Only requested streams may cause a readiness result. Input EOF and output
hangup/error are ready so the next bounded I/O call can observe their outcome;
readiness does not promise that the following call transfers bytes. Invalid
masks/timeouts are rejected. With no interests, a finite timeout is a timer wait;
indefinite empty waits are invalid. Every call is bounded by its finite timeout,
including interruptions: recompute remaining time from a monotonic deadline
rather than restarting the full interval after EINTR.

Raw acquisition requires both the raw-lease callback and this wait operation,
plus bounded nonblocking console read/write behavior. A missing capability
keeps the host in legacy fallback; do not acquire a partially usable lease.
Successful raw acquisition saves/restores all changed input/output flags and
terminal attributes as specified by TERM-01. Aliased stdout/stderr/stdin open
file descriptions must not cause a modified snapshot to replace the original.

For leased I/O, a positive count is actual progress, zero write progress and
EAGAIN are backpressure, zero read is persistent input EOF, and other negative
results are genuine errors. A host must not loop until a write completes.
Legacy hosts keep the existing mandatory callbacks and behavior; the optional
contract must not be inferred from a large struct size alone.

## Root and task scheduling

The root pump retains at most the existing three-byte echo suffix. Each sweep
makes at most one bounded echo write and a finite amount of input progress.
After a positive partial write, another sweep may run after other runnable tasks
and deadline processing. After zero/EAGAIN, stop retrying until a wait operation
reports output readiness. No additional input is consumed while echo remains.

Task console writes in leased mode must use the same backpressure boundary:
return positive partial progress to the caller; on zero/EAGAIN before any
progress, block on that output stream and yield. On wake, retry once and recheck.
Do not return spurious permanent errors or hold a task stack in a host write-all
loop. Zero-length writes return immediately without waiting. Poll writable
readiness uses the optional output mask instead of unconditional success.
Legacy task write behavior stays unchanged.

When no task is runnable, wait on the union of useful input/output interests,
with the earliest finite task deadline. Suspend input interest when the engine
cannot accept more bytes. Output interest exists for pending echo and blocked
writers; existing canonical guest readiness comes from queues, not raw input.
When tasks are runnable use nonblocking host checks and retain fair task turns.

A ready indication followed by EAGAIN can race. It must not cause a tight retry
loop: after a no-progress readiness cycle use a bounded timer backoff while
still servicing other interests/tasks and their earlier deadlines. Propose a
1 ms retry eligibility deadline for that stream, not a blocking sleep. A
zero/failed monotonic clock uses the existing explicit scheduler error policy;
it cannot be used to skip an otherwise finite output deadline indefinitely.
Independent streams continue to progress while one is backed off.

## Completion and failure

Normal input operation has no invented congestion timeout. Pending bytes may
wait while tasks/deadlines remain serviceable. Program completion is different:
before reporting a successful orderly shutdown, service pending echo with a
finite drain deadline. Proposed initial bound is 1000 ms, measured from the
start of shutdown, not reset by partial progress. No suffix means no wait.
On expiry or a genuine write error, preserve the unsent count for diagnostics,
restore host state through the established cleanup path, and report failure;
do not publish successful terminal completion or guest acceptance.

Implementation must pin this reporting boundary in tests: `kernel_run` can
return a negative internal result for the failed drain; `kernel_destroy` still
restores the lease and must complete before the caller reports normal success.
A restoration failure retains cleanup ownership and invokes the nonreturning
fatal path already specified by TERM-01. Forced external termination is outside
orderly-drain guarantees. No implicit unbounded retry or silent successful
cancellation of echo is permitted.

## Falsifiable implementation tests and stages

- Mock wait records interest unions and requested timeouts. Inject stdin silence
  with stdout readiness: a partial echo completes without another input event.
- Inject zero/EAGAIN, readiness races, partial writes, and genuine errors. Prove
  suffix/input preservation, bounded retry count per sweep, independent stderr,
  runnable-peer progress and earlier finite poll deadlines.
- Task writers block/yield on congestion; short positive writes remain partial.
  Zero-length calls do not wait. Writable poll changes with actual host readiness.
- Test timed waits across repeated interruption and aliased descriptors using
  isolated fixtures, never the developer's terminal. Exact host state restores.
- Shutdown succeeds immediately with no suffix; completes a partial suffix;
  reports failure at the original deadline under endless partial/no progress;
  always attempts restoration before normal caller completion.
- Old-size, full-size NULL, and partial capability combinations preserve legacy
  startup/input without raw acquisition. Existing console tests remain enabled.

First implement the host wait contract with deterministic mocks and no real raw
lease; then scheduler/engine integration; then the Linux adapter on isolated
pseudo-terminals. The Mac adapter remains a distinct later task. Every runtime
stage requires its own red test, exact Woodpecker checks and fresh guest gate.
This design has only source inspection and publication/diff validation; it
claims neither a working callback nor guest behavior.
