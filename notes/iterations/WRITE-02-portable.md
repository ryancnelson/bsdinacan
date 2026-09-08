# WRITE-02-portable: bounded console write results

Assigned portable half of reviewed WRITE-02-design `f734884`, based on freshly
fetched accepted main `e65e36fc0444193445b8a304f6e818d2768d5b62`.
Branch: `work/WRITE-02-portable`. The Linux adapter loop is owned separately;
this feature changes no host_linux code, runtime ABI, registry capacity, or
upstream utility source.

## Contract and change

Descriptor/count/non-NULL validation remains in api_write before dispatch.
At the console boundary, a valid zero-count write returns zero without calling
the host or changing incoming task errno. A nonempty host result must be a
positive count no greater than requested, or a negative errno magnitude that
fits int. Zero progress and positive over-return become -1/EIO. Malformed
negative magnitudes are checked against -INT_MAX before negation or narrowing,
so INT64_MIN cannot cause signed-overflow undefined behavior. Valid positive
partial results and negative host errors retain their prior meaning.

The contract is bounded blocking console output. RAMFS/pipe write semantics
remain unchanged. This boundary cannot preempt an arbitrary host callback that
never returns; the separate Linux-loop fix remains necessary. It adds neither
signals nor a general nonblocking-I/O policy.

## Actual red then green

A new shared test first ran against unchanged core code in the pinned
`tribblix-woodpecker-agent:3.18.0` Linux container (Alpine 3.22.5, GCC 14.2,
Clang 20.1.8), network disabled:

```sh
make LDLIBS=-lucontext build/test_core
build/test_core --console-write
```

The build succeeded. A checked real task issued a nonempty write to a finite
fake host returning zero. Before the fix, the test executable exited 1 with
`FAIL: portable console write probe status 11`; the task received the old
zero result rather than -1/EIO. The helper had already checked tracked payload
release before kernel destruction and complete context destruction, so setup
or cleanup failure was not substituted for this behavioral red.

Pre-fix core SHA256:
`b6cee976b8cf1d836e01ee67364241a6bab4a7c4e5c298202d358daf422ec70f`.
Red source archive SHA256:
`7756d7d6e5db617873d2b36b76fa4a4675cc542d46a8753b38f157d6b477a022`.
After the bounded core change, the same command passed, exit 0, printing
`portable console write tests passed`. Fixed core SHA256:
`0379574b65f46aef059e94d267715ff841f50d6c0f93763ef4410a6aecfc9012`.
The test source stayed unchanged, SHA256:
`ea3682f4991c48fea369dd231c6c85d83889fae85e748eb9027e315e4201d0c9`.

## Shared test and lifecycle boundary

The helper creates a temporary kernel using real platform context operations
and a finite fake console callback. It checks registration, boot, actual task
entry/exit, one tracked payload release before teardown, and destruction of
all root/child contexts. It runs only on the host/root stack, outside any live
kernel; the temporary kernel is destroyed before regular Mac cases begin.
No permanent probe is added to the ordinary program registry.

The real task checks zero progress, exact and positive-partial results, immediate
and above-UINT32_MAX over-returns, EPIPE, -1, -INT_MAX, one value beyond that
negative bound, and INT64_MIN. Callback stream, buffer identity, request count
and single-call bounds are asserted. Valid empty writes with unusable/NULL
buffers make no callback and preserve errno; invalid/read-only/closed descriptor
errors still precede zero handling. NULL nonempty and unrepresentable counts
retain existing EINVAL. A later successful stderr write checks recovery and
stream routing after all error cases.

Linux shared acceptance and Mac main invoke this same helper. The 65 existing
records and acceptance_cases.def remain unchanged. A standalone
`PASS consolewrite` after contexts makes **66 PASS records plus ALL PASS**;
host staging expects exactly this new transcript. There is no nested running
kernel or manual UI action in this test.

## Validation state

`python3 -B tests/test_mac_guest.py`: 18 tests passed.
Full `make LDLIBS=-lucontext SANITIZE_CC=clang ci`: passed, including
native/shared cases, ASAN/UBSAN, architecture/source boundaries, publication,
protocol checks, clean build-mode isolation and GCC static analysis.
Exact Woodpecker and coordinator-owned fresh Mac acceptance: pending. No guest
execution, throughput, or host-adapter progress guarantee is claimed here.
