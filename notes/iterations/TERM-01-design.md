# TERM-01: bounded terminal discipline and host capability contract

Status: revised design only; no runtime implementation or terminal-mode change.
Review base: `bc52bca`, freshly fetched `origin/main`. Branch:
`work/TERM-01-review`. The original proposal and correction are preserved by
attributed cherry-picks of `3bef5ec` and `11d4699`; this revision supersedes
those proposals' overflow, timing, fallback, and mode-transition rules.

## Current implementation and scope

`src/core.c` creates separate console input/output/error `cb_open_file` objects;
these are not VFS terminal nodes. Its read path polls and reads the host, and its
scheduler also polls the host before waking console readers. `src/host_linux.c`
uses inherited stdin through host `poll`/`read`; it does not currently select or
restore termios settings. `platform/mac68k/host_mac.c` owns a canonical edit
buffer: the application-stack event pump handles erase, Return-to-newline and
echo, and task-side reads consume only its completed line. Neither backend is
currently a guaranteed raw byte source. TERM-01 must not add a second discipline
on top of either inherited Linux processing or Mac editing.

This design defines one kernel console's shared attributes, a bounded core
input discipline, and an optional raw-input lease from the host. It proposes
`isatty`, `tcgetattr`, and `tcsetattr` through private libc headers and appended
runtime operations. It does not import an upstream terminal library. No claim
is made to full POSIX termios, a working curses/vi port, PTYs, baud settings,
window sizing, signals, process groups, job control, output processing, or
inter-byte timers. Any ordinary consumer import requires its own pinned-source
inventory; defining this contract is not permission to add speculative APIs.

## Shared owner and optional host lease

A `cb_terminal_state` owned by `cb_kernel` holds attributes, bounded input
storage, readiness, and the host-lease flag. All three standard console open
files refer to that same state. Inheritance and `dup` retain the open file;
attributes are device-wide, not per task or per descriptor. `tcsetattr(1, ...)`
therefore affects the same input discipline as `tcsetattr(0, ...)`. Existing
non-console files acquire no terminal state. No `/dev/tty` lookup is added.

Append optional `int (*console_set_raw)(int enable)` to the *actual end* of
`cb_host_ops_v1`. Its result convention is host-style `0` or `-CB_E...`:

- `enable=1` acquires one exclusive console lease. Success means input will
  arrive without host canonical editing, echo, signal-character handling, or
  input newline translation. The host saves every setting it changes first. While leased, both console
  reads and writes must be bounded, nonblocking calls returning actual progress
  or a host error; the current Linux write-all loop does not meet this promise.
  A second acquisition while leased fails `-CB_EBUSY` and changes nothing;
  callers must not disable a lease they did not acquire.
- `enable=0` restores the saved state for the acquired lease; calling it with
  no lease is a harmless success. Restore the saved values, never generic
  "cooked" defaults. No newly allocated cleanup resource may be needed here.
- Unsupported acquisition returns `-CB_ENOSYS` without changing the host. An
  ordinary returned acquisition error likewise means partial setup was fully
  rolled back. If that rollback itself fails, the adapter must retain ownership
  of its saved snapshot, report the cleanup failure, and enter its existing
  nonreturning fatal path; it must not return an ordinary error claiming the
  host is unchanged. The adapter's cleanup owner exists independently of the
  kernel lease flag, which cannot be set for a failed acquisition.
- A restore failure retains the saved snapshot and active ownership. Do not
  clear the lease or free its owner before restoration succeeded. Reporting a
  fatal cleanup failure is an explicit failure outcome, not an assertion that
  restoration happened.

The kernel negotiates once for a successfully prepared boot, before any guest
runs. Queues and default attributes must exist first. `ENOSYS` or an absent
optional field selects legacy pass-through; another ordinary host error fails
boot and unwinds its staged resources. Record kernel lease ownership only on
successful acquisition.

**Cleanup/reporting boundary:** `cb_kernel_run` returning is not restoration;
it can leave execution instances and blocked tasks alive. Restore during
`cb_kernel_destroy`, after destroying guest execution instances and closing
console users, but before freeing the terminal owner or kernel. On successful
restore, clear the lease and complete destruction. Because this existing API
returns `void`, restore failure must call `host->fatal` and must not return
normally or publish successful shutdown. The host retains its saved cleanup
state until successful restore or explicit fatal termination; a failure-path
test can intercept fatal before termination to inspect that retained ownership.
Normal application completion requires destroy to return, not just run to
return. No new public shutdown API is proposed.

Post-acquire boot failure uses the same internal restore-or-fatal path before
returning the boot error. Every successful acquisition has exactly one successful
restore; repeated cleanup cannot restore another kernel's lease. A failure is
never counted as that successful restore. Abrupt external process death (for
example SIGKILL), or an unrecoverable host restoration failure, cannot promise
restored physical terminal state; these are reported limitations, not normal
shutdown outcomes.

A Linux implementation must save the original complete `tcgetattr` state and
any file-status flags it changes, including pre-existing `O_NONBLOCK` bits.
Do not overwrite unrelated flags, reopen stdin, or close inherited descriptors.
Disable inherited input processing/echo before selecting core mode; make input
reads and writes nonblocking while leased, and restore the exact saved termios
and file-status flags on every covered exit, including any output descriptor
whose flags changed. Handle aliased descriptors/open-file descriptions without
resnapshotting already-modified flags as if they were the original state. If input is not a tty, retain legacy pipe/file input
rather than pretending raw terminal control succeeded. One active console lease
per adapter is the explicit limit; a second kernel cannot restore the first
kernel's console by failing its own acquisition.

The Mac adapter may initially return `ENOSYS`, preserving its existing UI
editing. Enabling its raw capability is a separate implementation step: the
root/application-stack event pump must enqueue raw keyboard bytes without echo
or editing and provide a bounded queue; no Toolbox event processing may move
onto guest task stacks. TERM-01 does not claim Mac raw support merely because
the core and Linux adapter can provide it.

## Honest fallback and public errors

Without a successful raw lease, existing host bytes pass through unchanged.
The core does not echo, erase, translate CR, fabricate a canonical profile, or
claim to know inherited Linux settings. `tcgetattr` and every `tcsetattr` request
return `-1` with task errno `CB_ENOSYS`, including requests that appear canonical
or change only echo: the adapter has not supplied enough information to promise
those attributes. Reads/poll retain the legacy host semantics; raw `VMIN`
guarantees below do not apply to fallback.

For a valid internal console descriptor, `isatty` returns 1 (the emulated
console classification, not a promise that inherited host stdin is a tty).
It returns 0 with `CB_ENOTTY` for valid pipes/files and 0 with `CB_EBADF` for an
invalid descriptor. Attribute calls use `-1` plus task errno: invalid fd
`CB_EBADF`, valid non-terminal `CB_ENOTTY`, NULL attributes `CB_EINVAL`, and
unsupported capabilities `CB_ENOSYS`. Use existing project error numbering;
add only actually missing private errors, without changing existing values.

## Supported attributes and atomic updates

Private `termios.h` exposes only the implemented subset, with private link
names; no host termios structure crosses the runtime ABI. Proposed attributes
have version/size metadata in their private ABI representation. They describe
the emulated discipline, not host baud rate or device control:

- Local flags: `ICANON` and `ECHO` only. Default: both on.
- Input flags: `ICRNL` only. Default: on; it maps CR to NL in canonical mode.
  In noncanonical mode TERM-01 accepts only input flags zero, so bytes are exact.
- Output/control flags: zero only; guest output goes through the existing
  console writer. Display rendering by a frontend is not terminal output-flag
  emulation.
- `VERASE=0x08`, `VEOF=0x04`, `VMIN=1`, `VTIME=0` defaults. Erase/EOF can be set
  to distinct nonzero control bytes (1..31 or 127), excluding CR and NL;
  unsupported encodings are rejected. No disabled-control-character sentinel
  is advertised yet.
- Noncanonical reads support only `VMIN=0` or `1`, with `VTIME=0`.
  `VMIN>1` or any nonzero `VTIME` is rejected `CB_ENOSYS` in either mode,
  never silently rounded down or stored for later use.
- Only `TCSANOW` is implemented. Known but unsupported drain/flush actions return
  `CB_ENOSYS`; invalid actions, version/size, and malformed values return
  `CB_EINVAL`. Unknown flag bits return `CB_ENOSYS`.

Validate the descriptor, full proposed attributes, action, and host capability
before changing any attribute, queue, EOF marker, or readiness. On rejection,
`tcgetattr`, subsequent byte delivery, echo, and waiter states must match the
pre-call state. Mode changes do not reacquire the host or replace its saved
snapshot: all core modes continue to use the same raw lease.

## Bounded canonical storage and delivery

Use fixed, explicit bounds: an edit buffer of `CB_PATH_MAX` bytes (1024 today),
a committed byte FIFO of the same size, and 16 committed record descriptors.
Each record stores its remaining byte count or an empty-VEOF event. No input
path allocates, and record counts/byte counts must not wrap. All values here are
bounds, not an invitation to allocate one unbounded buffer per input line.

At most 1023 ordinary bytes enter the edit buffer; reserve its final byte for
NL. With a full edit buffer, discard further ordinary bytes without echo, but
continue recognizing erase, NL, CR when `ICRNL`, and VEOF. Erase removes one
retained byte and permits another ordinary byte to be accepted. A terminator
always ends the line, including at capacity; it must never be discarded because
ordinary bytes filled the buffer. This prevents a permanently unfinishable line.

NL is committed as part of the record. VEOF commits existing edit bytes without
adding itself; an empty VEOF commits a zero-byte event. A completed edit record
can wait in its own buffer when the committed FIFO or record descriptors lack
space. While it waits, stop draining host bytes rather than overwriting queued
records or discarding delimiters. Resume once consumers free enough room.
Bounded host-side storage/backpressure is separate from the core edit-line
policy and must be specified by any adapter advertising raw capability.

A canonical read returns from at most one record. A short read leaves the
record's remaining bytes ready; it never concatenates the next line or consumes
an adjacent EOF event. Empty VEOF returns 0 exactly once, then disappears.
`data VEOF VEOF more NL` delivers `data`, then 0, then `more\n`; the first VEOF
does not also queue a second zero return. A zero-length read always returns 0
without consuming bytes or an EOF event. Incomplete edits are not readable.

Echo occurs once when input is accepted, never again when read or moved between
queues. In canonical mode, erase of a retained byte emits `\b \b`; erase at
an empty line and VEOF emit nothing. Accepted NL echoes NL. Overflow-discarded
bytes do not echo. Raw-mode ECHO writes the accepted bytes literally, with no
erase interpretation.

The only echo storage is **three bytes** plus offset/remaining counters: the
largest generated response is the three-byte erase sequence. Before consuming
another host input byte, finish the pending suffix. This conservative rule
forbids all additional input draining while echo remains, so no later character
can produce another suffix or reorder echo. There is no separate output queue.

At the start of each root scheduler sweep, service a pending suffix with one
bounded host `console_write(1, ...)` call. A positive short write advances its
offset; since at most three bytes exist, at most three progress-making calls
finish it. While a suffix remains, schedule another root sweep before any
blocking stdin poll, even if no further input ever arrives. Other runnable
tasks and deadline checks still get a turn between sweeps. Current input-only
`console_poll` is never used as a proxy for output readiness.

TERM-01 deliberately does not implement waiting for writable output. Zero
progress, `-CB_EAGAIN`, another negative result, or an impossible count greater
than the requested suffix latches a terminal I/O failure. Stop draining input,
preserve accepted input and the unwritten suffix for inspection, wake blocked
terminal readers with `-1`/`CB_EIO`, and report `CB_POLLERR` rather than spinning
or claiming echo completion. Later reads fail without consuming queued data;
changing attributes does not clear this terminal fault. Before reporting run
completion, the scheduler services any pending suffix using the same bounded
sweeps even if the boot task has exited; a latched terminal fault makes
`cb_kernel_run` return failure, not a successful run with silently lost echo.
Teardown still restores the host. This fail-fast backpressure policy is a stated limitation; supporting
recovery from temporarily unwritable output needs a separately designed host
output-wait capability. Do not advertise such support through this interface.

## One drain owner, readiness, and EOF

Only a scheduler/root-stack `terminal_pump` may consume host `console_read`
bytes in raw-capable mode. Task read/poll callbacks inspect or consume core
storage and request scheduler service; they never independently drain the
host. The root pump is also the only place that drives Mac event polling.
Use a finite per-sweep byte budget, and read at most the amount safely storable
(one byte is sufficient for the initial implementation). This prevents losing
bytes beyond a terminator when a record fills the final queue slot.

A leased host read distinguishes `-CB_EAGAIN` (no byte presently available) from
0 (actual persistent source EOF); add `CB_EAGAIN` only if absent. Host poll may
report that raw bytes are available, but guest canonical `POLLIN` becomes true
only for a committed record, an empty-VEOF event, or persistent source EOF.
Do not wake every blocked reader just because a raw host byte arrived. Wake
eligible waiters only after the relevant guest readiness changes, and recheck
readiness when each task resumes. Competing readers share and consume the same
queue; readiness is not a reservation of a line for every reader.

If no echo suffix remains, the edit contains incomplete input, and no more host
input is available, the scheduler waits through the host's blocking/timed poll
using the earliest finite
poll deadline. It must not keep waking canonical readers on that incomplete
line. If core queues are full, suspend input draining and do not spin on the
host's still-readable backlog; service ready consumers/other tasks instead.
A stale ready indication followed by `EAGAIN` makes no input progress and does
not wake readers; use the host wait/yield path before retrying, not a tight
poll/read loop. Preserve IO-01 deadline checks while other tasks are runnable.

Persistent host EOF flushes an incomplete canonical edit once (without an
invented newline); queued data/events are delivered first, then reads keep
returning 0. Stop polling that exhausted input source. Guest poll remains
readable at EOF. This is distinct from the one-shot VEOF event. In raw mode,
`VMIN=1` blocks until data or source EOF, whereas `VMIN=0` returns 0 immediately
when empty; `poll` still reports actual data/EOF rather than readiness merely
because a zero-minimum read would return immediately.

## Raw storage invariant

Raw input uses the same two byte arrays, never a third queue. At all times,
including repeated transitions, `fifo_used <= CB_PATH_MAX`,
`edit_used <= CB_PATH_MAX`, and their sum is at most **2048 input bytes** today.
The independent three-byte echo suffix does not hold additional input. Metadata
is bounded by the existing 16 record descriptors plus fixed counters/flags;
mode changes cannot append an unbounded list of transition records.

On entering raw mode, drop canonical record boundaries and empty-VEOF markers
but retain payload bytes: FIFO bytes precede edit-buffer bytes. Raw reads drain
that order. While old edit-buffer bytes remain, do not append new host bytes
ahead of them in the FIFO; pause host draining until both old segments have been
consumed. Normal raw ingestion thereafter uses only the FIFO, with `edit_used`
zero, stopping at FIFO capacity. Raw data needs no per-byte record descriptors.

On entering canonical mode, coalesce the entire existing FIFO payload into one
committed record (if nonempty). If the edit array also contains old raw-readable
bytes from an immediate reverse transition, mark that array as one completed
pending record and stop host draining until it can be enqueued. Thus a transition
requires at most one FIFO record and one fixed pending-edit state, regardless
of how many times mode changes occur without a read. No bytes are copied into
insufficient space, discarded for metadata exhaustion, or reprocessed as control
characters. Queue lengths/offsets and at most 16 canonical records continue to
bound subsequent input. These invariants must be asserted by transition tests.

## Mode transitions and pending input

An accepted transition is atomic at a cooperative scheduling boundary:

- Canonical to raw: preserve unread committed bytes in order, then expose the
  incomplete edit bytes. Preserve a pending completed record's payload too.
  Empty VEOF markers are discarded because they are canonical control events;
  they do not become raw bytes or persistent EOF. Drain existing storage in
  order rather than copying into a buffer too small to hold both queues.
- Raw to canonical: bytes already accepted into core storage remain readable
  as committed data, even without a newline. Do not retroactively erase,
  translate, or echo them. Newly received bytes use the new canonical rules.
  This explicit transition record prevents existing raw bytes being stranded
  or transformed after the program has selected different attributes.
- Canonical attribute changes preserve accepted edit bytes; changed erase/EOF
  characters and echo/CR flags affect only subsequent incoming bytes.
- A persistent source-EOF state survives every attribute/mode change. A mode
  change itself never invents an EOF event. Reevaluate blocked reads and poll
  waiters after applying the transition, including canonical-to-raw availability
  and an empty `VMIN=1` to `VMIN=0` change.

## Versioned boundaries and deterministic acceptance

Append public operations to the actual `cb_api_v1` tail at implementation time;
do not reorder poll, directory, dirname, or any other accepted fields. Preserve
the old mandatory prefix in `api_is_usable`; an old table can still start an
ordinary program. Each optional call checks both field-end `struct_size` and
non-NULL pointer before access. Public `isatty` returns 0/`CB_ENOSYS` when its
operation is absent; attribute wrappers return -1/`CB_ENOSYS`.

Likewise, extend the host table after its actual last field and make
`host_valid` accept the unchanged old mandatory prefix. Never read or call the
new tail for a truncated table. The existing console, clock, context and fatal
callbacks remain required; optional terminal control must not weaken them.
A full-size NULL host callback and a short old host table both select honest
pass-through. Failure of an available callback has the explicit lease semantics
above; it is not permission to manufacture supported attributes.

Implementation must add actual failing tests before runtime changes and retain
all existing console/IO tests. The bounded acceptance matrix is:

1. Short host table, full-size NULL callback, explicit ENOSYS acquisition, and
   inherited Linux non-tty input: unchanged pass-through, no double echo, no
   fabricated attributes. One-field-at-a-time absent public operations return
   exact errors without preventing ordinary startup.
2. Successful raw lease: capture initial state, apply once, share across all
   console descriptors/dup/child tasks, restore the exact original settings and
   flags once. Inject partial setup failure, post-acquire boot failure, normal
   exit, kernel destruction with blocked readers, second-owner acquisition,
   and restoration failure; inspect restoration before freeing its owner.
   Assert that run returning alone does not restore; destroy returning does.
   Inject acquisition rollback failure separately: intercept adapter fatal and
   inspect its saved cleanup owner, with no false unchanged-host return. Inject
   destroy restoration failure: intercept core fatal before owner release and
   assert no successful shutdown return. Host-specific tests must never change
   the developer's actual terminal.
3. Canonical ordinary/erase/CR/NL input, exact echo, erase-at-empty, and short
   reads across multiple lines. Test 1022/1023 ordinary bytes, discarded
   overflow followed by NL/VEOF, erase after overflow, 16 empty VEOF records,
   full byte FIFO plus a waiting completed edit record, and drain resumption.
4. VEOF with and without pending bytes, consecutive VEOF, zero-length reads,
   physical EOF with an unfinished line, queued data before EOF, and persistent
   EOF after subsequent mode changes. Assert exact bytes and number of zero
   reads; do not equate transient EAGAIN with physical EOF.
5. A three-byte erase echo with successive one-byte host writes drains fully
   without any additional stdin event; no input read occurs between partial
   writes. Zero/EAGAIN/hard-error/invalid-count writes latch the explicit I/O
   failure without retry spins. Existing Linux write-all behavior must not be
   used to satisfy this mock contract by assumption.
6. Raw `VMIN=0/1`, literal control bytes, empty/readable poll, and two readers
   consuming one ready line. Count host reads to prove only root drains and
   count scheduling/wakeup events to reject repeated incomplete-line wakeups,
   full-queue readiness spin, stale-ready/EAGAIN spin and zero-progress echo
   loops. Interleave a runnable peer and finite IO-01 deadlines.
7. Reject unsupported VTIME/VMIN, actions and flag bits atomically, including
   a request that also changes ICANON/ECHO. Assert attributes, bytes, markers
   and readiness stayed unchanged. Exercise both mode transitions with queued,
   partial, overflowed and EOF input; assert no double echo or retroactive edit.
   Repeat transitions at 2048 bytes and at 16 records without intervening reads,
   then verify byte order and fixed storage/metadata limits.
8. Ordinary-source private-header/symbol checks plus full exact Woodpecker
   checks. A future Mac adapter claiming raw capability requires a direct guest
   probe of that exact artifact. A legacy-only Mac result must be labeled
   ENOSYS/pass-through, not evidence of raw/termios behavior.

## Implementation stages, not one combined runtime assignment

Each stage is a separate future worker assignment with its own red test,
applicable full CI, review, and exact-artifact guest obligations. This document
queues no work by itself. Dependencies below are required, not suggestions to
implement all stages in one branch:

1. **Classification and honest fallback.** Introduce the shared console owner,
   private isatty classification and unsupported attribute results, and old
   public/host prefix guards. Preserve existing console reads/poll unchanged.
   No host enters raw mode. Depends only on this approved design and the actual
   API tail in the implementation base.
2. **Isolated canonical engine.** Pure bounded queue/record/erase/VEOF/echo
   state transitions, exercised with deterministic byte inputs and no host
   calls. Prove the 2048-byte input and three-byte echo limits, delimiters at
   capacity, and error states. Depends on stage 1's owner/layout; does not
   change live console input routing.
3. **Attributes and mode transitions.** Add validation and the supported
   attribute model using the engine, sharing and atomic-rejection tests.
   Demonstrate repeated transitions preserve the storage invariant. Depends
   on stages 1 and 2; real hosts still take legacy fallback.
4. **Scheduler and mock lease.** Integrate the single root drain owner, pending
   echo service independent of stdin, readiness, and restore-or-fatal lifecycle
   using deterministic mocks. Preserve IO-01 deadline behavior. Depends on
   stages 2/3 and accepted IO-01; prove the documented bounded/nonblocking host
   writer and fail-fast output policy before any real adapter is enabled.
5. **Linux adapter.** Implement exclusive acquisition, saved state, bounded
   nonblocking reads/writes, exact restoration and injected failure tests on
   isolated fixtures. Remove the leased writer's write-all/zero-progress loop;
   this is a prerequisite, not assumed current behavior. Depends on stage 4.
   Raw acquisition must remain disabled until all callback/restoration promises
   are satisfied. A proposal to support output EAGAIN recovery instead of this
   design's fail-fast policy blocks that part of the stage pending a separate
   output-wait interface decision; do not quietly add an output API here.
6. **Mac adapter, later.** Inventory and implement bounded root-stack keyboard
   delivery without host edit/echo, plus exact UI-mode restoration. Depends on
   stage 4 and a separate reviewed Mac buffer/event contract; remains **blocked
   for assignment** until that contract and serialized guest acceptance plan
   exist. Mac legacy ENOSYS/pass-through remains supported in all earlier stages.

Prior design commit `50b6093` passed all three exact Woodpecker workflows.
This revision makes the output failure policy, cleanup boundary, raw-storage
invariant, and staging dependencies explicit; its own exact checks are pending.

This branch has no runtime red/green claim and needs no guest execution.
Validation for the revised design: source inventory above, design consistency
review, passing `make check-publication`, and `git diff --check`. Complete
exact feature Woodpecker checks are pending at this commit; no local runtime
build or simulated guest result is claimed.
Only this owned note changes; shared backlog/status updates remain with the
coordinator after independent review.
