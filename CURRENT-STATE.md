# Current State — cannedBSD

**Last verified:** 2026-09-08
**Iteration count:** 27 completed Iterate Bot loops; the prototype predates the loop log

## What this is

cannedBSD is a small user-space operating system hosted inside one native process.
Its portable core owns tasks, descriptors, pipes, paths, files, and shell semantics;
a narrow host adapter supplies irreducible platform services. The first host is
Linux. The design deliberately avoids CPU emulation and does not pretend that
internal tasks are host processes.

The normative v0.1 contract is in `SPEC.md`. Historical research and design
discussion live in `PROJECT_NOTES.md` and `TARGETS.md`.

## How to run a loop

Follow `AGENTS.md`. It defines orientation, stable backlog IDs, isolated
worktrees, task claiming, the red/green loop, both Woodpecker gates, and the
evidence required at handoff. `BACKLOG.md` is the authoritative worker queue;
`CAPABILITY-MAP.md` records dependency planning without making everything ready.

One behavior change per loop. If it cannot be demonstrated by a focused test,
it is not done.

## Acceptance comparison correction, 2026-09-08

MAC-14 is accepted at `f16873d246ab3afcc70966b89ae11b9557f6055a`.
Native and Mac text acceptance now require exact captured byte lengths; Mac
capture also rejects discarded output. Focused tests reproduce and reject the
former leading-NUL false pass, hidden suffix and truncated matching prefix.
Independent review and exact Woodpecker #308 all three checks passed. Fresh
Basilisk II run `run-6i_uo4y9` passed all 62 records in 21.58 seconds, with the
screenshot/full transcript inspected and normal shutdown, closed disks and
slot release verified. Artifact archive SHA256:
`e8c6c8e4cb0989a06dc5f6c6bf0805272f0217eb9e663c8eecab5420b3f24789`.

## Latest accepted head prerequisites, 2026-09-08

Bounded unbuffered fread and pinned NetBSD strtoimax with C-locale ctype helpers
are accepted at `ff08dd5c22f6c74de0bed5afce7a9798d666b84b`. Independent feature
and integration reviews passed; exact Woodpecker #301 passed ci, mac68k and
mac-automation. Fresh Basilisk II guest `run-3c7aibdj` passed all 62 records in
22.43 seconds from cold boot through normal shutdown and slot release.
The complete fresh transcript and screenshot were inspected; the bound receipt
confirms the app and both guest disks closed. Archive SHA256:
`f863c4c70f2518c9bf4f23f6aab33f147eb4cc62b50bc80f01e4ec22d8770e37`.

Antigravity implements the remaining fwrite prerequisite; Claude audits the
head stack/resource boundary as PORT32-01. The unchanged head import remains
pending both. 32-bit classic Mac execution remains a merge gate.

## Latest accepted file-stream prerequisite, 2026-09-08

Read-only fopen/fclose and owned input wrappers are accepted at
`0e35e50ba91bd78284b3333dcf4ffb75441637cf`. Independent runtime/integration
review and all three Woodpecker #291 checks passed. Fresh guest `run-q3vxq533`
passed all 59 records in a 22.93-second cold automated cycle. Screenshot and
bound receipt were inspected; app/guest shutdown, closed disks and slot release
were verified. Archive SHA256:
`3cd475ba9b847a50db996f4f34c040a8dcfa9bfd2a1032142e6262752b837fba`.

Stream acquisition rollback and exec/exit cleanup are observed before teardown;
non-CLOEXEC descriptor inheritance and the original stdin/output ABI remain
intact. The later accepted fread and numeric conversion milestone is recorded
above; fwrite and the unchanged head import remain pending.

## Latest accepted string prerequisite, 2026-09-08

Pinned unchanged NetBSD strcpy is accepted at
`bb1190ce0d911bb333d788df58ed7df6538b5e46`. Independent source/runtime and
integration reviews passed; all three exact Woodpecker #280 checks succeeded.
Fresh guest `run-8td2212t` passed all 57 records in a 21.02-second cold automated
cycle. Screenshot, exact receipt, normal shutdown, closed disks and released
slot were verified. Archive SHA256:
`c94e86780a4a056f1db8521fb8de5ebdbb34d3ec127687d134b4a95abf03930b`.
The ordinary probe covers destination identity, NUL, byte values, input
preservation and canaries; an unchanged probe rejected a deliberately wrong
return pointer in a disposable implementation regression control.

Claude implements numeric conversion, the coordinator worker implements
read-only file streams, and Antigravity prepares concrete head command fixtures.
Head itself remains pending those prerequisites plus fread/fwrite and its
explicit stack-budget acceptance.

## Latest accepted input prerequisite, 2026-09-08

Stdin byte reads and independent EOF/error queries are accepted at
`27bb7be462f500c99c2fa05fb42c90838da65cab`, after clean independent review and
all three exact Woodpecker #277 checks. Fresh guest `run-q732x7z0` passed 56
records in 24.94 seconds from cold launch through screenshot, normal shutdown
and verified slot release. Screenshot and exact receipt were inspected.
Archive SHA256:
`4c7a00a082297ab857af2d4133f100cfd6992940f6d7d202a884da5aff4dc453`.

The original output state remains compatible with older runtimes. Input flags
are task-owned, preserve failed exec/rebind state, and reset on successful exec.
Read-only fopen/fclose wrappers are next (STDIN-02). Claude implements numeric
conversion; strcpy is in coordinator integration testing, not yet accepted.

## Latest accepted head prerequisite, 2026-09-08

Returning `warn` is accepted at `6f860c426e8842119615efd289f6bee2bcf6b678`.
Independent integration review and all three Woodpecker #266 checks passed.
Fresh Mac run `run-4j885q64` passed 54 records, including all prior 50 and four
warn cases, in a 24.82-second cold automated cycle. The screenshot and exact
receipt were inspected; normal shutdown, closed disks and slot release passed.
Archive SHA256:
`21a82baf3c465dd6386be50ea66c59573ddd210d3488adbe0ba1fa1279c81ef7`.

The read-only stream design is reviewed and included. The coordinator worker
implements STDIN-01, Antigravity implements STRCPY-01, and Claude corrects the
numeric conversion design and measures the actual Retro68 type contract.
Unchanged head still needs these input/string/conversion and fwrite prerequisites.

## Latest accepted utility and parser milestone, 2026-09-08

Unchanged NetBSD echo, registered as `netbsdecho`, and required-argument getopt
are accepted at `b2dc0ce9fef946d2ae6a7c6e800888c785253101`. Independent
review and all three exact Woodpecker #254 workflows passed. Fresh Mac run
`run-j5c9_75j` passed all 50 records, including the prior 43, in a complete
24.58-second cold automated cycle. Screenshot inspection, bound receipt,
normal app/guest shutdown, closed disks and slot release were verified.
Archive SHA256:
`64e508437e92fc23e55389aa94a3b130eec5cd7032b03433f6b7d87c0bf08862`.
Native exact-output and fault tests exercise the actual imported echo; a
post-implementation false-ferror control confirms the error-status regression
is caught. Native and Mac getopt probes exercise required arguments and actual
peer-task/exec isolation. The existing builtin echo and YES tests remain intact.

Claude is preparing the numeric conversion design for head. Antigravity is
repairing ERR-02 review findings; warn is not accepted yet. General input streams
and numeric conversion remain prerequisites for an unchanged head import.

## Latest accepted runtime prerequisite, 2026-09-08

Argument storage ownership is fixed at
`4ee800e309a10e7495236a054dbbe0537334aaaf`, independently reviewed with
all three Woodpecker #242 workflows green. A real ordinary argv-pointer rewrite
previously produced an ASan double-free; the runtime now retains original string
ownership separately from the mutable argv vector. Allocation-observing native
tests cover exit/reap, exec success/failure, allocation failures and live teardown.
Fresh Mac run `run-e256a3_h` passed all 43 records in 15.33 seconds from cold
launch through screenshot and normal shutdown; screenshot, receipt, closed disks
and released slot were verified. Archive SHA256:
`c5506052e724c00594e44c6536e618606c080cdb0f9707789906e9302db30a34`.

The reviewed head dependency plan identifies required option arguments and
returning warn diagnostics as the next steps, now assigned to the coordinator
worker (GETOPT-02) and Antigravity (ERR-02). General input streams and numeric
conversion remain design work. Claude continues the nearer NetBSD echo milestone.

## Latest accepted utility milestone, 2026-09-08

Unchanged NetBSD basename and its separate writable task-owned libc result are
accepted and merged at `5906b9c69551c6b7e3fd8fa14e777c204b5e1610`.
Woodpecker #219 passed ci, mac68k and mac-automation; a separate full Linux
`make ci` also passed. Exact archive SHA256:
`51456b9d06aa1a7995abc0fbb03cf836f190e25d76ac43a87560fc944459f04b`.
Guest run `run-rt_33o6i` produced all thirty-eight fresh PASS records, including
seven basename cases and the previously accepted program-name and VFS probes.
The coordinator reviewed the screenshot and acceptance receipt, verified app
closure and normal guest shutdown with mounted disks closed, and released the
slot. A focus interruption occurred before outputs; the successful resumed
portion took 12.92 seconds. This is not a cold-boot measurement.

PROGNAME-01 was separately accepted and merged at
`a7acb54bd569ef66d93884f9808cce66561e01d6`: Woodpecker #213 passed all three
workflows. Exact archive SHA256:
`4332ea8f1b4abfb52dd31146d767234d007ce049523704ef6b33b34c11d0c87f`.
Guest run `run-3h5wky79` produced thirty-one fresh PASS records. Multiple
prelaunch focus/mouse interruptions left output evidence empty; the successful
resumed portion took 12.42 seconds, not a cold-boot measurement. The coordinator
reviewed the screenshot and receipt and verified app closure, normal guest
shutdown, closed mounted disks and slot release.

STDOUT-01 and MAC-12 are now accepted at
`894b75316eea9965438aa45b12e5875c53f5136a`. Woodpecker #230 passed all
three workflows. Archive SHA256:
`ca21a340b41340796078a4236ac81fa3123cd31fa93de85d6c258932a31dcd1c`.
Fresh guest `run-mraq4k0o` produced the complete 42-case transcript and ALL PASS.
The coordinator inspected the decoded screenshot and bound receipt; app closure,
closed guest disks and slot release were verified. The cold automated cycle took
16.09 seconds, including verified window positioning, cursor parking, launch,
tests, screenshot capture and normal shutdown. One successful measurement does
not establish a reliability rate or guarantee against external focus changes.

Claude is implementing unchanged NetBSD echo with the accepted prerequisites.
Antigravity is implementing ERR-02 from the reviewed HEAD-01 dependency plan;
the head command import remains deferred. Codex owns reviews and serialized guest runs.

The earlier unchanged dirname/locale milestone remains accepted at `96d5936`:
Woodpecker #174 all three checks, twenty-eight PASS records in `run-u4ko4rto`,
and archive SHA256
`4c391d5d6a280ba89b755d5eb79010a185c9c49e68d4e03485ca061fbf05001b`.
That run's coordinator-reviewed screenshot and normal shutdown released the slot;
its automated cycle took 15.48 seconds.

VFS-02 is now accepted at
`56e79e1dc1f26ef0e2ed6ecd581f8ffe419e6655`: Woodpecker #203 passed all three
workflows, and fresh guest run `run-0t8ymt4d` passed thirty records in a
18.96-second cold cycle. The screenshot was reviewed; app and guest disks
closed normally and the slot was released. Exact archive SHA256:
`cf9e30caf83f0432896de86cfc0e3ebdbe377cb3fad95a05034d3f35503086f3`.
See the integration note for executable-node error and lifetime coverage.

## Accepted directory iteration integration, 2026-09-08

Exact commit `6e83f00` passed ci, mac68k and mac-automation in Woodpecker #182.
Archive SHA256:
`f6cc4b48b3a0cf21d6adb2c752e9c7455cfc2d0cbf43edb5406726ad65e8453c`.
Basilisk II acceptance `run-26u3wozf` produced twenty-nine fresh PASS records
including the direct ordinary dirent probe. The coordinator reviewed the
screenshot, observed app closure and normal guest shutdown, verified closed
mounted disks, and released the serialized slot.

Two focus interruptions occurred before outputs were produced. After moving
the emulator window left, the coordinator resumed the existing staged run;
the successful resumed automation took 15.02 seconds. This is not a cold-boot
measurement. See notes/iterations/VFS-03.md for the implementation and review.

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
- a task-local libc `environ` view that follows mutation and exec without
  leaking across cooperative task interleaving;
- the v0.1 acceptance command producing `HELLO`.

That statement is bounded by the current tests. It is not evidence of complete
POSIX semantics, memory safety under untested operations, classic-host
portability, networking, dynamic modules, or WASM execution.

## Mac acceptance automation update, 2026-09-07

The Hammerspoon runner and supplied image templates live in
`platform/mac68k/automation`. It uses MAC-01 staging, validates the launched
PID/preferences, saves a fresh result and screenshot, exits the guest program,
and selects normal guest shutdown. A prototype cold cycle took 12.7 seconds;
the project version with staging/receipt/slot checks took 15.4 seconds.

MAC-01, MAC-02, MAC-04, and MAC-05 are merged on main at `5374fad`.
Woodpecker #54 passed all three workflows, and its exact artifact passed the
System 7 startup suite with a fresh result, screenshot, normal shutdown, and
slot release. MAC-05 also passed its dedicated nested-task service tests.

PENV-02 (exit), PENV-03 (bounded getopt), PENV-04 (bounded stdio), PENV-05
(errx), FS-01 (truncate/ftruncate), PORT-01 (adapter conformance), and MAC-06
(matcher readiness) are merged at `4f80e83`. All three Woodpecker #66 workflows
passed. Its exact archive SHA-256 is
`2b0095150422e38b2d74e7c76524f69316f903c953cea69bdc08d964fa1f0d2f`.
The fresh System 7 smoke suite, screenshot, normal shutdown, and slot release
completed in 19.03 seconds. These eight records are a guest regression smoke;
MAC-07 adds direct libc/getopt/truncate guest probes.

The former Documents-based runtime stalled on iCloud-evicted NumPy modules
and shared files. Runtime, staging, scratch, and verified clean seed now live
outside iCloud. Matcher readiness is required before boot. MAC-03's positive
and failure cases are verified and merged. Directory iteration is now accepted
at `6e83f00`; Antigravity is correcting
executable VFS node regression coverage. Polling is merged; pending runtime
changes still require review and guest acceptance.

## Current evidence

The experimental `mac-system7` branch adds `platform/mac68k` without changing
the Linux backend or portable core. Woodpecker pipeline #16 passed the existing
Linux gate and the separate pinned Retro68 cross-build. Its checksum-verified
artifact ran in Basilisk II / System 7.5.3: two independent stacks completed
512 child yields, all seven startup shell cases passed, and an interactive
`echo ci | tr a-z A-Z` returned `CI`.

The first guest run exposed failed allocations after moving onto heap-backed
task stacks; expanding the application heap with `MaxApplZone` before the first
switch fixed those failures. A second run passed the shell cases but System 7
reported error 28 when event handling ran on a task stack. Pumping events only
on the original scheduler stack fixed the interactive run. Guest acceptance is
separate from CI compilation; see `platform/mac68k/README.md` for the transfer
and evidence procedure. This is initial System 7 evidence, not verification of
other classic hosts or the full Linux suite on the Mac.

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

Iteration 24 added the matching NetBSD `memmove.c` wrapper over the already
pinned `bcopy.c` engine. The source boundary was first red because the wrapper
was absent. It now follows the same compact implementation, private GCC/Clang
definition name, archive, hash, and provenance rules as `memcpy`. Direct tests
prove its returned pointer, zero-length behavior, and overlapping movement in
both directions; the ordinary-source probe must resolve only to
`cb_libc_memmove`. The complete gate passes locally and in the exact Alpine
Woodpecker agent image.

Iteration 25 began from a compile inventory of pinned NetBSD `printenv`, rather
than speculative libc growth. Its direct behavioral test was first red with an
undefined `cb_libc_memcmp`. The generic NetBSD `memcmp.c` is now imported
byte-for-byte unchanged with its own private object and archive member. Direct
tests prove zero length, equality, both order directions, and unsigned high-bit
ordering; source tests pin its hash and provenance and require an ordinary
translation unit to resolve only to `cb_libc_memcmp`. The complete Linux gate
passes in the Alpine runner environment, and the Retro68 application builds.

Iteration 26 continued that inventory with its smallest independent missing
operation, `strchr`. The behavioral test was first red with an undefined
`cb_libc_strchr`. NetBSD's generic source is now imported byte-for-byte unchanged
with a minimal import namespace shim; its unused `index` alias is deliberately
outside the advertised libc surface. Direct tests cover first and later matches,
missing input, the terminal NUL, and conversion of `int` to `char`. Source tests
pin provenance and require private ordinary-source and archive symbols.

Iteration 27 added a task-local libc `environ` location through an append-only
program API operation. Its test first failed because the ABI accessor was
absent, then forced a 10,000-byte pipe write to block while a peer mutated its
own environment. The resumed task retained its original vector and values;
setenv, unsetenv, spawn inheritance, and successful exec all expose the live
task-owned environment. An ordinary-source probe resolves `environ` only
through the private libc veneer. Woodpecker pipeline #28 passed both the Linux
gate and Retro68 cross-build on the final evidence commit.

## What's next

Directory iteration and executable VFS nodes are under implementation review.
Polling is merged with seventeen-check Mac acceptance at `819a964`. The reviewed MacPerl/GUSI audit identifies
MoreFiles as a candidate for a bounded future Mac host filesystem probe, with
source pins and license distinctions recorded in `notes/iterations/REUSE-01.md`.
The original bounded bootstrap `wc` remains scaffolding.

## Key files and commands

| Purpose | Location or command |
|---|---|
| Coding-agent procedure | `AGENTS.md` |
| Worker queue | `BACKLOG.md` |
| Dependency inventory | `CAPABILITY-MAP.md` |
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

## Accepted integration, 2026-09-07 evening

Main `c93e2ba` includes pinned NetBSD printenv, reviewed mount routing,
guest-owned screenshot/completion support, and direct memory/getopt/truncate
probes. Woodpecker #77 passed all three workflows. Exact archive SHA256
`18eab40bbaefad2ec8b5580d7f199650fa06135457fee9fbbe0bf7c7697a6d20`
ran fifteen named checks in System 7, including named/missing/empty/invalid
printenv inputs. Run `run-sdonh80f` completed with a fresh screenshot, normal
shutdown and slot release in 14.29 seconds.

Current work: fix polling errno and finite-deadline scheduling; implement
owned directory iteration; complete optional fully automatic guest evidence
collection; audit MacPerl/GUSI source before building more Mac-specific layers.

Main `ca0cd19` additionally includes MAC-09 supported autorun and the reviewed
MacPerl/GUSI/MoreFiles source audit. All three #87 workflows passed; its exact
Mac artifact completed fifteen guest checks in 13.76 seconds with decoded
screenshot, normal shutdown, acceptance receipt, and slot release.

Main `02ee32b` adds errno-bearing err(3), including real task-interleaving
and partial-write tests. All three #102 workflows passed; the exact artifact
passed sixteen System 7 checks in 13.46 seconds with fresh screenshot and
normal shutdown. MoreFiles feasibility work has found missing SDK types and
headers; a diagnostic result is under review, not a working host adapter.

Main `819a964` adds reviewed poll/pipe support and direct Mac readiness/error
acceptance. All three #118 workflows passed; seventeen fresh guest checks
completed in 15.31 seconds with screenshot and normal shutdown. The MoreFiles
compile diagnostic is a negative feasibility result. A smaller wrapper using
real SDK catalog records is the next isolated host experiment.


## Current integration and next utility milestone

Main `592ae41` includes MAC-11's locked-desktop preflight and TERM-02 console
classification/honest attribute fallback. Woodpecker #154 passed all three
workflows; run `run-h_uugpe6` passed eighteen fresh Mac records in 13.21 seconds
with screenshot, normal shutdown, receipt and slot release. Archive SHA256:
`98da005d4afce5df1a32d04bd7e4656dc7e3b1505141b798f5f4102e0c2f5455`.

Main `a9346ea` adds the separate REUSE-03 catalog experiment. Woodpecker #156
passed all three workflows. The normal eighteen-check artifact passed in
`run-grt4pnjg` (15.19 seconds). The separate catalog probe passed all thirteen
records in cold run `run-kpjadavd`; screenshot showed successful shared-file
writing, followed by normal shutdown and slot release. Protected fixture SHA256
remained `5adc9b2bb9cb9d1d7119ad45dc2d8470e221aee776a6b653d9e788e445e56f58`.
This is an isolated catalog experiment, not an implemented host filesystem mount.

The user-selected next milestone is the unchanged NetBSD dirname command.
Actual compilation measured only dirname and setlocale as missing private
imports; host libgen/locale header leakage was detected, not counted as support.
Claude owns libc dirname; Codex owns the C-only locale prerequisite. VFS-03 and
VFS-02 remain unmerged until their outstanding review corrections pass. Broader
terminal implementation is deferred in favor of this real utility milestone.
