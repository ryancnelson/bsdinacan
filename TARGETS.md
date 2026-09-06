# cannedBSD host platform survey

This document explores how different host operating systems resist or assist a
cannedBSD port. The recurring fault lines are:

- Whether applications may load and execute native code
- Whether the host provides processes, threads, virtual memory, or protection
- How closely the host filesystem resembles Unix
- Whether TCP/IP and general socket access are available
- How applications integrate with the display and event loop
- Whether platform policy permits an application to behave like an operating
  system

Old operating systems are often technically awkward but permissive. Modern
appliances often have ample CPU and memory but intentionally restrict code
loading, filesystems, processes, and networking.

## Resistance scale

- **1 — Docile:** essentially a source/toolchain port
- **2 — Mild:** compatibility library plus modest adaptation
- **3 — Substantial:** requires the real cannedBSD runtime
- **4 — Severe:** major architectural or policy constraints
- **5 — Feral:** nearly impossible without changing the project rules,
  exploiting the device, or accepting a much smaller environment

## Headline targets

| Host | Resistance | Likely cannedBSD form |
|---|---:|---|
| AmigaOS | 3 | Native m68k/PPC runtime using Exec tasks, messages, host files, console windows, and `bsdsocket.library` |
| BeOS | 2 | Host-native processes with a NetBSD compatibility personality |
| Haiku | 1 | Mostly a curated NetBSD userland; internal runtime optional |
| TempleOS | 5 | Native x86-64 tasks, private Unix runtime, custom storage, and new networking |
| illumos | 1 | Recompiled NetBSD userland on an existing Unix |
| Roku | 5 | Public platform offers BrightScript/SceneGraph rather than a general native runtime |
| Samsung Tizen TV | 4 | WASM cannedBSD inside a signed Tizen web application |
| Android | 2–3 | NDK application with internal native modules, host processes, WASM, or a hybrid |
| iOS/iPadOS | 3–4 | Signed native base modules plus WASM tools; iSH and a-Shell prove two viable but different models |
| NeXTstation / NeXTSTEP | 1–2 | Native m68k portability, old-toolchain and memory test; host is already Unix |
| Solaris 9 sun4m VM / SPARCbook | 1–2 | Immediate UTM/QEMU portability gate, followed by physical 32-bit big-endian SPARC hardware |
| Browser | 2–4 | JavaScript/WASM runtime, virtual storage, canvas terminal, and gateway networking |

## AmigaOS

AmigaOS is an excellent target. It already supplies Exec tasks and message
ports, native preemptive scheduling, loadable code, console windows, file APIs,
shared libraries, and—for networked installations—`bsdsocket.library`. It also
has mature histories of m68k and PowerPC GCC ports.

The resistance is the lack of memory protection on classic machines, absence
of `fork()`, differences between Exec tasks and Unix processes, AmigaDOS path
and metadata semantics, and shared host-library state.

Two models are possible:

```text
AmigaOS
├── cannedBSD supervisor
├── shell as an Exec task
├── compiler as an Exec task
└── utilities as Exec tasks
```

or:

```text
AmigaOS
└── one cannedBSD application
    └── internally scheduled Unix processes
```

Using Exec tasks as scheduling mechanisms while cannedBSD owns PIDs,
parentage, descriptors, signals, and Unix state may be the best compromise.
`ixemul.library` and GeekGadgets are important historical precedents.

## BeOS and Haiku

BeOS already has POSIX-style descriptors and file I/O, sockets, threads,
teams, semaphores, GCC, and a capable native filesystem. cannedBSD can map
processes to teams, threads to host threads, networking to host sockets, and
its UI to the Interface Kit. Remaining work centers on `fork()`, signals,
`kqueue`, NetBSD API differences, metadata, and packaging.

Haiku is still more accommodating because of its improved POSIX facilities and
modern compiler environment. On Haiku, cannedBSD risks becoming merely a
well-curated NetBSD userland and compatibility library. Both are valuable
reference hosts but do not exercise the project's most distinctive machinery.

## TempleOS

TempleOS is technically open and uses native x86-64 code, but strongly resists
the proposed environment. It has its own task model, HolyC, a distinctive
executable and filesystem environment, no conventional GCC C ABI, no memory
protection, and no TCP/IP stack.

A port would require one of:

1. A conventional C toolchain and native cannedBSD ABI brought to TempleOS.
2. A cannedBSD-WASM interpreter written in HolyC.
3. A cross-built native runtime with extensive TempleOS-specific adaptation.

Networking would need a new stack and drivers or a serial/virtual link to a
modern gateway. TempleOS is an illuminating art/research target, not a good
architectural starting point.

## illumos and traditional Unix workstations

illumos already provides POSIX processes, virtual memory, filesystems, sockets,
PTYs, shells, and compilers. cannedBSD there is primarily NetBSD-compatible
headers, library routines, base utilities, `kqueue` emulation, pkgsrc, and
possibly a private root.

The same general assessment applies to Solaris, IRIX, HP-UX, AIX, Tru64, QNX,
and the BSDs. These make good reference and test backends because they allow
the API, libc boundary, toolchain, filesystem image, and conformance tests to
stabilize before antique host constraints are added.

## Roku

The normal Roku application model is BrightScript plus SceneGraph rather than
a general native-code application environment. It does not expose ordinary
process creation, arbitrary native executable loading, a general filesystem,
or a raw Unix socket personality to public channels.

A BrightScript shell imitation would not meet cannedBSD's native-code goals.
An exploited or rooted Roku might expose the underlying Linux system, but that
would make the port device- and exploit-specific. Under the supported public
platform, Roku is effectively untameable.

## Samsung Tizen TV

Samsung officially supports WebAssembly inside Tizen TV web applications on
newer sets. This permits an unusual inversion in which WASM, rather than native
modules, becomes the principal cannedBSD executable type:

```text
Tizen TV web application
└── cannedBSD-WASM
    ├── scheduler
    ├── WASM processes
    ├── virtual filesystem
    ├── terminal in HTML canvas
    └── network adapter through allowed web/Tizen APIs
```

Obstacles include signed packages, the browser/Tizen sandbox, restricted
process creation and storage, declared network privileges, application
lifecycle suspension, remote-control input, and model-dependent behavior.

Nevertheless, a virtual disk, canvas terminal, `.cbwasm` utilities, and a
WebSocket gateway are plausible. It would be a strong demonstration of the
portable secondary executable ABI.

## Android

Android already uses Linux and supports native ARM, ARM64, x86, and x86-64
code through the NDK. Several forms are possible:

1. A Termux-like environment using real Android/Linux processes.
2. Internal native cannedBSD modules loaded into one application.
3. A cannedBSD-WASM-only application.
4. Packaged native base tools plus downloadable WASM tools.

The resistance comes from sandboxing, SELinux, scoped storage, background
lifecycle limits, Bionic/NetBSD differences, linker hardening, and restrictions
around writable or downloaded executable code. A sideloaded build can be much
more capable than a store-distributed application.

Android is technically easy but administratively opinionated. It is also a
good hybrid-ABI test host.

## iOS and iPadOS

iOS is technically capable but strongly policy-constrained. Two mature open
source applications demonstrate the available approaches.

iSH provides broad binary compatibility by interpreting x86 instructions and
implementing a Linux syscall personality. Ordinary Alpine i386 programs,
including its compiler and package manager, run unchanged. Guest tasks map to
host threads, Unix metadata is maintained beside host files in SQLite, and
network calls translate to host sockets. This approach is comprehensive but
conflicts with cannedBSD's initial goal of executing native code directly.

a-Shell packages individually ported commands as signed native ARM64
frameworks. Its command dispatcher invokes renamed `main()` functions on
threads, redirects thread-local standard streams, uses host pipes, and tracks
synthetic process state. User-built C and C++ programs target WASM/WASI and run
through WebAssembly in a hidden WKWebView. The WASM environment supports files
and pipes but lacks sockets and real fork; packaged native network commands use
iOS networking directly.

The best cannedBSD form on iOS would resemble a-Shell structurally while
making the internal Unix process model more systematic:

1. Sign native cannedBSD, shell, runtime, interpreters, and selected base tools
   into the application.
2. Run downloaded and locally compiled commands as `.cbwasm`.
3. Put both native and WASM commands in the same cannedBSD PID, descriptor,
   filesystem, signal, and pipeline namespace.
4. Map native networking to permitted iOS socket APIs and accept that raw
   sockets, arbitrary listeners, background execution, and external file
   access are constrained by sandbox and lifecycle policy.

iOS is consequently a strong hybrid-ABI validation target, though a poor first
host for developing the native module loader because code signing prevents the
free loading of newly produced native executables.

## Owned hardware targets

### NeXTstation running NeXTSTEP

The NeXTstation is an explicit physical target. NeXTSTEP already supplies a
Mach/BSD Unix environment, so this port does not prove that cannedBSD can
create Unix semantics on a non-POSIX host. It remains valuable for validating
native m68k execution, big-endian assumptions, conservative compiler support,
small-memory behavior, a native terminal window, and eventual Display
PostScript or X11 integration.

The honest form may be a thin NeXTSTEP host backend beneath the same cannedBSD
core rather than replacing working host Unix facilities for ideological
purity. Tests must still be able to run the one-process/internal-task mode so
that this target exercises shared cannedBSD semantics.

### Solaris 9 sun4m VM and SPARCbook

A working Solaris 9 sun4m virtual machine under UTM/QEMU is the immediate
non-x86 portability target after the Linux MVP. It can expose endian, width,
alignment, compiler, and libc assumptions without first mixing those problems
with old laptop hardware support.

The physical SPARCbook is a separate follow-on target. Solaris 9 does contain
sun4m support; Sun's platform guide lists SPARCclassic, LX, and SPARCstation
4/5/10/20 systems in that platform group. Whether the particular Tadpole
SPARCbook boots Solaris 9 and has working display, PCMCIA, power-management,
and network devices depends on its exact model and vendor support software.

Solaris/sun4m is unusually useful despite Solaris already being Unix.
Big-endian 32-bit SPARC catches byte-order, alignment, integer-width,
function-pointer, and context-switch assumptions that an x86-64 Linux-only
implementation might conceal. The portable core should therefore be kept free
of little-endian and LP64 assumptions from its first revision.

## Additional classic systems

| Host | Resistance | Principal issue or opportunity |
|---|---:|---|
| Classic Mac OS 7–9 | 3–4 | Canonical target; no Unix processes or fork, weak/no protection, but MacMINIX and MachTen prove the design |
| OS/2 Warp | 2–3 | Protected x86 processes, threads, VM, TCP/IP, PM, and EMX/kLIBC precedents; fork and Unix filesystem semantics remain |
| Atari TOS | 4 | Minimal process model and inconsistent networking require an internal runtime |
| Atari MiNT | 1–2 | Already substantially Unix-like; mainly a userland/API port |
| RISC OS | 3–4 | Native ARM and UnixLib help; cooperative WIMP tasks, files, fork, and relocation resist |
| MorphOS | 2–3 | Modern Amiga-like host with stronger facilities |
| AROS | 2–3 | Open source eases integration, but Amiga process/filesystem semantics remain |
| DOS on 386+ | 4 | DPMI/DJGPP give flat native code; runtime must add scheduling, files, network, and terminals |
| DOS on 8086 | 5 | Segmentation and conventional-memory limits defeat the intended large toolset |
| Windows 3.1 | 4 | Cooperative 16-bit segmented host; WinSock helps networking |
| Windows 95/98/ME | 2–3 | Win32 processes, VM, and Winsock help; fork and Unix metadata do not |
| Windows NT/2000 | 2 | Strong host mechanisms; naturally tends toward Cygwin's architecture |
| NeXTSTEP/OpenStep | 1 | Already Unix; mostly install or port the desired userland |
| QNX | 1 | Already POSIX with strong process and IPC facilities |
| Plan 9 | 2–3 | Powerful primitives, but `rfork`, namespaces, devices, and networking intentionally differ from POSIX |
| OpenVMS | 3 | Excellent process/I/O system but record files, versioned paths, CLI, signals, and Unix semantics differ; GNV is precedent |
| Palm OS | 5 | Tiny memory, event-driven application model, and no suitable processes |
| Newton OS | 4–5 | Native ARM exists, but runtime, toolchain, storage, and application integration are difficult |
| Symbian | 3–4 | Native code/networking exist; descriptors, active objects, capabilities, and signing interfere |
| Apple IIgs GS/OS | 5 | Address-space, memory, and toolchain constraints dominate |
| GEOS | 5 | Segmented object runtime and limited resources strongly mismatch |
| OS-9/68K | 1–2 | Already a capable Unix-like real-time environment |

## The browser as a host

A browser is a natural cannedBSD host once `.cbwasm` exists:

```text
Browser
└── cannedBSD
    ├── kernel/runtime in JavaScript or WASM
    ├── internal process scheduler
    ├── virtual filesystem
    ├── terminal in Canvas/DOM
    ├── X server in Canvas/WebGL
    └── cannedBSD-WASM programs
```

### Host mechanism mapping

| cannedBSD need | Browser mechanism |
|---|---|
| Memory | WASM linear memory and `ArrayBuffer` |
| Persistent disk | OPFS or IndexedDB |
| Temporary files | In-memory filesystem |
| Terminal display | Canvas, DOM, or a terminal widget |
| Keyboard and mouse | Browser events |
| Scheduling | JavaScript event loop |
| Parallel execution | Web Workers |
| Timers | Browser timer APIs |
| Networking | WebSocket, WebTransport, and `fetch()` |
| X11 framebuffer | Canvas or WebGL |
| Clipboard | Clipboard API |

### Process model

The first implementation can run all processes on one browser event loop. Each
process has a WASM instance and linear memory plus cannedBSD PID, descriptors,
current directory, credentials, environment, signals, and scheduler state.

```text
JavaScript event loop
└── cannedBSD scheduler
    ├── shell WASM instance
    ├── editor WASM instance
    ├── compiler WASM instance
    └── utility WASM instance
```

Processes run until they block or yield. Web Workers can add parallelism later,
but complicate shared kernel state, synchronization, and IPC.

System calls are WASM imports. Pointers passed to the kernel are offsets within
the caller's linear memory, allowing the runtime to validate and copy data at
the boundary. The same `.cbwasm` tool can therefore run on System 7, OS/2,
AmigaOS, Android, Samsung TV, and the browser.

### Browser networking gateway

Browsers cannot normally create arbitrary TCP or UDP sockets. A companion
gateway can multiplex cannedBSD sockets over WebSocket or WebTransport:

```text
cannedBSD socket
      ↓
multiplexed WebSocket
      ↓
cannedBSD gateway on modern Unix
      ↓
TCP, UDP, DNS, SSH, TLS, and the Internet
```

The framing protocol needs operations such as `OPEN`, `DATA`, `CLOSE`,
`LISTEN`, and `DNS`. This gateway can also serve classic hosts whose old TCP or
TLS implementations cannot handle the modern Internet.

### Browser filesystem and display

A private root image can live in OPFS or IndexedDB. User-approved native files
can appear under `/Host`, subject to browser permission rules. Explicit
flushing, journaling, or snapshots protect against refreshes and crashes.

An internal X server can accept local X protocol connections and render its
root window into Canvas/WebGL. Clipboard and input bridge through browser APIs.

### JavaScript-only fallback

A browser with JavaScript but no native WebAssembly engine could run a WASM
interpreter written in JavaScript. Typed arrays make this more plausible;
truly old JavaScript engines would be slow and may support only a reduced
environment. Possible tiers are:

- Modern browser: built-in WebAssembly engine
- Older browser with typed arrays: JavaScript WASM interpreter
- Very old browser: smaller custom bytecode interpreter or unsupported

### Self-hosting in the browser

Running precompiled `.cbwasm` tools is easier than compiling new ones. Full
LLVM is likely too heavy. Early choices include a small C compiler, an
ACK-derived compiler, a deliberately minimal cannedBSD compiler, or compilation
through the companion gateway.

The eventual experience remains compelling:

```sh
browser-cannedBSD$ cc hello.c -o hello.cbwasm
browser-cannedBSD$ ./hello.cbwasm
```

The browser demonstrates that cannedBSD is an OS personality and process model,
not a particular kernel or physical machine.

## Suggested target sequence

```text
Linux reference core
    ↓
OS/2 host-process backend
    ↓
Amiga internal-task backend
    ↓
Classic Mac internal-process backend
    ↓
WASM secondary ABI
    ↓
Browser and Samsung TV experiments
```

The reference hosts stabilize the shared interfaces. OS/2 validates a capable
non-POSIX mapping. Amiga and classic Mac force the internal process runtime to
be real. Browser and TV targets then validate portable `.cbwasm` tools and the
gateway model.

## Sources

- [Roku SceneGraph/BrightScript application structure](https://developer.roku.com/dev/docs/developing-scenegraph-applications)
- [Samsung TV WebAssembly overview](https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/overview.html)
- [Samsung TV application configuration and privileges](https://developer.samsung.com/smarttv/develop/guides/fundamentals/configuring-tv-applications.html)
- [Android NDK application ABIs](https://developer.android.com/ndk/guides/abis)
- [Be filesystem POSIX interface](https://www.haiku-os.org/legacy-docs/practical-file-system-design.pdf)
- [Solaris 9 Sun Hardware Platform Guide](https://docs.oracle.com/cd/E19957-01/816-1664-05/6m82ltoqt/index.html)
- [Solaris 9 system requirements](https://www.oracle.com/solaris/technologies/solaris9-os-system-requirements.html)
