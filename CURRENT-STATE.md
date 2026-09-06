# Current State — cannedBSD

**Last verified:** 2026-09-06
**Iteration count:** 12 completed Iterate Bot loops; the prototype predates the loop log

## What this is

cannedBSD is a small user-space operating system hosted inside one native process.
Its portable core owns tasks, descriptors, pipes, paths, files, and shell semantics;
a narrow host adapter supplies irreducible platform services. The first host is
Linux. The design deliberately avoids CPU emulation and does not pretend that
internal tasks are host processes.

The normative v0.1 contract is in `SPEC.md`. Historical research and design
discussion live in `PROJECT_NOTES.md` and `TARGETS.md`.

## How to run a loop

1. Read this file, `BACKLOG.md`, and the relevant part of `SPEC.md`.
2. Run `make clean test` to establish a fresh non-sanitized baseline.
3. Pick only the first ready Priority 1 item.
4. State one falsifiable hypothesis and write the smallest test that disproves
   the current implementation.
5. Run that test and record the failure mode: red.
6. Make the smallest coherent implementation change: green.
7. Run the canonical `make ci` gate.
8. Review the diff for ABI leaks, host-OS leakage, ownership errors, and tests
   that accidentally assert implementation details.
9. Update `BACKLOG.md` and this file with only verified facts.
10. Commit if this directory has been placed under version control.

One behavior change per loop. If it cannot be demonstrated by a focused test,
it is not done.

## Verified working

On 2026-09-06, the existing suite completed successfully and demonstrated:

- one Linux host process running cooperatively scheduled internal tasks;
- native program registration, spawn, exec, PID preservation, exit, and wait;
- task-local descriptor tables and close-on-exec;
- blocking pipes with multi-stage pipelines;
- atomic cleanup when any pipe allocation or descriptor installation fails;
- an in-memory filesystem, working directories, and shell redirection;
- Unix-style lifetime for an unlinked RAMFS file that remains open;
- shell variables, `$?`, `cd`, `pwd`, `export`, and `unset`;
- the v0.1 acceptance command producing `HELLO`.

That statement is bounded by the current tests. It is not evidence of complete
POSIX semantics, memory safety under untested operations, classic-host
portability, networking, dynamic modules, or WASM execution.

## Current evidence

```text
$ make test
all core tests passed
1
ABC
one
two
/tmp
```

Iteration 1 added `unlinkprobe`. Before the implementation change, ASan reported
`heap-use-after-free` at `src/fs.c:452` in `node_read`. After the change,
`make sanitize`, `make clean test`, `make check-architecture`, and GCC's
`-fanalyzer` all completed successfully. ASan warns that it has incomplete
support for `makecontext`/`swapcontext`; no sanitizer error was reported in the
green run.

Iteration 2 added deterministic failure injection for all three allocations in
`pipe()` and a one-descriptor-left `EMFILE` case. Before the change,
ASan/UBSan reported a null-member access and SEGV in `pipe_read_close`. Pipe
construction now initializes and publishes each owned object sequentially, so
every failure path has a valid destructor. Sanitizer, clean optimized, static
analyzer, and architecture checks pass after the change.

Iteration 3 added `tests/test_build_modes.sh`. Its red run showed that a plain
`make test` after `make sanitize` still selected `build/cannedbsd` linked to
`libasan`. Sanitizer output now lives under `build/sanitize/`; a normal test run
rebuilds and selects the uninstrumented `build/cannedbsd`. `make
check-build-modes` verifies this behavior through `ldd`.

Iteration 4 added `tests/test_launcher.sh`. Its red run established that the
agreed `build/bsdinacan` launcher did not exist. The normal and sanitizer
artifacts now use that name, the normative acceptance command uses it, and the
launcher probe passes in both variants. The project and diagnostics retain the
name cannedBSD.

Iteration 5 added a scheduling-sensitive zero-length pipe probe. Its red run
returned status 66: `read(fd, buffer, 0)` yielded and allowed the peer task to
run before returning. `pipe_read` now handles zero count before testing empty
buffer/writer state. Zero-byte pipe reads and writes return without scheduling
the peer; the full verification set passes.

Iteration 6 added `pipeedgeprobe` and `pipeedgepeer`. The characterization test
passed without an implementation change: it observes the reader enter the
blocked state, proves final-writer close wakes it to EOF, and verifies a write
after final-reader close fails with `EPIPE`. Normal, sanitizer, analyzer, and
architecture gates pass.

Iteration 7 added a 10,000-byte pipe saturation test. It starts the writer before
the peer can run, forcing the 4,096-byte buffer to fill and the writer to block.
The peer reads in 777-byte chunks, forcing repeated wakeups and ring wrap. Every
byte matches a deterministic pattern through EOF. The existing implementation
passed; no behavior change was required.

Iteration 8 introduced a deterministic console backend and direct terminal
tests. The red run returned status 115 because a zero-byte terminal read still
entered host polling. Terminal reads now return zero without consulting the
host and translate negative host errors to `-1` plus task-local errno. Tests
also prove fd 0/1/2 terminal metadata and directionality, blocking/wakeup on
console readiness, stdout/stderr separation, interactive prompts and input,
EOF, and interactive `exit 3` status.

Iteration 9 extended the exec probe and went red at status 22: same-fd
`dup2(fd, fd)` incorrectly cleared close-on-exec, leaking the descriptor through
exec. It is now a true no-op. `descriptorprobe` also proves shared offsets,
replacement and flag semantics, inherited open-file sharing with independent
child descriptor tables, ordinary descriptor retention across exec, and
invalid-descriptor errors.

Iteration 10 added `processprobe` and explicit task wake reasons. Its first run
returned status 152 because a parent resumed from a blocking wait without a
recorded cause. Pipe, console, and child-exit wakeups now record distinct
reasons; voluntary yield records none. Direct tests prove PID 1 parentage,
monotonic PIDs, copied argv/environment, parent/child cwd and environment
isolation, blocking wait, exit status, zombie collection, repeated wait and
non-child `ECHILD`, and exec cwd/environment/descriptor behavior.

The canonical `make ci` gate has also passed inside the Alpine 3.22 Woodpecker
agent image. The runner contains its build dependencies; the repository
workflow only selects the sanitizer compiler, links `libucontext`, and runs the
same gate used locally.

Iteration 11 added `ramfsprobe`. Its red run returned status 170 after truncate,
seek, and write: retained allocation bytes leaked into what should have been a
zero-filled sparse hole. RAMFS now clears the gap from the prior logical end and
treats zero-byte writes as non-mutating. Direct tests cover the initial tree and
modes, live absolute/relative resolution, root confinement, independent file
contents and inodes, stat/fstat agreement, sparse writes, append, access modes,
mkdir/unlink errors, and cwd failures.

Iteration 12 expanded the shell black-box matrix. Successive red runs proved
that invalid environment identifiers were accepted, nonnumeric `exit` became
zero, built-ins could not run in pipelines, and redirection setup errors became
status 127. Identifier syntax is now shared by the runtime API and lexer;
`exit` strictly parses signed decimal values; pipeline built-ins execute as
internal child tasks with isolated state; and setup failures retain status 1.
The suite covers quoting, escaping, empty arguments, expansion timing, syntax
diagnostics, every built-in, pipeline status and state isolation, redirection
and partial-spawn cleanup, and the required native commands.

## What's next

See `BACKLOG.md`. The next behavior loop is Priority 1, Iteration 13: ABI,
capability, registration, and host-clock contracts.

## Key files and commands

| Purpose | Location or command |
|---|---|
| Public ABI | `include/cannedbsd/abi.h` |
| Portable runtime core | `src/core.c` |
| RAM filesystem | `src/fs.c` |
| Linux host adapter | `src/host_linux.c` |
| Native commands | `src/programs.c` |
| Shell | `src/shell.c` |
| Tests | `tests/test_core.c` |
| Fresh test run | `make clean test` |
| Canonical local/CI gate | `make ci` |
| Sanitizer run | `make sanitize` |
| Build-mode regression | `make check-build-modes` |
| Portability boundary check | `make check-architecture` |

## Known limitations

- `/bin/sh` and other command paths are resolved through a native program
  registry rather than genuine executable filesystem objects.
- `sed`, `awk`, a curses demo, and a tiny vi are the second-stage usability
  demo, not the kernel proof gate.
- There is no network API in v0.1. SOCKS is a proposed early transport option,
  not an implemented capability.
- A local git repository records the verified baseline. Public repository and
  Woodpecker activation are publication steps, not runtime capabilities.
