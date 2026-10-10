# UNIQ-STATE-01-design — isolate the pinned uniq execution state

Documentation only, based on freshly fetched main
`d80eec8` (UNIQ-ADMISSION-01). This note owns no source, header, import,
registration or runtime behavior. Full UNIQ-01 remains blocked on prerequisite
and ls/tee qualification, including fresh Mac and Solaris acceptance. The runtime
composition inspected below is candidate `39e4528`, not accepted main.

## Source evidence and bounded hypothesis

The complete cached NetBSD `usr.bin/uniq/uniq.c` was inspected and SHA256 checked:
revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, hash
`78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`.
UNIQ-ADMISSION-01 measured strict compilation on the candidate; it did not
measure link closure or execution. NEXT-UTIL-03 and the writable-FILE, fgetln
and asprintf notes remain the dependency evidence.

Hypothesis for the eventual implementation: importing this exact source without
isolating its six file-scope ints leaks flags, comparison offsets and final run
counts between executions and across actual I/O suspension. A fresh execution
must begin with six zeros; each resumed execution must regain its own values.
This is a falsifiable future hypothesis, not a demonstrated runtime failure here.

| Source declaration | Exact C type | Use requiring isolation |
| --- | --- | --- |
| cflag | int | -c assignment; show chooses count output |
| dflag | int | -d assignment; show suppresses singletons |
| uflag | int | -u assignment; show suppresses repeated runs |
| numchars | int | -s/legacy +N assignment; skip comparison prefix |
| numfields | int | -f/legacy -N assignment; skip fields |
| repeats | int | increment equal lines, reset on differing line, final show |

All six have implicit zero initialization. No writable file-scope pointer,
function-local cache or anonymous callback object exists in this source. Local
FILE pointers, line buffers, lengths and getopt iteration live on the native
execution stack. Do not invent additional uniq slots for them. Getopt state,
stdin/FILE state, allocator ownership and program name already belong to the
libc/runtime boundary. Preserve upstream's unchecked fgetln/output errors,
C-string binary limitations, int conversion and repeats arithmetic as separate
characterization concerns; state isolation does not repair them.

## Existing conventions and proposed wiring

Accepted-main head uses `commands/head_module.c`, `cb_libc_start`, a renamed
ordinary main and a 128 KiB stack descriptor because its pinned source has a
65536-byte automatic buffer. It needs no static-state executor. Accepted-main ls
is still the small cannedBSD implementation. Candidate `39e4528` has the pinned
ls and tee implementations: their module descriptors use CB_LIBC_PROGRAM, and
`src/programs.c` registers them individually through static-reset executors.
`src/static_reset.c` supplies the existing shared slot machinery. Its actual tee
implementation uses a typed `struct _list *` slot, superseding the bespoke
executor proposal in TEE-STATE-01-design. Its ls type corrections are documented
by STATICS-TYPES-01; uniq has none of ls's anonymous-type difficulties.

Once a qualified composition containing that machinery is selected, add exactly
six declarations `extern int cb_uniq_cflag`, `cb_uniq_dflag`, `cb_uniq_uflag`,
`cb_uniq_numchars`, `cb_uniq_numfields`, `cb_uniq_repeats` to the runtime-owned
slot registration, with one `{ &symbol, sizeof(symbol) }` entry each. These are
native int objects, not assumed 32-bit cells, packed integers or pointer casts.
Reuse cb_static_reset_ops and its existing common callbacks/capability flag.
A command-specific accessor following cb_tee_static_reset_executor is an internal
registration helper, not a new public ABI operation. No global ownership list,
TLS facility, new state header or dedicated executor wrapper is justified.

The eventual ordinary source remains byte-for-byte pinned under
`upstream/netbsd/usr.bin/uniq/uniq.c`, retaining its license and receiving the
usual UPSTREAM.md hash entry. Compile against private compatibility/libc headers
with strict existing flags and `-Dmain=cb_uniq_main`. As ls demonstrates, renaming
alone cannot change static linkage. Apply precise object-level rename/globalize
operations to each of the six symbols, e.g. `cflag` -> `cb_uniq_cflag`; leave
static helper functions and every other symbol alone. Never use `-Dstatic=`.
Follow ls's trailing `-O0` precedent: optimization can narrow int flag storage
before objcopy makes its address externally visible, invalidating typed slot
copies under ASan. Verify the emitted objects on each compiler/target, including
exactly one expected original symbol, actual storage size and absence of stale
unprefixed dependencies; a missing or optimized-away slot must fail the build.
Objcopy syntax/symbol spelling must use each target's actual supported tools.
The eventual check-statics-types coverage should validate these six declarations
against the source and object layout, without adding speculative type machinery.

Add a cannedBSD-owned commands/uniq_module.c with CB_LIBC_PROGRAM for the renamed
main and the normal default stack budget unless measured stack evidence requires
more. Add its command object and module to native/test links and applicable Mac
and Solaris build recipes; register the descriptor through the uniq static-reset
executor in programs.c. Preserve architecture and build-parity gates. Ordinary
uniq includes only ordinary private headers; src/static_reset.c alone sees
runtime-private execution state. This design changes none of those files.

## Execution, argv and cleanup ownership

The existing wrapper creates runtime-owned sidecar, inner native execution and
saved bytes; captures per-program compiled defaults; seeds every fresh saved
copy from defaults. Creation failure unwinds the allocations it acquired without
resetting another live task's slots. Defaults acquired for the program remain
program-owned until program destruction. Before native start/resume, copy the
execution's saved values into the six live ints. On return, save only a live
task's state and restore compiled defaults before the scheduler runs a peer.
Suspend and request_termination delegate directly to cb_native_executor, never
through inner->executor (which would recurse). Preserve cooperative-interrupt
capability. Instance destruction releases native context, saved bytes and sidecar;
program destruction releases defaults and native program. No task-owned buffer
is freed by this wrapper.

Successful exec replaces the old execution with a fresh zero-seeded instance;
failed exec preserves saved values, argv and heap. Normal return, err/errx and
exit follow existing task termination. Zombie/dead values are discarded rather
than captured, and defaults are restored at the scheduling boundary. Repeated
kernels in the same host process must therefore capture real defaults too. Test
cleanup at its actual lifetime boundary before reap/teardown can conceal it:
task heap/FILE cleanup at exit, execution storage at destruction, program defaults
at program destruction. Do not assert sidecar disposal at exit if the existing
execution remains until reap.

Uniq calls obsolete before getopt. The original task argv values vector is
mutable and independent from the private owned_argv string ledger in core.c;
obsolete replaces a value pointer, never the ledger. `-3` becomes allocated
`-f3`; `+3` becomes allocated `-s3`. Their asprintf results are task allocations,
unfreed by upstream, reclaimed by existing task heap cleanup on exit/successful
exec. Original argv strings remain owned by the private ledger and are freed
exactly once by argument_vector_destroy. Do not free the current argv entries
as though every entry were an original allocation, or introduce a special argv
allocation registry. Do not reset getopt per yield or modify the borrowed fgetln
buffer ownership. Capture argv input before exec cleanup using existing copying.

## Smallest falsifiable implementation fixtures (not run here)

1. Sequential actual command fixture on input `a\na\nb\n`: run `uniq -c`, then
   default uniq in one kernel. Require exact outputs `   2 a\n   1 b\n` and
   `a\nb\n`, each exit 0. Additionally use all-equal `a\na\n` twice with -c:
   require `   2 a\n` twice; the prior final repeats value otherwise inflates the
   next count. Run -d then default, and -u then default on the same mixed input
   to distinguish suppression leakage from count leakage.
2. Offset fixture `x a\ny a\n`: -f 1 must emit `x a\n`, then default must emit
   both lines. Character fixture `xa\nya\n`: -s 1 must emit `xa\n`, then default
   both lines. Repeat using actual obsolete -1/+1; require caller argv contents
   unchanged, task value vector rewritten, and no task allocation remaining at
   exit before reap. Include -- and first operand stopping rewrite. These are
   actual command tests; do not label an extracted obsolete probe full uniq.
3. Minimal conflicting pipeline: feed `a\na\nb\n` through `uniq -c | uniq` and
   require `   2 a\n   1 b\n`. Force producer suspension in a real pipe write
   while downstream default uniq runs, then resume; shared cflag would cause
   downstream to prefix its own counts. Assert spawn/setup, suspension reached,
   complete bytes, every child exit and bounded scheduler completion. Add a
   second interleaved pair with distinct offsets and all-equal -c input, suspending
   inside actual read as well as write callbacks, to expose all six slots.
   Use deterministic mock adapter barriers/small pipe capacity, not timing or
   shell background syntax. Merely running sequential pipelines is insufficient.
4. Creation-failure and lifecycle probe reuses existing runtime test facilities:
   fail sidecar/context/defaults/saved allocation stages, preserve an already
   suspended peer, then inspect cleanup before kernel teardown. Test failed exec
   retention and successful exec fresh defaults with the production wrapper
   and a test-only cooperating entry; uniq itself never calls exec. Repeat a
   kernel after prior exit. Observe heap and runtime allocation classes separately.

Record exact future red diagnostics/bytes and focused green commands in UNIQ-01,
then complete Linux make ci, exact-commit Woodpecker ci/mac68k/mac-automation,
independent review and fresh serialized Mac and Solaris qualification. File/stdin,
output-file, empty/long/unterminated/NUL/numeric and injected-I/O fixtures remain
UNIQ-01 acceptance, not evidence supplied by this design.

Numeric characterization must record each target's actual int/long widths.
The source assigns strtol's long directly into int, checks only the resulting
int for negativity and the end pointer, and ignores ERANGE. Exercise INT_MAX,
INT_MAX+1, LONG_MAX and overflow strings for both -f/-s and legacy forms;
record actual diagnostics/status without promising identical narrowing across
64-bit hosts and classic Mac. repeats++ and repeats+1 can overflow int at
extreme record counts; do not construct infeasible billion-line fixtures or
claim a source arithmetic repair. Inspect/characterize that inherited limit
separately. For lengths, fgetln's SIZE_MAX-1 policy bounds psize+1, while
ordinary allocation exhaustion occurs much earlier. Inject read failure after
a complete line and writable-output failure: pinned main does not check
ferror, ignores fprintf results, never explicitly fclose's its streams and
can return 0. Assert the actual libc error/cleanup evidence separately from
that inherited command status; do not invent an expected nonzero uniq exit.

## Verification here

Source/hash and existing implementation inspection only. No implementation red,
focused runtime green, full runtime gate, command execution or guest artifact is
claimed. Documentation validation: git diff --check and make check-publication
with this note tracked/staged. Documentation-only guest acceptance is not required;
full runtime import qualification remains pending. Shared symbols touched: none.
