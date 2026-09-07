# cannedBSD Backlog

The ordering is intentional. Priority 1 contains one-loop tasks that protect the
runtime model before the human-facing demo grows.

## Priority 1 — ready

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

## Priority 2 — usable-system demo

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

## Priority 3 — portability and extensions

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
