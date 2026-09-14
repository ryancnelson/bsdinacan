# TERM-03 — isolated canonical input engine

Base: `1f906a876998567cbe686eeadc454eb31e24c3e9`. Explicit coordinator assignment
on `work/TERM-03`; TERM-02 is accepted at `592ae41`, despite the stale blocked
queue entry at this base. The integrator owns shared status changes.

## Hypothesis and bounded implementation

The accepted [terminal design](TERM-01-design.md)'s canonical byte/record/EOF
and echo rules can be implemented without host calls, dynamic allocation,
live console routing, or a new public ABI. `src/terminal.h` and `src/terminal.c`
provide a private caller-owned engine. The existing kernel console owner is not
enlarged or connected to this engine in this stage. Current host editing and
termios ENOSYS fallback remain the live behavior.

Each engine has two CB_PATH_MAX (1024) byte arrays, sixteen record lengths and
three echo bytes plus bounded counters/flags. Defaults are canonical with echo,
CR-to-NL, erase byte 8 and VEOF byte 4. Attribute validation/change and raw mode
belong to TERM-04; no public configurable settings are added here.

A feed consumes one byte or reports backpressure without mutation. Ordinary
bytes past 1023 edit bytes are deliberately consumed/discarded without echo;
erase, NL/translated CR and VEOF still act. A completed edit moves into the FIFO
only when the entire payload and one record descriptor fit. Otherwise it stays
in its array and blocks ingestion. Reads take at most one record, retain short
read suffixes, consume an empty VEOF once, and do not consume anything for a
zero-length read. Explicit physical EOF commits unfinished input once, retains
queued records/events, and stays readable after they drain. An EOF notification
can preserve an outstanding echo suffix; it is not a host read operation.

Echo acknowledgment accepts actual byte counts, including zero without mutation;
an over-ack rejects without changing input or echo. The suffix gates every new
feed until fully acknowledged. This pure operation accepts no host error code
and makes no promise about scheduling or output waits. WRITE-02's existing
nonempty zero-write EIO policy is unchanged; any future leased backpressure
translation belongs to the separately reviewed service/scheduler stage.

There is no allocated state or destructor to hide cleanup: caller-owned engines
have no host resource owner. Tests inspect bytes, records, offsets, readiness,
backpressure and invariants immediately after transitions. Two interleaved
engine objects verify isolation without static mutable state.

## Executed tests

Pinned Linux image: `tribblix-woodpecker-agent:3.18.0` (Alpine 3.22.5, GCC 14.2),
network disabled, source exported to `/work`. Baseline before edits:

```sh
make clean test LDLIBS=-lucontext
```

Baseline passed (exit 0). The initial new basic-line test compiled against an
explicit inert engine scaffold before implementation. Actual behavioral red:

```sh
make LDLIBS=-lucontext build/test_core
build/test_core --terminal-engine
```

Build exit 0; test printed `FAIL: terminal engine probe status 11`, exit 1:
feeding the first byte did not report consumption. This is a new-module
scaffold failure, not a regression in preexisting live console behavior.
After implementation, the same focused command passed (exit 0), including
expanded boundaries: both 1022/1023 line lengths, overflow/erase/NL/CR/VEOF,
partial record reads, zero reads before EOF events, empty/consecutive VEOF,
physical EOF behind full storage, NUL/255 bytes, full 2048-byte payload,
sixteen records and a pending event, ring wrap, echo one-byte/zero/invalid
acknowledgments, and interleaved owners.

The identical pure-state helper is wired into the native full acceptance path
and Mac main before any ordinary kernel boot. `PASS terminalengine` adds one
record to the prior 67, for **68 expected records**, without adding a registered
program or changing the fixed 64-program capacity. Protocol tests passed:
`python3 -B tests/test_mac_guest.py` (18 tests). The new guest case will prove
this deterministic engine on 32-bit Mac; it will not prove raw host input.

Review identified a native incremental-build gap: both all-source binary targets
now depend explicitly on `src/terminal.h`. A disposable build proved it:

```sh
make LDLIBS=-lucontext build/bsdinacan build/test_core
touch src/terminal.h
make -q LDLIBS=-lucontext build/bsdinacan build/test_core
# Install corrected Makefile, then repeat make -q and rebuild both targets.
```

With the old dependency list, `make -q` incorrectly returned 0. With the fix it
returned 1, both binaries actually rebuilt newer than the touched header, and
`make -q` returned 0 afterward. The focused engine probe passed again. The first
full gate found a Clang `-Wstring-plus-int` warning in a test suffix comparison;
explicit array indexing fixed the expression without suppressing warnings.
A subsequent fresh-container attempt stopped at Git safe-directory setup before
runtime tests; that environment setting was corrected before the next gate.
Snapshot comparisons use `memcpy` so structure padding is explicitly copied.

Full pinned Linux `make LDLIBS=-lucontext SANITIZE_CC=clang ci` passed (exit 0),
including normal and sanitizer suites, source boundaries, build-mode isolation,
publication and analyzer checks. Engine source SHA256:
`7ca3b87418ec2cdef2c411dca49d1ee2d810256ef5b90357cca78205a266b361`.
Shared probe SHA256:
`75a79fa629c45d56e465e4698090a30c02ec3a47beb7c87c699a117e23b6a4cd`.
Final publication/diff checks passed. Exact feature Woodpecker checks, independent
review and coordinator-owned fresh Mac acceptance are pending. Solaris qualification is
pending under the required-target policy; the coordinator prioritizes that
platform gate before merging new shared runtime work. No guest was controlled
or guest result claimed by this worker.

## Accepted Solaris port integration

At the coordinator's request, merge accepted main
`adf62f122a675706f0df7681b6cfb0ee2f039062` into the preserved feature `995f94f`.
The automatic merge had no conflicts. The Solaris stdint adapter, literal
Solaris 9 context-stack convention, monotonic clock, ILP32 guards/tests and
HEAD_STACKFLAGS override are retained. Relative to that main, the only code
changes remain the original isolated engine, its shared test and build/guest
wiring. Source/probe hashes above are unchanged; no live host routing is added.

The failed feature #389 is retained as infrastructure evidence: Linux stopped
at clone setup with `could not download plugin-git, binary for this os/arch not
found`, before source execution. Both Mac workflows succeeded. No empty commit
or authentication change was used to retry it; this required integration will
receive fresh exact CI. The merged protocol tests passed all 18 tests, retaining
68 records and the 64-program limit. After interruption, the original combined
run had no retained source/CI result, so it was not counted as passing. A fresh
export of this merged tree passed the focused terminal engine test and complete
`make LDLIBS=-lucontext SANITIZE_CC=clang ci` (exit 0) on 2026-09-10, using the
same pinned agent image with GCC 14.2.0 and Clang 20.1.8. Normal, sanitizer,
source-boundary, build-mode, publication and analyzer checks completed. Byte
comparison confirmed the engine/Mac wiring unchanged from reviewed `995f94f`
and accepted Solaris runtime/build files unchanged from `adf62f1`.

Exact pushed-commit CI is pending at this note revision. Mac acceptance and
native Solaris qualification require separately assigned coordinator slots;
no old platform result is promoted to acceptance of this new engine.
