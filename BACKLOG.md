# cannedBSD Backlog

This is the authoritative worker queue. `CAPABILITY-MAP.md` inventories later
work, while `AGENTS.md` defines how an agent claims and completes one loop.

Every worker item has a stable ID, status, base, dependencies, hypothesis, red
test, and acceptance boundary. `Base: main` means the freshly fetched
`origin/main`. A coordinator launching several agents assigns distinct IDs.

## Ready worker queue

The order is intentional. Choose the first ready item unless a coordinator
assigns an ID. Items whose dependencies are Done may proceed in parallel when
their paths do not overlap.

**Current assignments:** Claude is running HEAD-02 fault-characterization tests.
Antigravity implements the reviewed synthetic TEE-STATE-01 prerequisite.
Codex coordinates STAT-01, reviews,
integration, backlog updates and serialized guest acceptance. HEAD-01
is accepted at `e65e36f` with 65 fresh Mac records, preserving all prior cases;
FWRITE-01 was accepted at `885d83c` with 64 records.

Solaris testing is now required for shared behavior changes under the transition
policy in `notes/CI.md`. Existing assigned workers retain their IDs and must
coordinate Solaris validation; Claude has claimed SOLARIS-01 for source work
after the HEAD-02 review corrections.

Mac guest acceptance is a serialized gate rather than a worker claim. After a
required `mac68k` build succeeds, the coordinator assigns one agent to test that
exact artifact in Basilisk II before integration. Other workers continue on
independent backlog items while the emulator is occupied.

### SOLARIS-01 — integrate the Solaris 9 SPARC runtime gate

- **Status:** Claimed by Claude on `work/SOLARIS-01`; source work follows
  HEAD-02 review corrections; shared rig ownership/acceptance pending
- **Base:** freshly fetched main
- **Depends on:** none; existing Linux and Mac gates remain mandatory
- **Hypothesis:** the current runtime and ordinary-source probes can pass a clean
  native GCC 3.4.6 build on Solaris 9 SPARC without weakening shared contracts.
- **Reference:** `work/SOLARIS-01-reference`, commit
  `a28f9ed8b369b37547df8fa62c08d3565bad8f23`, contains the earlier local port.
  Reconcile only the necessary changes against current main; it also contains
  older review fixes and Makefile changes and must not replace newer code wholesale.
- **Rig:** use the existing isolated Solaris 9 sun4m VM. Coordinator operational
  notes are in `~/wiki/hosts/cannedbsd-solaris9.md` on the documentation workstation.
  Obtain the current serialized rig slot before opening its console or changing
  media; do not copy, replace, or concurrently attach active writable images.
- **Red:** compile the exact current source in the guest and record the first
  relevant compiler/runtime failure. A lost SSH connection or occupied rig is
  setup failure, not behavioral red evidence.
- **Accept:** preserve the original Solaris 9 context-stack ABI, provide bounded
  old-toolchain/header adapters, and run the complete applicable core, libc,
  command and launcher acceptance suite with warnings treated as errors.
  Test 32-bit overflow behavior without speculative multi-gigabyte allocation.
  Capture compiler identity, source commit and archive digest, commands, exit
  statuses, fresh full output and explicit PASS completion. Do not promote the
  historical Solaris result to acceptance of current main. Run existing Linux
  CI and exact-artifact Mac acceptance for shared runtime changes.
- **Handoff:** add a public platform runbook, portable gate command and
  `notes/iterations/SOLARIS-01.md`; keep private rig addresses in operational
  configuration. This integration precedes automated Solaris CI.

### SOLARIS-02 — serialized exact-commit Solaris CI

- **Status:** Blocked on SOLARIS-01; next Solaris task after integration
- **Base:** main with accepted SOLARIS-01
- **Depends on:** SOLARIS-01
- **Hypothesis:** a trusted CI runner can test an exact source commit in the
  existing guest and reject stale output, missing completion and failed probes.
- **Red:** harness controls must reject wrong source identity, nonzero probe exit,
  stale PASS output, timeout and a competing rig owner.
- **Accept:** add a distinct `solaris9` workflow/status with serialized ownership,
  fresh per-run staging and logs, source/artifact digests, bounded timeouts and
  cleanup that preserves other runs and the original disk images. The owner
  lock must cover both manual acceptance and CI, and live ownership must never
  be stolen merely because a run is slow. Test failed-run cleanup and a subsequent
  successful run. Keep credentials and private addresses out of the repository;
  untrusted PR code must not receive infrastructure credentials. A skipped,
  unreachable or occupied guest cannot produce a passing Solaris result.
  Record an actual green exact-commit CI run before declaring automation done.

### ECHO-01-design — output errors and program identity for unchanged echo

- **Status:** Done; reviewed design `d0b5515`, all three exact CI checks passed
- **Base:** main
- **Depends on:** PENV-04, ERR-01 (Done)
- **Hypothesis:** the four observed missing interfaces in pinned NetBSD echo
  can be specified as small task-owned groundwork without general buffered I/O.
- **Evidence:** `notes/iterations/utility-roadmap-20260908.md` records the actual
  Linux compile diagnostics: setprogname, putchar, fflush, ferror. The existing
  runtime getprogname callback does not supply the public setter or getter.
- **Red:** design review must reject a constant-zero ferror or shared mutable
  stream flag: unchanged echo ignores individual output returns and would
  silently succeed after a write error or contaminate another task.
- **Accept:** define task-owned sticky output errors, participation by existing
  printf/fprintf/puts, putchar return/error behavior, the unbuffered fflush and
  ferror boundary, and program-name setter/getter ownership and exec lifetime.
  Specify old/missing ABI behavior, invalid-stream handling and deterministic
  failure/interleaving tests. Split implementation into bounded prerequisites;
  do not implement or import echo in this design task.

### PENV-01 — task-local libc process state and `environ`

- **Status:** Done in iteration 27
- **Base:** main
- **Depends on:** none
- **Hypothesis:** a task-local libc location/accessor can expose the current
  task's environment without leaking it during cooperative interleaving.
- **Red:** an ordinary-source probe referencing `environ` fails today. The
  behavioral red test must use distinct environments in two tasks, force one
  writer past pipe capacity so it blocks, mutate an environment, and cover exec.
- **Accept:** each task observes only its own current vector before and after
  resumption; setenv/unsetenv and exec are reflected; startup no longer discards
  the environment by relying on an envp snapshot; old ABI sizes remain valid.
  A single unscoped host global is not enough.

### VFS-01 — two-mount routing boundary

- **Status:** Done; merged at `c93e2ba` after review, CI, and exact guest acceptance
- **Base:** main
- **Depends on:** none
- **Hypothesis:** path traversal can cross a mount boundary without exposing a
  filesystem-specific node to tasks or descriptors.
- **Red:** mount two RAM filesystems at deterministic paths and show lookup,
  cwd, stat, and open cannot currently route into the second mount.
- **Accept:** both mounts remain isolated; traversal and `..` at mount roots are
  specified; cross-mount mutation returns the chosen error; retain/release and
  failed-install cleanup are proven; the one-root behavior remains compatible.

### PORT-01 — host adapter conformance harness

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** main
- **Depends on:** none
- **Hypothesis:** a reusable mock-host suite can prove the version, size,
  capability, allocation, clock, console, and context contracts without a
  platform-specific implementation.
- **Red:** demonstrate at least one contract currently checked only through the
  Linux host or duplicated platform tests.
- **Accept:** the same deterministic harness can be invoked by Linux and future
  host ports; it tests undersized/oversized tables and absent capabilities; it
  adds no host API to the portable core and changes no runtime behavior.

### MAC-01 — repeatable Basilisk II artifact acceptance

- **Status:** Done; merged after Woodpecker and exact-artifact guest acceptance
- **Base:** main
- **Depends on:** none
- **Hypothesis:** a host-side runner can stage and identify the exact Woodpecker
  artifact and collect fresh guest evidence without relying on manual notes.
- **Red:** demonstrate that the current manual procedure can leave an old
  `cannedbsd-result.txt` or lose the tested commit/checksum association.
- **Accept:** one command verifies `SHA256SUMS`, stages the resource metadata,
  removes stale evidence, records commit and artifact checksum, and rejects a
  missing or non-`ALL PASS` fresh result. GUI launch may remain serialized and
  manual until Basilisk II exposes a reliable automation seam.

### MAC-02 — System 7 private-stack and Toolbox safety

- **Status:** Done; merged after Woodpecker and exact-artifact guest acceptance
- **Base:** main at `78a1e5b`
- **Depends on:** none
- **Hypothesis:** preserving stack-sniffer state and dispatching Toolbox work on
  the original stack removes intermittent System 7 error 28.
- **Red:** old guest artifact failed on two of three cold launches; direct
  service callbacks fail the real alternate-stack dispatcher test.
- **Accept:** preserve StkLowPt at switches, execute Toolbox calls on root,
  require a single CODE segment, green CI and repeated exact-artifact cold runs.

### MAC-03 — guest-owned screenshot and autorun completion

- **Status:** Done; merged at `c93e2ba` after review, CI, and exact guest acceptance
- **Base:** MAC-02 at `9a3e4db`
- **Depends on:** MAC-02 integration
- **Hypothesis:** an opt-in guest mode can flush results and an actual pixel
  screenshot before publishing completion and exiting.
- **Red:** result-only completion cannot prove screenshot persistence.
- **Accept:** result and PICT close/flush precede PASS/FAIL completion; evidence
  failure leaves the app open; normal interactive mode remains available.

### MAC-04 — fast Hammerspoon guest test driver

- **Status:** Done; merged after Woodpecker and exact-artifact guest acceptance
- **Base:** MAC-01 at `5c013c5`
- **Depends on:** MAC-01 integration
- **Hypothesis:** image-matched actions remove manual mouse and tool-call delays.
- **Red:** previous manual boot-to-result measurement excluded screenshot,
  exit, and shutdown; stale guest cursor made host clicks unreliable.
- **Accept:** staged artifact identity, serialized slot, image-matched launch,
  fresh ALL PASS, saved screenshot, shell exit, held Special-menu drag, and
  verified guest shutdown. Fail closed on ambiguous images or lost focus.
  The local prototype completed the whole cycle in 12.7 seconds.

### MAC-05 — resume the actual Toolbox-service requester

- **Status:** Done; merged after Woodpecker and exact-artifact guest acceptance
- **Base:** MAC-02 at `9a3e4db`
- **Depends on:** MAC-02 integration
- **Hypothesis:** a service requested after root → A → B must resume B,
  not the root dispatcher's remembered destination A.
- **Red:** use real alternate stacks with A directly switching to B, then
  have B request a root service and verify its continuation before A resumes.
- **Accept:** store the requester with the pending service; preserve root-only
  nested services and current root/task behavior; verify correct continuation
  and stack-local state with focused/full CI and exact guest regression smoke.
  Current kernel execution uses root/task only; this closes the broader host
  context-contract gap rather than an observed current guest failure.

### FS-01-design — review the regular-file resize contract

- **Status:** Done; reviewed design implemented in FS-01 and merged at `4f80e83`
- **Base:** main
- **Depends on:** none
- **Hypothesis:** a bounded design can resolve FS-01's API and ownership questions.
- **Red:** FS-01 remains blocked without defined adapter and failure semantics.
- **Accept:** specify truncate/ftruncate offsets, zero growth, permissions,
  nonregular-file errors, overflow, allocation failure, append behavior,
  versioned interfaces, and a falsifiable test matrix. No runtime changes.

## Dependency-ordered queue

These entries become Ready only after every named dependency is Done. An agent
must not implement a blocked item merely because its design looks obvious.

### PENV-02 — `exit(3)` and `__dead`

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** main
- **Depends on:** PENV-01 (Done)
- **Hypothesis:** the existing task-exit operation can provide a non-returning
  ordinary C `exit` while preserving task cleanup.
- **Red:** ordinary source calling `exit(7)` fails to link.
- **Accept:** the parent observes status 7, code after exit never runs, heap and
  descriptors are reclaimed, and the declaration carries `__dead` metadata.

### PENV-03 — empty-option `getopt`

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** main
- **Depends on:** PENV-01 (Done)
- **Hypothesis:** task-local getopt state can support the exact empty optstring
  used by pinned `printenv` without claiming the full extension surface.
- **Red:** compile and run an ordinary argv probe for no options, `--`, and an
  unknown option.
- **Accept:** getopt, optind, optarg, opterr, and optopt have isolated task
  state, reset on exec, and behave correctly across forced interleaving.

### PENV-04 — bounded unbuffered formatted output

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** main
- **Depends on:** PENV-01 (Done)
- **Hypothesis:** literals, `%%`, and `%s` are sufficient for every format in
  pinned `printenv` and can preserve descriptor errors without buffered state.
- **Red:** ordinary printenv-format probes fail because printf, fprintf, stdout,
  and stderr are absent.
- **Accept:** exact return counts, partial writes, EPIPE/error propagation, and
  stdout/stderr redirection are tested. Unsupported conversions are not claimed.

### PENV-05 — `errx(3)` diagnostic

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** integrated dependencies
- **Hypothesis:** the bounded formatter plus exit can provide printenv's exact
  fatal diagnostic without a general stdio implementation.
- **Red:** an ordinary errx probe fails because `err.h` and errx are absent.
- **Accept:** program prefix, message, newline, fd 2, status 1, no fd 1 output,
  and task-local program identity are explicit and tested.

### PENV-06 — unchanged NetBSD `printenv`

- **Status:** Done; merged at `c93e2ba` after review, CI, and exact guest acceptance
- **Base:** integrated dependencies
- **Hypothesis:** the exact pinned NetBSD source will compile unchanged and run
  entirely through the cannedBSD libc and runtime.
- **Red:** the source/import boundary fails while the file is absent.
- **Accept:** pin source and license, hash and provenance it, register its
  descriptor, reject host symbol imports, and test named, missing, empty,
  enumerate-all, `=` error, usage, pipeline, and redirection behavior. The raw
  pinned file's expected SHA-256 is
  `d355c07fc5a351d38e2f8552899b456f1300a61408ebf2e2af47c5f52de974db`.

### VFS-02 — runtime-visible executable nodes

- **Status:** Done at `56e79e1`; thirty actual Mac checks accepted
- **Base:** main
- **Hypothesis:** registered programs can appear as executable VFS objects and
  shell lookup can resolve those objects instead of a hidden path registry.
- **Red:** prove `/bin/sh` and registered commands cannot currently be statted
  or opened as executable nodes.
- **Accept:** lookup distinguishes ENOENT, EACCES, and ENOEXEC; open and unlink
  lifetime is specified; spawn/exec atomicity and registry-source cleanup pass.

### VFS-03 — directory iteration and libc `dirent`

- **Status:** Done; integrated at `6e83f00`, all three #182 gates and exact 29-record guest acceptance
- **Base:** main plus VFS-01
- **Hypothesis:** a versioned node iterator can expose directories without
  leaking RAMFS representation.
- **Red:** an ordinary opendir/readdir/closedir probe fails to compile.
- **Accept:** root enumeration, EOF versus error, independent cursors, mutation
  policy, allocation failure, and cleanup are tested before inventorying `ls`.

### IO-01 — public descriptor polling

- **Status:** Done; reviewed and merged at `819a964` with all three #118 gates and direct Mac polling acceptance
- **Base:** main
- **Hypothesis:** the existing readiness seam can implement poll without host
  descriptors or busy waiting.
- **Red:** ordinary source cannot wait on multiple cannedBSD descriptors.
- **Accept:** regular files, console, pipe data/EOF/EPIPE, invalid descriptors,
  timeout zero, blocking wakeup, and peer progress are deterministic.

### FS-01 — truncate and ftruncate

- **Status:** Done; merged at `4f80e83` after review, CI, and guest smoke
- **Base:** main
- **Hypothesis:** VFS node/open-file operations can resize regular files while
  preserving shared open-file offsets and failure atomicity.
- **Red:** ordinary truncate/ftruncate probes fail to compile.
- **Accept:** shrink, zero-filled growth, read-only and pipe errors, overflow,
  allocation failure, and independent/shared descriptor behavior are tested.

### TERM-01 — terminal mode contract

- **Status:** Design complete; implement only the separate stages below
- **Base:** integrated dependency
- **Hypothesis:** task-visible termios state can implement canonical/raw input,
  echo, erase, and EOF over a deterministic console adapter.
- **Red:** isatty and tcgetattr/tcsetattr ordinary-source probes are absent.
- **Accept:** mode transitions and byte delivery are deterministic; then window
  size, resize notification, PTYs, sessions, and foreground groups get separate
  backlog IDs before curses or an editor.

### NET-01 — mock connect-only byte stream

- **Status:** Blocked on IO-01
- **Base:** integrated dependency
- **Hypothesis:** a socket-like descriptor over a mock transport can establish
  portable stream semantics before any real host network adapter exists.
- **Red:** ordinary socket/connect/send/recv source is absent.
- **Accept:** connect failures, partial I/O, EOF, shutdown, dup/inheritance, and
  poll readiness are deterministic. SOCKS5, name resolution, real host I/O,
  listening, datagrams, and virtual NICs remain separate tasks.

## Completed Iterate Bot loops

- [x] **Iteration 1: preserve open RAMFS files across unlink.** Hypothesis: the
  directory unlink path frees a node still referenced by an open-file object.
  Falsifying test: a native probe opens and writes a file, unlinks its path,
  seeks and reads through the still-open descriptor, verifies the pathname is
  absent, then closes the descriptor under ASan/UBSan. Define the intended Unix
  lifetime semantics in `SPEC.md` before making the implementation pass.
- [x] **Iteration 2: make pipe construction failure atomic.** Introduce a
  deterministic allocation-failure seam, fail each allocation in `pipe()`, and
  prove there is no null dereference, leak, or partially installed descriptor.
- [x] **Iteration 3: install a fresh-build test discipline.** Prevent `make test`
  from silently reusing sanitizer-built artifacts with different flags; make
  normal and sanitizer configurations distinguishable or force correct rebuilds.
- [x] **Iteration 4: expose the agreed `bsdinacan` launcher.** Test that
  `build/bsdinacan -c 'echo ok'` works, then rename the artifact and diagnostics
  without changing the internal project name.
- [x] **Iteration 5: zero-length pipe I/O must not block.** Spawn a peer that can
  be observed if scheduled; prove `read(pipefd, buffer, 0)` and
  `write(pipefd, buffer, 0)` return immediately without yielding, even when an
  ordinary nonempty read would block.
- [x] **Iteration 6: explicit pipe EOF and broken-pipe tests.** Prove the final
  writer close wakes readers to EOF and the final reader close makes writes fail
  with `EPIPE`.
- [x] **Iteration 7: pipe capacity and wakeup.** Move more than 4096 bytes through
  two tasks and prove a full-buffer writer blocks, a reader wakes it, byte order
  is preserved across ring wrap, and neither task starves.
- [x] **Iteration 8: terminal descriptor and interactive-I/O gate.** Prove fd
  0/1/2 types and directionality, stdout/stderr separation, zero-byte input,
  host-read error translation, prompt/line/EOF behavior, and interactive exit
  status with a deterministic console backend.
- [x] **Iteration 9: descriptor sharing and inheritance.** Prove `dup`/`dup2`
  shared offsets and replacement, spawn inheritance and isolation, CLOEXEC
  retention/closure, invalid descriptor errors, and cleanup.
- [x] **Iteration 10: process and scheduler contract.** Prove PID 1, unique PIDs,
  PPID, copied argv/environment, cwd/environment independence, blocking wait,
  zombie collection, `ECHILD`, explicit yield order, and wake reasons.
- [x] **Iteration 11: RAMFS contract.** Cover the initial hierarchy, live path
  resolution, creation modes, independent contents, holes, stat/inode
  stability, mkdir/unlink error cases, and per-task cwd.
- [x] **Iteration 12: shell and base-command contract.** Cover quotes, escapes,
  empty words, braced and quote-dependent expansion, syntax errors, every
  built-in, native command status/errors, redirection failures, and cleanup.
- [x] **Iteration 13: ABI and capability contract.** Test wrong versions/sizes,
  exact capabilities, host-clock operations, and program-registration limits.
- [x] **Iteration 14: explicit native executor boundary.** Route preparation,
  instance creation, resume, suspension, termination, and destruction through
  a generic versioned executor table; keep native contexts out of the task core.
- [x] **Iteration 15: explicit VFS boundary.** Route pathname and lifecycle
  operations through versioned mount/node operation tables rather than calling
  RAMFS entry points directly from the program API.
- [x] **Iteration 16: release evidence and portability audit.** Prove the
  one-process model through Linux `/proc`, total runtime allocation cleanup,
  dirty host-allocation independence, descriptor readiness, signed byte-count
  and offset overflow handling, registration-source ownership, complete README
  instructions, and the exact Alpine Woodpecker command.
- [x] **Iteration 17: first libc/source-compatibility slice.** Add task-owned
  allocation operations with exit and exec reclamation. Build a prefixed libc
  veneer and startup adapter, then compile an external ordinary `main()` in a
  separate translation unit that includes no runtime-private header. Prove it
  with `echo -n hello | wc -c` producing exactly `5`.
- [x] **Iteration 18: task-local libc errno.** Append a writable errno-cell
  operation without exposing a task object. Prove distinct cells and retained
  values across cooperative interleaving, zero state after successful `exec`,
  and ordinary-source `errno` plus `strerror()` after a failed `open()`.
- [x] **Iteration 19: first unmodified NetBSD utility.** Pin and import
  `usr.bin/yes/yes.c` from NetBSD commit `b890038f7ae5`, retain its file license,
  and compile it byte-for-byte unchanged. Add only the `puts`, `EXIT_FAILURE`,
  and metadata-header compatibility it requires. A test reader consumes one
  line and closes the pipe, requiring exact `ok\n` output and clean `EPIPE`
  termination.
- [x] **Iteration 20: first unmodified NetBSD libc routine.** Pin and import
  `common/lib/libc/string/strlen.c` from the same NetBSD revision, retain its
  file license, and compile it byte-for-byte unchanged under the private
  `cb_libc_strlen` link name. Require `libcannedbsd.a` to contain the separate
  object, prevent a host `strlen` import, and route `puts` through the routine.
- [x] **Iteration 21: NetBSD `strcmp`.** Pin the matching generic NetBSD source,
  compile it unchanged as a distinct `cb_libc_strcmp` archive member, and prove
  signedness-sensitive ordering semantics. Replace the bootstrap `wc` command's
  private string comparison with the standard interface and exclude a host
  `strcmp` dependency.
- [x] **Iteration 22: complete the basic allocation veneer.** Add `calloc` and
  `realloc` over the task-owned program heap. Prove zero fill, multiplication
  overflow, zero-size behavior, growth and shrink preservation, foreign-pointer
  rejection, and failed-resize atomicity with deterministic failure injection.
- [x] **Iteration 23: NetBSD `memcpy`.** Pin both the generic `memcpy.c` wrapper
  and its included `bcopy.c`, compile the upstream size-optimized implementation
  unchanged under `cb_libc_memcpy`, and prove return value, zero-length, whole
  and offset buffers, and high-bit byte copying without a host `memcpy`
  dependency.
- [x] **Iteration 24: NetBSD `memmove`.** Reuse the pinned `bcopy.c` engine
  through the matching unchanged NetBSD wrapper and a private link name. Prove
  zero-length, returned-pointer, forward-overlap, and backward-overlap behavior
  from both direct and ordinary-source boundaries.
- [x] **Iteration 25: NetBSD `memcmp`.** A pinned NetBSD `printenv` compile
  inventory identified `memcmp` as a missing independent dependency. Import
  the generic routine unchanged under a private link name and prove zero-length,
  equality, unsigned ordering, archive provenance, and ordinary-source routing.
- [x] **Iteration 26: NetBSD `strchr`.** Continue the pinned `printenv` inventory
  with its smallest independent missing string operation. Import NetBSD's
  generic routine unchanged and prove first-match, missing-character, terminal
  NUL, integer conversion, provenance, archive, and ordinary-source behavior.

## Unrefined roadmap — usable-system demo

These ideas are not claimable until an integrator turns one into a worker item
with an ID, dependencies, red test, and acceptance boundary above.

- [ ] Represent `/bin/sh` and native commands as real runtime-visible executable
  objects instead of treating the registry as an invisible path oracle.
- [ ] Choose a deliberately bounded route to `sed` and write its compatibility
  tests before importing or implementing regex machinery.
- [ ] Choose a deliberately bounded route to `awk` and test scripts, fields,
  variables, stdin, and pipelines before implementation.
- [ ] Add terminal capability and size APIs, followed by a small curses/dialog
  demonstration. Do not import ncurses until the terminal contract is tested.
- [ ] Add a tiny vi-like editor only after filesystem update, terminal mode,
  resize, and signal/cancellation semantics have explicit tests.

## Unrefined roadmap — portability and extensions

- [x] Initial System 7 / 68K host slice on the isolated `mac-system7` branch:
  dedicated Retro68 Woodpecker build, stack-yield probe, seven guest shell
  acceptance cases, and interactive pipeline verified on 2026-09-07.
  Broader classic-host coverage remains open below.

- [ ] Split and test a classic-host adapter contract for Mac OS 7/9, NeXTSTEP,
  AmigaOS, OS/2, Solaris 9/sun4m, and other targets in `TARGETS.md`.
- [ ] Prototype outbound TCP through a host-provided SOCKS service. Specify it as
  a narrow capability, not as a general network stack.
- [ ] Prototype a common in-can IP stack plus virtual NIC; evaluate libslirp only
  below that NIC boundary.
- [ ] Define dynamic native command modules.
- [ ] Define the portable `.cbwasm` executor as a secondary ABI.
- [ ] Add persistent and mounted filesystems, PTYs, compilers/interpreters, and
  eventually an X11 server/window integration.

## Friction log

- 2026-09-07 (resolved): merge pipelines #19/#20 exposed an intermittent libc
  symbol-check failure. Early-exit `rg -q` consumers could SIGPIPE `nm`/`ar`
  under `pipefail`; a controlled producer reproduced status 141. The checks
  now consume all producer output while retaining matching and error checks.

- 2026-09-06 (resolved): Woodpecker pipeline #3 caught a Clang-only
  maybe-uninitialized warning in `test_vfs_contract`; initializing the test
  pointer made GCC and Clang agree. A pre-push run in the exact agent image then
  caught GNU `find -printf` in the new `/proc` test; a Bash pathname glob now
  works under Alpine BusyBox as well as Ubuntu.
- 2026-09-06 (resolved): `.gitignore` used `core.*` for crash dumps, which also
  excluded the essential `src/core.c`; it was absent from the initial commit.
  Dump patterns are now root-anchored as `/core` and `/core.*`, and `src/core.c`
  is included in Iteration 8's commit.
- 2026-09-06: `make test` reused binaries produced by `make sanitize`; Make does
  not encode `CFLAGS`/`LDFLAGS` in target freshness. This can confuse evidence
  about which build was tested. Tracked as Priority 1, Iteration 3.
- 2026-09-06 (resolved): no git metadata was present. A local repository was
  initialized after the first five documented loops; future loops can produce
  one reviewable commit each. No remote is configured.
- 2026-09-06: the current test harness is a single raw-C executable. That is
  sufficient for now; adding a framework would be ceremony until isolation,
  diagnostics, or selective execution becomes painful.

## Completed

- [x] **Iteration 24 (2026-09-06): NetBSD `memmove`.** The red boundary run
  failed on the absent pinned wrapper. NetBSD's unchanged `memmove.c` now
  reuses the existing hash-pinned `bcopy.c` engine, compact implementation,
  private definition link-name adapter, and archive discipline. Direct tests
  prove return and zero-length behavior plus overlapping copies in both
  directions; an ordinary-source object must import only `cb_libc_memmove`.
  The canonical gate passes locally and in the exact Alpine Woodpecker agent
  image.

- [x] **Iteration 23 (2026-09-06): NetBSD `memcpy`.** The red source-boundary
  run failed because both pinned inputs were absent. NetBSD's tiny `memcpy.c`
  wrapper and shared `bcopy.c` implementation are now hash-pinned and compiled
  unchanged with NetBSD's own size-optimized byte-copy branch. The first link
  failed because host fortification makes upstream undefine `memcpy`; the
  definition-side GCC/Clang link-name adapter now handles that case without
  touching upstream source. Object and ordinary-source tests exclude host
  `memcpy`; direct cases prove return, zero-length, offset, sentinel, NUL, and
  high-bit-byte behavior. ARM alias support remains explicitly unclaimed. The
  canonical gate passes locally and in the exact Alpine Woodpecker agent image.

- [x] **Iteration 22 (2026-09-06): basic allocation veneer.** The red build
  failed because `cb_libc_calloc` and `cb_libc_realloc` were absent. `calloc`
  now rejects multiplication overflow with `ENOMEM`, allocates on the existing
  task heap, and clears every requested byte; `realloc` delegates to the
  ownership-enforcing resize operation. A task-level probe covers zero-size
  allocation, overflow, grow/shrink preservation, foreign pointers, and a
  deterministically failed resize that leaves the original allocation intact.
  A separate ordinary-source object spells `calloc` and `realloc` and must
  import only their private veneer symbols. The canonical gate passes locally
  and in the exact Alpine Woodpecker agent image.

- [x] **Iteration 21 (2026-09-06): NetBSD `strcmp`.** The red boundary test
  failed on the absent pinned source. NetBSD's generic `strcmp.c` is now
  hash-pinned, compiled unchanged, and archived separately as
  `cb_libc_strcmp`. A naïve macro rename then failed to link because upstream
  deliberately undefines `strcmp`; the GCC/Clang header adapter now assigns the
  private assembler link name without touching the source, and the portability
  ledger records the need for an equivalent on other compilers. The ordinary
  bootstrap `wc` now imports private NetBSD `strcmp` and `strlen` through their
  standard spellings. Direct tests include unsigned-byte and prefix ordering.
  The canonical gate passes locally and in the exact Alpine Woodpecker agent
  image.

- [x] **Iteration 20 (2026-09-06): first unmodified NetBSD libc routine.** The
  red boundary test failed because the pinned `strlen.c` source was absent.
  NetBSD's generic routine is now hash-pinned, compiled byte-for-byte unchanged
  as `cb_libc_strlen`, and retained as its own `libcannedbsd.a` member. The first
  build exposed the host's `assert.h` depending on the deliberately small
  cannedBSD `sys/cdefs.h`; a private import-only shim now isolates that unused
  include and a guard rejects the shim if upstream begins using `assert()`.
  Symbol tests exclude host `strlen`, `puts` consumes the imported routine, and
  direct tests cover empty, ordinary, embedded-NUL, and interior-pointer input.
  The canonical gate passes locally and in the exact Alpine Woodpecker agent
  image.

- [x] **Iteration 19 (2026-09-06): first unmodified NetBSD utility.** The red
  command test returned status 127 because `yes` did not exist. NetBSD's
  `usr.bin/yes/yes.c` is now pinned at commit `b890038f7ae5` and compiled
  byte-for-byte unchanged; a SHA-256 test protects the source and its provenance
  record. Compatibility headers and a prefixed, partial-write-safe `puts` are
  implemented outside the imported file. A one-line pipeline proves exact
  `ok\n` output, while a direct two-child probe waits for both tasks and proves
  the writer returns `EXIT_FAILURE` after final-reader close produces `EPIPE`.
  The canonical gate passes locally and in the exact Alpine Woodpecker agent
  image.

- [x] **Iteration 18 (2026-09-06): task-local libc errno.** The first red run
  made `abiprobe` return 181 because the new errno-location operation was
  absent. Each task now owns a separately allocated scalar errno cell shared by
  get/set operations and the libc lvalue; direct tests prove distinct addresses
  and retained values across cooperative yields plus zero after successful
  exec. The second red run failed because `libc/include/errno.h` did not exist.
  The libc now supplies its declared errors, a modifiable `errno`, and
  `strerror()`. The ordinary `wc` source reports a failed open through that
  path while preserving errno across diagnostic writes. Normal, ASan/UBSan,
  architecture, and analyzer checks pass locally and in the exact Alpine
  Woodpecker agent image.
- [x] **Iteration 17 (2026-09-06): first libc/source-compatibility slice.** The
  allocation contract's red build failed on the absent task-allocation
  observer. The program API now appends task-owned allocate, resize, and
  release operations. Focused tests prove atomic failure, ownership, resize,
  release, exit-before-wait reclamation, successful-exec reclamation, and final
  teardown. The source-boundary test then failed because no external command
  existed. `commands/wc.c` now compiles separately with an ordinary `main()`
  and standard `read`, `write`, `open`, `close`, `malloc`, and `free` spellings
  provided by a prefixed libc veneer. Its exact integration output is `5`.
  The complete canonical gate passes locally and in the exact Alpine
  Woodpecker agent image, including Clang ASan/UBSan.
- [x] **Iteration 16 (2026-09-06): release evidence and portability audit.**
  `tests/test_one_process.sh` holds a three-task internal pipeline active while
  `/proc` reports one host thread and no host children. An allocation ledger
  returns to zero after the complete acceptance workload. A host allocator that
  fills new memory with `0xa5` first crashed the runtime; the core now clears
  every new object independently of the backend. `overflowprobe` first returned
  status 192; oversized read/write counts now fail before backend dispatch, and
  signed seek overflow is verified. Tests also cover open-file readiness and
  mutation of a descriptor source after registration. README and CI guidance
  now describe prerequisites, native commands, and the one-process gate. The
  exact Alpine agent image passed the complete canonical command.
- [x] **Iteration 15 (2026-09-06): explicit VFS boundary.** The architecture
  test first failed because there were no mount or node operation tables. Path
  normalization, traversal, cwd rendering, task errno translation, and root
  installation now live in `vfs.c`; the private RAMFS representation lives in
  `ramfs.c`. Versioned mount operations own root discovery/destruction, while
  versioned node operations own retain/release, lookup, create, unlink, open,
  stat, parent, and name. `test_vfs_contract` rejects incomplete tables and
  proves root installation/lifecycle. Existing RAMFS, cwd, descriptor,
  open-unlink, redirection, and acceptance tests all pass through the boundary.
- [x] **Iteration 14 (2026-09-06): explicit native executor boundary.** An
  architecture test first failed because no executor operation table existed
  and the task core directly owned contexts and called native entry points. A
  versioned generic executor now prepares runtime-owned program objects and
  owns per-task execution instances. A delegating test executor proves prepare,
  create, start/resume, suspend, requested termination, instance destruction,
  and program destruction. Existing spawn, exec, wait, pipeline, and acceptance
  tests pass through the boundary; the architecture gate rejects native entry
  or task-context details in `core.c`.
- [x] **Iteration 13 (2026-09-06): ABI and capability contract.** A direct test
  first crashed when a required host callback was null. Kernel
  creation now validates every v1 host operation before dereferencing the
  table; new registration checks reject malformed descriptors—including empty
  names and unknown flags—duplicates, and entries beyond its fixed capacity.
  The Linux backend supplies both monotonic and
  Unix-epoch wall clocks. `abiprobe` verifies the complete program API table,
  exact capabilities, every declared error string, unknown-error behavior,
  and errno isolation across cooperatively interleaved parent and child tasks.
- [x] **Iteration 12 (2026-09-06): shell and base-command contract.** Focused
  black-box cases exposed four independent gaps: invalid environment names were
  accepted, nonnumeric `exit` silently became zero, built-ins were rejected in
  pipelines, and redirection setup failure was overwritten with status 127.
  The runtime now enforces identifier syntax, `exit` parses signed decimal
  status strictly, pipelined built-ins run as isolated internal tasks, and setup
  failures preserve status 1. Tests cover quoting/escaping/empty words,
  expansion and timing, syntax errors, all built-ins, pipeline status/isolation,
  combined redirection, cleanup after partial spawn, and every required native
  command's success and error paths.
- [x] **Iteration 11 (2026-09-06): RAMFS contract.** `ramfsprobe` first returned
  status 170: truncation reset logical size but a later sparse write within the
  retained allocation exposed stale bytes. Writes now zero-fill from the old
  logical end to the new write offset, and zero-byte writes do not extend files.
  The probe also covers the initial hierarchy and modes, root confinement,
  relative live lookup, independent objects, stable inode/stat metadata,
  append, access modes, mkdir/unlink errors, and cwd error behavior.
- [x] **Iteration 10 (2026-09-06): process and scheduler contract.** The first
  focused run returned status 152 because a parent resumed from blocking wait
  without a recorded cause. Tasks now record pipe-change, console-ready, and
  child-exit wake reasons, while voluntary yield records none. Tests prove PID
  1 parentage, monotonic child PIDs, copied argv/environment, cwd/environment
  isolation, blocking wait, exit status, zombie collection, `ECHILD`, and exec
  preservation/replacement rules.
- [x] **CI infrastructure (2026-09-06): canonical Woodpecker gate.** `make ci`
  is the single local and remote entry point. A live Alpine 3.22 agent run proved
  the GCC optimized suite, Clang ASan/UBSan suite, build-mode isolation,
  architecture checks, publication hygiene, and GCC static analysis. The
  runner image owns tool installation; the project workflow does not mutate its
  environment.
- [x] **Iteration 9 (2026-09-06): descriptor and exec semantics.** The extended
  exec test first returned status 22 because same-fd `dup2` cleared CLOEXEC and
  leaked the descriptor. The fix preserves flags on the no-op path. Direct
  tests also cover shared offsets, replacement, spawn inheritance and table
  isolation, retained descriptors across exec, invalid fds, and cleanup.
- [x] **Iteration 8 (2026-09-06): terminal and interactive I/O.** A deterministic
  host console exposed that zero-length terminal reads polled the host. The core
  now returns immediately and normalizes host read errors to `-1`/task errno.
  Tests cover fd 0/1/2 metadata/direction, console blocking and wakeup, stream
  separation, prompt/input/EOF, and interactive exit status.
- [x] **Iteration 7 (2026-09-06): pipe capacity, wakeup, and ring wrap.** A
  10,000-byte deterministic pattern forces the 4,096-byte writer buffer full;
  a reader using 777-byte chunks wakes it repeatedly and verifies byte order
  through multiple wraps and EOF. The existing implementation passed under
  normal and sanitizer builds.
- [x] **Iteration 6 (2026-09-06): pipe EOF and `EPIPE` characterization.** The
  focused test passed against the existing implementation. It observes a child
  blocked on an empty pipe, closes the final writer, and proves wakeup to EOF;
  it separately proves writing after final-reader close returns `EPIPE`. No
  behavior change was needed.
- [x] **Iteration 5 (2026-09-06): nonblocking zero-length pipe I/O.** A peer-task
  probe first returned status 66, proving zero-length `read` yielded while the
  pipe was empty. `pipe_read` now returns before examining endpoint state; both
  zero-byte read and write are verified not to schedule the peer. All normal,
  sanitizer, build-mode, analyzer, and architecture checks pass.
- [x] **Iteration 4 (2026-09-06): `bsdinacan` launcher.** The launcher test first
  failed because only `build/cannedbsd` existed. Normal and sanitizer products
  are now named `bsdinacan`; the launcher test runs against the selected build
  variant, and the normative acceptance command uses the public name. cannedBSD
  remains the project and runtime name.
- [x] **Iteration 3 (2026-09-06): isolate build modes.** The black-box regression
  test first proved that `make test` reused an ASan-linked artifact after
  `make sanitize`. Sanitizer products now live in `build/sanitize/`; ordinary
  products retain the `build/` path. `make check-build-modes` verifies the
  selected normal executable does not link `libasan`.
- [x] **Iteration 2 (2026-09-06): atomic pipe construction.** A deterministic
  allocation-failure probe first reproduced a null dereference in
  `pipe_read_close`. `pipe()` now constructs the pipe and endpoints in ownership
  order. All three allocation failures return `ENOMEM`; a one-slot descriptor
  table failure returns `EMFILE`; neither case changes the output array or
  leaks a descriptor. Sanitizer and clean optimized suites pass.
- [x] **Iteration 1 (2026-09-06): open-file lifetime after unlink.** The new
  `unlinkprobe` first produced an ASan heap-use-after-free in `node_read`.
  RAMFS nodes now count open-file-object references; unlink detaches the name
  and final close reclaims the nameless node. The same probe and full test suite
  pass under ASan/UBSan and a clean optimized build. The spec now states the
  lifetime contract.
- [x] Pre-loop prototype: versioned host and native-program interfaces, internal
  tasks, scheduler, descriptors, pipes, RAMFS, shell, base commands, Linux host
  adapter, acceptance test, sanitizer target, and architecture grep checks.
- [x] 2026-09-06: read the canonical Gilfoyle and Iterate Bot documents; adopted
  evidence-only claims, falsifying tests, immediate notes, and the
  `CURRENT-STATE.md`/`BACKLOG.md` handoff loop.

### MAC-06 — matcher readiness and local storage

- **Status:** Done; merged at `4f80e83`
- **Base:** MAC-04
- **Hypothesis:** a matcher handshake before boot detects stalled imports before
  starting a guest; local storage avoids iCloud hydration in the test path.
- **Accept:** no boot before readiness, late readiness cannot launch, owned
  helper cleanup, exact guest run with local state. Verified by #65/#66 and
  the 19.03-second cold acceptance cycle.

### MAC-07 — direct libc and filesystem guest coverage

- **Status:** Done; merged at `c93e2ba` after review, CI, and exact guest acceptance
- **Base:** `4f80e83`
- **Depends on:** PENV-03 and FS-01 (Done)
- **Hypothesis:** identical ordinary-source probes can run on Linux and System 7
  to expose target-specific string, getopt, and truncate failures.
- **Red:** the old eight-record result must not pass the expanded acceptance.
- **Accept:** share exact case definitions, execute memory/string boundaries,
  getopt and truncate probes in the guest, reject missing/duplicate records,
  and preserve fresh evidence and normal shutdown.

### IO-01-design — polling and deadline contract

- **Status:** Claimed by Antigravity; review corrections in progress
- **Base:** main
- **Hypothesis:** explicit readiness and unavailable-clock behavior can unblock
  deterministic descriptor polling without host descriptors or busy waiting.
- **Accept:** finite deadlines must never silently become infinite; specify
  task ownership, masks/errors, clock loss, wake/cancel and overflow behavior.

### MAC-08 — image-matched failure-window closure

- **Status:** Done; merged at `52cc6f9` after live close verification and CI
- **Base:** main
- **Accept:** match the cannedBSD title/go-away box, validate staged PID and
  focus, retain before/after evidence; no automatic shutdown or pass claim.

### MAC-09 — supported guest-owned autorun cycle

- **Status:** Done; merged at `ca0cd19`, exact integration autorun passed in 13.76 seconds
- **Base:** `c93e2ba`
- **Accept:** stage all marker/evidence files before boot; validate exact fresh
  results, completion and readable screenshot; observe app closure, then normal
  shutdown. Failures retain the guest and never produce acceptance.

### REUSE-01 — MacPerl/GUSI source reuse audit

- **Status:** Done; reviewed and merged at `ca0cd19` with all three integration workflows green
- **Base:** main
- **Hypothesis:** historical MacPerl and GUSI already implement useful classic
  Mac filesystem, networking and compatibility operations.
- **Accept:** identify concrete source modules, pinned provenance, actual
  licenses, System 7/68K/Retro68 constraints, and map reuse candidates to host
  adapters versus cannedBSD-owned task/VFS semantics. No runtime import until
  code and integration requirements are understood.

### VFS-03-design — directory lifetime and mutation contract

- **Status:** Done; reviewed contract implemented and accepted with VFS-03 at `6e83f00`
- **Base:** main
- **Accept:** task-owned handles release node references on close/exit/exec;
  mutation promises match the algorithm; directory names preserve existing
  path-length support; independent streams and failure cleanup are testable.

### ERR-01 — errno-bearing err(3) for pinned dirname

- **Status:** Done; merged at `02ee32b` after review, all three #102 checks and exact guest acceptance
- **Base:** main
- **Depends on:** PENV-05 (Done)
- **Hypothesis:** existing task errno, strerror, formatter and exit can supply
  the errno diagnostic required by pinned NetBSD dirname without a new ABI.
- **Source evidence:** NetBSD revision
  `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`,
  `usr.bin/dirname/dirname.c` calls err after dirname fails.
- **Red:** ordinary err.h source calling err has no private declaration/symbol.
- **Accept:** snapshot errno before writes; emit program name, supplied message,
  saved strerror and newline to fd 2; exit without continuation; test short
  writes and task interleaving, private symbols and exact CI/guest acceptance.
  Bound formats to the existing formatter and document that boundary.

### REUSE-02 — MoreFiles read-only catalog feasibility probe

- **Status:** Done as a negative feasibility result; reviewed and merged at `819a964`; no host backend claimed
- **Base:** main
- **Depends on:** REUSE-01 (Done)
- **Hypothesis:** a small pinned MoreFiles catalog subset compiles under our
  Retro68 toolchain and correctly reads a protected real HFS fixture.
- **Red:** record actual compile/link or guest failure if present; a successful
  characterization requires no manufactured red.
- **Accept:** per-file provenance/licenses, bounded compatibility shims, exact
  CI probe artifact, directory/type/fork-length/callback/error checks, unchanged
  fixture hash and normal guest shutdown. Keep this a separate host probe;
  no changes to core VFS, directory streams or polling.

### CI-01 — concurrent printenv capture isolation

- **Status:** Done; main `0ebfd2f`, all three CI checks and eight concurrent
  real-binary behavioral matrices passed.
- **Base:** main
- **Accept:** independent temporary captures and cleanup across simultaneous
  local Woodpecker workflows. No runtime changes.

### MAC-10 — direct polling and pipe guest acceptance

- **Status:** Done; merged at `819a964`, seventeen fresh guest records in 15.31 seconds
- **Base:** reviewed IO-01 integration `6b71813`
- **Accept:** compile and execute unchanged ordinary normalpollprobe in System 7,
  covering pipe readiness, hangup, descriptor exhaustion and errno. Seventeen
  fresh records; finite deadline injection remains a separate Linux test.

### LIBGEN-01-design — dirname source and result lifetime

- **Status:** Done; reviewed design merged at `89afe30`
- **Base:** main
- **Accept:** pinned source inventory, path edge cases, task-local result
  lifetime, cleanup, C-locale dependency and a bounded deterministic test plan.

### TERM-01-design — actual host terminal capability contract

- **Status:** Reviewed staged design merged at `d3884af`; live integration blocked on output service
- **Base:** main
- **Accept:** actual Mac canonical-buffer and Linux inherited-tty inventory;
  shared core terminal owner, optional raw capability, readiness/EOF/overflow
  semantics and deterministic tests before implementation.

### REUSE-03 — bounded catalog wrapper using existing SDK records

- **Status:** Done at `a9346ea`; thirteen catalog checks and normal eighteen-check guest suite accepted
- **Base:** main
- **Depends on:** REUSE-02 (Done)
- **Hypothesis:** a clearly marked derivative of the pinned MoreFiles
  single-directory catalog loop can use existing PBGetCatInfoSync records
  without the unsupported broad helper headers.
- **Accept:** retain notices and extraction provenance; accept explicit volume
  reference/directory ID, copy callback metadata, bound index/count, propagate
  access errors, and use only real SDK layouts. Compile a separate probe,
  verify a protected HFS fixture and exact CI artifact in the guest; no core
  mount or resumable directory-stream claims. A concrete compile blocker is
  a useful negative result, not permission to invent ABI types.

### MAC-11 — reject locked-host guest launches

- **Status:** Done at `592ae41`; exact #154 CI and eighteen-check unlocked guest regression passed
- **Base:** main
- **Depends on:** MAC-06, MAC-09
- **Scope:** Hammerspoon session preflight before matcher startup and immediately
  before guest launch. A positively locked session must fail without launching,
  accepting, or releasing the staged guest slot. Handle real unlocked session
  dictionaries explicitly; an absent lock key alone must not be interpreted as
  a documented false value.
- **Acceptance:** deterministic locked, unlocked, unknown and lock-during-startup
  cases; exact Woodpecker gates; fresh unlocked guest regression when available.
  Keep the existing locked-host run and its disk ownership undisturbed.

### MAC-12 — optional pre-match window and cursor calibration

- **Status:** Done at `894b753`; exact Woodpecker #230 and fresh calibrated guest accepted
- **Base:** main after accepted BASENAME-01 `5906b9c`
- **Depends on:** MAC-06, MAC-09, MAC-11 (Done)
- **Hypothesis:** optional placement of the verified emulator window and parking
  the host cursor before image matching prevents the measured overlay/focus and
  cursor-over-template misses without relaxing any existing safety checks.
- **Evidence:** PROGNAME-01 `run-3h5wky79` had multiple prelaunch focus/mouse
  interruptions with empty output evidence; BASENAME-01 `run-rt_33o6i` had a
  focus interruption before output. Both subsequently completed as resumed runs.
- **Scope:** bounded, explicitly configured window positioning and cursor parking
  in the existing driver before matching. Preserve target PID/window validation,
  locked-session and foreground/focus checks, image-confidence rejection, staged
  evidence freshness, normal shutdown and slot ownership. Do not automatically
  dismiss dialogs, accept ambiguous matches, or claim focus from window position.
- **Red:** deterministic driver fixtures must expose a configured calibration
  that is omitted or occurs after matching; cover wrong/missing target, failed
  positioning, focus loss, and disabled configuration in mocks without driving
  the shared guest.
- **Accept:** prove calibration precedes matching only for the verified target;
  preserve default behavior when disabled and fail safely on calibration errors.
  Run exact CI and a coordinator-owned guest regression with reviewed screenshot,
  full fresh transcript, normal shutdown and slot release. Record interruptions
  and distinguish resumed elapsed time from a cold-boot measurement.

### LIBGEN-01 — task-local dirname

- **Status:** Done at `96d5936`; included in twenty-eight accepted Mac checks
- **Base:** main
- **Depends on:** LIBGEN-01-design (Done), ERR-01 (Done)
- **Scope:** implement the reviewed `notes/iterations/LIBGEN-01-design.md`.
  Preserve the pinned NetBSD source, use import-only `PATH_MAX == CB_PATH_MAX`,
  copy into inline task-owned storage with existing strlen/memcpy, and append
  an optional accessor after the actual ABI tail. No locale or strlcpy work.
- **Accept:** ordinary-source edge cases, independent interleaved task results,
  old-size/null-callback behavior, exact CI and direct fresh Mac acceptance.

### TERM-02 — console classification and honest fallback

- **Status:** Done at `592ae41`; eighteen Mac records passed in 13.21 seconds
- **Base:** main
- **Depends on:** TERM-01-design (Done)
- **Scope:** first stage of the reviewed terminal design: shared console owner,
  private isatty classification, optional public-call guards, and honest ENOSYS
  attribute fallback. Existing input/poll behavior remains the regression
  contract; no real host enters raw mode and no output-wait ABI is introduced.
- **Accept:** console/dup/child classification, file/pipe/invalid descriptors,
  old-size and individually absent operations, ordinary source symbols, exact
  CI and fresh Mac fallback evidence. Add only errors demonstrated missing.

### TERM-03 — isolated canonical engine

- **Status:** Blocked on TERM-02
- **Base:** integrated dependency
- **Scope:** deterministic queue/record/erase/EOF transitions without live host
  routing, following the accepted terminal design's fixed storage bounds.
- **Accept:** delimiter processing at capacity, one-shot versus persistent EOF,
  short-read ordering, bounded echo acknowledgments and backpressure retention.

### TERM-04 — attribute validation and mode transitions

- **Status:** Blocked on TERM-03
- **Base:** integrated dependency
- **Scope:** atomic supported-attribute updates and repeated raw/canonical
  transitions over the isolated engine. Real hosts still use fallback.
- **Accept:** rejected modes preserve all state, task sharing, 2048-byte input
  and three-byte echo bounds, ordering through repeated mode changes.

### TERM-05-design — bounded host output service

- **Status:** Ready for design only
- **Base:** main
- **Scope:** resolve the explicitly open output progress, completion and
  cancellation contract before scheduler or raw-adapter implementation.
- **Accept:** retries independent of stdin, bounded waits/writes, finite
  deadlines, preserved echo/input under backpressure and orderly cleanup.
  Inventory the real host callbacks; do not advertise a speculative ABI.

Scheduler/mock-lease integration, Linux raw control and Mac raw control remain
blocked until TERM-04 and TERM-05-design are accepted. Give those later stages
separate IDs before assigning implementation.

### LOCALE-01 — C-only locale needed by NetBSD dirname

- **Status:** Done at `13fb618`; twenty-one accepted Mac checks
- **Base:** main
- **Scope:** measured `setlocale(LC_ALL, "")` dependency; private locale header,
  honest C/POSIX support, task-environment selection, unsupported requests
  return NULL with no state change and no host locale calls.
- **Accept:** ordinary-source private symbols, empty-string environment
  precedence, explicit C/POSIX, query, rejection and task isolation; exact CI
  and guest evidence. No full locale subsystem or speculative categories.

### DIRNAME-01 — unchanged NetBSD dirname command milestone

- **Status:** Done at `96d5936`; exact streams/status and fresh Mac acceptance
- **Base:** integrated dependencies
- **Scope:** import pinned `usr.bin/dirname/dirname.c` unchanged with module,
  source hash/license and private linkage. See `notes/iterations/DIRNAME-01-plan.md`.
- **Accept:** exact stdout/stderr/status for paths, root/repeated slashes,
  empty input, argument-count/option failures, dash-leading path, repeated
  invocation and pipeline; exact Woodpecker artifact runs in Basilisk II.

The next visible milestone is unchanged NetBSD `head`; `echo` and STDOUT-01
are accepted. Defer broader terminal and filesystem expansion unless needed
for the active command milestone or a reviewed correctness fix.

### BASENAME-01 — unchanged NetBSD basename utility and libc prerequisite

- **Status:** Done at `5906b9c`; exact Woodpecker #219 all three checks and
  guest `run-rt_33o6i` with 38 PASS records accepted
- **Base:** main after accepted dirname milestone `96d5936`
- **Depends on:** DIRNAME-01, LOCALE-01 (accepted in main)
- **Scope:** pinned unchanged libc basename plus command, separate writable
  task-owned buffer, optional accessor after actual ABI tail, private symbols
  and existing import shims. See `notes/iterations/BASENAME-01-plan.md`.
- **Accept:** exact command streams/status including suffix and empty-input
  distinctions, libc boundary/truncation/input preservation, interleaved tasks,
  independent retained dirname results, old/null accessor, source pins and
  actual Mac command/probe registration; exact CI and fresh guest acceptance.
  Preserve production capacity and every existing executed test.

### ECHO-01 — unchanged NetBSD echo after measured prerequisites

- **Status:** Done at `b2dc0ce`; exact Woodpecker #254 and fresh 50-case Mac run accepted
- **Base:** integrated dependencies
- **Depends on:** PROGNAME-01, STDOUT-01
- **Hypothesis:** unchanged pinned bin/echo/echo.c will correctly report output
  failures through the owned libc stream state as well as reproduce its output.
- **Red:** exact audited source currently fails compilation on four missing
  interfaces; a fake ferror would fail an injected earlier-write-error case.
- **Accept:** pin source/license/hash and private symbols; scope any unavoidable
  unused-argc warning exception to this import. Compare exact streams/status
  for empty input, spaces, leading -n, literal --/-e/backslashes, write failure
  and independent subsequent task success. Require exact CI and fresh guest
  acceptance. Preserve the existing cannedBSD echo behavior unless the reviewed
  design explicitly chooses and tests its command-resolution relationship.

### HEAD-01 — unchanged NetBSD head utility milestone

- **Status:** Done at `e65e36f`; independent clean review, all three exact #324
  workflows and fresh 65-record Mac acceptance (21 internal head cases)
- **Base:** integrated accepted dependencies
- **Depends on:** GETOPT-02, ERR-02, ARGV-01, STRCPY-01, CONV-01,
  STDIN-01/02/03, STDOUT-01, FWRITE-01 and reviewed PORT32-01 (Done)
- **Scope:** unchanged pinned head source, private import namespace, owned
  128 KiB command stack request, exact native/shared Mac command fixtures.
- **Evidence:** `notes/iterations/HEAD-01.md` records initial missing-interface
  compile diagnostics separately from behavioral controls. Default 10 to 9
  lines fails case 0/status 20; stopping after one byte block fails case
  13/status 33; restored source passes. Neither is claimed as prior TDD.
- **Accept:** exact private build and real 65538-byte RAMFS output verification,
  source/hash/symbol fences, full exact CI and fresh Mac execution. Isolated
  compiler frame sizes do not establish total peak stack or a safe margin.

### PROGNAME-01 — startup-initialized public program identity

- **Status:** Done at `a7acb54`; exact Woodpecker #213 all three checks and
  guest `run-3h5wky79` with 31 PASS records accepted
- **Base:** main
- **Depends on:** ECHO-01-design (Done)
- **Scope:** implement the reviewed program-identity section of
  `notes/iterations/ECHO-01-design.md`: saved startup reference, private public
  getter selecting the last component, and startup-initialized inert setter.
- **Red:** argv[0] pointer replacement must not rename the program; restore the
  vector before teardown. Cover plain/path/empty final component and task/exec
  isolation, preserving failed-exec identity and current diagnostic cases.
- **Accept:** no host dependency, independent ordinary probe, exact CI and fresh
  Mac acceptance. No speculative process-title or mutable-name API.

### STDOUT-01 — task-owned standard-stream errors

- **Status:** Done at `894b753`; coordinator integration, exact Woodpecker #230
  and guest `run-mraq4k0o` with 42 PASS records accepted
- **Base:** main
- **Depends on:** ECHO-01-design (Done)
- **Scope:** implement reviewed optional versioned stream-state accessor and
  putchar/fflush/ferror with participation by existing stdio output paths.
  Follow `notes/iterations/ECHO-01-design.md` exactly for errno, invalid stream,
  missing/malformed state, unbuffered flush, sticky flags and exec lifetime.
- **Red:** partial write followed by failure remains visible after successful
  output, without contaminating stderr or another task; zero progress is EIO.
- **Accept:** deterministic mock failures and task interleaving, old-size/NULL/
  bad-state rejection before new output, successful/failed exec tests, ordinary
  Mac probe, exact CI and guest acceptance. Preserve capacity and existing tests.

### ARGV-01 — own argument storage separately from mutable argv slots

- **Status:** Done at `4ee800e`; exact Woodpecker #242 and fresh 43-case Mac run accepted
- **Base:** accepted main `894b753` or its documentation descendant
- **Depends on:** PROGNAME-01 (Done); prerequisite for HEAD-01
- **Evidence:** pinned head obsolete() allocates a replacement argument and writes
  its pointer into argv. Current exit frees that tracked replacement, then reap
  frees it again through the mutated argv; the original string is lost. Successful
  exec and live-kernel destruction have the same ownership conflict. This is a
  static finding pending an executed regression, not a claimed observed crash.
- **Scope:** own original argument allocations independently of the mutable vector
  exposed to ordinary main. Preserve startup-name lifetime, argument content edits,
  optional exec replacement and failure cleanup. No ABI expansion or global limits.
- **Accept:** ordinary probe replaces argv[1] with malloc storage without restoring
  it, then exits/reaps, successfully execs, or fails exec then exits. Verify original
  and replacement allocations each released exactly once, including live-kernel
  destruction and allocation-failure unwind. Require exact CI and a fresh Mac
  artifact probe, preserving all existing tests and scoped fixture capacity.

### GETOPT-02 — required option arguments for unchanged head

- **Status:** Done at `b2dc0ce`; exact Woodpecker #254 and fresh Mac argument/lifecycle probes accepted
- **Base:** main
- **Depends on:** reviewed HEAD-01 dependency plan
- **Scope:** extend the existing task-owned parser for single-colon required
  arguments, attached or separate. Preserve stopping at the first operand and
  `--`, clustered flags and existing diagnostics. A leading colon selects silent
  missing-argument `:`; otherwise return `?` with diagnostic when opterr permits.
  No permutation, getopt_long, or double-colon optional-argument extension.
- **Accept:** exact option/optarg/optind/optopt results for `-n10`, `-n 10`,
  `-qn10`, dash-leading values, missing values, unknown flags and literal `--`.
  Clear optarg where no argument is returned. Interleave two real tasks with
  different arguments and verify existing exec reset. Assert exact stderr for
  missing arguments and suppression. Existing printenv/dirname/basename behavior
  remains tested. Require exact CI and fresh ordinary Mac probe acceptance.

### ERR-02 — returning warn diagnostic for unchanged head

- **Status:** Done at `6f860c4`; exact #266 CI and fresh 54-record Mac acceptance
- **Base:** main
- **Depends on:** ERR-01 (Done), reviewed HEAD-01 dependency plan
- **Scope:** expose private `warn` using the existing bounded formatter and saved
  startup identity. Snapshot errno for its message and return to the caller;
  preserve incoming errno. No warnx or broader format language is required.
- **Accept:** exact stderr for ordinary, empty and null format with representative
  errors, no stdout, execution continues afterward, and failing stderr does not
  terminate the task or overwrite the original errno. Source fence plus exact
  CI and fresh ordinary Mac probe acceptance.

HEAD-01's original dependency audit is `notes/iterations/HEAD-01-plan.md`.
Its historical missing-interface list is superseded by the task statuses below
and the current accepted libc inventory in `LIBC.md`.
Those prerequisites were split into independently tested tasks and are now
accepted. Future stream or integer extensions still require explicit design.

### CONV-01-design — measured numeric and small string prerequisites for head

- **Status:** Reviewed clean at `fe51f3d`; actual Retro68 compile measurements recorded
- **Base:** main
- **Depends on:** reviewed HEAD-01 dependency plan
- **Scope:** measure pinned head's strtoimax/ERANGE/intmax limits against the
  actual Retro68 and Linux types. Evaluate unchanged pinned libc import and
  its private symbol/compiler-helper requirements. Specify conversion bases,
  whitespace/sign/endptr/no-digits, boundary saturation and errno contracts,
  separating strcpy and isdigit into bounded subsequent tasks.
- **Accept:** source hashes, concrete diagnostics or compiler type checks,
  exact boundary test matrix, no host-header leaks or speculative APIs. No
  runtime implementation or guest claim in this design task. Keep 32-bit
  classic Mac compatibility; no 16-bit port work is assigned.

### STDIN-01-design — owned read-only streams for unchanged head

- **Status:** Reviewed clean at `5fcc687`; all three exact #262 CI checks passed
- **Base:** main
- **Depends on:** HEAD-01 dependency plan, STDOUT-01 (Done)
- **Scope:** design unbuffered stdin/fopen/getc/fread/feof/fclose and input ferror
  support from actual runtime ownership. Preserve immutable standard-stream
  identities while isolating EOF/error per task; define dynamic wrapper and fd
  cleanup on failure, exit and exec. Preserve old-size/absent optional ABI behavior.
- **Accept:** exact element-count, partial-read, zero/overflow, binary-byte, EOF
  versus error and ownership test matrix; explicit task/exec/rebind contracts and
  feof(stdout) errno preservation. Separate fwrite and write-mode buffering work.
  No runtime implementation or guest acceptance claim in this design task.

### STDIN-01 — task-owned stdin and input indicators

- **Status:** Done at `27bb7be`; exact #277 CI and 56-record Mac acceptance
- **Base:** coordinator integration branch `work/warn-stdin-integration`
- **Depends on:** reviewed STDIN-01-design `5fcc687`
- **Scope:** stage 1 of the reviewed design: separate optional input-state
  accessor, immutable stdin, getc, feof and input ferror, with lifecycle resets.
  Preserve the existing output ABI minimum and every accepted output behavior.
  No fopen/fclose/fread or dynamic wrappers in this step.
- **Red:** actual byte 255 versus EOF, clean EOF versus injected EIO, sticky
  task-owned flags under real interleaving, and successful versus failed exec.
- **Accept:** independent old-size/null/version guards before I/O; errno
  preservation, feof(stdout), rebind recovery, ordinary native/Mac probe,
  full exact CI, independent review and coordinator-owned fresh guest test.

### STRCPY-01 — private strcpy required by head obsolete arguments

- **Status:** Done at `bb1190c`; exact #280 CI and 57-record Mac acceptance
- **Base:** main
- **Depends on:** reviewed HEAD-01 plan; ARGV-01 (Done)
- **Scope:** pinned unchanged NetBSD strcpy with private declaration/renaming,
  source hash/license and existing string import conventions. The caller owns
  sufficient nonoverlapping destination storage. No broader string API.
- **Red:** ordinary-source behavior test for destination identity, exact bytes
  including terminating NUL, empty string and unsigned-byte data; distinguish
  missing-symbol setup from a behavioral negative control.
- **Accept:** canaries stay intact, input stays unchanged, no host strcpy or
  compiler substitution leakage, strict source fence and both build systems,
  exact CI and coordinator-owned Mac acceptance. Preserve all existing cases.

### STDIN-02 — read-only fopen and fclose ownership

- **Status:** Done at `0e35e50`; all three #291 checks and fresh 59-record Mac acceptance
- **Base:** main after dependency acceptance
- **Depends on:** STDIN-01, reviewed STDIN-01-design
- **Scope:** stage 2 of the reviewed design, read-only r/rb wrappers and
  task-owned list validation, acquisition rollback and close consumption.
- **Accept:** descriptor/allocation failure cleanup observed immediately;
  foreign-pointer rejection without dereference; exec success/failure,
  raw non-CLOEXEC descriptor retention, exit and early destruction. Preserve
  prior stdin/output state contracts, all tests, exact CI and Mac acceptance.

### STDIN-03 — fread complete-element counts and overflow

- **Status:** Done at `ff08dd5`; exact #301 all three CI checks and 62-record guest acceptance
- **Base:** main after dependency acceptance
- **Depends on:** STDIN-02, reviewed STDIN-01-design
- **Scope:** stage 3 of the reviewed design: bounded read accumulation,
  complete-element counts, zero-size no-op and private EOVERFLOW mapping.
- **Accept:** positive partial transfers, final partial elements at EOF/error,
  exact byte prefixes/canaries, actual target-size overflow, errno/flags,
  old-capability guards, exact CI and fresh Mac acceptance.

### FWRITE-01-design — bounded output elements for head

- **Status:** Reviewed clean and merged at `32da718`; documentation only
- **Base:** accepted runtime `6f860c4`
- **Depends on:** reviewed HEAD-01 plan and STDOUT-01 (Done)
- **Scope:** define stdout/stderr-only fwrite, completed-element/error semantics,
  zero/overflow guards, old output-state compatibility and tests. No code.
- **Accept:** source-backed head requirement and concrete implementation task
  boundary; preserve existing output behavior and require actual Mac acceptance
  for subsequent implementation. No host or buffered I/O expansion.

### FWRITE-01 — unbuffered standard-stream fwrite

- **Status:** Done at `885d83c`; all three exact #322 workflows and fresh
  64-record Mac acceptance; see `notes/iterations/FWRITE-01-review.md`
- **Base:** main at accepted file streams `0e35e50`
- **Depends on:** reviewed FWRITE-01-design; coordinate the identical EOVERFLOW mapping with STDIN-03
- **Scope:** implement the reviewed output-element design with existing output
  state; no new ABI field, writable fopen or generic buffered streams.
  Parallel STDIN-03 shares exactly CB_EOVERFLOW=84, private EOVERFLOW and
  lowercase strerror `value too large to be stored in data type`; integration
  retains one identical definition. No other input-stream changes belong here.
- **Accept:** exact bytes/element counts and sticky flags with partial/zero/error
  injection, binary ordinary probe, old-table rejection before I/O, full exact
  CI and fresh coordinator-owned Mac artifact acceptance.

### CONV-01 — pinned strtoimax and its bounded C-locale prerequisites

- **Status:** Done at `ff08dd5`; exact #301 all three CI checks and 62-record guest acceptance
- **Base:** coordinator `work/warn-stdin-integration` after reviewed design integration
- **Depends on:** reviewed CONV-01-design `fe51f3d`
- **Scope:** unchanged pinned strtoimax.c and _strtol.h, private inttypes/ctype
  surface, C-locale isdigit/isspace, ERANGE and its error string. Follow the
  measured Retro68/LP64 contracts and import-only assert/nbtool shims exactly.
  STRCPY-01 is excluded; no runtime ABI, locale tables or new compiler helpers.
- **Red:** real behavior tests or honestly labelled implementation regression
  controls for endptr, saturation, base handling and errno. Missing symbols
  are separate source-boundary evidence, not behavior red.
- **Accept:** complete reviewed base0/2..36 and invalid-base matrix, signs,
  whitespace, no conversion, full overflow digit consumption, INTMAX_MIN/MAX,
  nonzero errno preservation, NULL endptr, exhaustive unsigned-byte/EOF ctype
  tests, no host symbols, both build systems and unchanged source hashes.
  Full exact CI and fresh coordinator-owned 32-bit Mac acceptance required.

### HEAD-01-testplan — exact command fixtures for the next utility

- **Status:** Done with coordinator pipe-API correction in `32ac5ca`; exact #295 all three CI checks passed
- **Base:** main after accepted strcpy `bb1190c`
- **Depends on:** reviewed HEAD-01 plan and CONV-01-design
- **Scope:** derive concrete input bytes, argv, stdout/stderr and status fixtures
  from pinned unchanged head.c for its eventual import. Cover default lines,
  -n/-c, obsolete numeric argv, q/v header precedence, multiple files including
  missing-file continuation, stdin/pipes, empty/binary input, and 65536-byte
  boundaries. Record source locations and distinguish inferred expected results
  from actually executed reference results. Own note only; no implementation.
- **Accept:** compact deterministic fixture table, explicit upstream limitations
  and stack-budget acceptance to carry into HEAD-01; identify required runtime
  setup and scoped program registration. Do not claim a host GNU head run proves
  NetBSD behavior, or bypass missing prerequisites. Exact docs CI required.

### MAC-13 — document retirement of closed disposable boot copies

- **Status:** Done at `0e35e50`; reviewed documentation and all three #291 checks
- **Base:** main after accepted strcpy
- **Evidence:** accumulated 1 GiB boot copies exhausted host working space and
  an agent application reported ENOSPC. Accepted closed copies were retired
  while preserving all source and acceptance evidence.
- **Scope:** document free-space inspection and exact eligibility for manual
  retirement of older accepted System.dsk copies. No automatic deletion,
  emulator termination, retention timer or runtime gate change.
- **Accept:** receipt/manifest identity, no current slot, fresh open-file check,
  ordinary copied file and exact deletion boundary; preserve newest/failed/live
  runs, seed, ROM and all non-boot evidence. Documentation CI; no guest required.

### PORT32-01 — audit the next utility's 32-bit resource boundary

- **Status:** Done; reviewed doc-only `8b0b1cb`, all three exact #314 workflows;
  historical at `731b447`, with actual later HEAD evidence linked in its note
- **Base:** main
- **Depends on:** reviewed HEAD-01-testplan; existing classic Mac target
- **Scope:** Documentation-only audit in `notes/iterations/PORT32-01.md`.
  Trace the actual task stack allocation and requested_stack_size path on Linux
  and Mac, the pinned head automatic buffer, RAMFS file size/allocation limits,
  and the descriptor redirection API needed to validate a 65538-byte result.
  Cite current code locations and distinguish measured facts from unexecuted
  predictions. Use real declared APIs; do not invent helpers. Propose the
  smallest concrete Linux and Mac acceptance fixture for the head import.
- **Red:** Existing default 64 KiB stack cannot be assumed sufficient for the
  source's 65536-byte automatic buffer plus call frames.
- **Accept:** Reviewable stack/resource and fixture plan with exact arithmetic,
  pending measurements clearly labeled, no runtime changes, unchanged captures,
  and no claim of new platform support. This supports the user's required
  32-bit direction; 16-bit work remains deferred. Run publication and exact CI.

### MAC-14 — reject hidden bytes and truncated acceptance output

- **Status:** Done at `f16873d`; exact #308 all three CI checks and fresh 62-record Mac acceptance
- **Base:** main at `731b447`
- **Depends on:** current native/Mac acceptance harness
- **Hypothesis:** string-only comparisons can falsely accept output beginning
  with NUL as empty output, or ignore bytes after an expected string.
- **Scope:** shared length-aware text acceptance comparison, Mac capture
  truncation tracking, and focused regression cases; preserve capture sizes,
  expected transcript and worker runtime changes.
- **Red:** leading NUL plus nonempty bytes must not match empty expected text;
  exact text followed by NUL/suffix and a truncated prefix must fail.
- **Accept:** focused behavior tests, full exact CI and the unchanged complete
  actual Mac suite using the corrected comparison. No binary console-output
  fixture may rely on text comparison; verify binary data internally instead.

### NEXT-UTIL-02 — select the utility after head from measured dependencies

- **Status:** Reviewed audit integrated; tee import blocked on state and signal contracts; WRITE-02 accepted
- **Base:** main
- **Depends on:** accepted libc inventory and HEAD-01 (Done)
- **Scope:** Documentation-only `notes/iterations/NEXT-UTIL-02.md`. Compare
  pinned NetBSD uniq, cut and tee against current private headers. Record
  unchanged source hashes/licenses, actual includes and called interfaces,
  precise supported versus missing behavior, and a bounded recommendation.
  Compile-only diagnostics must record commands and private include paths;
  missing symbols are feasibility evidence, not behavioral regression tests.
  No host-header fallback may masquerade as a private-veneer build.
- **Red:** a current private-header compile or source contract demonstrates an
  unsupported interface for each candidate; distinguish transitive helpers.
- **Accept:** recommend the smallest coherent next milestone with exact missing
  contracts and proposed dependency task IDs, source references and explicit
  uncertainty. No implementation, speculative ABI or guest work. Publication
  and exact CI are required; coordinator reviews before activating proposals.

### HEAD-02 — characterize unchanged head input and output failures

- **Status:** Ready and assigned to Claude; implementation running on `work/HEAD-02`
- **Base:** freshly fetched main with accepted HEAD-01 `e65e36f`
- **Depends on:** HEAD-01, accepted read/write stream and diagnostic contracts
- **Scope:** Tests only, expanding the existing portable head helper while
  preserving all 65 top-level records. Execute the actual unchanged imported
  head entry against bounded injected input/output failures; do not patch
  source, add signals or expand runtime/libc APIs.
- **Source contract:** `head.c` line mode stops on getc EOF; byte mode stops on
  zero fread; neither checks input ferror. Characterize first-read and
  prefix-then-read-error cases separately from clean EOF. A zero exit with
  truncated input is this upstream limitation, not proof of successful input.
  Output putchar failure calls `err(1, "stdout")`; a short fwrite calls
  `err(1, "failure writing to stdout")` because supported feof(stdout) is zero.
- **Red/control:** inject exact EIO/EPIPE and zero-progress output as appropriate,
  preserving actual accepted stream error mappings. Assert emitted prefix,
  stderr diagnostic, exit status and finite callback counts, including positive
  short-write retry. Prove real task setup, bindings and failure injection;
  no constant-failure wrapper may masquerade as the upstream command.
- **Accept:** repeated successful tasks after failures demonstrate isolation;
  first/prefix input errors retain their truthful zero-exit characterization,
  output failures preserve exact nonzero status and diagnostics. Restore any
  temporary binding on every path before its storage expires. Keep existing
  source hashes, capacity, stack budget and fixture coverage; run full exact CI,
  independent review and coordinator-owned fresh Mac acceptance. No new guest
  run or error behavior is claimed by this assignment itself.

### WRITE-02-design — finite console write progress

- **Status:** Done; reviewed design `f734884`, exact #328 all three checks passed
- **Base:** main at `e65e36f`
- **Depends on:** NEXT-UTIL-02 source audit
- **Scope:** `notes/iterations/WRITE-02-design.md` separates portable callback
  validation from the Linux adapter loop that cannot be stopped by its caller.
- **Accept:** bounded real-function tests for zero progress and partial failure;
  preserve zero-count errno and avoid malformed negative-result overflow.

### WRITE-02-linux — finite Linux console writes

- **Status:** Done in `e290168`; exact #340 all three checks and fresh 66-record Mac acceptance
- **Base:** main at `e65e36f`
- **Depends on:** reviewed WRITE-02-design
- **Hypothesis:** zero progress must terminate and a failed continuation must
  retain the count of an already emitted prefix.
- **Red:** actual old backend fails bounded zero-progress and partial-error
  assertions; see `notes/iterations/WRITE-02-linux.md` on the worker branch.
- **Accept:** eight exact deterministic syscall plans, all three exact CI checks,
  independent review. Linux-host-only; no fresh guest required for this half.

### WRITE-02-portable — validate console callback counts

- **Status:** Done in `e290168`; exact #340 all three checks and fresh 66-record Mac acceptance
- **Base:** main
- **Depends on:** reviewed WRITE-02-design
- **Hypothesis:** a real task must receive EIO for nonempty zero progress or
  malformed host counts, preserving valid short writes and old error behavior.
- **Red:** a checked task currently receives zero for nonempty fake-host output.
- **Accept:** actual shared Linux/Mac fake-host task tests including wide positive
  over-return, INT64_MIN, negative values outside int range, negative EPIPE,
  successful recovery and valid zero-count errno/no-callback preservation.
  Preserve all 65 existing guest records, fixed program capacity, checked setup
  and cleanup. Review, exact all-three CI and fresh Mac acceptance required.

### TEE-STATE-01-design — isolate tee's output list per execution

- **Status:** Done; reviewed `bda5c6c`, exact #341 all three checks passed
- **Base:** main
- **Depends on:** reviewed NEXT-UTIL-02 audit
- **Scope:** documentation-only design using existing executor delegation and
  typed per-execution state, with unchanged source and compile-time symbol names.
- **Red:** resetting a shared list at entry does not isolate interleaved tasks;
  task exit already frees list allocations before executor termination.
- **Accept:** exact actual lifecycle references, no duplicate list freeing or
  dangling-pointer inspection, safe inner-context delegation, failed/successful
  exec and allocation rollback, deterministic pre-teardown ownership assertions.
  No tee import or implementation authorized by this design task. Signal remains
  a separate prerequisite; WRITE-02 now supplies accepted raw-write progress.

### STAT-01 — private default file-creation mode for tee

- **Status:** Done on Linux/Mac; reviewed integration f1a769a, exact #356 all
  three checks, fresh run-nszacm8k 66 records; Solaris qualification pending
- **Base:** main
- **Depends on:** reviewed NEXT-UTIL-02 source audit
- **Scope:** private sys/stat.h exposes only the DEFFILEMODE integer constant
  0666 required by pinned tee. Do not advertise struct stat, stat/fstat,
  chmod, umask or a permissions system that does not exist.
- **Red:** missing private header is compile feasibility evidence. A deliberately
  incorrect constant must fail the actual mode assertion as a labeled
  after-implementation regression control.
- **Accept:** ordinary private-header open with DEFFILEMODE; runtime-side stat
  and fstat observe stored mode 0666, close/unlink cleanup is checked, and source
  fences reject host header/open fallback. Preserve existing records/capacity;
  independent review, all three exact CI checks and fresh Mac acceptance.

### TEE-STATE-01 — synthetic proof of isolated module state

- **Status:** Claimed by Antigravity on `work/TEE-STATE-01`
- **Base:** main
- **Depends on:** TEE-STATE-01-design
- **Scope:** implement only the reviewed executor wrapper with a synthetic
  pointer-global program; no actual tee import or signal implementation.
- **Red:** repeated and interleaved execution with a shared list must demonstrate
  cross-task state failure before the adapter.
- **Accept:** typed saved state, native delegate with inner execution, all
  lifecycle methods, no list traversal during teardown, and exact ownership
  observations. Separately inject sidecar allocation, native execution allocation
  and host context creation failures. Preserve another active task's state;
  prove failed/successful exec and blocked teardown. Review, exact CI and actual
  shared Mac tests required before runtime integration.

### SIG-01-design — define honest interrupt behavior before tee -i

- **Status:** Done; reviewed b2425a2, integrated f1a769a with exact #356 all
  three checks; design only, no signal implementation
- **Base:** main
- **Depends on:** reviewed NEXT-UTIL-02 audit
- **Scope:** define a bounded task-owned interrupt/disposition contract and real
  deterministic delivery path before exposing signal(SIGINT, SIG_IGN). A stub
  returning failure or success does not establish tee -i because tee ignores
  the return value. No implementation or new API is authorized by this task.
- **Red:** current core exposes no task signal/disposition API; identify exact
  task lifecycle and scheduling boundaries rather than treating missing headers
  as the only requirement.
- **Accept:** specify supported signal/dispositions, unsupported requests,
  previous-disposition return, ownership, spawn inheritance, exec reset/preserve
  behavior, blocked/running task delivery and cleanup. Distinguish proposed API
  from existing declarations and cooperative delivery from host preemption.
  Define bounded Linux/Mac tests for ignored versus default delivery, peer-task
  isolation and failed/successful exec. Do not advertise host keyboard wiring,
  arbitrary handlers, full POSIX signals or tee acceptance without proof.

### SIG-01 — cooperative task interrupt disposition and delivery

- **Status:** Blocked on SOLARIS-01 integration; unassigned
- **Base:** freshly fetched main
- **Depends on:** accepted SIG-01-design, SOLARIS-01
- **Scope:** implement the reviewed narrow SIGINT default/ignore contract in
  notes/iterations/SIG-01-design.md, including genuine queued delivery at safe
  native task boundaries. No no-op signal stub, arbitrary handlers, host keyboard
  integration, asynchronous host callbacks or preemption.
- **Red:** deterministic request to a blocked task must distinguish ignored
  continuation from default termination; preserve a runnable peer and observe
  task resources before teardown. A missing declaration alone is not behavior red.
- **Accept:** supported/unsupported requests, old-size API/executor tables,
  previous disposition, inherited ignore, pending state, failed and successful
  exec, blocked pipe/console/wait/poll wakeup, default status 130 and cleanup.
  Test both phases of EXEC_PENDING, including pending_program already cleared
  during new execution allocation. Keep unknown executors usable for ordinary
  tasks and reject unsupported interrupt delivery without mutation.
  Require independent review, exact CI and Linux/Mac/Solaris acceptance under
  the current policy. Preserve the fixed program capacity and all prior tests.
  The unchanged tee -i proof is downstream TEE-01 acceptance; synthetic
  real-request behavior establishes this prerequisite without importing tee.

### TEE-01 — import unchanged NetBSD tee with isolated execution state

- **Status:** Blocked; unassigned
- **Base:** freshly fetched main
- **Depends on:** STAT-01, WRITE-02-linux, WRITE-02-portable, TEE-STATE-01,
  SIG-01, SOLARIS-01
- **Scope:** import pinned tee byte-for-byte, retaining license and hash; connect
  the reviewed executor state wrapper and real interrupt disposition. Keep all
  adaptation outside upstream source. No unrelated libc expansion.
- **Red:** use source-feasibility diagnostics to confirm dependencies separately
  from failing command behavior. Add observable stdout/file/status cases before
  adapting the import.
- **Accept:** empty and binary stdin, data larger than tee's read buffer,
  simultaneous stdout and multiple files, default truncation and -a append,
  missing parent or directory destination with continuation to valid outputs, -i versus
  default interrupt behavior, output failures and bounded write progress.
  Check repeated and interleaved invocations, exact bytes/status/diagnostics,
  closed descriptors and allocation ownership before teardown. A missing leaf
  is a successful create case; permissions enforcement remains deferred. Inject
  zero progress through the real console callback boundary, where WRITE-02
  converts it to EIO, not a fake raw write that bypasses that contract. Preserve prior
  guest coverage/capacity and require exact applicable platform gates.
