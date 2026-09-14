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

Exact pushed-commit CI: Woodpecker run #413 on `ea667d6e1bc7e3d544649e26563dcfb27845fcc6` passed all 3 pipelines (`linux-ci`, `mac68k`, `mac68k-clang`).

## Native Solaris 9 SPARC qualification

Native-tested commit: `ea667d6e1bc7e3d544649e26563dcfb27845fcc6`.
Guest environment: `SunOS solaris 5.9 Generic sun4m sparc SUNW,SPARCstation-5` (QEMU SS-5, PID 21427 on `tribblix`, up since 2026-09-04T20:59:15Z). Toolchain: Sunfreeware GCC 3.4.6 (`/usr/local/bin/gcc`), GNU Make 3.81 (`/usr/local/bin/make`), `librt`.

Source staging artifacts generated from exact commit `ea667d6`:
- USTAR tar archive: `cannedbsd-src-SIG-01-ea667d6-ustar.tar` (SHA256 `03c598e0b0a675678509c56d38c9fc1d9487f800859a0d54a402b67526610dab`)
- Gzip tar archive: `cannedbsd-src-SIG-01-ea667d6-ustar.tar.gz` (SHA256 `81f2e61a584dcbaf83ecf16ef0b1cdb5f8a91d911eaab6689859a276b3900892`)
- Rock-Ridge ISO: `source.iso` (SHA256 `4855b76c12e28f0e715eaa1283abfd49e05b1bad7584df575391ac72fbb51f80`)

Guest build and test execution:
Clean extraction of ISO to `/var/tmp/sq-SIG-01-ea667d6` (326 regular files). Executed under `/bin/ksh` with `PATH=/usr/local/bin:/usr/ccs/bin:/usr/bin:/usr/sbin CC=gcc MAKE=make tools/solaris9-build.sh`.

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
[... gcc compilation of objects including tests/signal_probe.c and static archive build/libcannedbsd.a ...]
gcc -D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9 -Icompat/solaris9/include -Iinclude -Isrc -std=gnu99 -Wall -Wextra -Werror -pedantic -Wno-unknown-pragmas -g -O2 src/main.c [...] build/libcannedbsd.a -o build/bsdinacan -lrt
gcc -D_XOPEN_SOURCE=600 -D__EXTENSIONS__ -DCANNEDBSD_SOLARIS9 -Icompat/solaris9/include -Iinclude -Isrc -std=gnu99 -Wall -Wextra -Werror -pedantic -Wno-unknown-pragmas -g -O2 tests/signal_probe.c [...] build/libcannedbsd.a -o build/test_core -lrt
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

Direct focused execution `./build/test_core --signals` returned exit status 0 (`SIG_PROBE_RC=0`), validating all 29 signal probe scenarios on native SPARC. All launcher and pipeline acceptance assertions (`HELLO` pipeline, exit status 1, libc `wc -c` count 5) passed cleanly under `set -eu`. Guest media was unmounted, drive ejected in QEMU monitor, and coordinator lock released.

## Mac68k / Basilisk II guest acceptance status

Per Ryan's directive: Mac guest (Basilisk II / System 7) live qualification is **EXPLICITLY PENDING**. The host machine (`lillehammer`) hosting the staged guest environment is a battery-powered laptop asleep in another house. Retro68 compilation is verified green via Woodpecker mac68k CI workflow #413.

