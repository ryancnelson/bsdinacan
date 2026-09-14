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

Exact pushed-commit CI: Woodpecker run #405 on `fe082ebc52aef40e65a03d16d4c967ac57720852` passed all 3 pipelines (`linux-ci`, `mac68k`, `mac68k-clang`).

## Native Solaris 9 SPARC qualification

Native-tested commit: `fe082ebc52aef40e65a03d16d4c967ac57720852`.
Guest environment: `SunOS solaris 5.9 Generic sun4m sparc SUNW,SPARCstation-5` (QEMU SS-5, PID 21427 on `tribblix`, up since 2026-09-04T20:59:15Z). Toolchain: Sunfreeware GCC 3.4.6 (`/usr/local/bin/gcc`), GNU Make 3.81 (`/usr/local/bin/make`), `librt`.

Source staging artifacts generated from exact commit `fe082eb`:
- USTAR tar archive: `cannedbsd-src-TERM-03-fe082eb.tar` (SHA256 `3e70454fbeac4df4e32e008f5c97da91a9cf135346042fdaa3c96065a557877d`)
- Gzip tar archive: `cannedbsd-src-TERM-03-fe082eb.tar.gz` (SHA256 `343c8bc0b027ab82ace71ad923f779107d5ea32c2bbcc8e239780646522835a9`)
- Rock-Ridge ISO: `source.iso` (SHA256 `43a816d58b0994e5ad0a9747549d064ab47330dcda8c5f3845464785113855fe`)

Guest build and test execution:
Clean extraction of ISO to `/var/tmp/sq-TERM-03-fe082eb` (76,208 blocks, 326 regular files). Executed under `/bin/ksh` with `PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin CC=gcc MAKE=make tools/solaris9-build.sh`.

Direct guest output transcript:
```
gcc (GCC) 3.4.6
GNU Make 3.81
This program built for sparc-sun-solaris2.9
make: Warning: File `Makefile' has modification time 2.8e+05 s in the future
rm -rf build
make: warning:  Clock skew detected.  Your build may be incomplete.
make: Warning: File `Makefile' has modification time 2.8e+05 s in the future
mkdir -p build
[... gcc compilation and static archiving of libcannedbsd.a and objects ...]
gcc -D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9 -Icompat/solaris9/include -Iinclude -Isrc -std=gnu99 -Wall -Wextra -Werror -pedantic -Wno-unknown-pragmas -g -O2 src/main.c [...] build/libcannedbsd.a -o build/bsdinacan -lrt
gcc -D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9 -Icompat/solaris9/include -Iinclude -Isrc -std=gnu99 -Wall -Wextra -Werror -pedantic -Wno-unknown-pragmas -g -O2 [...] build/libcannedbsd.a -o build/test_core -lrt
make: warning:  Clock skew detected.  Your build may be incomplete.
Orequired getopt tests passed
argv ownership tests passed
fread tests passed
file ownership tests passed
stdin tests passed
clockloss test passed
runnable timeout test passed
all core tests passed
launcher test passed
build/bsdinacan:        ELF 32-bit MSB executable SPARC Version 1, dynamically linked, not stripped
SOLARIS9_CANNEDBSD_TEST=PASS
EXIT=0
```

All core tests, terminal engine probes, launcher tests, and acceptance assertions (`HELLO` pipeline, exit status 1, libc `wc -c` count 5) passed cleanly under `set -eu`. Guest media was unmounted, drive ejected in QEMU monitor, and coordinator lock released.

## Mac68k / Basilisk II guest acceptance status

Per Ryan's directive: Mac guest (Basilisk II / System 7) live qualification is **EXPLICITLY PENDING**. The host machine (`lillehammer`) hosting the staged guest environment is a battery-powered laptop asleep in another house. Retro68 compilation is verified green via Woodpecker mac68k CI workflow #405.

