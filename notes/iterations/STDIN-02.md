# STDIN-02: read-only fopen/fclose ownership

Base: freshly fetched main `27bb7be462f500c99c2fa05fb42c90838da65cab`.
Branch: `work/STDIN-02`. Assigned stage 2 of reviewed STDIN-01-design;
no `fread`, buffering, write/update modes, fdopen, or fileno are introduced.
The base had coordinator-verified exact CI and 56-record Mac acceptance.

## Change and bounded contract

`fopen` supports exactly `r` and `rb`, both using the existing read-only VFS
open operation. Its wrapper is allocated with the tracked task allocator and
published only after acquisition succeeds. Open errors propagate; wrapper
allocation failure closes the new descriptor immediately and restores ENOMEM
even when the close callback changes errno. Success preserves incoming errno.

The input state appends `stdin_closed` and an opaque dynamic-list pointer. The
stage 1 minimum stays frozen through `stdin_error`; a separate stage 2 minimum
covers the list pointer. Old stage 1 objects continue to support stdin reads and
status. Ownership operations and dynamic streams reject incomplete capability
before I/O or allocation. Original output state/accessor behavior is unchanged.
Only libc knows the wrapper layout. A supplied dynamic pointer is compared
against the calling task's owned list before dereferencing it; wrappers and
indicators are not inherited by child tasks.

`fclose` unlinks a dynamic stream, closes its descriptor once, and releases its
wrapper once. A close error consumes the stream and preserves the close errno;
retry cannot close a descriptor that has since been reused. Closing stdin marks
only the current task's identity closed, does not free the global identity,
and raw rebinding does not revive it. Closing stdout/stderr is unsupported.
Valid input reads/status now work on dynamic streams as well as stdin.

The core clears the opaque list before any tracked-allocation reclamation.
Successful exec resets logical stdin state and reclaims wrappers while retaining
non-CLOEXEC descriptors as raw task-owned descriptors; CLOEXEC descriptors close
under the existing policy. Failed exec leaves wrappers, indicators and
references intact. Exit closes task descriptors and releases wrappers before
reap; early destruction does the same for live tasks. There is no forced
CLOEXEC or new resource tracker. Direct free/resize of a FILE, stale pointers
after allocator address reuse, and raw replacement of a live dynamic stream's
owned descriptor remain outside the supported ownership contract.

## Focused tests and regression control

Initial source archive SHA-256:
`c99cfb929b3991d1a0f53805b955c8fc0a25e2f4cdd30b029762c7b6cb6ffe82`.
The disposable Linux environment used `tribblix-woodpecker-agent:3.18.0`
(Alpine 3.22.5, GCC 14.2, Clang 20.1.8), with network disabled during testing.

```sh
make LDLIBS=-lucontext build/test_core
./build/test_core --file
```

The focused suite returned zero and printed `file ownership tests passed`.
The falsifiable cleanup hypothesis is that the list must already be NULL when
an executor change releases its first old wrapper, before replacement entry,
wait/reap, or eventual destruction can hide missing cleanup. A separate
snapshot removes only the core's list-clear statement and runs that same
focused command. This is an after-implementation regression control, not a
claim that the test preceded implementation. The negative executable exited 1
with `FAIL: file ownership list not cleared before wrapper release`. Good
`src/core.c` SHA-256:
`fe849b9a26d3e09efb499093baff96b8324e15b30065758ec73f1da9fdd87fcf`;
negative SHA-256:
`2a16704a896f111ef63ae21e2e3e7314e7a5a13b53eadc71a02b2cfcacc8fed6`.
The worker source remained unchanged.

Native tests use quarantined allocation observers and a fresh kernel per case.
They check immediate descriptor and payload/bookkeeping rollback, unchanged
existing list and allocation ownership, and close-error consumption before a
raw descriptor is reused. EACCES comes from an injected VFS node open failure;
these tests do not claim RAMFS mode-bit enforcement. Descriptor exhaustion uses
all 64 real slots and verifies no wrapper allocation and no leaked open-file
object. Lifecycle checks observe wrapper releases inside replacement entry,
raw versus CLOEXEC descriptors, seven actual failed exec preparation allocations,
exit cleanup before wait, and live kernel destruction. Read injection tests
sticky dynamic-stream error/EOF independently of descriptor ownership.

The shared ordinary probe checks independent r/rb offsets, bytes 0/255/65,
EOF/status, mode and path errors, foreign-pointer rejection, failed exec,
child inheritance, closed stdin rebinding, and successful exec with a retained
raw descriptor. Shared compatibility tests use actual short allocations,
separate absent outer field/null callback/null result/wrong returned version
cases, old stage 1 and partial stage 2 objects, no acquisition on rejection,
and safe restoration of the full binding while preserving a live stream.
Mandatory cleanup callback absence rejects at libc startup before acquisition.
Private source/symbol fences require the new ordinary calls to use the veneer.

All 56 prior Mac records are retained. `fileprobe` and `filecompat` bring the
expected shared transcript to **58 PASS records plus ALL PASS**. Both ordinary
objects and modules are linked and checked at registration in the 32-bit Mac
application. The native shared fixture stays scoped; the production 64-program
limit and the nearly full general fixture are unchanged.

## Validation state

`python3 -B tests/test_mac_guest.py`: 18 tests passed.
Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed, including
native/shared command tests, ASAN/UBSAN, source boundaries, publication hygiene,
clean build-mode isolation, protocol checks, and GCC static analysis.
Exact feature Woodpecker and coordinator-owned exact-artifact guest acceptance:
pending. No guest run is claimed by this worker.

## Coordinator acceptance

Reviewed worker `b23cc0cc02117999aa18f014ba87208139232110` is integrated with
accepted strcpy at `0e35e50ba91bd78284b3333dcf4ffb75441637cf`. Runtime/API and
new file tests match the reviewed worker; build/test conflict resolution
retains all existing cases. Exact Woodpecker #291 passed all three checks.
Fresh guest `run-q3vxq533` passed 59 records in 22.93 seconds from cold launch
through screenshot and normal shutdown. Receipt, transcript and screenshot
were inspected; both disks were closed and the slot released. Archive SHA256:
`3cd475ba9b847a50db996f4f34c040a8dcfa9bfd2a1032142e6262752b837fba`.
Merged to main; fread remains the next distinct task.
