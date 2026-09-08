# TEE-STATE-01 review repair — synthetic execution-local list state

Base: `70bedbb` (fresh accepted main). Branch: `work/TEE-STATE-01-review`.
This is the coordinator-assigned replacement of the earlier worker fixture,
using the reviewed TEE-STATE-01 design. The other worker branch is preserved.
No upstream tee import, signals, runtime API, or generic TLS mechanism is added.

## Earlier evidence and corrected boundary

Reviewed worker `7277becf5f0e5e50fb2887ce92259e52b6ced0b7` failed exact
Woodpecker #365: Linux rejected unused ownership variables and Mac linking
failed because `cb_tee_state_probe` was absent from CMake sources. Earlier
boot-time failure injection exercised shell creation rather than the wrapped
command. Later aggregate counters miscounted root contexts and expected an
already reaped wrapped command to remain live. Those are fixture/setup
failures, not behavioral TDD evidence.

The replacement is a bounded shared native/Mac synthetic test. Its wrapper
retains the reviewed explicit native-executor delegation and swaps a single
typed list head at scheduler boundaries. No cleanup path traverses list nodes.
The native program owns the wrapper executor pointer; delegation never calls
back through the inner execution's recorded executor. The fixture allocates
three temporary program registrations, preserving `CB_MAX_PROGRAMS == 64`.

The native orchestrator exercises an owner interleaved with a wrapped child
and a non-tee peer, repeated wrapped executions, nonzero `api->exit(17)`, failed
exec with exact `CB_ENOENT` and unchanged state, and successful exec to both
fresh wrapped and native targets. The synthetic nonzero exit tests the common
termination lifecycle; it does not claim another ordinary `err()` import test.

## Actual failure and ownership observations

- Failure injection is armed by an active wrapped owner immediately around
  `spawn`. The host allocator observes the wrapper's real sidecar allocation,
  the delegated native-instance allocation, and its context-creation callback.
  Each of the three failures must actually trigger once, return `ENOMEM`,
  leave the child entry uncalled, preserve the parent's non-NULL head/value,
  and restore every payload/bookkeeping/sidecar/native allocation and context
  count **before** any yield, reap, or kernel destruction. There is no
  allocation-offset guessing or unbounded search for the first success.
- The observer records actual allocation identities and releases separately
  for list payload, task bookkeeping, sidecar and delegated native instance.
  It does not count merely reaching `instance_destroy` as freeing memory.
- `request_termination` requires payload and bookkeeping already released,
  before it delegates the nonreturning suspension. Subsequent reap cannot
  erase a failed observation. The orchestrator checks no wrapped allocations
  remain immediately after every child wait while its own kernel is live.
- Successful exec has a different ordering: old execution destruction occurs
  before old heap cleanup. The observer checks that ordering; the next target
  must see the old payload/bookkeeping gone and a fresh list head.
- A second kernel deliberately finishes its boot shell while a wrapped child
  remains yielded/runnable with one live payload, bookkeeping, sidecar and
  native instance. The root-side check proves that state before destroying
  the kernel. Instance destruction then requires its task heap already freed.
- Each root context is captured once on creation and retained as an identity.
  Destruction only finds/removes existing identities; it never creates a
  context. Root, shell, orchestrator and wrapped contexts are counted separately
  from allocations. Both temporary kernels must end with zero live tracked
  allocations/contexts and exactly one root creation/destruction each.

## Shared execution and protocol

`tests/tee_state_probe.c` is compiled as a runtime-side source in both native
Make and Mac CMake. Native `--tee-state` is focused; the full suite invokes it
before its shared Mac cases. Mac main runs it only from the host/root stack,
before any ordinary case kernel is created, and adds `PASS teestate` to the
strict expected transcript. All **66 prior records remain; total 67**. The
existing guest protocol tests check that total and the additional ordered
record. This code does not drive the guest or alter its slot protocol.

## Validation and limits

The first actual focused build/run of this replacement passed in the pinned
Linux `tribblix-woodpecker-agent:3.18.0` container (Alpine 3.22.5, GCC 14.2),
with network disabled and the source export mounted at `/work`:

```sh
make LDLIBS=-lucontext build/test_core
build/test_core --tee-state
```

All 18 host staging/protocol tests passed with `python3 -B tests/test_mac_guest.py`.
Two deliberate after-implementation behavioral controls are run in a separate
export: replace the wrapper's saved head with NULL at suspension, and separately
remove only `api_exit`'s immediate `task_release_allocations` call. The latter
control deliberately leaves later task destruction intact, testing whether
pre-teardown observation catches a defect that eventual cleanup could hide.
These are regression controls, **not tests observed red before implementation**.
Observed outcomes: lost saved head fails with fixture status **32**, executable
exit 1; delayed exit cleanup fails with observer status **65**, executable exit 1.
Restoring both original files and rebuilding passes again (exit 0). Every
control build succeeded; no source/hash gate was used against the mutants.
The fixture SHA256 is
`629ffd0e3570eff1036c813aad160722036fce53b87cd58c9aedc2c62adba925`;
production core remains unchanged from the base, SHA256
`0379574b65f46aef059e94d267715ff841f50d6c0f93763ef4410a6aecfc9012`.
The exact control edits were `e->saved = head` to `e->saved = NULL` in the
fixture, and removal of the single `task_release_allocations(task)` call in
`api_exit` in the separate export. All commands use the focused recipe above.
Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed (exit 0), including
normal/sanitizer suites, source boundaries, build-mode isolation, publication
checks and analyzer. Final local publication and diff checks passed. Immutable
pushed CI results are recorded at handoff.

No guest result is claimed here. Exact-artifact Mac execution is coordinator
owned. Solaris acceptance remains pending integration under `notes/CI.md`; its
slot belongs to the separately assigned Solaris worker.

## Coordinator integration acceptance

Reviewed integration `1f906a876998567cbe686eeadc454eb31e24c3e9` passed exact
Woodpecker #376 ci, mac68k and mac-automation. Fresh Basilisk II run
`run-on2elfqu` passed all 67 records, including teestate and all prior 66.
Archive SHA256:
`a6ec2bca4d2e53d5bb2bb78c0d75cf361c32dde222aeff2cd3cc08cf0a740745`.
The complete fresh transcript and decoded screenshot were inspected. Receipt
confirms normal application and guest-disk closure; runner confirms slot
release after 25.87 seconds from cold start. This accepts the synthetic state
proof on Linux/Mac, not actual tee or signals. Solaris remains pending
integration under notes/CI.md.
