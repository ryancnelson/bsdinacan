# COMM-STATE-01 — comm isolation and executable fixture design

Preparation only, from COMM-ADMISSION-01 exact
`4c11e432f6e61eb6dfa50b8e48cbae8f52e06cec`, branch `work/COMM-STATE-01`.
Own note only; no import, registration, runtime/header edit, execution or guest
operation. The coordinator owns the queue and full-candidate authorization.
Pinned comm.c at NetBSD revision b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c
has SHA256 b2e98757c0e188d9abfe57aff2b12ed0ac50ee0ec47e57188bda6c3b18ff6eb2.
COMM-ADMISSION-01 proves unchanged private object/native archive linking only.

## State and adapter decision

Hypothesis for a later implementation: the ordinary CB_LIBC_PROGRAM adapter and
existing native executor suffice; no command static-reset slots are needed.
The sole static data is tabs[]: three pointers to string literals, never changed
by this source. Source-local flags, comparison pointer, FILE pointers, read/done
flags, two line buffers and selected column pointers live on each task stack.
Do not make the tabs array writable per-task or globalize it merely because nm
labels it local data. The existing ls/tee wrappers isolate source-written static
state; they are not evidence for adding such state to comm.

Use unchanged import, a cannedBSD-owned module with main rename and public libc
adapter header, and the ordinary build wiring. The source gets familiar private
headers, never runtime-private headers. No new ABI, API, locale or stream surface
is justified. The driver/probe may inspect existing internal task state for
cleanup and scheduler barriers, keeping this instrumentation out of the command.
Use a fresh dedicated kernel/fixture registry when its capacity is needed.

cb_libc_start binds the API and calls main. comm always calls exit, including
usage and err paths; restoration placed after cb_libc_start is unreachable.
Production task switching must preserve the existing binding convention. Any
fault wrapper must route every exit through an override that rebinds the real
API before real->exit, with storage valid until all observations finish. Limit
fault wrappers to one sequential child unless their own state is demonstrably
per-task and suspension-safe. Successful real ordinary invocations and actual
pipe children must not all run through a synthetic failure wrapper.

## Exact column fixture table

For regular files A="a\nc\ne\n", B="b\nc\nd\n", assert stdout below,
empty stderr and child status 0. Escapes represent literal bytes. This covers
both asymmetric comparisons and tail draining. Suppressed columns remove their
tab positions; options do not merely filter a fixed-width output.

| Options | Exact stdout |
| --- | --- |
| none | `a\n\tb\n\t\tc\n\td\ne\n` |
| -1 | `b\n\tc\nd\n` |
| -2 | `a\n\tc\ne\n` |
| -3 | `a\n\tb\n\td\ne\n` |
| -12 | `c\n` |
| -13 | `b\nd\n` |
| -23 | `a\ne\n` |
| -123 | empty |

Run each suppression form followed by default with the same files. Repeat mixed
ordering `-3 -1`, `-1 -3`, and clustered forms; repeat common duplicate records
A="a\na\nc\n", B="a\na\nb\n". Pairwise equal records are consumed, not
collapsed into a set. Empty/empty outputs nothing; empty/A outputs tab-prefixed
A in default, A/empty outputs A unprefixed. Add both tail directions with two
remaining records so show() must read beyond its initial buffer.

For -f, A="A\nC\n", B="a\nb\n": expected
"\t\tA\n\tb\nC\n", status 0. Equal comparisons print the first file's
spelling. Default on those bytes instead produces "A\nC\n\ta\n\tb\n".
Follow -f with default and interleave them. Add punctuation/high-byte cases
whose unsigned ASCII-folded ordering is derived explicitly from the reviewed
C/POSIX helper; no host locale oracle or unsupported-locale promise. Inputs are
sorted for the selected comparator. An unsorted characterization may record the
inherited streaming result; comm performs no sortedness validation.

## Two FILE inputs and stdin aliasing

For two distinct paths, assert both fopen calls actually succeeded, two distinct
input stream nodes/descriptors exist while suspended, and reads advance each
position independently. Passing A twice opens two independent FILEs at offset
zero: default prints all A records in column 3. Parent/peer handles must remain
usable after the child exits. No open count or descriptor number alone proves
ownership; inspect identity/position during a real pause and behavior afterward.

Test `comm - B` with stdin=A and `comm A -` with stdin=B: same default table
result, with exactly one owned fopen input and the task's stdin identity. `-`
is recognized only as an operand; include `-- - B`. For `comm - -`, fp1/fp2 are
the same stdin stream, not two independent copies. With stdin="a\nb\nc\nd\n",
source-derived output is "a\n\tb\nc\n\td\n". With
stdin="a\na\nb\nb\n", it is "\t\ta\n\t\tb\n"; with odd
"a\nb\nc\n", "a\n\tb\nc\n". Require exactly one shared cursor and
no duplicate stream allocation/free. Final unterminated "a" yields "a\n"
for -/-, because the second read sees EOF. These are planned assertions,
not executed results. Failure of the second named fopen after the first succeeds
must reclaim the first FILE and descriptor before zombie reap.

## Deterministic suspension and reentry

Use the existing actual bounded pipe and scheduler states, not timing/sleep.
Read pause: feed stdin a complete first record plus an incomplete next record,
keep its writer open and run until the actual comm child is CB_TASK_BLOCKED_PIPE
with a read wait. For example -f stdin="A\nC" against B="a\nb\n": it
has emitted common A, then blocks reading the unfinished C record. Run a default
peer on the same A/B capitalization files to completion, supply "\n" and EOF,
then assert resumed exact -f output above. Repeat with suppression -13 and an
input arrangement that reaches both comparison and tail paths. Observe real
blocked child, live two-input ownership and bounded scheduler progress; all
setup/spawn/pipe/wait results are mandatory assertions.

Write pause: producer `comm -12 P P` where P has fixed-width sorted unique
numbered records and total output exceeds the actual pipe capacity (currently
4096). Keep downstream reader dormant until the producer demonstrably blocks
writing. Run unrelated default and -f peers to completion, then start an actual
consumer comm comparing that pipe to a file with identical records. Assert both
comm child statuses, exact consumer common-column bytes, empty diagnostics,
finite yields and complete records after the capacity boundary. File inputs
remove ambiguity about whether the producer blocked on read. Choose bounded
record/output storage and drain callback storage without hiding truncation.
A second consumer with mismatched records can exercise retained current-line
state. A fake yielding callback alone is insufficient pipeline evidence.

Run successful invocations immediately after every failure and after a child
closes stdout. A parent stdout write and a concurrently suspended peer's later
write must succeed: stdout_closed/error state is task-owned despite the shared
sentinel FILE pointer. Recreate and run a second kernel in the same process.
Reuse existing generic exec tests for lifecycle support; comm has no exec call,
so do not claim source-driven exec coverage or invent an exec API for it.

## Errors and cleanup observations

Use deterministic callback faults through the real API, count their delivery,
and enforce finite call/yield budgets with a distinct nonzero failure status.
Bound scenario exit codes below 256 so root eight-bit masking cannot convert a
failed fixture to success. Assert root, child and setup results separately.

- Missing first path and missing second path: status 1, empty stdout and exact
  err prefix/path/ENOENT diagnostic for the real task identity. Unknown option
  and wrong operand counts: status 1 and exact getopt/usage bytes as applicable.
- Input EIO before the first byte and after complete/partial records, in each
  named input and stdin: source never checks input ferror, so expected status is
  0 if output succeeds. For A failing immediately versus B="b\nc\n", default
  emits "\tb\n\tc\n". A delivering "a" then EIO versus B="b\n" emits
  "a\n\tb\n". Assert fault hit, sticky input error while observable,
  exact output/empty stderr and inherited EOF-like behavior; verify this on the
  actual candidate rather than treating a desired nonzero read exit as correct.
- stdout EIO/EPIPE before or after a fixed accepted prefix: printf fails, its
  sticky stdout error causes err(1,"stdout"). Require status 1, exact accepted
  prefix and diagnostic, with stderr still functional. Positive short writes
  must finish exactly; inject zero progress through the actual console boundary
  where existing WRITE-02 translates it to EIO, not a raw callback contract that
  bypasses that translation. Check the failure on comparison and show() paths.
- fopen allocation failure at the first and second stream nodes: assert the
  chosen allocator call occurred, truthful err status/diagnostic and earlier
  input cleanup. Do not count an unrelated kernel-creation failure as comm OOM.
- Output-close failure only if the accepted veneer exposes a real reachable
  failure mechanism. Current ordinary stdout fclose marks task-local closure
  and returns success for a usable state, without closing descriptor 1 there.
  Source's ferror || fclose short circuit means it does not call fclose after
  a write error. Do not manufacture an unsupported close-failure API or claim
  that injecting descriptor-close failure exercises this stdout fclose branch.

At zombie observation before waitpid/reap or kernel teardown, require the child
allocation ledger empty, input_state.input_streams NULL, and every child
FD entry's file NULL. During normal live input pause require owned nodes exist;
this control prevents vacuous cleanup success. Where the exit override can
observe it safely before real exit, assert successful stdout_closed versus
write-error short-circuit state. Do not require closed flags zero on a zombie:
cleanup clears resources, while reset semantics concern a fresh execution.
Keep parent/peer stream identity and resources live and functioning. Verify
repeated success after faults and a second kernel. Failed spawn/creation tests
reuse the existing allocator/runtime framework without attributing its outcomes
to comm execution that never began.

## Inherited limits and target requirements

LINE_MAX is 2048, so each automatic line buffer has 2049 bytes. getnextln stores
at most 2048 data bytes, overwrites index 2048 while consuming an overlength
tail, then writes its terminator there. It consumes through newline/EOF; it does
not split long input into multiple records. Test retained lengths 2047, 2048,
2049 and a substantially longer tail followed by a distinct record. Distinct
records differing only after byte 2048 compare equal after truncation; output
contains the retained first-file prefix plus one newline. Test both full and
unterminated boundary records. Empty newline is a record; EOF with zero bytes is
not. Embedded NUL is counted while reading but comparison/printf stop at the
first NUL: A="a\0x\n", B="a\0y\n" prints "\t\ta\n", not a binary
preservation promise. getc high bytes must remain unsigned 0..255, distinct from
EOF. No upstream edit normalizes these behaviors.

The two buffers alone reserve 4098 stack bytes. The existing generic descriptor
requests 64 KiB; retain that initially, subject to actual target evidence rather
than inferring safety from LP64 syntax/link success or inflating a global default.
Measure compiler stack-use/frame output and the deepest comparison/getc/printf/
err call paths on pinned Linux and Retro68 builds; include scheduler/libc frame
costs and available guard/canary evidence. Mac host aligns its stack frame to
four bytes and allocates stack_size+4; its 8 MiB application partition must fit
concurrent tasks, fixtures and output storage. Bounded line/output tests should
use heap/VFS storage rather than large controller automatic arrays. Fresh exact
Mac artifact acceptance and Solaris 9 SPARC ILP32/big-endian build/run gates are
required for a runtime candidate. Record actual target widths and artifacts.
Only measured insufficiency justifies an explicit per-command budget adjustment
like head's existing source-derived allocation; no target sufficiency claimed here.

## Validation and handoff boundary

Source and existing runtime inspection only; no red/green execution claimed.
Doc validation: staged diff check and publication hygiene. Full make ci, hosted
workflows and guest/Solaris runs are not evidence supplied by this design.
Documentation-only guest acceptance is not required. Future full candidate must
establish a meaningful behavioral red/control, strict unchanged import/source
fence, these actual command fixtures, serial Linux full gate, exact all-three
Woodpecker checks, independent review and fresh serialized target qualification.
Shared symbols touched: none. No full comm candidate is authorized by this note.
