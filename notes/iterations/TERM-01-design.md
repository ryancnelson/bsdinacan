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
  input newline translation. The host saves every setting it changes first.
  A second acquisition while leased fails `-CB_EBUSY` and changes nothing;
  callers must not disable a lease they did not acquire.
- `enable=0` restores the saved state for the acquired lease; calling it with
  no lease is a harmless success. Restore the saved values, never generic
  "cooked" defaults. No newly allocated cleanup resource may be needed here.
- Unsupported acquisition returns `-CB_ENOSYS`; all acquisition failures leave
  the host exactly as found. Partial setup must roll back before failure is
  returned. A restore failure must be reported, retain the saved snapshot for
  retry/fatal host cleanup, and must not be labeled successful restoration.

The kernel negotiates once for a successfully prepared boot, before any guest
runs. Queues and default attributes must exist first. `ENOSYS` or an absent
optional field selects legacy pass-through; another host error fails boot and
unwinds its staged resources. Record lease ownership only on successful
acquisition. Boot failure after acquisition, normal shutdown, and kernel
teardown must each restore exactly once. Destroy guest execution instances and
close console users before releasing the kernel's terminal owner. Restoration
failure goes through the host's reported failure path, not a successful shutdown
receipt. Abrupt external process death (for example SIGKILL) cannot promise a
cleanup callback; this design claims orderly/error-unwind restoration only.

A Linux implementation must save the original complete `tcgetattr` state and
any file-status flags it changes, including pre-existing `O_NONBLOCK` bits.
Do not overwrite unrelated flags, reopen stdin, or close inherited descriptors.
Disable inherited input processing/echo before selecting core mode; make input
reads nonblocking while leased, and restore the exact saved termios and flags
on every covered exit. If input is not a tty, retain legacy pipe/file input
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
erase interpretation. Short echo writes retain their unwritten bounded suffix;
never busy-loop on a zero-progress writer. The pump must finish/queue an echo
suffix before accepting further echo-producing input. Output errors follow the
existing host-console error policy and must not corrupt the input record.

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

If the edit contains incomplete input and no more host input is available, the
scheduler waits through the host's blocking/timed poll using the earliest finite
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
   Host-specific tests must never change the developer's actual terminal.
3. Canonical ordinary/erase/CR/NL input, exact echo, erase-at-empty, and short
   reads across multiple lines. Test 1022/1023 ordinary bytes, discarded
   overflow followed by NL/VEOF, erase after overflow, 16 empty VEOF records,
   full byte FIFO plus a waiting completed edit record, and drain resumption.
4. VEOF with and without pending bytes, consecutive VEOF, zero-length reads,
   physical EOF with an unfinished line, queued data before EOF, and persistent
   EOF after subsequent mode changes. Assert exact bytes and number of zero
   reads; do not equate transient EAGAIN with physical EOF.
5. Raw `VMIN=0/1`, literal control bytes, empty/readable poll, and two readers
   consuming one ready line. Count host reads to prove only root drains and
   count scheduling/wakeup events to reject repeated incomplete-line wakeups,
   full-queue readiness spin, stale-ready/EAGAIN spin and zero-progress echo
   loops. Interleave a runnable peer and finite IO-01 deadlines.
6. Reject unsupported VTIME/VMIN, actions and flag bits atomically, including
   a request that also changes ICANON/ECHO. Assert attributes, bytes, markers
   and readiness stayed unchanged. Exercise both mode transitions with queued,
   partial, overflowed and EOF input; assert no double echo or retroactive edit.
7. Ordinary-source private-header/symbol checks plus full exact Woodpecker
   checks. A future Mac adapter claiming raw capability requires a direct guest
   probe of that exact artifact. A legacy-only Mac result must be labeled
   ENOSYS/pass-through, not evidence of raw/termios behavior.

This branch has no runtime red/green claim and needs no guest execution.
Validation for the revised design: source inventory above, design consistency
review, passing `make check-publication`, and `git diff --check`. Complete
exact feature Woodpecker checks are pending at this commit; no local runtime
build or simulated guest result is claimed.
Only this owned note changes; shared backlog/status updates remain with the
coordinator after independent review.
