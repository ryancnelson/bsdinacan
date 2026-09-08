# Next real NetBSD utility: dirname

This is a source/compile inventory, not an implementation or working command.
Choose `usr.bin/dirname` as the next visible upstream utility. The libc
`dirname()` function Claude is implementing is a prerequisite; importing the
ordinary `dirname` command is a separate task with its own module, provenance,
command tests, CI artifact and direct guest acceptance.

## Immutable inputs and observed compilation

- NetBSD repository: https://github.com/NetBSD/src
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` (existing UPSTREAM.md pin).
- Source: `usr.bin/dirname/dirname.c`.
- Exact URL: https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/usr.bin/dirname/dirname.c
- SHA256: `839bee201d87fd1024fac687ce8b26963b33919b6c1086373d7ddfdaf6882cd9`.
- File-specific license: three-clause Regents of the University of California;
  keep the complete existing notice and source byte-for-byte unchanged.
- Header snapshot: cannedBSD `592ae410770ae10dbe8c9707b64ab6a1daf220a9`.
- Diagnostic environment: Alpine 3.22, aarch64, GCC 14.2.0; build-base installed
  into a disposable container. Alpine image ID:
  `sha256:2c15e55df5d63efb31b629a557df305130612a16feb029c93447e54dda2c4189`.

Actual command (the audit directory is mounted at `/audit`):

```sh
cc -D_XOPEN_SOURCE=700 -I/audit/headers/include \
  -I/audit/headers/libc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 -H \
  -Dmain=cb_audit_main -c /audit/upstream/usr.bin/dirname/dirname.c \
  -o /audit/results/dirname.o
nm -u /audit/results/dirname.o
```

Compilation exited 0, but **does not establish cannedBSD support**. Include
tracing proves host `/usr/include/libgen.h` and `/usr/include/locale.h` leaked
through. The object imports unprefixed `dirname` and `setlocale`. All remaining
imports are existing private symbols: `cb_libc_err`, `cb_libc_exit`,
`cb_libc_fprintf`, `cb_libc_getopt`, `cb_libc_getopt_state_location`,
`cb_libc_printf`, and `cb_libc_stderr_stream`. There was no linking or execution.
No source changes, replacement headers, or stub APIs were used in the audit.

## Exactly what locale surface this command uses

Line 60 calls `setlocale(LC_ALL, "")` once, before getopt; its return value is
ignored. There are no NULL/query calls, other locale categories, or locale
functions in this source. Line 74 calls `dirname(*argv)` and reports failure
through the already implemented `err` path.

The measured remaining prerequisite after Claude's libc dirname is a private
`locale.h`/`setlocale` boundary with an explicitly approved **C-only policy**.
Do not forward to host setlocale, silently advertise arbitrary locale support,
or substitute a no-op macro. Define how the empty-string request consults the
calling task's environment, succeeds only for supported effective locale
settings, and rejects unsupported settings without changing the active C
profile. Queries are not demanded by this utility; if included in the chosen
minimal API contract, test them explicitly rather than claim broad locale
support. Any environment/state access must remain task scoped. This needs one
bounded implementation loop, not an import of a full locale subsystem.

## Proposed backlog sequence

1. Finish/review the separately assigned libc `dirname` work and its private
   `libgen.h` mapping. That does not register a shell command.
2. Implement the measured C-only locale boundary under its own assigned ID.
   Require private-symbol and header-origin checks, supported/unsupported
   empty-string resolution, and no host locale state or cross-task leakage.
3. Import the unchanged `usr.bin/dirname/dirname.c`, pin hash/license, rename
   only its entry point at build time, and add its native program descriptor.
   Repeat strict compile and inspect undefined symbols: both formerly missing
   calls must now map privately. Run Linux/full exact Woodpecker gates and a
   fresh direct Basilisk II case from the exact artifact.

Acceptance for the command should cover ordinary parent paths, plain names,
root/repeated slashes, empty argument, zero/too-many arguments, invalid options,
`--` before a dash-leading path, repeated invocations and a pipeline. Pin exact
stdout/stderr/status rather than testing only a successful link. Suggested
visible guest command: `dirname /tmp/example`, expecting `/tmp\n`, status0.
The coordinator owns adding the command case and resulting transcript count.

## Other candidates, already measured but deferred

`basename` also compiled only through host libgen/locale headers and imports
unprefixed `basename` and `setlocale`; its existing strcmp/strlen calls already
map privately. It becomes a small follow-on after the locale task and an actual
libc basename implementation.

`head` and `cat` both failed strict compilation. `head` needs a larger stdio input
surface, warnings, numeric conversion/error boundaries and strcpy; its source
also has a 65536-byte stack buffer. `cat` additionally uses locking/nonblocking
flags, stat metadata, stream error state, ctype and program-name setup. Neither
is the smallest next step. Their exact unmodified source, hashes and raw
compiler diagnostics remain in this audit directory; no broader implementation
is proposed here.
