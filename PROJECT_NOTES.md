# cannedBSD project notes

## Purpose

The objective is a familiar NetBSD-style working environment on arbitrary old
and unusual platforms. The intended user experience includes a shell, C
compiler, build tools, Python, Perl, TCP/IP applications, multiple terminals,
and eventually an X11 server displayed in a native host window.

The machine's existing operating system already knows how to operate its
hardware. cannedBSD should use that OS as its hardware abstraction layer while
supplying consistent Unix behavior above it.

This is not CPU emulation. A 68000 target runs native 68000 programs, a
PowerPC target runs native PowerPC programs, and so forth. Userland programs
are rebuilt against the cannedBSD API and runtime for each target.

## Verified System 7 implementation (2026-09-07)

The Linux reference implementation now has an experimental native 68K Mac
backend in `platform/mac68k`. It uses the existing portable core, shell,
commands, and RAM filesystem. The backend supplies Toolbox memory allocation,
cooperative stack switching, monotonic time, and a basic window with
line-buffered ASCII input. The application requests an 8 MiB partition.

The first target tested is System 7.5.3 in Basilisk II. The application itself
contains native 68K code; Basilisk II supplies the test machine. Two independent
task stacks passed 512 child yields. Seven startup acceptance cases verified
pipelines, redirection, `wc`, append, shell status, working directory, and exit
status. The decisive `echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result`
command returned `HELLO`. An interactive pipeline also returned the expected
output. Woodpecker pipeline #18 passed both build workflows, and its application
passed the guest startup checks.

Two classic Mac constraints were found through guest execution: the application
heap must be expanded with `MaxApplZone` before switching onto heap-backed task
stacks, and Toolbox event pumping must run on the original scheduler stack to
avoid System 7 error 28. Stack switching preserves the C ABI's callee-saved
registers, including A5. The initial build uses software floating point.

### Builds and transfer

Woodpecker runs the Linux gate and a separate Retro68 cross-build on every push
and pull request. The Mac workflow pins the toolchain image by digest and uses
a dedicated Docker runner selected by the mandatory `!role=retro68` label.
The existing local runner continues to handle its existing workflows. Retained
artifacts are identified by pipeline, source commit, and run, with checksums.
See `CI.md` and `platform/mac68k/README.md` for the build and acceptance procedure.

Basilisk II's `Unix` disk is its host shared folder; in the container setup,
that host is the container and the folder is bind-mounted from the workstation.
Transfer the application data fork together with its `.rsrc` and `.finf`
metadata, or use the packaged MacBinary or HFS disk image. The application
writes `Unix:cannedbsd-result.txt` as guest execution evidence. This sharing
mechanism is separate from cannedBSD's internal RAM filesystem.

### Remaining Mac work

Guest execution is currently a separate acceptance check, not an automated
Woodpecker step. The frontend has a bounded text display and no terminal escape
processing, persistent filesystem, networking, or forced task preemption. UTC
wall time is unavailable. Other classic Mac versions and hardware are untested.

[Symantec C++ 7.0](https://macintoshgarden.org/apps/symantec-c-70) is recorded as
a possible compiler for self-hosted development inside the Mac. It has not been
installed or validated for this project; current builds use Retro68.

## Historical model

### MacMINIX

MacMINIX is direct proof of the basic architecture. Classic Mac OS launched a
`Macboot` application containing MINIX's kernel, memory manager, filesystem
server, `init`, and user processes. The kernel and commands executed directly
as native m68k code.

```text
Classic Mac OS
└── MacMINIX application
    ├── MINIX kernel and IPC
    ├── memory manager
    ├── filesystem server
    ├── init
    ├── shell process
    └── compiler and other processes
```

Mac OS saw one application. MINIX saw its own process tree. MacMINIX saved and
restored native CPU contexts and maintained PIDs, pipes, file descriptors,
signals, `fork`, `exec`, and `wait` internally. It used Macintosh facilities
for windows, menus, input, disks, and cooperative execution alongside other
applications.

Its hard-disk partitions could be ordinary Macintosh files whose logical
blocks were interpreted as MINIX devices. Under MultiFinder, MacMINIX yielded
periodically to Mac OS; when another Mac application had the CPU, all MINIX
processes paused.

MINIX's microkernel architecture made this port tractable by concentrating
scheduling and IPC in a small kernel and leaving filesystem and memory
management in message-oriented servers. A microkernel is helpful, not
essential: monolithic kernel components can also be adapted to a host-process
environment.

### MachTen

MachTen is best understood as a native-code hosted Unix operating system
inside classic Mac OS. It was more than an API compatibility library because
it maintained persistent Unix-wide state and coordinated processes,
descriptors, filesystems, sockets, credentials, daemons, and tools.

The classic OS remained the outer operating system and owned the physical
machine. MachTen supplied the internal Unix personality. This is the desired
conceptual position for cannedBSD.

## Related architectures

### QEMU user-mode

QEMU user-mode runs a foreign binary without a guest kernel:

```text
Foreign application
    ↓ translated instructions and syscall ABI
QEMU user-mode
    ↓ host operations
Host kernel
```

The host owns processes, memory mappings, files, pipes, terminal I/O, and
networking. QEMU translates instructions when architectures differ and also
translates syscall numbers, data structures, flags, signals, and `ioctl`
interfaces.

QEMU's execution/interposition envelope could be placed above a hosted kernel,
but that is distinct from the kernel-to-host hypercall layer:

```text
Guest application
    │ guest system calls
    ▼
Hosted kernel
    │ host-service calls or hypercalls
    ▼
Host OS
```

For source-compatible, native builds, cannedBSD should not require QEMU. A
custom libc/syscall boundary can call the runtime directly.

### NetBSD rump kernels

A rump kernel packages selected real NetBSD kernel components for execution
inside a process or a persistent server. Useful components include
filesystems, networking stacks, drivers, VFS, and kernel-side syscall
implementations.

`rump_sp` permits multiple clients to call a rump server. `rumphijack` can
redirect selected libc calls from dynamically linked applications. This
supports useful hybrids in which host processes use rump-owned filesystem or
network state.

Rump intentionally omits several facilities required for a complete nested
Unix:

- Process creation
- Virtual-memory address spaces
- Thread scheduling

It is therefore useful source material and potentially an excellent FFS/VFS
or networking component, but it is not by itself the cannedBSD process
runtime.

### User-mode kernels

User-Mode Linux (`ARCH=um`) is a complete Linux kernel port that runs as an
ordinary Linux program. Guest code uses native instructions; host facilities
are used to isolate guest address spaces and divert guest syscalls into the
hosted Linux kernel. This is very close architecturally to a modern MacMINIX,
although UML is Linux-host-specific.

NetBSD has a research-grade `sys/arch/usermode` port. Unlike rump, it includes
the scheduler, VM, and process machinery of a complete nested NetBSD kernel.
Existing work is tied primarily to a NetBSD host and has depended on a special
`syscallemu` host-kernel module. It is prior art rather than a portable,
turnkey NetBSD-on-Linux/macOS implementation.

The Linux Kernel Library (LKL) is closer to rump than UML: it compiles Linux
kernel services into a library behind a host-operations interface, but is not
itself a complete multi-process hosted Linux environment.

### iOS precedents: iSH and a-Shell

iSH and a-Shell demonstrate two nearly opposite ways to construct a usable
Unix environment inside a tightly sandboxed application. Together they bound
much of cannedBSD's design space.

#### iSH: binary compatibility below the programs

iSH runs ordinary 32-bit x86 Alpine Linux ELF binaries using user-mode x86
instruction emulation and Linux syscall translation:

```text
unmodified i386 Alpine executable
            ↓
    x86 instruction interpreter
            ↓
       Linux syscall layer
            ↓
       Darwin/iOS operations
```

It does not boot a Linux kernel. Instead, the application contains a
substantial independently implemented Linux personality: an ELF loader,
virtual address spaces, tasks and PIDs, signals, descriptors, pipes, futexes,
`fork`, `vfork`, `clone`, `execve`, `wait`, `/proc`, credentials, filesystem
semantics, and socket translation.

Each guest task carries emulated CPU and process state and is executed by a
host pthread. A guest `fork()` creates another internal task and host thread
while copying or sharing the appropriate guest state according to Linux clone
flags. Internet sockets normally become real Darwin sockets after translating
the Linux address family, type, protocol, flags, and structures.

iSH stores file contents in an iOS-accessible host directory but uses a SQLite
side database to preserve guest inode identity and Unix metadata such as mode,
uid, gid, device number, paths, and hard links. This is a useful alternative to
a fully image-backed filesystem when the host filesystem is close enough for
file contents but not for Unix metadata.

The x86 engine is an interpreter, not a native-code JIT. It generates threaded
sequences of pointers to precompiled instruction "gadgets," avoiding iOS's
prohibition on generating executable machine code while improving on a simple
switch-dispatch interpreter.

Because iSH presents the Linux binary ABI, Alpine's ordinary x86 GCC can run
inside it and produce more x86 Linux programs that execute immediately. That
provides broad package compatibility at the continuing cost of instruction
emulation. Architecturally it resembles QEMU user-mode plus considerably more
of the process, virtual-memory, filesystem, and kernel personality.

#### a-Shell: ported native commands above the host OS

a-Shell takes a source-porting approach. Its `ios_system` layer parses command
lines and dispatches commands that were compiled ahead of time as signed
ARM64 frameworks inside the application:

```text
shell parser
    ↓
ios_system dispatcher
    ↓
dlopen bundled framework; resolve command_main()
    ↓
run command on a pthread
    ↓
Darwin libc and iOS frameworks
```

A port normally renames `main()` to a command-specific entry point, replaces
or intercepts process-wide operations, routes standard streams through
thread-local variables, and makes mutable global state thread-local. The
dispatcher uses `dlopen()` and `dlsym()` to call the selected entry function.
Pipelines use host pipes between command threads.

This process model is deliberately approximate. `fork()` allocates a synthetic
PID rather than duplicating the calling context. `execv()` calls another
registered command rather than replacing an OS process. Per-command tables
track environment, current directory, streams, exit status, and associated
thread; `waitpid()` waits for the thread. Programs that depend on taking both
branches after a real `fork()` must be changed. Native commands can use host
sockets, subject to the iOS sandbox.

Bundled tools therefore run at native speed but must be curated and ported one
by one. Interpreters such as Python are precompiled native components; their
scripts do not become separate native executables.

For programs compiled by the user, a-Shell uses a second execution model. Its
native Clang driver silently targets `wasm32-wasip1`, producing WebAssembly
instead of an unsigned ARM executable. The resulting program is loaded into a
hidden `WKWebView`; JavaScript creates a `WebAssembly.Module` and supplies WASI
arguments, environment, standard I/O, and filesystem access. This use of the
system WebKit engine stays inside iOS's executable-code policy.

The a-Shell WASM personality supports files, terminal I/O, and pipelines, but
documents no sockets or `fork()` for WASM programs. Native prepackaged network
commands remain able to use Darwin networking.

#### Consequences for cannedBSD

iSH is the maximal-compatibility alternative to cannedBSD's current premise.
Running unmodified NetBSD binaries would require a NetBSD syscall personality
and, when their CPU differs from the host, an instruction emulator. That is a
valid future project but not the native-source-compatible first design.

a-Shell is much closer to the proposed implementation and provides concrete
precedent for:

- A stable `command_main(argc, argv)`-style native module ABI.
- Prepackaged native base commands plus portable WASM user commands.
- Per-command environment, directory, descriptors, status, and thread state.
- Host pipes, files, and sockets beneath an application-owned namespace.
- Treating process semantics independently from the execution engine.

cannedBSD should go farther than a-Shell by making its PIDs, parentage,
descriptors, signals, waits, credentials, filesystem namespace, and scheduling
an intentional common runtime rather than a collection of compatibility
shims. Both native modules and `.cbwasm` instances should enter that same
process model:

```text
                    cannedBSD shell
                         │
          ┌──────────────┴──────────────┐
          │                             │
native command module              .cbwasm command
command_main(argc, argv)           portable import ABI
          │                             │
          └──────── cannedBSD API ──────┘
                         │
                  host adaptation layer
```

On a permissive classic host, native modules can be loaded from disk and offer
speed and self-hosting. On an iOS-like policy-constrained host, the native base
set must be signed into the application while downloaded or locally compiled
tools use WASM. In a browser, the same WASM side becomes the primary execution
path. This validates keeping native execution primary without making it the
only cannedBSD executable format.

## API and toolchain model

The initial contract should be source compatibility, not binary compatibility.
Programs are rebuilt for the target CPU using NetBSD-compatible headers and a
runtime or adapted libc:

```text
NetBSD sh, ls, make, gcc, Python, Perl
                  ↓
              NetBSD libc
                  ↓
          cannedBSD system API
                  ↓
       portable core and host backend
```

Because programs are rebuilt, cannedBSD does not initially need to preserve
historical syscall trap instructions, syscall numbers, or register ABIs.
NetBSD libc's low-level wrappers can call normal functions such as:

```c
int     cb_open(const char *, int, mode_t);
pid_t   cb_spawn(const char *, char *const[], char *const[]);
ssize_t cb_read(int, void *, size_t);
int     cb_kevent(/* ... */);
```

Running unmodified historical NetBSD binaries would be a separate binary-ABI
project requiring syscall trapping or an execution envelope.

### Long-term cannedBSD-WASM interoperability

Native code remains the primary ABI: it performs well, permits realistic
self-hosting, and preserves the character of each machine. A secondary WASM
process type is worth reserving as a long-term interoperability goal.

For example, cannedBSD on System 7/m68k and cannedBSD on OS/2 Warp/x86 could
exchange the same cannedBSD-WASM utility unchanged:

```text
tool.cbwasm
    ├── System 7 cannedBSD → m68k WASM interpreter/runtime
    └── OS/2 cannedBSD     → x86 WASM interpreter/runtime
```

The WASM tool should enter the same cannedBSD process namespace and use the
same descriptors, pipes, PTYs, filesystem, sockets, environment, credentials,
and exit-status conventions as native tools. The difference is only its
execution engine and memory representation:

```sh
$ native-command | portable-tool.cbwasm | native-command
```

WASM linear memory also offers useful isolation on systems without an MMU and
makes process-memory snapshots or copies less dependent on physical load
addresses. Those benefits do not justify making WASM the initial or exclusive
ABI: interpretation may be slow on old CPUs, JIT backends would multiply the
porting effort, current WASI lacks the desired Unix process and terminal
semantics, and a modern WASM compiler is not a realistic first self-hosted
toolchain on small classic machines.

The design should therefore reserve, but not initially implement:

- A cannedBSD-specific WASM import namespace corresponding to the versioned
  cannedBSD system-call table.
- A `.cbwasm` executable type recognized by the common loader and shell.
- Mixed native/WASM pipelines and parent-child relationships.
- Capability discovery and ABI version negotiation shared with native modules.
- Distribution of small architecture-neutral administrative and recovery
  tools across cannedBSD platforms.

This is closer to a portable application format inside cannedBSD than to the
architecture of cannedBSD itself.

Porting the entire NetBSD libc immediately may be unnecessarily difficult due
to startup objects, the runtime linker, TLS, pthreads, locales, DNS, NSS, and
host ABI assumptions. An incremental version can use the host compiler runtime
under NetBSD-compatible headers and supply missing or behaviorally different
interfaces in a compatibility library. NetBSD's `libnbcompat`, used when
building host tools on other operating systems, may provide useful seed code,
but it is not a complete personality.

## Portable core and platform backends

The portable core owns Unix semantics. A narrow host interface supplies
mechanisms:

```c
host_alloc_memory();
host_read_file();
host_write_file();
host_poll_events();
host_tcp_connect();
host_tcp_send();
host_draw_surface();
host_get_time();
host_yield();
```

Candidate backends include:

- Classic Macintosh Toolbox with MacTCP or Open Transport
- AmigaOS with `bsdsocket.library`
- Atari TOS/MiNT/GEMDOS
- Linux, macOS, NetBSD, FreeBSD, and OpenBSD
- Other workstation operating systems with native C toolchains

On a POSIX host the implementation may remain mostly a compatibility library.
On a non-POSIX host, the runtime must implement the missing Unix operating
system facilities itself.

## Process model

On classic non-POSIX systems, Unix commands cannot be mapped naturally to host
processes. They should be internal processes inside the enclosing application.
Each requires:

- PID and parent/child relationships
- Saved native CPU registers
- Stack, data, and heap arena
- File-descriptor table
- Current directory and root
- Credentials
- Signal state
- Scheduler state and wait condition

A cooperative scheduler is the natural first implementation. It changes tasks
when a process blocks on I/O, waits for a child, sleeps, yields, or reaches a
safe software scheduling point. The entire runtime periodically yields to the
host OS event loop.

Host APIs that are not reentrant must be serialized. For classic Mac OS, calls
into Toolbox and ROM services should pass through a single host gateway; an
internal process should not be switched out midway through such a call.

## Descriptor and event model

cannedBSD maintains Unix descriptor numbers independently of the host. Common
operations dispatch according to backing object:

```text
descriptor
├── private Unix file
├── translated host file
├── host-backed TCP socket
├── internal Unix-domain socket
├── pipe
├── PTY
└── virtual device
```

The descriptor/event layer converts asynchronous host APIs into blocking Unix
operations and scheduler wakeups. `select`, `poll`, or `kqueue` must combine
readiness from terminals, pipes, files, timers, and network endpoints.

## Executable loading

The initial executable format can be a cannedBSD-native module rather than
historical NetBSD a.out or ELF. A compiler driver such as `cbcc` or `nbcc`
would produce a file containing native text, relocations, initialized data,
stack requirements, and an entry point. A custom `crt0` builds `argc`, `argv`,
and `environ` and transfers control to `main`.

Position-independent code and base-relative data are valuable on systems
without virtual address translation. Immutable PIC text can potentially be
shared among multiple internal processes.

## Filesystem

Directly treating a native filesystem such as HFS as Unix causes semantic
conflicts involving path separators, case sensitivity, filename lengths,
links, device nodes, users, groups, permissions, and atomic operations.

The cleaner structure is a private Unix root stored inside an ordinary host
file:

```text
/
├── bin
├── etc
├── home
├── tmp
├── usr
└── Host
    ├── Macintosh HD
    └── removable media
```

The private root provides stable Unix semantics across platforms. `/Host`
exposes selected native volumes through a translation adapter for exchange.
NetBSD rump FFS/VFS is a possible implementation: its block device can be a
host file while the filesystem behavior remains NetBSD's.

Persistence must be handled conservatively. A crash of the enclosing process
should not leave the image needlessly corrupt. Explicit flushes, orderly
shutdown, and eventually journaling or snapshot support matter on hosts with
weak failure isolation.

## Networking

The initial implementation can use the native host TCP/IP stack:

```text
Unix socket API
       ↓
cannedBSD descriptor/event layer
       ↓
Open Transport / MacTCP / bsdsocket / host sockets
```

The adapter must provide Unix blocking semantics over event-oriented native
interfaces and handle `bind`, `connect`, `listen`, `accept`, send/receive,
readiness notification, DNS, socket options, and `SIGPIPE`. Unix-domain sockets
can be entirely internal.

Direct mapping to host sockets is a practical early backend, but it does not
make TCP semantics literally identical across hosts. Linux, Open Transport,
MacTCP, and `bsdsocket.library` differ in supported address families, socket
options, readiness behavior, listeners, multicast, error reporting, and edge
conditions. The common descriptor and socket API must own the contract and
advertise backend limitations rather than allowing host behavior to define the
contract accidentally.

For stronger uniformity, cannedBSD can eventually own a common NetBSD/rump
TCP/IP stack and present it with a virtual network interface:

```text
cannedBSD application socket
       ↓
common NetBSD/rump TCP/IP stack
       ↓ packets
virtual network interface
       ↓
host packet transport or slirp-style NAT
       ↓
host network
```

Slirp belongs below the virtual interface. It is a user-space packet-to-host-
socket translator/NAT, not the guest socket API or guest TCP stack. Embedding
slirp without first having an internal packet-speaking stack would put it at
the wrong architectural layer. A libslirp-style backend is attractive on hosts
where raw Ethernet is unavailable; hosts with packet access can bridge or
attach the virtual interface directly.

A companion gateway on a modern computer could make present-day Internet
services practical. The classic machine uses a simple trusted connection; the
gateway handles current TLS, certificates, DNS peculiarities, HTTP proxies,
Git transports, and expensive cryptography.

## Terminals and X11

The first UI is a native host window containing a terminal emulator connected
to an internal PTY. Multiple windows can connect to the same hosted system and
process namespace.

An X server is a natural later component. X clients connect through internal
Unix-domain or TCP sockets. The X server renders its root framebuffer into one
native window. A platform backend supplies a pixel surface, repaint events,
keyboard and mouse input, clipboard conversion, and timers. Rootless mapping
of each X window to a host window can come later.

This lets a classic workstation act both as a local Unix development terminal
and as an X terminal for applications running on another machine.

## MMU constraints

An MMU is not required. MacMINIX ran on plain 68000 Macs. Its absence changes
the guarantees and implementation techniques:

- Internal processes can still be scheduled by saving native registers and
  stacks.
- PIDs, pipes, descriptors, signals, filesystems, and networking remain
  possible.
- Processes do not receive hardware-enforced isolation.
- A bad pointer can corrupt another process, the runtime, or perhaps the host.
- Programs need position-independent, base-relative, relocatable, or fixed-slot
  memory conventions.

Protection and correctness are separate questions. A multiprogramming runtime
can be correct without preventing a faulty or hostile program from escaping
its assigned memory.

### The fork problem

Normal `fork()` expects the child to see all pointers at the same virtual
addresses as the parent. Copying a memory arena elsewhere without address
translation leaves arbitrary C pointers referring into the parent's arena.

The pragmatic progression is:

1. Implement `spawn()` and `exec()`.
2. Port a small shell to use spawning.
3. Implement `vfork()` with the requirement that the child immediately calls
   `exec()` or `_exit()`.
4. Add general `fork()` only on backends that can provide appropriate virtual
   mappings, or after defining a sufficiently constrained executable ABI.

Shells and compiler drivers commonly fork only to exec another command, so
spawn/vfork can support substantial real work.

### Capability tiers

- **68000 without MMU:** cooperative scheduling, no isolation, relocatable
  programs, spawn/vfork-centered creation.
- **68020/68030/68040:** larger address spaces and possible MMU assistance when
  the native OS permits it.
- **PowerPC and modern hosts:** host mappings, stronger protection, and
  potentially copy-on-write or host-process-backed fork.

The public API can stay consistent while each backend reports its capabilities
honestly.

## Staged implementation plan

1. Build one host-native terminal window.
2. Implement cooperative tasks and native context switching.
3. Add descriptor tables, pipes, and PTYs.
4. Add a RAM filesystem and Unix path handling.
5. Implement spawn, exec, wait, and minimal signals.
6. Port `echo`, `cat`, `ls`, `test`, and `tr`.
7. Port a small shell such as NetBSD `sh`, `ash`, or `pdksh` before Bash.
8. Add persistent image-backed storage and `/Host`.
9. Add TCP sockets through the native host stack.
10. Bring up assembler, linker, compiler, make, and other base tools.
11. Port versions of Perl and Python appropriate to target memory sizes.
12. Add an X server in a native window.
13. Add further platform backends.

The first decisive integration test is:

```sh
$ echo hello | tr a-z A-Z > /tmp/result
$ cat /tmp/result
HELLO
```

This proves that executable loading, scheduling, pipes, descriptor inheritance,
redirection, and filesystem behavior cooperate. At that point cannedBSD is
already a small Unix.

## Open decisions

- Which host should follow the working Linux reference and initial classic
  m68k Mac OS backend, and what acceptance coverage should it require?
- How much NetBSD libc should be reused initially?
- Should the root filesystem use rump FFS, a simpler image filesystem, or
  another NetBSD filesystem?
- What native module format and relocation/data-base ABI should be used?
- Should scheduling remain cooperative or eventually allow safe preemption?
- What exact spawn/vfork contract is practical, and which upstream programs
  require patches?
- How should native host filenames and metadata appear beneath `/Host`?
- Should networking be host-backed, rump-backed, or selectable?
- Which modern protocols should be delegated to a companion gateway?
- Is eventual NetBSD binary compatibility valuable enough to justify syscall
  trapping or instruction interposition?

## Terminology

- **Not a CPU emulator:** programs execute directly on the target CPU.
- **More than a portability library:** on non-POSIX hosts it owns persistent
  Unix process, descriptor, filesystem, signal, and terminal state.
- **Not necessarily a full NetBSD kernel:** it may reuse NetBSD APIs and rump
  components around a purpose-built process runtime.
- **Library OS / hosted user-space OS:** the best general descriptions.
- **MachTen/MacMINIX-like:** the historical architecture and intended feel.

## References

- [MacMINIX: MINIX on the Apple Macintosh](https://www.pliner.com/macminix/documentation/macint.pdf)
- [NetBSD: Kernel Servers using Rump](https://www.netbsd.org/docs/rump/sysproxy.html)
- [NetBSD rump server/client tutorial](https://www.netbsd.org/docs/rump/sptut.html)
- [NetBSD rump_sp(7)](https://man.netbsd.org/rump_sp.7)
- [NetBSD rump(3)](https://man.netbsd.org/NetBSD-7.1/rump.3)
- [Linux User-Mode Linux documentation](https://www.kernel.org/doc/html/v6.6/virt/uml/user_mode_linux_howto_v2.html)
- [Original NetBSD/usermode discussion](https://mail-index.netbsd.org/tech-kern/2007/12/28/0001.html)
- [NetBSD/usermode status discussion](https://mail-index.netbsd.org/netbsd-users/2017/04/30/msg019524.html)
- [Linux Kernel Library](https://github.com/lsds/lkl)
- [iSH source and architecture notes](https://github.com/ish-app/ish)
- [iSH task implementation](https://github.com/ish-app/ish/blob/master/kernel/task.c)
- [iSH fake filesystem metadata](https://github.com/ish-app/ish/blob/master/fs/fake-db.h)
- [a-Shell](https://github.com/holzschu/a-shell)
- [ios_system command runtime](https://github.com/holzschu/ios_system)

## Host platform survey

The detailed target/resistance matrix, including AmigaOS, BeOS, Haiku,
TempleOS, illumos, Roku, Samsung TV, Android, classic workstation systems,
locked appliances, and the browser backend, lives in [TARGETS.md](TARGETS.md).


## Fast native Mac acceptance (2026-09-07)

A Hammerspoon prototype completed a cold boot, desktop click, `zzz-run-tests`
launch, fresh eight-PASS/ALL PASS result, screenshot, shell `exit`, and clean
System 7 shutdown in **12.6957 seconds**. The earlier 15.6-second measurement
covered only boot to test result; manual capture and shutdown took additional
interaction time. This comparison measures automation overhead, not a faster
underlying test suite.

Use the MAC-04 driver under `platform/mac68k/automation` once integrated with
MAC-01 artifact staging. It uses the supplied Trash/desktop, launcher, shell,
Special, and open-menu crops. Explicit mouse-move events precede clicks; menu
selection holds mouse-down through a drag and then releases over Shut Down.
Never force-terminate a guest merely because a confirmation or save dialog is
still open. Unknown states require inspection or a new template.

The timing prototype tested the currently configured disk, not an identified
new commit. Exact artifact receipts remain a separate acceptance requirement.
The first staged MAC-02 run (`9a3e4db`, archive
`3f808aa9a09c5f3cf7cd6c18941a9608282edc919675c071ebc1e7f347dce164`)
returned fresh ALL PASS and shut down in 14.262 seconds. Repeated cold-run
acceptance is in progress; this is startup regression/context coverage, not
execution of every new libc probe.

## Historical reuse lead: MacPerl and GUSI

MacPerl author Matthias Neeracher described implementing stat/opendir families,
path handling and BSD sockets over classic Mac protocols. GUSI is the relevant
POSIX/pthreads/sockets library. Audit its source before writing new Mac host
filesystem or networking adapters; distinguish host Mac operations from
cannedBSD's internal task descriptors, VFS and process lifecycle.

Sources: [author's 1996 account](https://www.foo.be/docs/tpj/issues/vol1_2/tpj0102-0005.html),
[GUSI project](https://sourceforge.net/projects/gusi/),
[MacPerl release history](https://sourceforge.net/p/macperl/news/).
REUSE-01 will identify exact modules, licenses and 68K/Retro68 constraints;
no claim of a drop-in dependency has been made.
