# Basename: bounded next command and libc milestone

Inventory only; no implementation, link/runtime success or guest result claimed.
Header snapshot is reviewed dirname+locale integration
`96d5936d8d5a6327724aead8af33c9bd998ec852`; the eventual worker must use the
coordinator's freshly accepted base and actual API tail.

## Exact source inputs

Both use the existing NetBSD revision
`b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` from UPSTREAM.md.

- Command: `usr.bin/basename/basename.c`, SHA256
  `717fc4757e656e2ff70e39e6aee6e8e79aca06b3459caff99f1908a26ee670bd`;
  three-clause Regents license, retained in the file.
- Libc: `lib/libc/gen/basename.c`, SHA256
  `f6202a8d1a89118f4743a2aa5880bc985ed8a6ca01b1c4ca654d789ff14adb87`;
  two-clause NetBSD Foundation license, retained in the file.

Exact primary URLs:
https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/usr.bin/basename/basename.c
https://raw.githubusercontent.com/NetBSD/src/b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c/lib/libc/gen/basename.c

## Actual diagnostic evidence

In disposable Alpine3.22/aarch64/GCC14.2.0 with build-base, using unchanged
sources and snapshotted private headers:

```sh
cc -D_XOPEN_SOURCE=700 -I/audit/headers/include \
  -I/audit/headers/libc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 -H \
  -Dmain=cb_basename_main -c /audit/usr.bin/basename/basename.c \
  -o /audit/results/command.o
```

This failed (exit1) at line79: implicit declaration of `basename`, followed by
pointer-from-int conversion. Private libgen.h exists but currently advertises
only dirname. Existing locale, err, getopt, stdio, strcmp and strlen are not
additional blockers in this compile. No fallback host libgen/locale declaration
is counted as support.

```sh
cc -D_XOPEN_SOURCE=700 -I/audit/headers/include \
  -I/audit/headers/compat/netbsd/include -I/audit/headers/libc/include \
  -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O2 -H \
  -Dbasename=cb_libc_basename_upstream \
  -c /audit/lib/libc/gen/basename.c -o /audit/results/libc.o
nm -u /audit/results/libc.o
```

This passed (exit0). The object defines `cb_libc_basename_upstream` and imports
only `cb_libc_strlen` and `cb_libc_memcpy`. Existing namespace, MIN, PATH_MAX and
empty import-libgen shims suffice. This is compile evidence, not a working
public basename veneer or command. No source edits or stub APIs were used.
`compile.sh`, `manifest.json`, and `results/` retain commands, hashes, logs,
compiler version and object symbols in this audit directory.

## Ownership and observable semantics

The pinned libc function never returns an interior input pointer: it copies
into its own static `result[PATH_MAX]`, including NULL/empty -> "." and
all-slash -> "/". It strips trailing slashes, then selects the final component,
clamping output to PATH_MAX-1 with a NUL. Its input is not modified.

Use a writable, separate `basename_buffer[CB_PATH_MAX]` per task and an optional
accessor appended after the actual current API tail. Copy from upstream's
static immediately, with no yield between the upstream call and copy. Reusing
the dirname buffer would invalidate a saved dirname result after a basename
call and needlessly couple two otherwise independent pinned static results.
Do not rename or repurpose the shipped dirname accessor. Same-function repeated
calls may replace that task's previous result; other tasks and the other
function must not overwrite it.

The command calls `setlocale(LC_ALL, "")`, already supplied by LOCALE-01.
It accepts one path and an optional suffix; getopt accepts no options but
supports `--`. For an empty path it prints just a newline and exits0 *before*
calling libc basename, unlike libc basename("") which returns ".".
It strips a suffix only if it is shorter than the result and matches its end.
Equal/longer/nonmatching suffixes leave the result unchanged; an empty suffix
is effectively a no-op. Exactly one matching suffix is removed. The command
writes a NUL into `p[off]`, so returning an immutable string would be incorrect.

## Suggested single bounded task

Implement the basename libc veneer and unchanged basename command together:
all supporting operations and import shims already exist. Separate compilation
identities must remain explicit: `basename_command.o` / `cb_basename_command`
for the command, `netbsd_basename.o` / a distinct libc target for the imported
function, and a separate ordinary probe object. Add only the basename accessor,
private libgen declaration/mapping, task buffer, descriptor, provenance and tests.
No locale expansion, broader libgen API, filesystem work or capacity increase.

Falsifiable acceptance:

- Libc NULL/empty/plain/root/repeated slashes/trailing slashes; input unchanged;
  writable result;1023-byte exact fit and longer-component upstream truncation.
- Same-task repeat, forced cross-task interleaving, and retained dirname versus
  basename results in the same task. Old-sized API and independently NULL
  accessor reject before touching the upstream static/result.
- Command exact stdout/stderr/status: ordinary path, root, trailing slashes,
  empty path, zero/too-many operands, invalid option, `--` dash-leading path,
  matching/nonmatching/equal/longer/empty suffix, repeated calls and pipeline.
  Examples: `basename /tmp/example.txt .txt` -> `example\n`; `basename foo foo`
  -> `foo\n`; `basename ''` -> `\n`, all status0.
- Pin both unmodified hashes/licenses and reject host-facing basename/locale
  symbols. Compile direct ordinary probes through private headers.
- Register the command and direct probe in the actual Mac main, not merely its
  CMake link list. Root owns transcript-count reconciliation, exact CI artifact
  and serialized guest acceptance. Preserve the64-slot production limit using
  scoped native fixtures.
