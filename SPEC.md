# cannedBSD v0.1 MVP specification

Status: implemented and verified normative specification

## 1. Purpose

cannedBSD v0.1 proves that a useful Unix personality can exist inside one
ordinary host application without booting another kernel and without using a
virtual CPU.

The reference host is 64-bit Linux, but the MVP must exercise the architecture
needed by non-POSIX systems. It must not obtain its process model by launching
Linux processes. Commands are native modules executed as internal cannedBSD
tasks. cannedBSD owns their Unix-visible PIDs, descriptors, pipes, paths,
working directories, process relationships, blocking, and exit status.

The release demonstration is:

```sh
$ echo hello | tr a-z A-Z > /tmp/result
$ cat /tmp/result
HELLO
```

This specification uses **MUST**, **SHOULD**, and **MAY** normatively.

## 2. Design principles

1. **One host application, one inner Unix system.** Linux sees one
   `bsdinacan` process. cannedBSD sees a shell and child processes.
2. **Native execution first.** v0.1 commands contain code for the real host
   CPU. No instruction emulator is involved.
3. **Runtime-owned semantics.** A host facility may implement a mechanism, but
   the portable core owns its Unix meaning.
4. **Source compatibility first.** Historical NetBSD binary syscall and ELF or
   a.out compatibility are not v0.1 requirements.
5. **No host assumptions in programs.** A cannedBSD command uses the cannedBSD
   API, not Linux syscalls, host descriptors, or host process globals.
6. **Version every boundary.** Public structures contain an ABI version and
   structure size so later fields can be appended.
7. **Execution engine is not process identity.** Native and future WASM
   commands must be able to inhabit the same process, descriptor, and
   filesystem model.
8. **Capability differences are explicit.** Missing MMU, fork, sockets, or
   dynamic loading are reported, not disguised.

"The same inside" means that program-visible contracts and portable runtime
semantics are shared on every host. It does not mean pretending that unlike
host mechanisms have identical capabilities. A backend may accelerate a
facility, but it MUST do so behind a common cannedBSD boundary and report
semantic limitations through capabilities.

For every interface included in v0.1, NetBSD's documented user-visible
behavior is the semantic reference unless this specification narrows it or
records a deviation. The `cb_` names distinguish the internal ABI; they do not
authorize gratuitously different behavior. A later libc can expose the normal
NetBSD/POSIX spellings over the same calls.

## 3. System boundary

```text
Linux host
└── cannedbsd process
    ├── Linux host backend
    ├── portable cannedBSD core
    │   ├── scheduler and tasks
    │   ├── process table
    │   ├── descriptor and pipe layer
    │   ├── VFS and RAM filesystem
    │   └── program registry and executor interface
    ├── terminal frontend
    └── native program modules
        ├── sh
        ├── echo
        ├── cat
        ├── tr
        ├── true
        └── false
```

Only the host backend MAY call Linux-specific APIs. The portable core SHOULD be
conservative C99 without compiler extensions. Context-switch assembly or host
APIs MUST be isolated behind the host context interface.

## 4. Required architectural interfaces

### 4.1 Host operations

The core receives a versioned `cb_host_ops_v1` table at initialization. It
contains only mechanisms unavailable in portable code:

- Allocate, resize, and release memory.
- Create, switch, and destroy stackful execution contexts.
- Read, write, and poll the host console.
- Return monotonic milliseconds and wall-clock milliseconds since the Unix
  epoch. A zero clock result reports that the host could not supply a value.
- Sleep or yield the enclosing application.
- Report a fatal runtime error.

Allocated memory may contain arbitrary bytes; the portable runtime clears new
objects before use. Resize preserves the existing prefix on success and leaves
the original allocation valid on failure. This keeps zero-fill assumptions out
of classic-host allocators.

No Unix process operation belongs in this table. In particular, it MUST NOT
contain host `fork`, `exec`, `waitpid`, or a facility for delegating command
execution to the host shell.

The Linux implementation MAY use `ucontext`, assembly, or another stackful
context facility internally. That choice is not part of the portable ABI.

### 4.2 cannedBSD program API

Programs see a versioned `cb_api_v1` function table. Its v0.1 surface includes:

```c
/* process */
cb_getpid, cb_getppid, cb_spawn, cb_exec, cb_exit, cb_waitpid, cb_yield

/* descriptors */
cb_open, cb_close, cb_read, cb_write, cb_lseek
cb_dup, cb_dup2, cb_set_cloexec, cb_pipe, cb_fstat

/* filesystem */
cb_stat, cb_mkdir, cb_unlink, cb_chdir, cb_getcwd

/* environment */
cb_getenv, cb_setenv, cb_unsetenv

/* diagnostics */
cb_strerror, cb_get_errno, cb_set_errno
```

The initial C library may expose conventional names through wrappers, but the
wrappers MUST ultimately use this table. Program code MUST NOT depend on the
table's concrete address or on internal runtime structures.

### 4.3 Native program descriptor

Every native command has a descriptor equivalent to:

```c
struct cb_program_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *name;
    uint32_t flags;
    size_t requested_stack_size;
    int (*start)(const struct cb_api_v1 *,
                 int argc,
                 char *const argv[],
                 char *const envp[]);
};
```

For v0.1 these descriptors are compiled into the application and registered at
startup. The registry and executor interface MUST not assume this permanently;
a later native file loader or WASM executor must be able to supply an
equivalent program object.

A small cannedBSD `crt0` adapter MAY preserve an ordinary
`main(int, char **, char **)` for ported sources.

### 4.4 Executor interface

Program lookup produces a program object handled by an executor. The executor
interface MUST separate these operations:

- Validate or prepare a program.
- Create its execution instance.
- Start or resume it.
- Request termination.
- Destroy its execution instance.

The executor operation table is versioned and immutable for the lifetime of
every program prepared through it. Preparation produces a runtime-owned generic
program object; the v0.1 native executor copies the descriptor and command name,
so the registration source need not remain allocated. Each task owns one
execution instance. `exec` destroys that instance only after replacement state
has been prepared, then creates a fresh instance without changing the task PID.

The portable task and scheduler core MUST interact with execution only through
the generic start/resume, suspend, termination, and destruction operations. It
MUST NOT call a native entry point or manipulate a task's native stack context.
The v0.1 native executor owns that context and calls the registered C entry
point. The interface MUST permit a future `.cbwasm` executor whose interpreter
state and linear-memory pointers never escape into the core.

### 4.5 VFS and descriptor object interfaces

Filesystem behavior MUST be separated from the initial RAM filesystem by VFS
node and mount operations. Descriptor entries MUST refer to cannedBSD open-file
objects, not directly to host file descriptors.

Both operation tables are versioned. A mount supplies root-node discovery and
mount destruction. A node supplies retain/release, child lookup and creation,
unlink, open, stat, parent discovery, and name discovery. Root-mount
installation validates the complete tables and ownership relationship before
publishing the mount. Task root and cwd fields, plus open files, retain generic
node handles; filesystem-specific node representations remain below the node
operations. Path traversal and task-local errno translation belong to the VFS
layer rather than to a filesystem implementation.

v0.1 installs one RAMFS root. Later mount routing may return nodes belonging to
other mounts during lookup without changing task, descriptor, or program APIs.

These boundaries must allow later implementations for:

- An image-backed private Unix root.
- NetBSD rump filesystems.
- `/Host` native-volume adapters.
- Host-backed Internet sockets.
- A common in-can TCP/IP stack connected to a packet-device backend.
- Internal Unix-domain sockets.
- PTYs and graphical terminals.

## 5. Task and process model

### 5.1 Required state

Every internal process has at least:

- PID and parent PID.
- Running, runnable, blocked, zombie, or dead state.
- Native execution context and private stack.
- Program identity, arguments, and environment.
- Descriptor table.
- Current directory and root VFS node.
- Exit status and wait relationship.
- A pending wake reason.

PID 1 is the initial shell or an initialization task that starts the shell.
PIDs MUST be unique among live and zombie processes.

### 5.2 Scheduling

v0.1 uses one host thread and cooperative, stackful scheduling. A task runs
until it:

- Calls `cb_yield`.
- Blocks on a pipe, terminal, or wait operation.
- Exits.
- Requests an exec transition.

The scheduler MUST make progress when runnable work exists and MUST return to
the host event/poll loop when all tasks are blocked. Core scheduling MUST NOT
depend on host-thread preemption or signal handlers.

This is a semantic choice for v0.1, not a permanent ban on host threads.
Future backends MAY use host threads or processes if they preserve cannedBSD
semantics.

### 5.3 Spawn, exec, exit, and wait

`cb_spawn` creates a child from a registered program, argument vector,
environment, working directory, and inherited descriptor table. It returns the
child PID to the parent.

`cb_exec` replaces the current program instance while retaining PID, parent,
working directory, environment supplied to exec, and descriptors not marked
close-on-exec. A successful exec does not return.

`cb_exit` closes descriptors, records status, wakes a waiting parent, and leaves
a zombie until collected.

`cb_waitpid` supports waiting for a specific child and returns its PID and exit
status. Waiting when the child is still active blocks the caller.

General `fork()` and `vfork()` are not in v0.1. Their absence MUST be visible
through capability discovery.

## 6. Descriptor semantics

Each process owns a descriptor-number table. A descriptor points to a shared,
reference-counted open-file object containing status flags, current offset when
applicable, and operations for reading, writing, seeking, polling, and closing.

The internal poll operation reports whether a requested read or write can
complete without blocking, including EOF and broken-pipe outcomes. It is an
open-file/scheduler seam; a public `poll(2)` API is deferred.

v0.1 descriptor types are:

- RAM filesystem regular file.
- Host-console terminal input or output.
- Pipe read endpoint.
- Pipe write endpoint.

Descriptors 0, 1, and 2 are inherited from the shell. `dup` and `dup2` share
the same open-file object. Spawn duplicates descriptor-table references rather
than underlying objects.

`dup2(oldfd, newfd)` closes and replaces `newfd` and clears close-on-exec when
the descriptor numbers differ. When `oldfd == newfd`, it is a successful no-op
and preserves descriptor flags, including close-on-exec.

### 6.1 Pipes

A pipe has a bounded byte buffer and independent counts of readers and writers.

- Reading an empty pipe blocks while a writer exists.
- Reading an empty pipe returns EOF when no writers remain.
- Writing a full pipe blocks while a reader exists.
- Writing when no readers remain fails with `EPIPE`.
- Reading or writing zero bytes returns zero immediately and never blocks or
  yields, regardless of endpoint state.
- Closing an endpoint wakes tasks whose condition may have changed.
- `pipe()` is atomic: if allocation or descriptor installation fails, it returns
  `ENOMEM` or `EMFILE`, installs no descriptor, leaves the caller's output array
  unchanged, and releases every partially constructed object.

The scheduler and pipe implementation must correctly run every stage of a
pipeline even though only one host thread exists.

## 7. Filesystem semantics

The initial root is an in-memory filesystem created on every launch. It
contains at least:

```text
/
├── bin
├── tmp
└── home
    └── user
```

Required objects are directories and regular files. Required behavior:

- Absolute and relative path resolution.
- `.` and `..` handling without escaping the process root.
- Per-process current directory.
- Open for read, write, create, truncate, and append.
- Truncation sets the logical size to zero. A later write beyond the logical end
  zero-fills the intervening hole even when old storage capacity is reused; a
  zero-byte write never changes size or contents.
- Independent directory entries and file contents.
- File offsets shared through duplicated open-file objects.
- Minimal `stat` information: object type, mode, size, and stable inode number.
- `mkdir` and `unlink` with appropriate type and non-empty-directory errors.
- Unlink removes the directory entry immediately, but the underlying regular
  file remains alive while any open-file object refers to it. Existing open
  descriptors continue to support read, write, seek, and `fstat`; a pathname
  lookup fails with `ENOENT`. The storage is reclaimed after the final open-file
  reference closes. Hard links are deferred, so v0.1 has at most one directory
  entry per file.

Symlinks, hard links, device nodes, ownership enforcement, timestamps,
persistence, and `/Host` are deferred. The VFS boundary MUST allow them later.

## 8. Shell and commands

The v0.1 shell is intentionally small. It MUST support:

- Interactive input and `-c command` operation.
- Simple commands and argument vectors.
- Single quotes, double quotes, and backslash escaping.
- Pipelines with `|`.
- Input redirection `<`.
- Output creation/truncation `>`.
- Output append `>>`.
- Sequential commands separated by `;`.
- Exit-status propagation and `$?`.
- Environment expansion `$NAME` and `${NAME}`.
- Built-ins `cd`, `pwd`, `export`, `unset`, and `exit`.

`NAME` uses `[A-Za-z_][A-Za-z0-9_]*`; `export` and `unset` reject other names.
`exit` accepts an optional decimal status, wraps it to eight bits, and exits
with status 2 after diagnosing a nonnumeric argument. Built-ins may appear in a
pipeline. They then run in an internal child task, so directory and environment
changes do not mutate the parent shell.

Globbing, command substitution, subshells, job control, background execution,
functions, loops, conditionals, and full POSIX shell syntax are deferred.

Required external native modules are:

- `echo`
- `cat`
- `tr`, supporting the `a-z A-Z` case used by acceptance testing
- `true`
- `false`

The small initial `tr` is not claimed to be a complete NetBSD `tr` port.

## 9. Error and data conventions

- API integers and structures use explicitly sized types where representation
  crosses an ABI boundary.
- Byte counts and offsets must detect overflow. A read or write count that
  cannot fit in `cb_ssize_t` fails with `EINVAL` before reaching a backend.
- Errors use stable cannedBSD/POSIX-style symbolic values such as `ENOENT`,
  `EBADF`, `EPIPE`, `ECHILD`, and `ENOMEM`.
- Errno is per internal process, never a naked host-global value.
- User strings and vectors are copied or given explicit ownership rules.
- No pointer to a task, VFS node, descriptor object, or host object crosses the
  public program ABI.

## 10. Capability discovery

The API exposes a versioned capability record. v0.1 reports exactly:

```text
native_modules       yes
cooperative_tasks    yes
memory_protection    no
spawn                yes
exec                 yes
fork                 no
vfork                no
ramfs                yes
persistent_fs        no
host_mounts          no
network_sockets      no
unix_sockets         no
pty                  no
wasm_executor        no
x11                  no
```

Capabilities are descriptive; applications must not infer host OS identity
from them.

## 11. Verification requirements

### 11.1 Unit and subsystem tests

Tests MUST cover:

- PID allocation, parentage, zombie collection, and wait errors.
- Environment and current-directory independence.
- Descriptor allocation, inheritance, duplication, close-on-exec, and cleanup.
- Pipe blocking, wakeup, EOF, broken-pipe, and buffer-full behavior.
- Absolute/relative path normalization and root confinement.
- File creation, truncation, append, offsets, stat, mkdir, and unlink.
- Scheduler progress with multiple runnable and blocked tasks.
- Exec retaining PID while replacing program state.
- Shell tokenization, quoting, expansion, pipeline construction, redirection,
  and status propagation.

### 11.2 Integration tests

At minimum, a noninteractive test invokes:

```sh
bsdinacan -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result'
```

It MUST exit zero and emit exactly:

```text
HELLO
```

Additional integration tests MUST demonstrate:

```sh
false; echo $?
echo abc | cat | tr a-z A-Z
echo one > /tmp/x; echo two >> /tmp/x; cat /tmp/x
cd /tmp; pwd
```

The test suite SHOULD run under AddressSanitizer and UndefinedBehaviorSanitizer
on the Linux reference host.

### 11.3 Architectural checks

The implementation MUST NOT call host `fork`, `execve`, `system`, `popen`, or
`posix_spawn` to implement a cannedBSD command. Linux-specific includes and
calls outside the host-backend directory fail review.

## 12. Explicit v0.1 non-goals

- Running existing NetBSD, Linux, or historical binary executables.
- CPU emulation or binary translation.
- Full `fork`, MMU-backed protection, hostile-code isolation, or multiple
  users.
- Complete POSIX, NetBSD libc, or shell compatibility.
- Dynamic native program loading.
- A WASM interpreter or `.cbwasm` loader.
- Persistent or rump-backed filesystems and host-volume mounts.
- TCP/IP, DNS, Unix sockets, PTYs, multiple terminal windows, or daemons.
- GCC, make, pkgsrc, Python, Perl, package management, X11, or a GUI.

These are deferred rather than contradicted. A v0.1 shortcut is unacceptable
if it makes one of these materially harder by exposing Linux objects as the
cannedBSD ABI.

## 13. Follow-on compatibility rules

Future work must be able to add features without changing these foundations:

1. A WASM program receives the same logical process and descriptor services as
   a native program through a marshalled import layer.
2. A native dynamic loader produces the same program abstraction as the
   built-in registry.
3. A classic-host backend replaces Linux context, console, clock, and event
   mechanisms without rewriting process, descriptor, pipe, or VFS semantics.
4. Persistent, rump, and host filesystems attach through VFS mounts.
5. TCP and Unix sockets become descriptor-object types and scheduler wait
   sources.
6. PTYs and X11 use the same descriptors, local sockets, and event model.
7. `vfork` and selected forms of `fork` may be added by capability tier without
   becoming prerequisites for spawn-oriented programs.
8. A libc layer exposes standardized POSIX names over the versioned program
   API. Native command translation units use a private symbol prefix below
   their headers so libc calls cannot interpose on the enclosing host adapter.
9. Program heap allocations belong to an internal task. They are reclaimed on
   exit and successful `exec`, independently of zombie collection, and are
   reclaimed during kernel teardown if execution stops early.

The first source-compatibility slice appends allocate, resize, and release
operations to the v1 program API. Appending operations preserves the existing
v1 prefix as required by the structure-size convention. `allocate(0)` returns
a releasable allocation when possible; `resize(NULL, size)` allocates;
`resize(pointer, 0)` releases and returns null; and `release(NULL)` is a no-op.
A pointer supplied to resize or release must belong to the calling task.
Invalid ownership sets `EINVAL`; allocation failure sets `ENOMEM` while leaving
an existing allocation unchanged. Successful resize preserves the shorter of
the old and new extents.

Networking has two permitted future implementations behind the same
cannedBSD socket API:

```text
direct:   cannedBSD socket object → host socket adapter
portable: cannedBSD socket object → common NetBSD/rump TCP/IP
                              → virtual network interface
                              → packet transport or slirp-style NAT backend
```

The direct form is smaller and is suitable for early ports, but host TCP stacks
can differ in options, readiness, addressing, listeners, multicast, and error
behavior. It MUST therefore be treated as a backend with declared
capabilities, not as the definition of cannedBSD networking.

The portable form is preferred when strict cross-host behavior matters. In
that model, a common in-can stack owns TCP/IP semantics. A virtual NIC carries
packets across the host boundary. A libslirp-like component may sit outside
that NIC and translate packets to host sockets; it is not itself the in-can TCP
stack. Hosts capable of raw Ethernet may instead attach the virtual NIC to a
native packet device or bridge.

A SOCKS backend is also a legitimate early shortcut for outbound connections:

```text
cannedBSD connect() → common socket object → host adapter
                                      → configured SOCKS proxy
```

It MUST be described by what it actually provides. A minimal implementation
may support only outbound TCP `CONNECT`; it must not imply working inbound
listeners, raw sockets, multicast, or general UDP. Proxy negotiation, proxy
DNS versus local DNS, authentication, and failure translation belong in a
portable SOCKS transport where possible, while each host adapter supplies only
a byte stream to the proxy. This shortcut can coexist with both direct host
sockets and the later common-stack/virtual-NIC design.

Known follow-on targets include an m68k NeXTstation running NeXTSTEP, a
Solaris 9 sun4m UTM/QEMU VM, and a physical 32-bit sun4m SPARCbook. The VM is
the early portability gate for big-endian,
strict-alignment, ILP32, and old-compiler assumptions; the SPARCbook follows as
the hardware test. Exact SPARCbook device support remains model-specific even
though Solaris 9 itself supports the sun4m platform group.

## 14. Definition of done

v0.1 is complete when:

- All required interfaces and semantics above are implemented.
- The required native commands run only as internal cannedBSD tasks.
- The acceptance and subsystem tests pass normally and under sanitizers.
- The architecture contains no Linux process or descriptor leakage across the
  host boundary.
- README documentation explains how to build, run, test, and add a native
  command.
- Known deviations from this specification are either fixed or explicitly
  recorded and accepted before tagging `v0.1.0`.
