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

That demonstration requires native program execution, scheduling, pipes,
descriptor inheritance, redirection, and a filesystem to work together. A
small shell and base utilities come before Bash, GCC, Python, Perl, or X11.

## Build and run the Linux prototype

The normal Linux build needs a C99 compiler, `make`, Bash, and `ripgrep`; the
one-process integration check also reads Linux `/proc`. The full gate additionally
needs Git, GCC's `-fanalyzer`, and a compiler/runtime with ASan and UBSan. On
Alpine, install `build-base bash clang20 compiler-rt git libucontext-dev
python3 ripgrep` and pass `LDLIBS=-lucontext SANITIZE_CC=clang` to `make`.

```sh
make LDLIBS=-lucontext SANITIZE_CC=clang ci
```

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

## First libc and upstream source targets

The first post-v0.1 compatibility slice compiles `commands/wc.c` as a separate
translation unit containing an ordinary `main(int, char **)`. Its familiar
`read`, `write`, `open`, `close`, `malloc`, and `free` names are supplied by
small cannedBSD headers and the prefixed `libcannedbsd.a` veneer over
`cb_api_v1`; the command does not include the runtime's private header or call
host I/O. The same slice now supplies a task-local `errno` lvalue and
`strerror()`.

```sh
build/bsdinacan -c 'echo -n hello | wc -c'
```

The exact output is `5`. The bootstrap command currently implements only
`wc -c [file]`; it is original code rather than imported NetBSD `wc`.

The next slice compiles NetBSD's `usr.bin/yes/yes.c` byte-for-byte unchanged.
Only its descriptor adapter and the minimal `puts`, exit-constant, and metadata
headers are cannedBSD code. The tests consume one line and close the pipe, then
wait for `yes` itself to prove it encounters `EPIPE` and returns failure instead
of running forever. [UPSTREAM.md](UPSTREAM.md) records the exact revision,
content hash, license, adaptations, and evidence. See [LIBC.md](LIBC.md) for the
compatibility hierarchy, heap ownership contract, and route toward pkgsrc.

## Add a native command

A v0.1 command is a C function that receives only the versioned cannedBSD API,
its copied argument vector, and its copied environment. It returns an
eight-bit-style exit status and must use `api->open`, `api->read`,
`api->write`, and the other callbacks instead of host system calls or host file
descriptors. For example:

```c
static int hello_main(const struct cb_api_v1 *api, int argc,
                      char *const argv[], char *const envp[])
{
    static const char message[] = "hello\n";
    (void)argc;
    (void)argv;
    (void)envp;
    return api->write(1, message, sizeof(message) - 1) < 0 ? 1 : 0;
}

static const struct cb_program_v1 hello_program = {
    CB_ABI_VERSION_V1,
    sizeof(struct cb_program_v1),
    "hello",
    0,
    64 * 1024,
    hello_main
};
```

Direct-API commands add the descriptor to the array in
`cb_register_base_programs()` in
`src/programs.c`. Registration copies the descriptor and name into a generic
runtime-owned program object, so execution goes through the native-executor
lifecycle rather than directly through the registry. Add a black-box command
case to `tests/test_core.c`, then run `make ci`. Command code may retain neither
the API's internal state nor argv/environment pointers after its invocation.

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
registry, a small shell, task-owned program allocations, an original
libc-backed `wc`, and an unmodified pinned NetBSD `yes`. This is not yet the richer demo: `sed`, `awk`,
curses, a tiny vi, networking, dynamic modules, WASM, and classic-host adapters
remain planned work. Read [CURRENT-STATE.md](CURRENT-STATE.md) first when
continuing development, then take the first ready item in
[BACKLOG.md](BACKLOG.md).
