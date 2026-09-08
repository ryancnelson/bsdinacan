# HEAD-01 Test Plan

This document establishes the exact execution contracts for `head` using the pinned, unchanged NetBSD source `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. It derives all expected behaviors directly from the C logic, intentionally disregarding GNU extensions or assumptions.

## Upstream Error Limitations
- **Conflation of short reads and errors**: `head.c:157` (`if (rv == 0) break;`) terminates the byte-reading loop cleanly whether the stream reached `EOF` or suffered a hard I/O error (`ferror`). The implementation does not evaluate `ferror(fp)`.
- **`getc` limitation**: `head.c:169` (`while ((ch = getc(fp)) != EOF)`) shares the identical limitation, breaking cleanly on `EOF` without evaluating `ferror`.
- **Formatting**: Warnings on `fopen()` failure emit `head: <filename>: <strerror>\n` without the file header block `==> <filename> <==`.

## Stream and Stack Prerequisites (Pending)
- **Stack size**: `head.c:145` statically allocates `char buf[65536];` on the stack. `96 * 1024` is a PROPOSED stack budget for `cb_program_v1`, not proven safe. We require compiling the frame and proving safety via real Linux/Mac boundary acceptance.
- **Conversions**: `strtoimax()` (base 10), `ERANGE` enforcement, and `isdigit()` for `obsolete()` argument rewrites.
- **I/O Wrappers**: Read-only `fopen()`, `fclose()`, `fread()`, `fwrite()`, `getc()`, `putchar()`, and `feof(stdout)`.

## Execution Matrix (Source-Derived Predictions)

| # | Feature | Argv | Input Fixtures | Exact Expected Stdout | Exact Expected Stderr | Status | Source Location |
|---|---|---|---|---|---|---|---|
| 1 | Default limits | `head` | stdin: `"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n"` | `"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n"` | `""` | 0 | `head.c:85`, `169-174` (linecnt defaults 10) |
| 2 | Obsolete Arg | `head`, `-3` | stdin: `"A\nB\nC\nD\n"` | `"A\nB\nC\n"` | `""` | 0 | `head.c:183-194` (rewrites `-3` to attached `-n3`) |
| 3 | Byte precedence | `head`, `-n`, `1`, `-c`, `3` | stdin: `"abcde"` | `"abc"` | `""` | 0 | `head.c:149` (`if (bytecnt)` block executes) |
| 4 | Reversed preced.| `head`, `-c`, `3`, `-n`, `1` | stdin: `"abcde"` | `"abc"` | `""` | 0 | `head.c:149` (bytecnt is still > 0, wins) |
| 5 | Multi-file hdr | `head`, `-n`, `1`, `A`, `B` | A:`"1\n"`, B:`"3\n"` | `"==> A <==\n1\n\n==> B <==\n3\n"` | `""` | 0 | `head.c:129-133` (header formatting) |
| 6 | Missing cont. | `head`, `miss`, `A` | A:`"1\n"` | `"==> A <==\n1\n"` | `"head: miss: no such file or directory\n"` | 1 | `head.c:124-128` (eval=1, continues, no hdr) |
| 7 | Quiet override | `head`, `-v`, `-q`, `A` | A:`"1\n"` | `"1\n"` | `""` | 0 | `head.c:105-108` (qflag=1 wins) |
| 8 | Verbose override| `head`, `-q`, `-v`, `A` | A:`"1\n"` | `"==> A <==\n1\n"` | `""` | 0 | `head.c:110-113` (vflag=1 wins, header restored) |
| 9 | Explicit dash | `head`, `-` | (none) | `""` | `"head: -: no such file or directory\n"` | 1 | `head.c:124` (no `-` stdin magic in upstream) |
| 10| Invalid count | `head`, `-c`, `0` | (none) | `""` | `"head: illegal byte count -- 0\n"` | 1 | `head.c:92-94` (`<= 0` check causes `errx`) |
| 11| Invalid flag | `head`, `-z` | (none) | `""` | `"head: illegal option -- z\nusage: head [-n lines] [file ...]\n"` | 1 | `head.c:116`, `197-204` (getopt prints err, usage) |
| 12| 65538+ scoped RAMFS | `head`, `-c`, `65538` | scoped stdin: `65538 * 'A'` | Scoped stdout file `65538 * 'A'` | `""` | 0 | `head.c:151-155` (fread chunks bounded by buf length) |
| 13| Binary line | `head`, `-n`, `2` | stdin: `\xFF\n\xFF` | `\xFF\n\xFF` | `""` | 0 | `head.c:169` (`EOF` differs from unsigned `\xFF`) |
| 14| Short fread() | `head`, `-c`, `10` | stdin: `"ab"` | `"ab"` | `""` | 0 | `head.c:156-158` (rv=0 cleanly breaks) |
| 15| Real pipe fixture| `head`, `-n`, `1` | pipe stdin: `"pipe\n"` | `"pipe\n"` | `""` | 0 | `head.c:138` (fallback to `stdin`, transparently pipe) |

*Note on Case 12*: The 65538 byte output exceeds `run_case` native 32768/Mac 4096 array captures. This test must be implemented with a scoped child execution using a RAMFS redirected output descriptor. Do **not** enlarge the global `CB_LIBC_TEST_MAX_CAPTURE` limit. The test must inspect the resulting RAMFS file in bounded chunks, checking length and byte contents, then report a compact `PASS` alongside the evaluated child `exit` status.
