# TERM-05 design review after WRITE-02

Status: proposed corrections to the existing design, for independent review.
Source base: `2bc2008c9018ad522f30efec51920de9d2aca6cc`.
Branch: `work/TERM-05-design-review`. Documentation only; no callback, scheduler,
terminal lease, error code, or host adapter is implemented here.

The earlier `work/TERM-05-design` worktree remains at
`6445fefa4492e18b32924bb03e0570b48cec44d2` and is preserved. Its
[proposal](TERM-05-design.md) was already reviewed; the queue's unqualified
“Ready for design only” entry is stale bookkeeping. The coordinator assigned
this separate review against accepted WRITE-02. The integrator should record
this claim and review outcome, without treating a design as runtime completion.

## Current source, not future capability

The following inventory refers to the full source base above:

- [Host table](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/internal.h#L11)
  has mandatory `console_poll(int)`, `console_read`, `console_write`, clock,
  context and fatal callbacks. The actual tail is `fatal`; no raw lease or
  output-interest wait exists. `host_ops_valid` in
  [core](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/core.c#L1830)
  currently requires the whole existing table and all mandatory functions.
- [Core console I/O](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/core.c#L276)
  waits for input-only readiness before reading. Output readiness is still
  unconditional. Accepted WRITE-02 makes valid zero-count writes return without
  a callback or errno change; nonempty zero, over-return and unrepresentable
  negative errno magnitudes become EIO. Positive partial progress survives.
  Descriptor/count/buffer validation still precedes backend dispatch.
- [Linux host](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/host_linux.c#L103)
  polls inherited stdin. Its write loop now stops on zero/hard failure and
  retains any emitted prefix. It still uses blocking writes and retries EINTR;
  neither a blocking syscall nor an unlimited series of interruptions has a
  new wall-clock deadline. WRITE-02's finite progress handling is not an
  asynchronous terminal service. Its accepted behavior and real controls are
  recorded in [the Linux note](WRITE-02-linux.md) and
  [the portable note](WRITE-02-portable.md).
- [Mac host](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/platform/mac68k/host_mac.c#L264)
  pumps canonical input and synchronously appends console output. Capture
  truncation is tracked separately. There is no raw-input lease; a future
  discipline must not process this edited input a second time.
- [The shared terminal owner](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/internal.h#L167)
  contains only its kernel pointer. Console classification is implemented;
  [attribute operations](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/core.c#L1183)
  still report ENOSYS for valid non-NULL console requests. The TERM-01 queues,
  attributes, raw acquisition and restoration are proposed, not present.
- [Scheduler waits](https://github.com/ryancnelson/bsdinacan/blob/2bc2008c9018ad522f30efec51920de9d2aca6cc/src/core.c#L768)
  use input polling and existing finite task-poll deadlines. Lost clock support
  wakes a finite poll so its own call can report ENOSYS. There is no existing
  sticky root-output failure or shutdown echo deadline to reuse unchanged.
  The task structure stores no bounded copy of blocked poll descriptor interests.

## Corrections required before implementation

**Preserve blocking WRITE-02 and make the new mode explicit.** Remove the old
observed-gap claim that Linux still spins on zero progress. Its remaining gap
is output readiness and nonblocking service. Legacy mode keeps the accepted
write-result rules, existing error mappings and current stdio behavior.
A larger host struct alone must never change them.

For a successfully negotiated future raw lease, use a distinct, explicit
would-block condition at the host boundary. Readiness can race with a later
read/write. A would-block read is not EOF; zero read remains persistent source
EOF. A would-block write retains its entire unsubmitted suffix. Recommend
retaining nonempty zero-write as EIO in this mode too, correcting the old
proposal's “zero/EAGAIN are backpressure” wording. The leased scheduler path
must recognize would-block before ordinary console error conversion; a blocking
public task write yields and retries rather than exposing zero as success or
turning congestion into a sticky stdio error. Positive partial counts and
malformed-count validation remain unchanged.

The current project error enum has **no EAGAIN/would-block value**. Assigning
that stable project result and its translation is part of the future boundary
implementation. Do not return a native host errno number, reuse EIO for
congestion, or advertise a shipped public nonblocking API. This review fixes
the semantic distinction, not its future C spelling or numeric value.

**Freeze the old mandatory host prefix when optional fields are appended.**
The current `sizeof(*host)` minimum must become the preserved old-prefix minimum
through `fatal`; merely appending fields while retaining that check rejects old
hosts. Each optional member needs its own size and NULL check. Old actual-size
allocations, full-size NULL callbacks and partial capability combinations must
remain in legacy mode without acquiring a lease. The new mode requires the
raw lease, output wait, and bounded nonblocking I/O semantics together. A
would-block code alone does not prove those capabilities exist.

**Make root failure and completion reporting explicit.** The old reference to
an “existing scheduler error policy” is insufficient for output-only work.
The future service needs its own sticky run failure for a genuine root echo
write error, invalid wait result, lost monotonic clock or failed bounded wait.
Do not relabel an expired task poll as a kernel failure. No zero/backwards clock
reading may extend a finite service deadline indefinitely; treat it as clock
failure, not a timer reset. Retain the unsent suffix for diagnostics and stop
new input draining after root failure.

## Retained scheduling, bounds and lifecycle

The following parts of the earlier proposal remain sound, with the corrections
above. They are requirements for later isolated implementation stages:

1. The wait operation selects only requested input/stdout/stderr interests.
   Output-only work progresses with silent stdin. EOF/hangup/error readiness
   permits the next bounded call to inspect the condition; it does not promise
   bytes will transfer. Invalid masks/timeouts fail. Empty masks permit finite
   timer waits only. Repeated EINTR consumes the original finite timeout budget.
2. A root sweep attempts at most one echo write and retains the exact remaining
   suffix, bounded to the TERM-01 three-byte limit. It does not consume another
   input byte while echo is pending. Preserve the accepted 2048-byte input
   storage bound, canonical record/EOF semantics, delimiter processing at
   capacity, and attribute-transition ordering from
   [TERM-01](TERM-01-design.md). No new output queue or drop-on-congestion rule.
3. Task output uses bounded nonblocking attempts in the leased mode. Return a
   valid positive partial result; if no bytes progress, block/yield on that
   output. Scheduler integration must provide a real scheduling opportunity
   between retry attempts, including ready-but-would-block races. A successful
   synchronous callback must not conceal a write-all loop. Valid zero-count
   calls still avoid callbacks, waits and buffer access.
4. Preserve blocked poll interests in a validated bounded copy, recompute
   against live descriptors, and clear on wake/exit/exec. Interests include
   echo, blocked writers and output-only poll callers. Suspend input interest
   when the engine cannot accept more data. While runnable tasks exist, use
   nonblocking readiness probes and service peer turns and deadlines.
5. Retain the proposed one-millisecond eligibility backoff after readiness
   followed by would-block. Suppress that stream's interest until eligible,
   without blocking other streams/tasks. Wait to the earliest task deadline,
   retry eligibility or shutdown deadline. An empty finite timer wait avoids
   spinning when every useful output is temporarily backed off. Tests must
   count retries and prove earlier deadlines and independent streams progress.
6. Ordinary operation has no invented congestion timeout. Orderly completion
   drains only already-pending echo with the proposed **1000 ms** budget, set
   once at shutdown entry and never refreshed by partial progress. No suffix
   means no drain wait. Do not accept new input during shutdown. Deadline expiry
   or write/wait/clock failure preserves the unsent count and reports failure;
   it must not publish successful completion after silently dropping echo.
7. Explicit orderly cancellation uses that same bounded stop/drain/restore
   boundary; cancellation is not a successful output result. Destroying a live
   kernel must stop its writers and release its lease without creating a new
   indefinite drain. A caller choosing direct destruction without orderly
   completion gets no promise that pending bytes were delivered. Forced process
   death remains outside restoration/delivery guarantees.
8. Retain TERM-01 lease ownership and exact restoration, including aliased open
   file descriptions, partial-acquisition rollback, and restoration after
   execution instances and console users close. A failed restore retains the
   snapshot/ownership and invokes the nonreturning fatal path. `kernel_run`
   returning is not restoration; successful completion requires destruction
   to return. No new cleanup allocation or implicit retry-until-success loop.

## Small next loops and review gates

Keep the earlier stage order: isolated bounded engine and attribute transitions;
then a mock-only optional host boundary; then scheduler integration; then each
real host adapter separately. No later task is implicitly claimed here.
The optional-boundary loop must prove old-prefix compatibility, exact interest
masks/timeouts, would-block versus EOF, zero-write EIO and interruption budgets.
The scheduler loop must prove stdout progress with silent stdin, partial echo
retention, independent stderr/peer progress, bounded readiness races, output-only
poll interest, and exact clock/wait/drain failure reporting. Lifecycle tests
must inspect pending bytes and ownership before eventual teardown can hide a
failure. Real raw-adapter tests must use isolated terminals and verify original
attributes/file flags, not the developer's active console.

The current WRITE-02 tests remain regression gates throughout. Every runtime
stage still needs independent review, exact Linux/Mac CI and fresh Mac artifact
acceptance, plus the current Solaris transition/qualification policy. This
review has source inspection and publication/diff checks only; documentation
requires no guest run. It makes no claim that asynchronous service exists.

## Independent review follow-up

The later bounded scheduler tests must include a clock stuck at a positive
value while finite waits keep reporting timeout. Shutdown must not become an
endless deadline loop: account for bounded wait expiry or diagnose stalled
time, while allowing normal coarse clock resolution. This is a proposed test
requirement, not a newly implemented clock API. Independent review and exact
feature #380 ci, mac68k and mac-automation passed for a283eef; no guest rerun is
required for this documentation-only correction.
