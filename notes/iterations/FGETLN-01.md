# FGETLN-01 — task-owned borrowed line input

Base: reviewed design `d4cb6066b1fd0cf992eb6fa8055bbf23f11bd9ea`, on writable
runtime candidate `7faa968fd4bae1c8d33d5b4e784f06f8774b7884`. The writable
candidate has coordinator-observed exact #554 all-three CI and independent
review; fresh Mac and Solaris qualification remain pending. This implementation
is a separate candidate, not an accepted-main claim. No full uniq import.

The hypothesis is that the unchanged pinned uniq's immediate length-based copy
can consume a task-owned borrowed line through the existing stream resolver,
without shared stdin storage or host libc. The ordinary-source red on the base
was `make build/libc_file_probe.o LDLIBS=-lucontext` in the isolated pinned
biggie container. It fails at the new call with `implicit declaration of
function 'fgetln'` and pointer/integer comparison under -Werror. An initial
container entrypoint setup error was corrected first and is not red evidence.

FILEs now own reusable line storage. Immutable stdin refers to a private object
through appended `stdin_line_storage`, guarded by its own offsetof/size minimum.
Existing input-state minima remain frozen. List-only dynamic streams work;
old-size stdin fgetln rejects ENOSYS before allocation/read. The resolver carries
its validated state and line pointer into fgetln, avoiding a repeated accessor
or list traversal. Core clears both opaque references before heap reclamation;
fclose releases buffers even when close fails and preserves the close errno.

Reads consume one byte at a time to avoid read-ahead changing mixed getc/fread
behavior. Length includes newline and embedded NUL/0xff; final unterminated
bytes return once. There is a spare NUL outside the reported length. A partial
record followed by read or allocation failure returns NULL/zero length with
sticky error and original failure errno. Consumed bytes are not replayed;
retained capacity survives for retry. Successful/clean-EOF calls preserve
incoming errno; EOF is sticky until clearerr. Arithmetic checks precede reads
and reallocations, and resize failure preserves the owned old pointer.
Borrowed storage expires on the next same-stream I/O attempt, close or task
lifecycle transition; metadata and zero fread preserve it. Other FILE/task I/O
preserves a borrow. See the reviewed design for the complete bounded contract.

## Tests and observed evidence

The production-code callback unit checks exact byte records, no read past
newline, getc mixing, sticky EOF/error and recovery, negative/oversized reads,
initial stdin-object/buffer and later growth failures, partial consumed offsets,
close failure reclamation, invalid/direction rejection, real short API/state
allocations including every partial appended-pointer size, list-only dynamic
success and checked size arithmetic. New unit joins the existing test/sanitizer
path. Existing writable tests gain only the appended NULL initializer.

The existing ordinary-source fileprobe adds a bounded line case (including
binary bytes, blank lines, final unterminated bytes and a growing 300-byte
record), then a real child task reads its own FILE and stdin while the parent
holds both borrows. Foreign FILE rejection, initially empty child cache,
independent cache identity and preserved parent bytes are asserted. No new
program registry slot is consumed. Native ownership tests prime two dynamic
buffers plus stdin storage and observe every resulting allocation's release
inside successful exec replacement, after exit before wait/reap, and at live
kernel teardown. Failed lookup/preparation exec preserves them and existing
fd/CLOEXEC assertions remain active.

Focused `make -j4 LDLIBS=-lucontext test` passed in a network-disabled container
using `tribblix-woodpecker-agent:3.18.0` with an explicit shell entrypoint on
biggie. The first focused run exposed a test injection index error (the second
stdin failure hit the object again rather than its buffer); the index was
corrected and the complete run passed. No runtime diagnostic was suppressed.
Final full-gate results are reported with the selected commit in the handoff.

Local build-parity and acceptance-output checks pass. The Mac suite preserves
84 previous cases and adds fileprobe line-probe: 85 records, 2167 bytes including
NUL. All 24 protocol tests pass, including rejection of the previous 84-record
transcript. Existing historical transcript controls still remove all later
cases rather than weakening their expected old counts. These checks do not
execute a Mac guest.

Only the pinned obsolete fixture remains imported; its hash is unchanged
`bd271ac5943202280d9f3dd6f22f5776a835789709c12263f9f641e5ff70beff`.
The cached full uniq hash was checked in the design; no full-source command
execution or binary uniq behavior is claimed. Uniq's unchecked NULL/error exit
behavior is unchanged.

The design note's personal worktree path is replaced with a relative sibling
name. The earlier design publication check ran before the new note was tracked
and therefore did not validate it; exact design #557 exposed that omission.
This candidate runs publication after staging all new files. No guest/emulator
control, main merge or Solaris execution occurred. Required exact CI, fresh Mac
and Solaris acceptance remain separate outstanding gates.

Shared symbols touched: cb_libc_fgetln (new), private FILE/line representation,
resolve_stream references, fopen initialization, fclose cleanup, optional
cb_input_state_v1 tail/minimum and task_release_allocations clearing. Existing
output dispatch semantics are retained and tested.
