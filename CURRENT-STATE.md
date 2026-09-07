# Current State — cannedBSD

**Last verified:** 2026-09-06
**Iteration count:** 23 completed Iterate Bot loops; the prototype predates the loop log

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
- task-owned program allocations and a separately compiled libc-backed `wc`;
- a pinned, byte-for-byte unmodified NetBSD `yes` using the libc veneer;
- a separately archived, unmodified NetBSD generic `strlen` used by `puts`;
- a separately archived, unmodified NetBSD generic `strcmp` used by `wc`;
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

Iteration 13 added direct ABI-boundary tests. The red run segfaulted when
`cb_kernel_create` dereferenced a missing required host callback. The core now
validates every required v1 host operation before allocation; new tests also
enforce strict program descriptors, duplicates, and the 64-entry capacity.
The Linux host supplies monotonic and Unix-epoch wall clocks. `abiprobe` proves
the version, size, and presence of every program API operation; exact v0.1
capability values; mappings for every declared error; unknown-error behavior;
and errno isolation across interleaved parent and child tasks.

Iteration 14 introduced the explicit native executor boundary. Its architecture
test first failed because `core.c` directly stored task contexts, selected stack
sizes, and invoked `program->start`. The registry now stores runtime-owned
generic program objects and each task owns an executor instance. A delegating
test executor proves prepare, create, start/resume, suspend, requested
termination, instance destruction, and program destruction. The native
executor alone owns the command descriptor and task context; existing exec and
process probes pass unchanged through the new boundary.

Iteration 15 introduced the explicit VFS boundary. Its architecture test first
failed because mount and node operation tables did not exist. Generic `vfs.c`
now owns path traversal, cwd rendering, errno translation, and validated root
installation; private `ramfs.c` owns storage and directory representation.
`test_vfs_contract` validates every versioned mount/node callback, ownership,
root metadata, lookup, retain/release, and destruction. Task cwd/root and open
files retain generic node handles, preserving open-after-unlink lifetime. All
prior filesystem and process probes pass without direct RAMFS access in the
portable core.

Iteration 16 closed the release-evidence gaps. A live `/proc` test holds
`echo READY; cat | cat` active and observes one Linux thread with no host child
processes. An allocator ledger proves complete runtime cleanup; a deliberately
dirty host allocator first crashed and now proves core objects are explicitly
initialized. `overflowprobe` first returned status 192 and now verifies
oversized byte-count rejection plus signed seek overflow. Internal readiness
operations cover RAM files, terminals, pipe blocking, EOF, and broken-pipe
outcomes. Registration-source mutation proves program preparation ownership.
README instructions now include prerequisites and a native-command example.
The complete current tree passed inside the exact Alpine Woodpecker agent image.

Iteration 17 began the post-v0.1 source-compatibility ladder. Its first red
build failed with an undefined task-allocation observer; the core now tracks
program allocations per internal task and reclaims them on exit, successful
exec, or teardown. Tests prove resize preservation, zero-size behavior, atomic
allocation failure, cross-task ownership rejection, exit-before-wait cleanup,
and exec cleanup. A second red test reported that `commands/wc.c` did not
exist. It is now compiled as its own ordinary-main translation unit against
minimal `unistd.h`, `fcntl.h`, and `stdlib.h` veneers. The bootstrap `wc -c`
uses standard source spellings while its object imports only prefixed
cannedBSD functions. The canonical gate passes locally and in the exact Alpine
Woodpecker agent image, including Clang ASan/UBSan.

Iteration 18 made errno part of the program rather than the host thread. The
first red `abiprobe` returned 181 for the missing API operation. Tasks now own
separate program-visible errno cells, with get/set and libc referring to the
same scalar. Interleaved tasks retain distinct values, and successful exec
resets the cell. The second red source-boundary run reported the missing
`errno.h`; the libc now provides a modifiable `errno` and `strerror()`. The
ordinary `wc` command uses them to report the exact cannedBSD error from a
failed open. The canonical gate passes locally and in the exact Alpine
Woodpecker agent image.

Iteration 23 imported NetBSD `memcpy.c` and the `bcopy.c` implementation it
includes. The source test first failed because both pinned inputs were absent.
The first link then showed that host `_FORTIFY_SOURCE` makes upstream undefine
the macro-renamed `memcpy`; the definition-side GCC/Clang assembler-name adapter
now handles it without changing either source file. `-Os` deliberately selects
NetBSD's own compact byte-copy branch. Hash, object, archive, ordinary-source,
and direct semantic tests exclude host libc and cover returned pointers,
zero-length and offset copies, sentinel boundaries, NUL, and high-bit bytes.
The provenance ledger explicitly leaves ARM EABI alias support for that port.
The complete gate passes locally and in the exact Alpine Woodpecker agent image.

Iteration 19 crossed the first actual NetBSD-source boundary. Its red command
test returned status 127 because `yes` was absent. NetBSD
`usr.bin/yes/yes.c` is pinned to commit `b890038f7ae5` and its exact SHA-256 is
checked on every build; its three-clause Regents license remains in the
byte-for-byte unchanged source. cannedBSD-owned headers supply only the
metadata macros, exit constant, and unbuffered `puts` it requires. One test
checks exact `ok\n` output. A separate probe directly waits for both reader and
writer, proving final-reader close reaches `EPIPE` and makes the original loop
return `EXIT_FAILURE` rather than relying on the shell's last-pipeline status.
The complete gate passes locally and in the exact Alpine Woodpecker agent image.

Iteration 20 imported the first NetBSD libc implementation. The boundary test
first failed because `common/lib/libc/string/strlen.c` was absent. It is now
pinned at the same NetBSD revision and exact hash, compiled byte-for-byte
unchanged as `cb_libc_strlen`, and archived as a distinct object. The first
compile also proved that allowing the host `assert.h` through would entangle
the source with glibc's `sys/cdefs.h` contract. A build-only empty assert shim
is narrowly safe because the pinned file contains no assertion, and a test
will reject it if that changes. `puts` uses the imported routine; boundary and
direct semantic tests exclude the host symbol and cover empty and embedded-NUL
inputs. The complete gate passes locally and in the exact Alpine Woodpecker
agent image.

Iteration 21 repeated the narrow libc-import loop for NetBSD's generic
`strcmp.c`. The boundary test was first red because the pinned source was
absent. Its first implementation then failed to link: the upstream file
deliberately undefines `strcmp`, defeating a command-line macro rename.
cannedBSD's GCC/Clang header adapter now assigns the standard declaration a
private assembler link name, leaving the hash-pinned source untouched; other
compilers will require an equivalent porting hook. The original `wc` source now
uses standard `strcmp` and `strlen` calls and is required to import only the
private symbols. Direct tests cover equality, prefixes, both ordering
directions, and high-bit bytes with unsigned-byte ordering. The complete gate
passes locally and in the exact Alpine Woodpecker agent image.

Iteration 22 completed the basic allocation veneer. Its red build reported
missing `cb_libc_calloc` and `cb_libc_realloc`. `calloc` now checks product
overflow, returns `ENOMEM`, and zeroes the requested extent; `realloc` delegates
to the already ownership-enforcing task resize operation. The libc startup
check now requires that operation. A deterministic resize failure proves the
old allocation and bytes survive, while direct cases cover zero-size behavior,
growth, shrinkage, foreign pointers, and `realloc(NULL, size)`. A separately
compiled ordinary-source probe must import the prefixed functions rather than
host allocation. The complete gate passes locally and in the exact Alpine
Woodpecker agent image.

## What's next

Add NetBSD `memmove.c` only if sharing the now-pinned `bcopy.c` implementation
survives an overlap-focused red test and the same link-name boundary. Do not
grow printf or getopt speculatively.
The original bounded bootstrap `wc` remains scaffolding, not imported-source
provenance evidence.

## Key files and commands

| Purpose | Location or command |
|---|---|
| Public ABI | `include/cannedbsd/abi.h` |
| libc/source compatibility contract | `LIBC.md` |
| libc veneer and headers | `libc/`, `include/cannedbsd/libc.h` |
| External command source | `commands/wc.c` |
| Portable runtime core | `src/core.c` |
| Generic VFS | `src/vfs.c` |
| RAM filesystem backend | `src/ramfs.c` |
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
- The libc surface is intentionally tiny; general stdio, most string,
  directory, time, signal, locale, and terminal APIs remain absent.
- `sed`, `awk`, a curses demo, and a tiny vi are the second-stage usability
  demo, not the kernel proof gate.
- There is no network API in v0.1. SOCKS is a proposed early transport option,
  not an implemented capability.
- The public GitHub repository and Woodpecker project are active. Successful
  pipelines prove the GitHub webhook, Alpine agent, and canonical gate end to
  end. Every push remains subject to that gate.
