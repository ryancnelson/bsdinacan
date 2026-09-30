# TEE-01 — unchanged tee with execution-local output lists

Base: `461504d12f88f7a00608ddd24f851cb41e90c96c`, branch `work/TEE-01-finish`.
The coordinator explicitly assigned this candidate base for development only.
It is not a claim that ls or the combined sprint has passed guest qualification.

## Scope and implementation

Import pinned NetBSD `usr.bin/tee/tee.c` byte-for-byte at revision
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`, SHA-256
`ebcf5dcb07756634ba5876630e2567bb7eb318c489a2a8a6c10fb6919f685a53`.
The retained license is the three-clause Regents of the University of California
license. Make and Mac CMake rename `main`, `head`, and `add` to private symbols;
`tests/test_netbsd_source.sh` checks the hash, provenance and private linkage.

The ordinary command module uses the existing libc entry veneer. The existing
state-slot executor now has one tee slot, the typed external `cb_tee_head`
pointer, and explicitly advertises cooperative interrupts. This reuses the
accepted generalization of TEE-STATE-01 instead of adding another executor.
Core task allocations own all list nodes and the input buffer; no wrapper
traverses, frees or retains them across exit. No libc symbol, public ABI field,
program-capacity limit, host adapter or upstream source behavior changes.

## Actual red and green

The first private-header compile during the readiness audit succeeded; that
was feasibility evidence, not a behavioral red. The command was then imported
and initially registered with the bare native executor. A new portable test
ran it repeatedly in the same kernel, checking stdout/files/status and cleanup:

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --tee
```

With bare registration, compilation succeeded and the test failed with
`FAIL: tee probe status 213`, exit 1: the second invocation failed collection
(case 2, local failure 13). The first empty-input invocation had succeeded.
This was observed before adding the production tee state slot. An earlier
signedness warning in the new fixture was a build setup error, not red evidence.

Adding the one-slot executor registration made the same test print
`tee command tests passed`, exit 0. Further positive cases also passed after
adding exact explicit-close checks and both-continuing interleaving.
All runs used an isolated source export in the existing pinned Linux image
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`,
with networking disabled. No shared checkout was compiled or modified.

## Portable command acceptance

`cb_tee_probe(host)` in `tests/tee_probe.c` runs from the host/root stack, with
one temporary kernel and the actual registered tee command. It covers:

- Empty and exact `A\0B` input, simultaneous stdout/two files, an 8193-byte
  input crossing the 8192-byte source buffer, and the same long input through
  controlled short reads. Truncate, append and stdout-only operation are checked.
- Missing parent and directory destinations emit exact diagnostics while a
  valid output still receives all bytes. A missing leaf is successfully created
  with stored mode 0666; this does not claim permission enforcement.
- First-read and post-prefix EIO, positive short writes, EIO and EPIPE that
  drop only the current destination's current-chunk remainder and retry that
  destination on the next read, and a short-write prefix followed by EIO.
- Zero progress is injected only through the real host console callback;
  WRITE-02 converts it to EIO. The callback count is exactly one, with a separate
  finite API-call budget protecting the fixture against accidental retry loops.
- A real successful close followed by an injected EIO checks the command's
  close-error diagnostic/status without pretending the descriptor remains open.
  The explicit close set contains stdout and every successfully opened output.
- Failure of the third command allocation (first file-list node, after the
  file is opened) exercises `err()` exit and core descriptor/heap cleanup.
- Two live tee lists interleave while one real request terminates a default
  peer with status 130 and `tee -i` continues. Ordinary tee preserves inherited
  ignore. A separate pair of ordinary tee tasks both continue and write distinct
  files, detecting state mixing without relying only on one task's termination.

Read/write/close/allocation fault callbacks use the real kernel API table and
dispatch by current program; they never bind libc to a task-stack API copy.
For every child, the controller waits until it is a zombie and observes every
recorded payload and bookkeeping release, the empty allocation list and all
closed descriptors BEFORE `waitpid` or kernel destruction. Repeated cases share
one live kernel, so a later invocation also exercises fresh list initialization.

## Qualification and integration boundary

Focused Linux runs and source/build-parity checks passed. Full Linux CI and the
exact pushed Woodpecker workflows remain pending at this note's initial writing;
final handoff must report their actual results and exact commit. The first full
gate reached the sanitizer source check, which rejected compiler-inserted
`__asan_*`/`__ubsan_*` symbols. The source fence now permits those instrumentation
symbols while still requiring every application dependency to be private.

The Linux full suite and `--tee` execute the portable helper. Mac CMake compiles
it, but this branch deliberately does not edit Mac main, acceptance cases or
transcript expectations, which belong to the separate milestone worker.
The coordinator must wire `cb_tee_probe(cb_mac_host_ops())` into an appropriate
host/root acceptance path and qualify the exact combined artifact. Compilation
alone does not establish Mac command execution. No guest or app was operated.
Native Solaris acceptance and final ls qualification remain coordinator gates.
No command or prerequisite is marked Done by this candidate.

Shared symbols added: `cb_tee_main`, `cb_tee_head`, `cb_tee_add`,
`cb_tee_program`, and runtime-private `cb_tee_static_reset_executor`.
The exported test helper is `cb_tee_probe`. Integration touches in
`tests/test_core.c` are one focused `--tee` dispatch and one helper call in the
Linux shared-acceptance runner; preserve parallel milestone changes when merging.

## Recovery checkpoint

The finishing worker preserved the original uncommitted candidate and copied
only its tracked diff and task files into a fresh worktree at the same base.
Production and probe review found no additional implementation change necessary.
Exact pushed CI evidence is reported by the finishing handoff; no Mac or Solaris
guest execution is claimed by this branch.
