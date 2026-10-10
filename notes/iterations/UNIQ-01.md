# UNIQ-01 — unchanged uniq candidate and per-execution state

## Assignment and source

Candidate implementation explicitly assigned by the coordinator under
qualification checkpoint `5865993`, superseding the earlier preparation-only
limit. Base is reviewed prerequisite composition
`39e45282c64904919cbcb50236f034139acc2654`; reviewed state design is `46a4255`.
This is candidate preparation, not runtime integration: ls, tee, quality remain
in that qualification order. Fresh Mac and Solaris acceptance are outstanding.
No reserved guest was operated.

Imported unchanged NetBSD `usr.bin/uniq/uniq.c` from revision
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, SHA256
`78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195`.
UPSTREAM.md records provenance; the license stays intact. All six static fields
are native int declarations and sizeof-based slots in the existing generic
static-reset executor. Ordinary module, strict private-header object, native/test
links and Retro68 source wiring are added. Object globalization names only those
six fields; its helper rejects missing/ambiguous objects and handles repeated
PRE_LINK calls. Both compilers use trailing -O0 following ls's storage-narrowing
precedent. No upstream edit, public ABI field, libc implementation or new header.

## Falsifiable loop

Hypothesis: without task-local saved slots, actual uniq flags/offsets/final repeats
leak to later or suspended peers. The clean prerequisite baseline ran Linux
`make LDLIBS=-lucontext clean test` successfully in the pinned image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`.

Regression red was deliberately measured after initial implementation, in a
separate disposable clone: replace only uniq's registration executor with
cb_native_executor, leaving the imported source and tests intact, then run
`make LDLIBS=-lucontext -j4 build/test_core && build/test_core --uniq`.
Actual failure: `FAIL: uniq probe status 205`, exit 1. Case 2 is default uniq
following -c, whose output contains leaked count prefixes. This is a reverted-fix
regression check, not evidence that the test preceded the implementation. Initial
launcher-path/setup mistakes and an already-rebuilt launcher probe are excluded
from red evidence.

The fixture uses actual pinned command tasks through the real API. Sequential
-c/-d/-u/default, field/character offsets and legacy -1/+1 compare exact bytes.
Original caller argv remains unchanged; legacy replacement allocations follow
existing task heap ownership and the private original-argv ledger. Empty,
long, unterminated, two-file creation/truncation, --, missing input and invalid
numeric operands are covered. Five interleaved pairs suspend inside actual pipe
reads after flags/offsets/repeats have changed, run a conflicting default peer,
then resume and compare each output file. A -c producer fills the actual
4096-byte pipe and is observed blocked before a default peer runs; a real default
uniq consumer then drains the producer and both children must exit 0 with exact
14000-byte output. Setup, block observation, complete bytes, both exits and
bounded controller waits are asserted.

At zombie observation, before waitpid/reap or kernel teardown, each actual uniq
task must have no task allocations and no descriptors left. The whole probe runs
twice in newly created kernels in one host process. Existing generic executor,
argv, heap and FILE tests remain responsible for failed/successful exec and
allocation-stage lifecycle contracts; the unchanged uniq itself never execs.

## Inherited behavior characterized

Uniq ignores fgetln error versus EOF and fprintf failures and never explicitly
closes streams. Injected read error after one complete line emits that line and
returns 0; injected output EIO emits nothing and returns 0. Initial fgetln
allocation failure likewise returns 0 as apparent EOF. Cleanup assertions still
run. Equal-length embedded-NUL records compare as C strings and show emits only
the prefix; this is characterized, not claimed binary correctness.

For field skips, INT_MAX succeeds, INT_MAX+1 differs across target widths:
ILP32 strtol clamps to LONG_MAX with ERANGE which uniq ignores, while LP64 narrows
the long to a negative int and rejects it. The 4294967296 operand narrows to zero
on measured LP64 (both lines remain distinct), but ILP32 clamps and skips the
whole line. Very large overflow strings clamp too: ILP32 accepts a positive skip;
LP64 narrows LONG_MAX to negative and rejects. Fixture expectations account for
actual sizeof(long)/sizeof(int); they do not normalize upstream behavior.
Extreme repeats++/repeats+1 int overflow is an inherited limit, not exercised with
infeasible record counts or silently repaired. fgetln's SIZE_MAX-1 storage bound
is consumed; practical allocation failure remains separate.

## Portable acceptance and qualification

Mac acceptance preserves all 85 previous records and adds the actual portable
uniq probe plus two ordinary sequential/pipeline cases: 88 records total. The
shared capacity expression grows to 2286 bytes including NUL. Historical
protocol fixtures explicitly remove new uniq records when reconstructing old
transcripts, and a previous-85 transcript must be rejected. No prior assertion
or runtime test is removed. Solaris's shared Makefile/core gate consumes the
same command and portable probe; native Solaris execution remains pending.

Verification on the selected commit, independent review and exact Woodpecker
ci/mac68k/mac-automation results will be handed to the coordinator. Fresh Mac
artifact execution/checksum evidence and Solaris are integration blockers;
no runtime merge or cross-platform acceptance is claimed here.

Shared symbols touched: six cb_uniq_* object names plus cb_uniq_main,
cb_uniq_program, internal cb_uniq_static_reset_executor and test cb_uniq_probe.
Existing libc/ABI symbols are consumed unchanged. No shared libc/header owner
was modified; src/internal.h adds only the internal executor accessor prototype.

Owned-file staged diff checking and publication hygiene pass. The full staged
diff check reports only `upstream/netbsd/usr.bin/uniq/uniq.c:132: trailing
whitespace` (an upstream tab-only line). That byte is preserved deliberately;
owned-file diff checking excludes only the hash-verified immutable import.
No broad whitespace rule or source edit is introduced.

The pinned Linux compiler adds __stack_chk_fail to the -O0 object. The source
boundary check permits this measured compiler instrumentation (and existing
ASan/UBSan instrumentation) while rejecting other unprefixed application imports.

Focused Linux green after a fresh clean build: `make clean && make
LDLIBS=-lucontext -j4 build/test_core build/bsdinacan && build/test_core --uniq`
prints `uniq command tests passed`. Tar transfer source SHA was checked against
the local probe/helper. Incremental overlay builds are not used as verification
because remote build timestamps can exceed local source timestamps.
Host protocol: 25 tests pass; build parity and publication hygiene pass.
Final serial full gate and hosted exact-commit workflows remain pending at push.

Offset suspension fixtures also put an equivalent skipped-prefix line in the
resume tail before a distinct line. This requires restoration of the resumed
comparison offset itself, in addition to keeping the default peer isolated.
Fresh Linux focused build/test_core --uniq passes this strengthened fixture.

The root task masks exit status to eight bits. RUN/INTER therefore return their
bounded nonzero scenario IDs rather than id*100+reason: the latter could turn
write-fault case 23, reason 4 (child exit/cleanup mismatch) into 2304 -> 0.
The original red205 above is a historical pre-correction diagnostic for case2;
the corrected selector reports case2 directly and never hides a failed assertion.

Dropped-slot mutation checks each remove one registration from the otherwise
complete adapter. cflag/dflag/uflag/numchars/numfields/repeats produce expected
nonzero scenario diagnostics 2/6/8/12/10/4 and host exit 1; restoring all six
registrations passes. A deliberate child-status mismatch in the write-fault
fixture now reports scenario 23 and host exit 1, directly falsifying the former
2304-to-zero masking path. These mutations are validation-only and uncommitted.
