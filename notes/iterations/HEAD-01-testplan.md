# HEAD-01 Test Plan

This document establishes the exact execution contracts for `head` using the pinned, unchanged NetBSD source `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. It derives all expected behaviors directly from the C logic, intentionally disregarding GNU extensions or assumptions.

## Upstream Error Limitations
- **Conflation of short reads and errors**: `head.c:149` (`if (rv == 0) break;`) terminates the byte-reading loop cleanly whether the stream reached `EOF` or suffered a hard I/O error (`ferror`). The implementation does not evaluate `ferror(fp)`.
- **Formatting**: Warnings on `fopen()` failure emit `head: <filename>: <strerror>\n` without the file header block `==> <filename> <==`.

## Stream and Stack Prerequisites (Pending)
- **Stack size**: `head.c:139` statically allocates `char buf[65536];` on the stack. The host program must define `cb_program_v1` specifying a `requested_stack_size` of at least `96 * 1024` to avoid stack corruption on Retro68 and Linux.
- **Conversions**: `strtoimax()` (base 10), `ERANGE` enforcement, and `isdigit()` for `obsolete()` argument rewrites.
- **I/O Wrappers**: Read-only `fopen()`, `fclose()`, `fread()`, `fwrite()`, `getc()`, `putchar()`, and `feof(stdout)`.

## Execution Matrix (Source-Derived Predictions)

| # | Feature | Argv | Input Fixtures | Exact Expected Stdout | Exact Expected Stderr | Status | Source Location |
|---|---|---|---|---|---|---|---|
| 1 | Default limits | `head` | stdin: `"1\n...\n11\n"` | `"1\n...\n10\n"` | `""` | 0 | `head.c:156-162` (linecnt defaults 10) |
| 2 | Obsolete Arg | `head`, `-3` | stdin: `"A\nB\nC\nD\n"` | `"A\nB\nC\n"` | `""` | 0 | `head.c:174-183` (rewrites `-3` to `-n 3`) |
| 3 | Byte precedence | `head`, `-n`, `1`, `-c`, `3` | stdin: `"abcde"` | `"abc"` | `""` | 0 | `head.c:141` (`if (bytecnt)` wins) |
| 4 | Multi-file hdr | `head`, `-n`, `1`, `A`, `B` | A: `"1\n"`, B: `"3\n"` | `"==> A <==\n1\n\n==> B <==\n3\n"` | `""` | 0 | `head.c:128-132` (header formatting) |
| 5 | Missing cont. | `head`, `miss`, `A` | A: `"1\n"` | `"==> A <==\n1\n"` | `"head: miss: No such file or directory\n"` | 1 | `head.c:122-126` (eval=1, continues, no header) |
| 6 | Quiet overrd | `head`, `-v`, `-q`, `A` | A: `"1\n"` | `"1\n"` | `""` | 0 | `head.c:98` (last flag wins, q skips header) |
| 7 | Invalid count | `head`, `-c`, `0` | (none) | `""` | `"head: illegal byte count -- 0\n"` | 1 | `head.c:83` (`<= 0` check causes `errx`) |
| 8 | Invalid flag | `head`, `-z` | (none) | `""` | `"usage: head [-n lines] [file ...]\n"` | 1 | `head.c:104,188` (`usage()`) |
| 9 | 65536+ buf bnd | `head`, `-c`, `65538` | stdin: `65538 * 'A'` | `65538 * 'A'` | `""` | 0 | `head.c:143-145` (loops bounds to sizeof buf) |
| 10| Binary line | `head`, `-n`, `2` | stdin: `\xFF\n\xFF` | `\xFF\n\xFF` | `""` | 0 | `head.c:157` (`EOF` != unsigned `\xFF`) |
| 11| Short fread() | `head`, `-c`, `10` | stdin: `"ab"` | `"ab"` | `""` | 0 | `head.c:148-149` (rv=0 cleanly breaks) |
| 12| Empty file | `head` | stdin: `""` | `""` | `""` | 0 | `head.c:157` (`getc` yields EOF immediately) |
