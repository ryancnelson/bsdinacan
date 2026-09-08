# STDIN-01-design: unbuffered read-only streams for head

Design only. Base `b2dc0ce`, branch `work/STDIN-01-design`. No interfaces,
implementation, tests or guest acceptance are claimed here. The coordinator
owns implementation assignments and shared backlog updates.

## Source boundary and current constraints

The pinned NetBSD `usr.bin/head/head.c` at
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` has SHA256
`33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`, independently
rechecked against the audit's saved source. Lines 124–169 use `fopen(path,"r")`,
`fclose`, `stdin`, `getc`, and `fread`; the output branch uses `fwrite` and
`feof(stdout)`. A zero fread result stops input without checking ferror; getc's
EOF result likewise conflates input error and exhaustion. Preserve that upstream
limitation rather than claiming that head diagnoses every input error. Fwrite
belongs to a later, separate task.

Current source inspected: `libc/include/stdio.h`, `include/cannedbsd/libc.h`,
`include/cannedbsd/abi.h`, `libc/cb_libc.c`, `src/internal.h`, and the creation,
read/close, allocation cleanup and exec paths in `src/core.c`. FILE is opaque to
ordinary source. Its current private definition contains only a descriptor;
stdout/stderr identities are global constants, while their error flags live in
`cb_stdio_state_v1` in each task. The output helper validates `sizeof` that
existing state. Extending that structure would accidentally make newly compiled
output functions reject the formerly sufficient state size.

`CB_MAX_FDS` is 64. `cb_ssize_t` is signed 64-bit, while size_t follows the native
compiler ABI; the Mac target is 32-bit. `api_read` rejects counts above
INT64_MAX. The plan uses these real types and their limits, not invented narrow
records or a new integer-model port. Runtime stream I/O uses existing VFS and
descriptor operations, with no host stdio dependency.

## Separate task state, immutable identities, and lookup

Append one optional `input_state_location` callback after the actual API tail at
implementation time (currently `stdio_state_location`). Return a new versioned
`cb_input_state_v1`, containing version/size, stdin EOF/error/closed indicators,
and an opaque pointer to the current task's dynamic-stream list. The runtime
owns this state; libc alone understands the private list nodes. Do not change
`cb_stdio_state_v1`, its accessor or the mandatory libc startup prefix.

New helpers check outer ABI version, the appended field's end and non-NULL
callback, then returned pointer, version and the defined v1 minimum field end.
They save/restore errno around lookup, including a callback that disturbs it.
Future structure additions must not enlarge that frozen minimum. Existing
output behavior must neither require this new callback nor read its state.

Add one immutable global stdin FILE identity for descriptor 0. Its mutable
indicators and logical closed state come from the calling task. Extend the
private FILE wrapper with descriptor, EOF/error flags and a next pointer for
dynamic read-only streams. Allocate these wrappers with the existing tracked
program allocator and publish them only in that task's input list. No new
allocation tracker or runtime dependency on libc's FILE layout is needed.

Resolve stdout/stderr/stdin by pointer equality first. For a dynamic argument,
walk only the current task's known live nodes and compare identities before
reading the supplied pointer's fields. Do not dereference an arbitrary/foreign
FILE pointer or free it when lookup fails. No valid task can own more dynamic
streams than descriptor slots. The core clears the opaque list head before
reclaiming tracked allocations, so it never leaves a traversable dangling list.
The ordinary program must not modify or free a FILE wrapper directly.

List membership is a validity check for live task-owned wrappers, not a promise
to recognize stale pointers after an allocator reuses their address. Use after
fclose, passing a parent FILE pointer to another task, or retaining a dynamic
FILE across exec remains invalid. Test foreign pointers and immediate repeated
close without pretending to solve arbitrary stale-pointer ABA.

## Opening, closing and descriptor ownership

Fopen supports exactly `r` and `rb`, both read-only with identical bytes and no
newline conversion. Reject NULL path/mode or unsupported modes with EINVAL;
never create/truncate or implement update/write modes. Validate capability and
cleanup availability before acquisition. Open through existing read-only API,
then allocate/initialize a wrapper. Publish the node only after both succeed.
An open failure preserves its error and publishes nothing. Wrapper allocation
failure closes the new descriptor exactly once, restores ENOMEM despite cleanup
callback effects, and leaves the list unchanged. A successful fopen preserves
incoming errno. Existing VFS type/permission/descriptor-limit errors propagate.

Fclose of a valid dynamic input stream unlinks it, closes its owned descriptor
once, and releases the tracked wrapper once. It consumes the stream even if
close reports failure, returning EOF with the close error; success returns zero
with incoming errno preserved. No retry may close a newly reused descriptor.
Invalid lookup performs neither close nor release. Fclose(stdin) closes only
the calling task's fd 0 and marks that task's stdin logically closed; it never
frees the global identity. Further input/status operations on that closed stdin
are rejected. This slice rejects fclose(stdout/stderr) with EINVAL and leaves
those descriptors and indicators alone.

The coordinator explicitly selected preservation of SPEC's descriptor policy:

| Transition | Stream state/wrappers | Descriptors |
| --- | --- | --- |
| New task / spawn | Fresh stdin flags, open logical stdin identity, empty dynamic list; no inherited FILE wrappers | Existing descriptor inheritance/actions remain unchanged; referenced open-file offsets may be shared |
| Successful exec | Old dynamic wrappers reclaimed; list and input flags/closed state reset before new entry | Only existing close-on-exec slots close; non-CLOEXEC descriptors remain owned raw descriptors of that task |
| Failed exec, including preparation allocation failure | Original wrappers, list and indicators remain usable | Existing descriptors unchanged |
| libc rebind | Preserve all task stream state | No implicit closes |
| Exit / early kernel destruction | Clear list head and reclaim wrappers with other tracked allocations | Existing fd_close_all releases every task slot exactly once |

Fopen does not silently force CLOEXEC. After successful exec, the task's
ordinary descriptor table owns the retained raw descriptors; the replacement
program may use/close known descriptor numbers, and eventual exit/destruction
closes any remaining slots. Tests must distinguish intentional raw-descriptor
retention from a leaked wrapper. Likewise a spawned child inherits eligible
raw descriptors, not parent stream flags or wrappers. Parent fclose releases
only its own slot/reference. No fdopen, fileno or reopening facility is added.

A dynamic stream exclusively manages its owned descriptor while live. Raw close
or dup2 replacement of that descriptor followed by continued FILE use violates
that ownership contract; the list does not validate descriptor generations.
Do not add a general alias tracker or promise to close the original open-file
object after its slot was externally rebound. Stdin, while logically open,
addresses the task's current fd 0; descriptor actions before entry are supported,
and EOF/error state is task/stream state rather than state in the shared open
file. Closing/rebinding fd 0 does not clear indicators or revive closed stdin.

## Read results, element counts, and error indicators

Use one bounded read helper for getc and fread. It operates on validated stdin
or a dynamic read-only stream, preserves errno on successful progress and clean
EOF, and records a negative descriptor read as sticky input error with the actual
read errno. Zero bytes from a positive-size descriptor read set EOF, not error.
A positive short read alone sets neither flag. Existing error stays sticky across
later successful reads; it does not itself prevent another read attempt. Existing
EOF returns exhaustion without another descriptor call. No clearerr, seek,
ungetc or hidden refill buffer is introduced; reopen/new task/exec supplies a
fresh stream state. Raw read does not set or clear stream flags.

Getc reads one unsigned byte and returns its promotion to int, including 255
for byte 0xff. It returns EOF only for exhaustion, read error or rejected input.
Never sign-extend a char into EOF. The public mapping must evaluate its stream
argument once.

Fread follows this order:

1. If size or nmemb is zero, return zero without dereferencing buffer/stream,
   consulting capabilities, issuing I/O, changing errno or touching indicators.
2. Validate input capability and live stream. Before multiplication, reject
   `size > SIZE_MAX / nmemb` with zero and EOVERFLOW. A NULL buffer for a
   nonzero request is EINVAL. These argument rejections issue no read and do not
   set a valid stream's error/EOF indicators.
3. Accumulate positive reads until size*nmemb bytes, clean zero, or a negative
   result. Limit each request to the smaller of remaining bytes and INT64_MAX,
   using types that cannot narrow the limit on the 32-bit target. Do not treat
   a positive short transfer as EOF; every loop makes progress or stops.
4. Return accumulated bytes divided by size, counting only complete elements.
   A partial final element may have consumed/written some bytes; no fragment is
   buffered for a later call. Tests check the written prefix and untouched
   canaries without claiming an extra completed element.

Examples: size 4/count 2, then 3+5 bytes, returns 2 without flags; 6 bytes then
zero returns 1 with EOF; 6 bytes then EIO returns 1 with error and no new EOF.
A full exact-size read does not predict EOF until a subsequent positive-size
read actually returns zero. No automatic retries after negative results, and
no speculative EINTR/nonblocking policy, belong here.

[NetBSD fread(3)](https://man.netbsd.org/fread.3) documents element counts, zero
requests and overflow. Add the missing bounded error mapping CB_EOVERFLOW=84,
private EOVERFLOW and runtime strerror coverage as part of the fread loop;
[pinned NetBSD errno.h](https://github.com/NetBSD/src/blob/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/sys/sys/errno.h)
confirms that value. Do not silently substitute EINVAL or import the host's
numerical errno value. No numeric-conversion implementation is implied.

## Status queries and preserving the output contract

For valid input, feof returns the task/stream EOF indicator and ferror returns
its independent error indicator, preserving errno in both cases. For known
stdout/stderr, feof returns zero with errno unchanged, without requiring input
state or consulting descriptors. Ferror(stdout/stderr) uses exactly the existing
output state/compatibility path. Thus feof(stdout) after a write error preserves
its errno and does not fabricate EOF for head's existing output-error branch.

A supported input state with NULL/unknown/foreign/closed stream rejects with
EINVAL: getc/fclose return EOF, fread returns zero, feof returns zero and ferror
returns nonzero. A known output passed to getc/fread/fclose is unsupported and
gets EINVAL without altering output flags. For a potentially dynamic pointer
when input capability is absent, return ENOSYS without traversing or mutating
anything (NULL can be rejected immediately with EINVAL). Unavailable or malformed
input state similarly rejects stdin operations: fopen NULL, getc/fclose EOF,
fread/feof zero, ferror nonzero, all with ENOSYS. Zero-size fread and feof on
known output identities are the explicit capability-independent exceptions.
These invalid/unavailable-pointer returns are bounded extension diagnostics,
not a claim about every standard library's behavior for invalid FILE pointers.

Keep fflush's current stdout/stderr/NULL support unchanged; it neither traverses
input lists nor acquires an input-state dependency. Input streams remain invalid
for fflush/fprintf in this slice. Existing output functions, including older
runtimes without input support, retain their accepted behavior.

## Small implementation loops and falsifiable acceptance

The following are proposed sequencing boundaries, not self-assigned Ready tasks:

1. Input task state and immutable stdin; getc, feof and input ferror; lifecycle
   reset and independent compatibility checks. Test real task interleaving with
   separate EOF/error states and actual fd-0 actions, byte 0xff, clean EOF vs EIO,
   errno-preserving queries, output feof and unchanged output fallback.
2. Read-only fopen/fclose wrappers and list validation. Test regular files,
   missing/permission/directory failures, mode rejection, descriptor exhaustion,
   allocator payload and bookkeeping failures with immediate close observation,
   two streams in one task, foreign pointer rejection, task inheritance and
   release counts before reap/teardown can hide missing cleanup.
3. Fread and EOVERFLOW. Test zero requests with unusable pointers/no callbacks,
   multiplication overflow on actual target sizes, short-read sequences,
   partial elements at EOF/error, exact bytes/counts/callbacks and saved errno.
   Reuse the validated lookup and state transitions, then run the full gate.

For each implementation loop, use old API allocations ending before the new
field, NULL callback, NULL returned state, valid-version short state, and
full-size wrong-version state independently. Prove rejection happens before
open/read/close/allocation, and restoring the full binding recovers an existing
stream with unchanged flags/list. Also prove the original-sized output state
still supports putchar/fflush/ferror when input capability is absent. Keep every
accepted command/source test and the 64-slot registry; use scoped fixtures.

Lifecycle probes must observe wrapper release and list invalidation inside the
replacement entry, fd retention for non-CLOEXEC and closure for CLOEXEC, and
unchanged wrappers after failed exec. Observe exit cleanup before wait and live
kernel cleanup directly. Do not pass a dead FILE to ordinary code after exec
and mistake pointer dereferencing for a valid lifetime test. Independent mock
reads verify sticky flags/callback counts; shared ordinary probes exercise real
RAMFS files and pipes on native and Mac. Exact CI and coordinator-run guest
evidence are required before calling each runtime loop accepted.

Design validation: source review and publication/diff checks only; exact docs
CI #262 passed all three checks on `5fcc687`. Independent review found no
design blocker; the design is included in accepted integration `6f860c4`. Guest execution is not required for this documentation-only change,
and no guest result or input-stream implementation is claimed.
