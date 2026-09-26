# SIG-01: cooperative internal interrupt core

Base: `206bfe4cb4bbe355faf29d214016b71927715430` (fresh accepted main).
This assigned core phase follows SIG-01-design and the independently reviewed
matrix `3594f9b`. It excludes the private signal.h veneer, public API setter,
tee import, host signals, arbitrary handlers and keyboard routing.

## Contract and implementation

Each task owns a default/ignore disposition and one coalesced pending bit.
Internal `cb_kernel_request_interrupt(kernel, pid)` queues only the proposed
interrupt; `cb_task_set_interrupt(task, disposition, previous)` changes only
supported integer dispositions and preserves task errno. Neither is a public
kill/signal interface. Missing or zombie/dead PIDs return `-CB_ENOENT`, missing
kernel returns `-CB_EINVAL`, and unsupported executors return `-CB_ENOSYS`.
Invalid setter arguments reject without state mutation. Selecting ignore clears
pending delivery, and ignored requests do not wake blocked tasks.

The optional executor capability is appended after the frozen original prefix.
Registration still accepts that original allocation size; capability lookup
checks version and the actual appended field boundary before reading it. Native
execution explicitly opts in. Existing delegating fixtures remain opted out;
only the tested signal fixture wrapper opts in explicitly. No public ABI layout
changes in this phase.

Delivery runs on the target stack inside `native_entry` before the command, or
after `cb_task_yield_as` resumes and reinstalls the current task. Default delivery
clears pending state and uses normal exit with project status 130. This is a raw
project exit status, not a POSIX encoded signal result. Requests never perform
cleanup on the scheduler or allocator stack and never resume EXEC_PENDING's old
successful-exec continuation. Non-yielding code is not preempted.

Spawn copies disposition but not pending state. Successful exec retains these
fields. Unsupported-target exec rejects pending/ignored state before allocation,
and checks again before publication because a serialized allocation adapter may
queue a request. Rejection preserves the old image and unwinds newly prepared
arguments/environment. During exec completion, request validation uses both
current/pending programs until replacement is installed, then the installed
program after pending_program becomes NULL. No native execution object casts
are introduced in the core.

## Falsifiable loop

Hypothesis: a default task blocked in a real pipe read can receive an internal
request, resume on its own stack, release its heap and descriptor references
before reap, and let its parent observe 130. Ignored requests must leave the
operation blocked until real progress and preserve its data.

Validation uses the project's pinned Linux validation image,
`tribblix-woodpecker-agent:3.18.0`, GCC 14.2.0, Clang 20.1.8, and libucontext.
No host signal functions or guest controls are used by the fixture.

- Clean baseline: `make clean test LDLIBS=-lucontext` passed.
- The first fixture registered its boot entry under a name other than `sh`, so
  setup returned 90. This was not behavioral red evidence.
- Corrected new fixture against an explicit ENOSYS request scaffold:
  `make LDLIBS=-lucontext build/test_core` succeeded;
  `build/test_core --signals` exited 1 with `FAIL: signal probe status 11`.
  It had already verified the child was actually BLOCKED_PIPE. This was before
  delivery implementation, not a deliberate regression of an existing feature.
- After implementation, the original probe and the expanded 29-scenario helper
  passed. The final helper also observes an unrelated blocked peer's exact
  allocation and unchanged blocked state, then supplies distinct data and reaps
  it successfully.

The helper covers pipe read/full write, console read, wait and timed poll with
both default and ignore behavior; before-entry and resumed-yield delivery;
coalescing, ignored pending discard and invalid setters; spawn inheritance;
successful/failed exec, allocation failure and late-request rollback; old-size,
absent and unknown executor capabilities; both exec phases; early ordinary exit;
and live-kernel destruction. Old executor tables are genuinely short allocated
objects. Each fixture kernel registers six programs, below the unchanged limit
of 64. Every host/executor callback has a finite budget.

At the normal termination callback, before reap/teardown, the observer checks
actual context identity, disappearance of the tracked target payload, an empty
task allocation list, cleared descriptor slots, exact remaining descriptor
reference counts and intact parent ownership. The blocked-peer case additionally
requires its state and allocation to remain intact. Post-teardown context counts
check that cleanup also completes when the kernel stops with a live child.

Two disposable after-implementation behavioral controls were run against the
same helper (these are not TDD red evidence):

1. Remove only `task_release_allocations(task)` from `api_exit`, leaving later
   reap cleanup intact: focused test exits 1 with status **74**, at the immediate
   ownership observer.
2. Remove only before-entry delivery from `native_entry`: focused test exits 1
   with status **1050**, because the before-entry case runs commands and returns
   the wrong status. The control driver's initial anticipated diagnostic was
   1052; the actual 1050 result was retained and inspected, not weakened into a
   passing test. The source was restored before the final focused rerun.

## Qualification

The helper runs before ordinary kernels in Linux's shared Mac acceptance suite
and in the actual Mac main sequence. The expected transcript adds
`PASS interrupts`, preserving all prior 67 records: **68 PASS records**, 1716
bytes including `ALL PASS`. No permanent registry slot is added.

`python3 -B tests/test_mac_guest.py`: all 18 protocol tests passed. An earlier
unittest-discovery invocation did not execute this script's main-only module
loader and failed with NameError; the documented direct script invocation passed.

`make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed on the final source,
including normal tests, shared Mac helper invocation, sanitizer execution,
build-mode isolation, publication/source boundaries and GCC analyzer. The
restored disposable source passed `build/test_core --signals` again; its core,
executor and probe hashes matched the candidate. The pinned image ID was
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`.

Exact feature CI and Mac/native Solaris qualification remain pending. No
successful signal guest execution or complete cross-platform acceptance is
claimed. TERM-03 remains a separate frozen feature, absent from this base.
