# FGETLN-01-design: borrowed lines for unchanged pinned uniq

Design candidate only. Base is immutable
`7faa968fd4bae1c8d33d5b4e784f06f8774b7884` (STDIO-WRITE-01 candidate),
not an acceptance claim for that runtime. Branch `work/FGETLN-01-design`,
sibling worktree `bsdinacan-FGETLN-01-design`. Only this note changes;
no headers, runtime implementation or full uniq import. The coordinator remains
owner of shared FILE representation and dispatch until final review.

## Evidence and scope

Read AGENTS.md, BACKLOG.md, CURRENT-STATE.md, LIBC.md, SPEC.md, UPSTREAM.md,
STDIN-01-design, FWRITE-01-design, ASPRINTF-01-design and the preceding
STDIO-WRITE-01-design from its preserved design worktree. The selected base
already provides r/rb/w/wb FILEs, membership-before-dereference lookup,
read-only resolution, independent stdin flags and task heap reclamation.
`cb_libc_file` in libc/cb_libc.c contains descriptor/eof/error/writable/next;
`cb_input_state_v1` ends at the opaque `input_streams` list. Core clears that
list before releasing allocations. No fgetln declaration or implementation is
present in this base.

The cached complete pinned uniq source was read, not imported. Its SHA256 is
`78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`,
matching the NetBSD pin `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
already recorded for the obsolete-option fixture. In main, each fgetln result
is immediately copied by its reported size into task-owned memory and terminated
there. Uniq does not free the borrowed pointer or retain it across its next
read. It distinguishes records by length plus strcmp on its copies and prints
with %s. Its early NULL return and loop termination do not check ferror;
therefore libc error correctness cannot promise uniq reports read failures.
Binary correctness of libc must be tested separately from uniq's C-string
comparison/output limitations. Full uniq admission and static-state isolation
remain a later assigned task.

Primary references at the same pin:
[fgetln manual](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/stdio/fgetln.3),
[fgetstr](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/stdio/fgetstr.c), and
[getdelim](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/stdio/getdelim.c).
These establish borrowed storage, delimiter inclusion and final unterminated
records. The source returns NULL/zero length on read or allocation failure,
even after partial consumption; EOF after bytes instead returns those bytes.
The local design follows those decisions. It uses the project's existing
EINVAL wrong-direction diagnostic and EOVERFLOW arithmetic diagnostic rather
than claiming identical NetBSD errno choices for every extension.

## Return and lifetime contract

Add private `cb_libc_fgetln(struct cb_libc_file *, size_t *)` returning `char *`
and ordinary stdio mapping/declaration through the coordinator's shared-header
review. For a writable length pointer, set *len to zero before validation.
NULL len returns NULL/EINVAL without any read/allocation/stream mutation.
Reject NULL/output identities with EINVAL; reject foreign, closed or writable
dynamic streams through the existing membership/direction resolver before
caller-pointer dereference. Missing capability returns NULL/ENOSYS before I/O
or allocation. Validation errors do not set a live stream's error indicator.

A successful call returns the next nonempty byte record, including its first
newline if present. A blank line has length one. Embedded NUL and 0xff are
ordinary bytes, and length is authoritative. EOF after bytes returns a final
record without inventing a newline. Empty EOF returns NULL with length zero.
Provide a convenience trailing NUL outside the reported length, but ordinary
callers must not rely on a C-string interpretation. No artificial line-length
limit tied to BUFSIZ, int, or host ssize_t is introduced.

The pointer is owned by the stream's task and must not be freed or resized by
the caller. The caller may change bytes strictly within the returned length.
Its borrowed lifetime ends at the next I/O attempt on that same stream
(successful or failed), fclose, successful exec, task exit or task destruction.
This is a permission/lifetime contract, not a promise that the address changes
or that invalidation can be observed by dereferencing a dead pointer. A later
fgetln may reuse or relocate the allocation. Nonzero getc/getchar/fread and
fgetln never promise preservation of earlier borrowed contents; failed output
attempts on an input stream also grant no continued lifetime. Metadata queries
feof/ferror/fileno, clearerr and setbuf(NULL) are not I/O and preserve the borrow.
Existing zero-size/count fread remains a no-callback no-op, so it does not end
the borrow. Invalid length arguments perform no stream operation. I/O on a
separate FILE, including a peer task's stdin, does not end this borrow.

## Storage, ABI and lifecycle

Use one private line-storage object containing pointer, capacity and current
length. Embed it in each dynamic FILE, initialize it to empty in fopen for both
read and write modes, and use it only for reads. No second ownership list and
no descriptor-keyed or global line cache. Resolve identity once using existing
owned membership and read access. Any extension to the input reference is
coordinated with the FILE owner rather than implementing a parallel resolver.
Dynamic buffers require the existing list-sized input state, not a new enlarged
minimum: their wrapper is private and created by the bound libc itself.

Immutable global stdin stays an identity only. Append a single opaque
`void *stdin_line_storage` to cb_input_state_v1 after input_streams, pointing to
a task-allocated private line-storage object. Use ABI version v1 and the same
input_state_location accessor, with a new field-specific minimum equal to
`offsetof(struct cb_input_state_v1, stdin_line_storage) + sizeof(field)`.
Keep both existing minima unchanged. Check outer API size/accessor, returned
state version and that exact field bound before accessing the append; never
check sizeof(new state) as a substitute for the field boundary. A layout ending
at input_streams still supports dynamic fgetln, existing stdin getc/fread/status,
output operations, fopen/fclose and old stream metadata. Its stdin fgetln alone
returns ENOSYS, with zero length and unchanged flags/list, before allocating or
reading. Do not fall back to a process-global buffer or steal storage from an
unrelated existing ABI field. Partially present appended pointer is unavailable.

Core initializes the appended pointer NULL and clears it, alongside the stream
list, before task heap release. Core never traverses private line objects.
New tasks inherit descriptors under existing policy but start with independent
empty stream state and caches. Successful exec invalidates all borrows, resets
these references and reclaims wrapper/storage/buffer allocations while retaining
non-CLOEXEC descriptors as SPEC requires; failed exec preserves them. Exit
reclaims before wait/reap; early destruction also reclaims. No cache pointer
may survive heap reclamation.

Dynamic fclose unlinks first, closes once, releases line buffer then wrapper,
and preserves close errno even if release disturbs it. Release occurs even on
close failure; no retry or still-live FILE claim. stdin fclose marks closure,
detaches and releases its private object and buffer if the append is available,
then follows the existing close(0) policy, preserving its result errno. Old-size
stdin close remains unchanged and never reads beyond its allocation. Explicit
release removes allocations from existing task accounting, preventing later
double release. Output fclose retains its existing independent state contract.
No general stale-pointer detection after allocator address reuse is promised.

## Allocation, progress and errors

A small unbuffered implementation reads one byte at a time through the existing
checked read_input path. This avoids consuming bytes past a newline and avoids
inventing a read-ahead/ungetc subsystem that would change getc/fread/raw fd
interactions. It can yield inside read: references and counters stay on the
current task stack; the storage belongs to that task across interleaving.
Peers cannot find or close its dynamic FILE. Performance optimization is a
later measured change, not part of this contract.

Lazily allocate stdin's private storage object and each line buffer via task
allocate/resize/release, never host malloc. Retain capacity across calls; no
allocation is needed for EOF already remembered. Reserve room for the next
byte and final NUL before consuming the next byte. Start with a small bounded
capacity, grow geometrically with checked arithmetic, and clamp an overflowing
geometric step to the checked required size. Never overwrite the old pointer
until resize succeeds. Check length + 2 before addition; overflow returns
NULL/zero length/EOVERFLOW and sticky stream error without a further read.
The largest representable returned length is SIZE_MAX-1 because of the trailing
NUL; attempting a further byte fails honestly. Do not impose NetBSD getdelim's
signed-return restriction on this size_t interface.

Initial or later allocation failure returns NULL/zero length with allocator
failure errno (ENOMEM for ordinary exhaustion) and sticky error. Keep any old
allocation owned and usable after clearerr/retry, but discard the current
partial record logically; do not publish it or replay consumed bytes. A retry
continues from the current descriptor offset. If creating stdin's object
succeeds but allocating its initial buffer fails, retain the empty object for
retry and reclaim it normally; no detached allocation leak.

Positive one-byte reads append exactly once. Oversized callback results become
EIO through read_input before narrowing. Negative reads stop immediately with
actual callback errno and sticky error, return NULL/zero length even after
bytes, and do not retry EINTR implicitly. Partial bytes already read remain
consumed; EOF stays clear unless independently previously set. Zero read sets
sticky EOF: return the accumulated record if nonempty, otherwise NULL/zero
length. A newline ends the call without an extra read, so the last newline
alone need not set EOF. Successful line return and clean EOF preserve incoming
errno despite accessors/allocator/successful callbacks disturbing it. Errors
preserve their intended errno through cleanup. Existing error flags stay set
on later successful reads; clearerr clears indicators only, not offsets,
capacity or returned bytes. As with existing getc/fread, a previous sticky error
does not itself prevent a later read; sticky EOF does until clearerr.

## Falsifiable implementation and unchanged-consumer plan

1. An ordinary private-header probe on the selected runtime must fail to compile
   or link for missing fgetln before implementation. Record exact diagnostics;
   setup failure is not red evidence. After implementation require blank lines,
   several lines, empty input, final unterminated line, newline-only ending,
   binary NUL/0xff, long growing lines and exact reported bytes/lengths. Verify
   no byte beyond newline is consumed by mixing fgetln, getc and fread on a real
   RAMFS file/pipe. Test convenience terminator separately from record length.
2. Mock reads cover byte progression, EOF before/after bytes, error before/after
   bytes, oversized result, sticky errors followed by success, sticky EOF and
   clearerr recovery. Assert offset, callbacks, zero length on error, errno and
   flags. Inject storage-object allocation, initial buffer and resize failures;
   observe allocation counts and descriptor offsets immediately, including
   retry and close. Exercise checked arithmetic with synthetic size counts
   instead of multi-gigabyte fixtures, paired with real growth-path tests.
3. Cover old API ending before accessor, NULL accessor, NULL state, wrong ABI,
   stage-one state, list-only state and partially present append independently
   using actual short allocations and canaries. Old stdin fgetln rejects before
   callbacks; list-only dynamic fgetln succeeds. Restoring a valid binding must
   recover unchanged stream/list/flags. All accepted output and old stdin APIs
   remain covered. Reject NULL len/stream, foreign and writable FILEs without
   read/allocation. Preserve zero fread behavior with unusable pointers.
4. Hold simultaneous borrowed lines from two dynamic FILEs and stdin in two
   interleaved tasks; require independent exact contents after peer I/O and
   yielding callbacks. Copy bytes before own next I/O to test invalidation
   without use-after-free. Exercise modifications within the returned size and
   next-line replacement. Close failure and ordinary close must release buffer
   plus wrapper immediately. Observe exit cleanup before reap, exec cleanup
   inside replacement entry with retained non-CLOEXEC fd, CLOEXEC closure,
   failed-exec preservation, and early kernel teardown.
5. Preserve the full uniq source hash above. For this prerequisite use a bounded
   licensed exact-source fixture of the actual fgetln/copy sequence, recording
   its extracted-range hash, or defer command-level execution to the separately
   assigned full import. Do not fake missing interfaces or import full uniq in
   this design. Later unchanged uniq acceptance covers default/-c/-d/-u,
   -f/-s, legacy options, empty and unterminated inputs, adjacent versus separated
   duplicates, varying long records, stdin/file input and file output. Its
   unchecked read-error exit behavior must be recorded honestly; binary libc
   tests do not imply binary uniq acceptance.
6. Run focused tests and full Linux make ci, independent review, all three exact
   Woodpecker workflows, then coordinator-serialized fresh exact-artifact Mac
   guest qualification with the ordinary fgetln probe added to the shared suite.
   Required Solaris qualification remains pending until actually observed.
   Design documentation itself requires diff/publication checks; no guest run.

## Validation of this preparation

Source and cached uniq hash inspection only; no implementation red/green,
runtime execution, complete local gate or guest acceptance is claimed.
The worker ran `make check-publication` before the note was tracked, so that
run did not cover this file. The coordinator subsequently caught and removed
a personal worktree path, then reran publication on the tracked composition.
`git diff --check` is also required.
Exact commit/CI status is reported in the handoff rather than guessed here.
No shared symbols touched. No merge performed.
