# cannedBSD libc and source compatibility

Status: first source-compatibility slice implemented

## Compatibility authority

cannedBSD follows this order when deciding a user-visible C interface:

1. POSIX specifies the interface and required behavior.
2. NetBSD is the behavioral reference where POSIX is silent or permits a
   choice.
3. A `cb_*` extension is introduced only for a genuinely cannedBSD-specific
   facility or for the versioned runtime boundary below libc.

This is a source-compatibility goal. It does not imply NetBSD binary ABI,
system-call-number, ELF, or a.out compatibility.

## Layer boundary

```text
ordinary C source
        |
cannedBSD libc headers and prefixed implementation
        |
versioned cb_api_v1
        |
portable cannedBSD runtime
        |
host operations adapter
```

Application translation units include familiar headers. Those headers map
public names to private `cb_libc_*` symbols, so a command's `read`, `open`, or
`malloc` cannot interpose on the enclosing host application's libc calls. The
veneer is built as `libcannedbsd.a` and uses only
`include/cannedbsd/abi.h`; it cannot include runtime-private types. A small
descriptor translation unit adapts an ordinary `main(int, char **)` to
`cb_program_v1` while native modules remain statically registered.

## Implemented first slice

- `unistd.h`: `read`, `write`, `close`, and standard descriptor numbers.
- `fcntl.h`: `open` plus read/write, append, create, and truncate flags.
- `stdlib.h`: `malloc` and `free`.
- `errno.h`: a task-local modifiable `errno` and all currently declared runtime
  error constants.
- `string.h`: `strerror` only.
- Startup adaptation from ordinary `main` to a native program descriptor.
- A separately compiled, original bootstrap `wc -c` command.

`commands/wc.c` contains no cannedBSD names or private headers. Its object is
compiled separately with `main` renamed, then linked with the descriptor
adapter. `tests/test_libc_source.sh` checks that boundary and rejects imports of
unprefixed host-facing I/O or allocation symbols.

This is not a complete libc and the bootstrap command is not NetBSD `wc`.
Notably absent are stdio, general string functions, directory traversal, time,
signals, terminal control, locale, and the rest of ISO C/POSIX libc.

## Program heap ownership

The program API appends allocate, resize, and release operations. Every
allocation is recorded against the current internal task. A task cannot resize
or release another task's allocation. The heap is reclaimed when the task
exits, when a successful `exec` replaces its program image, or when the kernel
is destroyed early. Zombie collection is therefore not responsible for
retaining a dead program's heap.

`malloc` and `free` dispatch through those operations. The resize operation is
available for a later tested `realloc`, but the libc does not expose untested
surface merely because the underlying primitive exists.

Each task also owns a separately allocated integer errno cell. It is not an
address inside the runtime task object. The cell supplies the libc `errno`
lvalue, is shared with the low-level get/set operations, remains distinct
during cooperative interleaving, and is reset on successful exec.

## Hybrid implementation and provenance policy

The runtime, scheduler integration, libc-to-runtime veneer, and host adapters
remain original cannedBSD code. Suitable NetBSD libc routines, tests, and
utilities may be imported where doing so supplies established behavior rather
than kernel coupling.

Every imported file must retain its file-specific copyright and license. The
commit importing it must record the upstream NetBSD repository path, revision,
local changes, and tests. There is no imported NetBSD code in the repository at
the time of this first slice.

## Compatibility ladder

1. Compile and run an ordinary external `main()` through the libc veneer.
2. Expand only the libc surface required by one unmodified NetBSD utility.
3. Import that utility with provenance and make its upstream tests pass.
4. Cross-build a small pkgsrc package for cannedBSD.
5. Run the pkgsrc build tools inside cannedBSD.
6. Eventually build selected packages inside the can itself.

Pkgsrc is a long-term portability witness, not permission to implement a large
set of poorly specified stubs. Unsupported calls remain absent or explicitly
report `ENOSYS` once their interfaces exist.
