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

**Current assignments:** Claude owns `VFS-03` directory iteration and its
reviewed design corrections. Antigravity owns `IO-01` polling corrections.
Codex owns review, serialized guest acceptance, integration, and the next
bounded reuse/utility plans. MAC-09 and REUSE-01 are merged at `ca0cd19`,
with fifteen actual guest checks accepted after Woodpecker #87.
Antigravity also has the separate VFS-02 design queued behind polling fixes.

Mac guest acceptance is a serialized gate rather than a worker claim. After a
required `mac68k` build succeeds, the coordinator assigns one agent to test that
exact artifact in Basilisk II before integration. Other workers continue on
independent backlog items while the emulator is occupied.

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

- **Status:** Design claimed by Antigravity on `work/VFS-02-design`
- **Base:** main
- **Hypothesis:** registered programs can appear as executable VFS objects and
  shell lookup can resolve those objects instead of a hidden path registry.
- **Red:** prove `/bin/sh` and registered commands cannot currently be statted
  or opened as executable nodes.
- **Accept:** lookup distinguishes ENOENT, EACCES, and ENOEXEC; open and unlink
  lifetime is specified; spawn/exec atomicity and registry-source cleanup pass.

### VFS-03 — directory iteration and libc `dirent`

- **Status:** Claimed by Claude; directory ownership and mutation contract being corrected
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

- **Status:** Blocked on IO-01
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

- **Status:** Claimed by Claude; corrections feed VFS-03 implementation
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

- **Status:** Claimed by Claude; after VFS-03 acceptance fixes
- **Base:** main
- **Accept:** pinned source inventory, path edge cases, task-local result
  lifetime, cleanup, C-locale dependency and a bounded deterministic test plan.

### TERM-01-design — actual host terminal capability contract

- **Status:** Antigravity design under review
- **Base:** main
- **Accept:** actual Mac canonical-buffer and Linux inherited-tty inventory;
  shared core terminal owner, optional raw capability, readiness/EOF/overflow
  semantics and deterministic tests before implementation.

### REUSE-03 — bounded catalog wrapper using existing SDK records

- **Status:** Claimed by Codex on `work/REUSE-03`
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
