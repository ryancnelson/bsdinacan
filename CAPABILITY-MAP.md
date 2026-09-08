# cannedBSD capability map

This is the dependency inventory for planning worker-sized backlog entries.
Implemented contracts remain defined by `SPEC.md`, `LIBC.md`, and the tests.
Only entries explicitly marked `Ready` in `BACKLOG.md` may be claimed.

## NetBSD utility ladder

NetBSD `usr.bin/printenv` is now imported and guest-tested at the repository's
pinned revision. Its source required `environ`, `exit`, `getopt`, `printf`,
`fprintf`, `stderr`, `errx`, and `__dead`; `strlen`, `memcmp`, and `strchr` are
already present. The completed dependency sequence was:

1. Per-task libc process state and an `environ` location/accessor that resolves
   the current task during interleaving and reflects environment mutation and
   exec.
2. Non-returning `exit(3)` plus the `__dead` declaration annotation.
3. The empty-option subset of `getopt`, with per-task state and exec reset.
4. Unbuffered `%s`/literal/`%%` formatting through `printf`, `fprintf`, stdout,
   and stderr, including short writes and errors.
5. `errx(3)` and the task-local program-name policy used in its diagnostic.
6. The unchanged, hash-pinned `printenv` source, descriptor, provenance, and
   behavioral acceptance tests.

Unchanged `echo` (registered as `netbsdecho`), `basename` and `dirname` are
accepted on Linux and classic Mac. The active next milestone is pinned `head`;
its measured dependency plan and assigned tasks live in BACKLOG.md.

Further imports must be confirmed by compiling the exact pinned source:
- `pwd`: libc wrappers for existing stat/getcwd/environment operations,
  `strstr`, and logical-versus-physical path behavior.
- `mkdir`: only after mode, `umask`, `chmod`, and setmode/getmode semantics.
- `head` and `cat`: only after a real unbuffered FILE read layer including
  open/close, byte and block reads, EOF/error state, and descriptor access.
- `sed`, then `awk`: bounded compatibility plans and tests must precede regex
  or language implementation.

## Existing runtime operations that still need libc veneers

- Process: getpid, getppid, spawn/exec startup policy, exit, waitpid, yield.
- Descriptors: lseek, dup, dup2, close-on-exec, pipe, and fstat.
- Filesystem: stat, mkdir, unlink, chdir, and getcwd.
- Environment: getenv, setenv, unsetenv, and the environment vector.

Expose these in slices driven by an imported program or an ordinary-source
acceptance test. Do not mirror the ABI into libc without a consumer.

## Runtime and filesystem operations to add

Dependencies run roughly from top to bottom within each lane:

- Executable objects: represent registered commands as executable VFS nodes;
  resolve shell execution through those nodes; specify ENOENT, EACCES, and
  ENOEXEC; then permit file-backed native or `.cbwasm` program objects.
- Directories: add a versioned node-iteration contract; expose
  opendir/readdir/closedir; define mutation, independent cursor, EOF, and error
  behavior; use it to inventory an unchanged NetBSD `ls`.
- File mutation: truncate/ftruncate; atomic rename including replacement and
  cross-mount errors; distinct rmdir; mode, access, chmod, and umask; symlink,
  readlink, link, and timestamps as demonstrated by consumers.
- Descriptor state: fcntl descriptor and status flags; nonblocking pipe and
  console behavior; a public poll interface over the existing readiness seam.
- Time: clock_gettime/gettimeofday over existing host clocks, followed by
  deadline-based nanosleep that lets peer tasks progress.
- Signals and cancellation: task dispositions, masks, and pending state;
  delivery at cooperative safe points; EINTR for blocked calls. Start with one
  source-driven signal rather than importing the entire interface.
- Terminal: isatty; canonical/raw termios, echo, erase, and EOF; window size and
  resize notification; PTY master/slave objects; sessions, process groups, and
  foreground-terminal rules before curses or an editor.
- Mounts and persistence: exercise routing and EXDEV with two memory mounts;
  then an image-backed filesystem with flush/recovery tests; then an explicit
  host-volume filename and metadata mapping.

## Networking and execution extensions

- Add byte-order and address types only with the socket slice that consumes
  them.
- Build a connect-only stream descriptor over a deterministic mock transport:
  connect failures, partial send/receive, EOF, shutdown, dup/inheritance, and
  poll readiness.
- Implement SOCKS5 negotiation over the byte-stream boundary and test it with a
  fake proxy before adding a real host adapter. Add name resolution separately.
- Treat bind/listen/accept, datagrams, a portable IP stack, and a virtual NIC as
  later capabilities with their own contracts.
- Load program objects from executable VFS nodes only after lookup semantics are
  stable. Prove prepare-before-exec atomicity, source lifetime, ABI rejection,
  and cleanup before adding dynamic native modules or `.cbwasm`.

## Host portability and self-hosting

- Keep the host adapter contract small and test every capability with mock
  operations before adding classic Mac OS, NeXTSTEP, AmigaOS, OS/2, or Solaris
  implementations.
- Use the existing Retro68 build as continuous compile evidence; use a packaged
  artifact and the documented guest procedure for System 7 runtime evidence.
- Self-hosted compilers require stable file update, directory, stdio, time,
  terminal, process, and build-tool slices. Symantec C 7 under System 7 remains
  a possible classic-Mac bootstrap path; pkgsrc remains the longer-term source
  portability witness.
