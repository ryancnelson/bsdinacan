# HEAD-01 Test Plan

This document establishes the exact execution contracts for `head` using the pinned, unchanged NetBSD source `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. It derives all expected behaviors directly from the C logic, intentionally disregarding GNU extensions or assumptions.

## Upstream Error Limitations
- **Zero-return EOF/error ambiguity and explicitly positive short reads are emitted**: `head.c:157` (`if (rv == 0) break;`) terminates the byte-reading loop cleanly whether the stream reached `EOF` or suffered a hard I/O error (`ferror`), but correctly continues and emits positive short reads. The implementation does not evaluate `ferror(fp)`.
- **`getc` limitation**: `head.c:169` (`while ((ch = getc(fp)) != EOF)`) shares the identical input error limitation, breaking cleanly on `EOF` without evaluating `ferror`.
- **Formatting**: Warnings on `fopen()` failure emit `head: <filename>: <strerror>\n` without the file header block `==> <filename> <==`.

## Stream and Stack Prerequisites (Pending)
- **Stack size**: `head.c:145` uses an automatic buffer `char buf[65536];`. `96 * 1024` is a PROPOSED stack budget for `cb_program_v1`, not proven safe. We require compiling the frame and proving safety via real Linux/Mac boundary acceptance.
- **Conversions**: `strtoimax()` (base 10), `ERANGE` enforcement, and `isdigit()` for `obsolete()` argument rewrites. `obsolete` only rewrites LEADING numeric arguments until the first non-numeric argument or `--`.
- **I/O Wrappers**: Read-only `fopen()`, `fclose()`, `fread()`, `fwrite()`, `getc()`, `putchar()`, and `feof(stdout)`.

## Execution Matrix (Source-Derived Predictions)

| # | Feature | Argv | Input Fixtures | Exact Expected Stdout | Exact Expected Stderr | Status | Source Location |
|---|---|---|---|---|---|---|---|
| 1 | Default limits | `head` | stdin: `"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n"` | `"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n"` | `""` | 0 | `head.c:85`, `169-174` (linecnt defaults 10) |
| 2 | Obsolete Arg | `head`, `-3` | stdin: `"A\nB\nC\nD\n"` | `"A\nB\nC\n"` | `""` | 0 | `head.c:183-194` (rewrites `-3` to attached `-n3`) |
| 3 | Obsolete limit | `head`, `-q`, `-3` | (none) | `""` | `"head: illegal option -- 3\nusage: head [-n lines] [file ...]\n"` | 1 | `head.c:185` (stops on non-numeric `-q`) |
| 4 | Obsolete succ. | `head`, `-3`, `-q`, `A` | A:`"1\n"` | `"1\n"` | `""` | 0 | `head.c:185` (rewrites `-3`, stops before `-q`) |
| 5 | Byte precedence | `head`, `-n`, `1`, `-c`, `3` | stdin: `"abcde"` | `"abc"` | `""` | 0 | `head.c:149` (`if (bytecnt)` block executes) |
| 6 | Reversed preced.| `head`, `-c`, `3`, `-n`, `1` | stdin: `"abcde"` | `"abc"` | `""` | 0 | `head.c:149` (bytecnt is still > 0, wins) |
| 7 | Multi-file hdr | `head`, `-n`, `1`, `A`, `B` | A:`"1\n"`, B:`"3\n"` | `"==> A <==\n1\n\n==> B <==\n3\n"` | `""` | 0 | `head.c:129-133` (header formatting) |
| 8 | Missing cont. | `head`, `miss`, `A` | A:`"1\n"` | `"==> A <==\n1\n"` | `"head: miss: no such file or directory\n"` | 1 | `head.c:124-128` (eval=1, continues, no hdr) |
| 9 | Quiet override | `head`, `-v`, `-q`, `A` | A:`"1\n"` | `"1\n"` | `""` | 0 | `head.c:105-108` (qflag=1 wins) |
| 10| Verbose override| `head`, `-q`, `-v`, `A` | A:`"1\n"` | `"==> A <==\n1\n"` | `""` | 0 | `head.c:110-113` (vflag=1 wins, header restored) |
| 11| Explicit dash | `head`, `-` | file named dash absent | `""` | `"head: -: no such file or directory\n"` | 1 | `head.c:124` (no `-` stdin magic in upstream) |
| 12| Invalid count | `head`, `-c`, `0` | (none) | `""` | `"head: illegal byte count -- 0\n"` | 1 | `head.c:92-94` (`<= 0` check causes `errx`) |
| 13| Invalid flag | `head`, `-z` | (none) | `""` | `"head: illegal option -- z\nusage: head [-n lines] [file ...]\n"` | 1 | `head.c:116`, `197-204` (getopt prints err, usage) |
| 14| 65538+ RAMFS | `head`, `-c`, `65538` | scoped stdin: `65538 * 'A'` | Scoped stdout file `65538 * 'A'` | `""` | 0 | `head.c:151-155` (fread chunks bounded by buf length) |
| 15| Binary line | `head`, `-n`, `2` | stdin: `\xFF\n\xFF` | `\xFF\n\xFF` | `""` | 0 | `head.c:169` (`EOF` differs from unsigned `\xFF`) |
| 16| Short fread() | `head`, `-c`, `10` | stdin: `"ab"` | `"ab"` | `""` | 0 | `head.c:156-158` (rv=0 cleanly breaks) |
| 17| Empty file | `head` | stdin: `""` | `""` | `""` | 0 | `head.c:169` (`getc` yields EOF immediately) |
| 18| Deterministic pipe| `head`, `-n`, `1` | `api->pipe` connected | `"pipe\n"` | `""` | 0 | `head.c:138` (checked producer/consumer setup) |

*Note on Case 14*: The 65538 byte output exceeds the native 32768 / Mac 4096 capture arrays. This test must be implemented with a scoped child execution using a RAMFS redirected output descriptor. Do **not** resize or enlarge the global capture arrays. The test must inspect the resulting RAMFS file in bounded chunks, checking length and byte contents, then report a compact `PASS` alongside the evaluated child `exit` status.
*Note on Case 18*: Use existing `api->pipe`, `api->spawn` with checked descriptor actions, and `api->waitpid`. The producer must write exactly five bytes `pipe\n`; close unused pipe ends in the parent and both children, verify both child exit statuses and exact output, and keep registration in a scoped fixture. A pre-filled RAMFS file is not a pipe test.
