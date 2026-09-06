# cannedBSD v0.1 compliance audit

**Audit date:** 2026-09-06
**Authority:** `SPEC.md`

This file distinguishes implemented-looking code from directly verified
behavior. `Proven` requires a named automated check. `Partial` means some of the
contract is implemented or tested, but the cited evidence is narrower than the
requirement. `Missing` is a release blocker. `Excluded` is an explicit v0.1
non-goal rather than unfinished v0.1 work.

## Architecture and ABI

| Requirement | Status | Evidence or blocker |
|---|---|---|
| One host application; commands are not host processes | Partial | `make check-architecture` rejects host process-launch APIs outside the backend. Add a Linux `/proc` integration check around a blocked multi-task workload. |
| Native execution with no CPU emulator | Proven | Native C modules are linked into `bsdinacan`; architecture scan finds no emulator boundary. |
| Versioned host-operations table | Partial | `cb_host_ops_v1` has version and size and `cb_kernel_create` validates both. Registration/version failure tests are missing. The specified wall-clock operation is missing. |
| Versioned program API and descriptors | Partial | `cb_api_v1`, `cb_program_v1`, and `cb_stat_v1` are versioned. Tests for undersized/wrong-version program descriptors are missing. |
| Explicit native executor boundary | Missing | The registry returns `cb_program_v1` directly and `core.c` starts it directly. There are no validate/prepare, instance create, resume, terminate, and destroy operations. |
| Explicit VFS node/mount boundary | Missing | Descriptor objects are runtime-owned, but `core.c` calls the single RAMFS implementation directly. No filesystem/mount operation table exists. |
| Linux-specific mechanisms confined to backend | Proven | `make check-architecture` scans forbidden headers and process calls; `host_linux.c` owns `ucontext`, poll, host read/write, and clocks. |

## Tasks and processes

| Requirement | Status | Evidence or blocker |
|---|---|---|
| PID, PPID, states, stack, argv/env, descriptors, cwd/root, exit/wait state | Proven | `processprobe`, `descriptorprobe`, and `execprobe` directly cover the complete task state model. |
| PID 1 initial shell and PID uniqueness | Proven | `processprobe` observes the initial shell as parent PID 1 and proves distinct, monotonic child PIDs across zombie collection. |
| Cooperative single-thread scheduling | Proven | Explicit yield ordering plus pipe, console, and blocking-wait tests exercise runnable and blocked transitions in one host thread. |
| Spawn copies argv/env/cwd/descriptors and returns child PID | Proven | `processprobe` mutates parent inputs after spawn and proves copied vectors plus cwd/environment isolation; `descriptorprobe` proves inherited open-file sharing with independent descriptor-table entries. |
| Exec preserves process identity/state and closes CLOEXEC descriptors | Proven | `execprobe` proves PID and cwd preservation, environment replacement, CLOEXEC closure, same-fd `dup2` behavior, and ordinary descriptor retention. |
| Exit creates zombie; waitpid blocks, collects, and reports errors | Proven | `processprobe` proves blocking wait, status 42, wake reason, collection, repeated-wait `ECHILD`, and non-child `ECHILD`. |
| Explicit wake reason | Proven | Focused probes distinguish pipe change, console readiness, child exit, and voluntary yield without a wake cause. |
| Fork/vfork absent and capabilities honest | Partial | Calls are absent and capability fields are zero-initialized. Capability record needs a direct exact-value test. |

## Descriptors, pipes, and terminal

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Per-task descriptor tables and runtime-owned open-file objects | Partial | Architecture and pipeline tests support the design. Direct independence and cleanup tests are missing. |
| `dup`/`dup2` share offsets; CLOEXEC; allocation/error behavior | Proven | `descriptorprobe`, `execprobe`, and `pipeallocprobe` cover shared offsets, replacement, same-fd behavior, CLOEXEC, inheritance, invalid fds, and exhaustion. |
| Pipe construction is atomic | Proven | `pipeallocprobe` fails all three allocations and the one-slot descriptor case, verifying errno and unchanged outputs under ASan/UBSan. |
| Pipe zero-byte I/O never yields | Proven | `pipezeroprobe` observes peer scheduling and covers read/write. |
| Pipe blocking, wakeup, EOF, EPIPE, full buffer, ring wrap | Proven | `pipeedgeprobe` proves block, final-writer wakeup to EOF, and `EPIPE`; `pipecapacityprobe` forces 4,096-byte saturation, repeated wakeups, 10,000 ordered bytes, and ring wrap. |
| Terminal-backed fd 0/1/2 | Proven | `terminalprobe`, `terminalpeer`, the missing-command stream assertion, and `run_interactive_case` cover metadata/direction, zero I/O, host errors, block/wakeup, stdout/stderr, prompt/input/EOF, and exit status. |

## RAM filesystem

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Initial `/`, `/bin`, `/tmp`, `/home/user` hierarchy | Proven | `ramfsprobe` verifies every initial object, type, mode, nonzero inode, and distinct inode identity. |
| Absolute/relative paths, `.`, `..`, root confinement | Proven | Normalization cases plus `ramfsprobe` exercise live relative lookup, parent traversal, and confinement at `/`; process tests prove task-local cwd isolation. |
| Create/read/write/truncate/append and independent contents | Proven | `ramfsprobe` covers exact modes, offsets, fresh and post-truncate sparse holes, zero-byte writes, append-at-write, access errors, and independent contents/inodes. |
| Shared duplicated-file offset | Proven | `descriptorprobe` reads through one duplicate after seeking/writing through the other. |
| Stable stat/fstat metadata | Proven | `ramfsprobe` verifies version/size fields, type, mode, size, inode stability, uniqueness, and path/descriptor agreement. |
| mkdir/unlink errors | Proven | `ramfsprobe` covers duplicate mkdir, non-directory parents, directory/root unlink errors, nonempty directories, and missing paths. |
| Open file survives unlink until final close | Proven | `unlinkprobe` first reproduced and now guards the former ASan use-after-free. |

## Shell and base commands

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Interactive shell and `-c` | Proven | `-c` integrations plus deterministic interactive input verify prompt, line execution, EOF handling, and `exit 3`. |
| Words, argv, single/double quotes, escapes | Partial | Simple argv works. Empty words, both quote modes, escapes, whitespace, and syntax-error cases need explicit tests. |
| Pipelines and redirections | Partial | Required integration pipeline plus `<`, `>`, and `>>` examples pass. Failure cleanup and combined redirection tests are missing. |
| `;`, `$?`, `$NAME`, `${NAME}` | Partial | `;`, `$?`, and `$NAME` pass. Braced expansion, quote-dependent expansion, unset variables, and expansion timing need tests. |
| `cd`, `pwd`, `export`, `unset`, `exit` | Partial | First four have happy-path coverage except `unset`; `exit` and error cases need tests. |
| Native `echo`, `cat`, `tr`, `true`, `false` | Partial | All register; integration exercises each except direct `true`. Command status/error/argument behavior needs focused coverage. |
| Registry does not obstruct later loaders/executors | Missing | Depends on the explicit executor boundary above. Runtime-visible executable filesystem objects are a post-v0.1 design choice, not required by the normative v0.1 descriptor rule. |

## Errors, capabilities, documentation, and release

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Stable error numbers and per-task errno | Partial | Constants and task-local field exist. Cross-task errno isolation and strerror coverage need tests. |
| Overflow and pointer/vector ownership rules | Partial | Several bounds exist, copied argv/environment are directly verified, and sanitizers pass current paths. Allocation-failure coverage remains narrow. |
| No internal pointer crosses program ABI | Proven | Public header contains scalar values, opaque behavior through function tables, strings/vectors, and versioned value structs; internal types are absent. |
| Capability record reports exact v0.1 truth | Missing | Values are initialized but no exact-value/version/size test exists. |
| Required integrations and exact acceptance output | Proven | `make test` checks all §11.2 commands and exact `HELLO`; `tests/test_launcher.sh` verifies the selected executable. |
| Normal and ASan/UBSan suites | Proven | `make test`, `make sanitize`, and `make check-build-modes`; ASan documents its `ucontext` support warning. |
| README build/run/test instructions | Partial | Build/run/test are present. Adding a native command is not documented. |
| Known deviations resolved or accepted | Missing | This table is the open deviation list. v0.1 cannot be tagged while any `Missing` row remains. |

## Explicit v0.1 exclusions

The following are `Excluded`: existing foreign binaries, CPU translation,
general fork/vfork, MMU isolation, dynamic loading, WASM execution, persistent
filesystems, host mounts, networking, Unix sockets, PTYs, daemons, GCC, make,
pkgsrc, Python, Perl, package management, X11, and GUI integration. Their
extension seams remain design requirements, but implementations are not release
requirements.
