# HEAD-01 Dependency Plan

## Context
The goal is to prepare for the unchanged import of NetBSD's `usr.bin/head/head.c` (RCS 1.24) at pinned revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`. As identified in the utility roadmap audit, compiling and running `head` requires significant stream input, error, string, and conversion capabilities. This plan maps the actual missing dependencies from `main` into small, ordered backlog proposals with observable acceptance criteria. It explicitly accounts for argument parsing, numeric conversion bounds, stream constraints, and the large automatic buffer, without implementing speculative features.

## Implemented Dependencies
By auditing the current `main` headers and source, the following `head.c` dependencies are confirmed to be already implemented:
- `setlocale()` (`LOCALE-01`, in `libc/include/locale.h`)
- `getprogname()` (`PROGNAME-01`, in `libc/include/stdlib.h`)
- `err()`, `errx()` (`libc/include/err.h`)
- `printf()`, `fprintf()` (`libc/include/stdio.h`)
- `malloc()`, `exit()` (`libc/include/stdlib.h`)
- `strcmp()`, `strlen()` (`libc/include/string.h`)

*Note: `STDOUT-01` (currently in review, not in `main`) will provide `putchar()`, `ferror()`, and `fflush()`. They are considered planned prerequisite coverage rather than new backlog items.*

## Backlog Proposals

### 1. Extended Option Parsing (`GETOPT-02`)
- **Requirement:** `head` uses `getopt(argc, argv, "c:n:qv")`, requiring argument consumption for `-c` and `-n`. The current `cb_libc_getopt` only supports flag options.
- **Scope:** Enhance `cb_libc_getopt` to parse option arguments (indicated by `:`). Support both immediate arguments (e.g., `-n10`) and space-separated arguments (e.g., `-n 10`), populating `optarg`. Manage missing required arguments predictably according to POSIX without aborting the task.
- **Acceptance Tests:** A custom probe parsing options with and without arguments. Validate `optarg` mapping, `optind` advancement, handling of missing arguments, and interleaved non-option arguments.

### 2. Diagnostics and Errors (`ERR-02`)
- **Requirement:** `head` uses `warn()` to report `fopen()` failures without aborting the task, and relies on `ERANGE` to catch numeric overflow during option parsing.
- **Scope:** Implement `cb_libc_warn` and `cb_libc_warnx` matching the existing `err`/`errx` formatting but safely returning to the caller. Map `CB_ERANGE` into `cannedbsd/abi.h` and expose `ERANGE` in `libc/include/errno.h`.
- **Acceptance Tests:** A probe invoking `warn`/`warnx` observing the correctly formatted output on `stderr` followed by continued execution. A separate check verifying `ERANGE` translates successfully.

### 3. String and Numeric Conversion (`CONV-01`)
- **Requirement:** `head` calls `strtoimax()` and `isdigit()` to parse the `-n` and `-c` string quantities, expecting `INTMAX_MAX` saturation. It also calls `strcpy()` during obsolete argument translation (`-10` to `-n 10`). None of these exist in `main`.
- **Scope:** Provide `<ctype.h>` with `isdigit()`. Provide `<inttypes.h>` defining `intmax_t` (mapped to `long long`) and `INTMAX_MAX`. Implement `cb_libc_strtoimax()` for base-10 parsing, managing whitespace, signs, and saturation to bounds alongside setting `ERANGE`. Expose `cb_libc_strcpy` in `<string.h>`.
- **Acceptance Tests:** Probes evaluating valid numbers, negative inputs, invalid character termination, and explicit overflow/underflow boundary testing that strictly assert `ERANGE` transitions. String probes verifying `strcpy` bounds behavior.

### 4. Input Streams and Files (`STDIN-01`)
- **Requirement:** `head` reads from files via `fopen()`, `getc()`, and `fread()`, defaulting to `stdin` if no files are supplied. It verifies file-end via `feof()` and cleans up via `fclose()`.
- **Scope:** Define the `cb_libc_stdin_stream` mapping to descriptor `0`. Implement `cb_libc_fopen` (read-only initially, wrapping `cb_libc_open`), `cb_libc_fclose`, `cb_libc_getc`, and `cb_libc_fread`. Implement `cb_libc_feof` to introspect `struct cb_libc_file` state.
- **Acceptance Tests:** Probes reading known data buffers via `stdin` and mocked files. Validate EOF state setting upon short reads or `0` returns from the host, isolating it strictly from `EIO` or explicit stream errors.

### 5. Output Stream Extensions (`FWRITE-01`)
- **Requirement:** `head` uses `fwrite()` for high-volume block writes during `-c` operations, subsequently checking `feof(stdout)` and `ferror()`.
- **Scope:** Implement `cb_libc_fwrite` integrated exactly with the `STDOUT-01` state-tracking to correctly set the sticky `stdout_error` upon partial/failed writes.
- **Acceptance Tests:** A probe simulating `fwrite` across `stdout` with mock partial, zero, and error returns from the host, verifying that it sets `ferror(stdout)` correctly and propagates the short byte count transparently.

### 6. The `HEAD-01` Milestone
- **Requirement:** Assemble the unchanged source into a functional `CannedBSD` task.
- **Scope:** Import `usr.bin/head/head.c` untouched. The original source allocates a massive `char buf[65536];` locally on the stack inside `head()`. Define the `CB_LIBC_PROGRAM` execution descriptor with a deliberately expanded capacity (e.g., `96 * 1024` or `128 * 1024`) bypassing the standard 64 KiB task limit to safely host the automatic buffer alongside `CannedBSD`'s standard call-frame margins.
- **Acceptance Tests:** Exact command tests comparing `stdout`, `stderr`, and exit statuses for empty input, argument counts (`-n 5`, `-c 100`), invalid options, missing files, obsolete argument rewrites (`-10`), and `q`/`v` headers. Execute block reading tests over large payloads verifying the 64 KiB stream boundaries.
