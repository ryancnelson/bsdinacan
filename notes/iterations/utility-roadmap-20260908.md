# Measured utility roadmap after basename

- Scope: documentation only; no runtime or source import.
- Documentation base: main `6e83f00`.
- Diagnostic header revision: `09b6fba88e054a9a317c6fabd8510a01437b4b4d`.
- Hypothesis: pinned head is a small next utility after basename. Actual source
  diagnostics falsified this; echo has the smaller measured boundary.
- Guest acceptance for this documentation change: not required. The separate
  VFS-03 acceptance below was observed by the coordinator on exact `6e83f00`.

## Exact source and license

Both inputs use the existing project NetBSD revision
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` and remain byte-for-byte unchanged
in the disposable audit. No new upstream source is imported by this commit.

| Source | SHA256 | File-specific license |
| --- | --- | --- |
| [usr.bin/head/head.c](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/usr.bin/head/head.c), RCS 1.24 | `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a` | Three-clause Regents, copyright 1980, 1987, 1992, 1993 |
| [bin/echo/echo.c](https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/bin/echo/echo.c), RCS 1.23 | `06d241a7305b4631b5154fe2ba72b433199e945f573dea46b9b0f17a4eeaed04` | Three-clause Regents, copyright 1989, 1993 |

Each source retains the full copyright, three conditions and disclaimer.
An eventual import must preserve its own notice and binary redistribution
obligation, add UPSTREAM provenance, and enforce exact hash/private linkage.

## Actual Linux diagnostic

Compiler: GCC 14.2.0, Alpine Linux 3.22.5, x86_64. The existing build-agent
image was `tribblix-woodpecker-agent:3.18.0`, exact local image ID
`sha256:7618701ca718787675a22f188899f03b8b80438721e17f74e4f166412d23b160`.
It ran in a disposable container with network disabled, a read-only root
filesystem and writable audit/TMPDIR mount. No packages were installed there.

Private headers were extracted with git archive from the diagnostic revision:
`include`, `libc/include`, and `compat/netbsd/include`. Only the first two
entered the ordinary-command include search path. With the audit mounted at
`/audit`, each untouched source was compiled using:

```sh
cc -D_XOPEN_SOURCE=700 -I/audit/headers/include \
  -I/audit/headers/libc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 -H \
  -Dmain=cb_audit_main -c SOURCE -o OBJECT 2>DIAGNOSTICS
```

SOURCE was the downloaded usr.bin/head/head.c or bin/echo/echo.c. Both actual
compile statuses were 1; neither produced an object. There were no invented
prototypes, shim stubs or warning suppressions. Raw diagnostics, compiler
metadata, header snapshot and untouched inputs were retained in the disposable
audit, separate from the repository. A prior local container setup failed when
its filesystem became read-only, before compilation; it contributes no red or
green source evidence. No successful link, execution, CI or guest run of head
or echo is claimed.

## Head findings

Compiler diagnostics reported undeclared ERANGE and stdin, plus implicit
function declarations for fopen, warn, fclose, fread, fwrite, feof, getc,
putchar, strcpy and getprogname. Missing prototypes also caused pointer
conversion and builtin-declaration mismatch diagnostics.

The include trace resolved ctype.h and inttypes.h to host musl headers;
isdigit and strtoimax therefore escaped compile diagnostics without proving
cannedBSD support. Existing import-only limits.h supplies only PATH_MAX for
libc dirname; adding that directory globally does not provide head's
INTMAX_MAX or an ordinary inttypes interface.

There are also concrete semantic blockers beyond declarations:

- Head uses `getopt(argc, argv, "c:n:qv")`; current cb_libc_getopt implements
  flag-only options and always sets optarg to NULL. Merely satisfying the
  strtoimax declaration would still leave required option arguments broken.
- Its head() function declares a 65536-byte automatic buffer. The standard
  native program macro requests a 64 KiB stack. An unchanged import needs a
  deliberately larger descriptor stack and measured call-frame margin.
- File and standard-input streams need ownership, EOF/error and close semantics;
  count conversion needs range-checked numeric parsing. The pinned source
  itself notes that fread returning zero does not distinguish EOF from error.
  Do not advertise stronger behavior than the exact command implements.

Head is deferred until separately reviewed prerequisite tasks exist.

## Echo findings and next loop

Actual diagnostics were four implicit declarations: setprogname (line 56),
putchar (line 67), fflush (line 71), ferror (line 72), and an unused argc
parameter warning promoted to error at line 52. Its ARGSUSED comment does not
suppress GCC's warning. Private locale/stdio/stdlib/string/err headers were
used; only fundamental stdbool/stddef/stdint types came from system headers.
No host callable dependency was counted as implemented.

The source already fits the existing `setlocale(LC_ALL, "")`, `%s` formatter,
strcmp and err paths. However, it ignores individual printf/putchar results,
then fflushes and tests ferror. Current stdout/stderr FILE wrappers contain only
a process-wide descriptor identity; printf returns failure without recording
a stream error. A constant-zero ferror would make unchanged echo falsely
succeed after a failed write. Shared mutable error flags would contaminate
independent cooperative tasks.

ECHO-01-design is ready to define bounded task-owned sticky output-error state,
participation by existing output functions, putchar, and an honest unbuffered
fflush/ferror boundary. It must also specify public setprogname/getprogname
ownership, lifetime and compatibility: an existing runtime getter does not
supply a public setter. The design should name small implementation prerequisites
before ECHO-01 proceeds. No general buffered-I/O subsystem or no-op facade is
a prerequisite inferred from this audit.

Eventual command tests must compare stdout, stderr and status for no arguments,
empty strings/spaces, initial -n, literal --/-e/backslashes, earlier output
failure and a subsequent independent task's success. Echo recognizes only the
first -n, does not use getopt, and does not interpret escapes. Resolve its
relationship with the existing cannedBSD echo explicitly. Keep any required
unused-parameter exception local to this unchanged import. Require real CI
and fresh exact-artifact guest acceptance before declaring the utility done.

## Documentation validation

The documentation branch runs staged publication hygiene and git diff checks,
then all three exact-commit Woodpecker workflows. No runtime tests were added
for a documentation-only change. VFS-03 acceptance is recorded in its own
iteration note and CURRENT-STATE; its resumed 15.02-second automation interval
is explicitly not a cold-boot measurement.
