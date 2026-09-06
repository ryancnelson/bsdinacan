# cannedBSD

`cannedBSD` is an exploratory project for putting a small, native-code,
NetBSD-flavored Unix environment inside an application on another operating
system—especially the original operating systems of classic and unusual
workstations.

The model is MachTen and MacMINIX, not a virtual machine:

```text
Classic workstation OS
└── cannedBSD application
    ├── Unix process runtime
    ├── descriptors, pipes, signals, and PTYs
    ├── private Unix filesystem
    ├── adapters to host files and TCP/IP
    ├── terminal and, eventually, X11
    └── native userland
        ├── shell
        ├── compiler and build tools
        ├── Python
        └── Perl
```

Programs are rebuilt for the machine's real CPU and execute as native code.
There is no simulated CPU and no requirement to boot a second machine. The
original OS continues to provide hardware drivers, display, storage, network
access, and event delivery; cannedBSD supplies the missing Unix personality.

On a POSIX host this can begin as a compatibility library. On a non-POSIX host
such as classic Mac OS it necessarily becomes a small user-space operating
system with its own Unix processes, scheduler, descriptors, pipes, signals,
filesystem namespace, and terminal state.

## Initial direction

The first useful vertical slice is deliberately small:

```text
$ echo hello | tr a-z A-Z > /tmp/result
$ cat /tmp/result
HELLO
```

That demonstration requires native executable loading, scheduling, pipes,
descriptor inheritance, redirection, and a filesystem to work together. A
small shell and base utilities come before Bash, GCC, Python, Perl, or X11.

## Build and run the Linux prototype

```sh
make clean test
build/bsdinacan
```

Or run one command noninteractively:

```sh
build/bsdinacan -c 'echo hello | tr a-z A-Z > /tmp/result; cat /tmp/result'
```

The expected output is `HELLO`. Use `make sanitize` for ASan/UBSan and `make
check-build-modes` to verify that a subsequent ordinary test does not reuse a
sanitizer-linked artifact.

`make ci` is the canonical pre-push and Woodpecker gate. See
[CI.md](CI.md) for its exact stages and safety constraints.

Likely early design choices:

- Source compatibility, with programs rebuilt for each target CPU.
- A narrow portable host-operations interface with per-platform backends.
- Cooperative internal scheduling on classic systems.
- `spawn()` and `vfork()+exec()` before fully general `fork()`.
- A private Unix root in a host file, with native volumes exposed below
  `/Host`.
- Host-provided TCP/IP initially, possibly with optional NetBSD rump services.
- Honest capability tiers for machines with and without an MMU.
- Native executables as the primary format, with a future cannedBSD-WASM
  format for portable tools shared unchanged between otherwise incompatible
  hosts such as System 7 and OS/2 Warp.

The iOS applications iSH and a-Shell are important modern precedents. iSH
shows the cost and reach of an emulated CPU plus a Linux binary personality;
a-Shell shows the closer cannedBSD model of signed native command modules,
thread-backed synthetic processes, host I/O, and WASM for user-supplied tools.
The detailed comparison and its design consequences are recorded in
`PROJECT_NOTES.md`.

See [SPEC.md](SPEC.md) for the normative v0.1 MVP contract,
[PROJECT_NOTES.md](PROJECT_NOTES.md) for the architectural discussion,
historical precedents, constraints, alternatives, and staged plan, and
[TARGETS.md](TARGETS.md) for the host-platform resistance survey and browser
backend notes.

## Status

Working Linux proof-of-concept. The v0.1 acceptance pipeline passes, and the
runtime has internal tasks, descriptors, pipes, RAMFS, a native-command
registry, and a small shell. This is not yet the richer demo: `sed`, `awk`,
curses, a tiny vi, networking, dynamic modules, WASM, and classic-host adapters
remain planned work. Read [CURRENT-STATE.md](CURRENT-STATE.md) first when
continuing development, then take the first ready item in
[BACKLOG.md](BACKLOG.md).
