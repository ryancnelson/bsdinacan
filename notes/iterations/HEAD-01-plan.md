# HEAD-01 Dependency Plan

## Context
The goal is to prepare for the unchanged import of NetBSD's `usr.bin/head/head.c` (RCS 1.24) at the pinned revision `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c` (SHA256: `33745355975529ef5b33256578bee822dae8e80fbb27dc615a1761385d7eb18a`). Compiling and running `head` safely requires specific file streams, argument parsing, numeric conversions, and memory lifecycles. This plan maps actual missing dependencies into small, ordered backlog proposals with observable acceptance criteria, without broadening existing scopes or speculating APIs.

## Implemented Dependencies
By auditing the current `main` headers and source, the following `head.c` dependencies are confirmed to be already implemented:
- `setlocale()` (`LOCALE-01`, in `libc/include/locale.h`)
- `getprogname()` (`PROGNAME-01`, in `libc/include/stdlib.h`)
- `err()`, `errx()` (`libc/include/err.h`)
- `printf()`, `fprintf()` (`libc/include/stdio.h`)
- `malloc()`, `exit()` (`libc/include/stdlib.h`)
- `strcmp()`, `strlen()` (`libc/include/string.h`)
- `putchar()`, `ferror()`, `fflush()` (`STDOUT-01`, accepted at `894b753`)

## Backlog Proposals

### 1. Extended Option Parsing (`GETOPT-02`)
- **Requirement:** `head` uses `getopt(argc, argv, "c:n:qv")`, requiring argument consumption for `-c` and `-n`. The current `cb_libc_getopt` only supports flag options.
- **Scope:** Enhance `cb_libc_getopt` to parse option arguments (indicated by `:`). Support both immediate arguments (`-n10`) and space-separated arguments (`-n 10`), populating `optarg`. Manage missing required arguments predictably according to POSIX without aborting the task.
- **Acceptance Tests:** A custom probe parsing options with and without arguments. Validate `optarg` mapping, `optind` advancement, handling of missing arguments, and interleaved non-option arguments.

### 2. Diagnostics (`ERR-02`)
- **Requirement:** `head` uses `warn()` to report `fopen()` failures without aborting the task.
- **Scope:** Implement `cb_libc_warn` matching the existing `err` formatting but safely returning to the caller. Do not implement `warnx` as it is not called by the pinned source.
- **Acceptance Tests:** A probe invoking `warn` observing the correctly formatted output on `stderr` followed by continued execution.

### 3. String and Numeric Conversion (`CONV-01`)
- **Requirement:** `head` calls `strtoimax()`, `isdigit()`, and `strcpy()` during obsolete argument translation (`-10` to `-n 10`). It also relies on `ERANGE`.
- **Scope:** 
  - Provide `<ctype.h>` with `isdigit()`. 
  - Provide `<inttypes.h>` defining `intmax_t` and `INTMAX_MAX`. **Critical**: Explicitly verify `intmax_t` type alignments and limits across the Retro68 guest and the host ABI.
  - Implement `cb_libc_strtoimax()`. **Policy limitation**: Do not silently expose a base-10-only implementation as a full standard interface. Explicitly enforce and test the supported-base policy (e.g., rejecting non-10 bases if only 10 is supported), managing whitespace, signs, and saturation alongside setting `ERANGE`. Map `CB_ERANGE` into `cannedbsd/abi.h` and expose `ERANGE` in `errno.h`.
  - Expose `cb_libc_strcpy` in `<string.h>`. Since `strcpy` has no bounds-checking API, implementations must exactly replicate standard unchecked behavior.
- **Acceptance Tests:** 
  - Numeric: Probes evaluating valid numbers, negative inputs, invalid bases, invalid character termination, and explicit overflow/underflow boundary testing that strictly assert `ERANGE` transitions. 
  - String: Exact bounds probes utilizing surrounding canaries and NUL checks inside a sufficient destination buffer.

### 4. Input Streams and Files (`STDIN-01`)
- **Requirement:** `head` reads from files via `fopen()`, `getc()`, and `fread()`, defaulting to `stdin` if no files are supplied. It cleans up via `fclose()`. **Upstream limitation**: `head.c` does NOT call `ferror()` directly and deliberately equates short reads directly with EOF without distinguishing input errors.
- **Scope:** Define the `cb_libc_stdin_stream` mapping to descriptor `0`. Implement `cb_libc_fopen` (read-only initially), `cb_libc_fclose`, `cb_libc_getc`, and `cb_libc_fread`.
  - Input `EOF`/`error` flags must be *task-owned*, never a mutable singleton `stdin` state, preserving strict task isolation.
  - `fopen` wrapper allocation failure must correctly close the newly-owned file descriptor to prevent leaks.
  - `fclose` must release *only* the owned wrapper and accurately close the descriptor.
  - `fread` must return completed *element* counts, not bytes (unless `size=1`). Account for `size * nmemb` multiplication overflow and zero arguments.
  - Ensure no simplistic EOF-on-positive-short-read; positive short underlying reads must be retried.
  - Expose `cb_libc_feof` to introspect `struct cb_libc_file` state.
- **Acceptance Tests:** 
  - Probes reading known data buffers via `stdin` and mocked files. 
  - Explicit `getc` binary test verifying that `0xff` evaluates to `255`, NOT `-1` (`EOF`).
  - Validate element-count returns, zero arguments, overflow limits, and EOF state setting upon final `0` returns from the host, isolating it strictly from `EIO` or explicit stream errors. 
  - Specify fresh tasks, `exec`, rebind, and interleaving tests for input streams to confirm isolation boundaries.
  - Record the upstream `head.c` error-conflation honestly in the tests.

### 5. Output Stream Extensions (`FWRITE-01`)
- **Requirement:** `head` uses `fwrite()` for high-volume block writes during `-c` operations, subsequently checking `feof(stdout)` and `ferror()`.
- **Scope:** Implement `cb_libc_fwrite` integrated exactly with the `STDOUT-01` state-tracking to correctly set the sticky `stdout_error` upon partial/failed writes.
  - Like `fread`, `fwrite` must return completed *element* counts, handle `size * nmemb` overflows, and handle zero arguments correctly.
  - Positive short underlying writes *must* be retried (e.g., a 2-byte write followed by a 3-byte write for a 5-byte request yields a full count, no error flag, and preserved `errno`).
  - Partial-then-error must return the completed element count and set the sticky error.
  - **Note**: `head` treats *any* short `fwrite` return as a failure.
  - Implement `feof(stdout)`. It is supported and must preserve the previous output `errno` (as `head`'s error path relies on it).
- **Acceptance Tests:** A probe simulating `fwrite` across `stdout` with mock partial, zero, and error returns from the host, verifying retry accumulation, short element count translation, and sticky error states. Verify `feof(stdout)` preserves `errno`.

### 6. Argv Memory Lifecycle (`ARGV-01`)
- **Requirement:** `head`'s `obsolete()` function dynamically allocates new strings via `malloc` and overwrites `argv` pointers (e.g., `-10` becomes `-n 10`).
- **Scope & Status:** **Already assigned to coordinator implementation worker. Do not implement it yourself.** The coordinator has confirmed the double-free + leak static path, and the worker has successfully reproduced `exit 139` (segfault) on `biggie` with an actual ordinary `argv` rewrite.

### 7. The `HEAD-01` Milestone
- **Requirement:** Assemble the unchanged source into a functional task.
- **Scope:** Import `usr.bin/head/head.c` untouched. The original source allocates an automatic `char buf[65536];` on the stack inside `head()`.
  - Do NOT raise global stack limits or use dynamic descriptors.
  - Define the static `CB_LIBC_PROGRAM` descriptor using the existing `requested_stack_size` parameter (e.g., `96 * 1024` or `128 * 1024`).
  - Quantify and verify the tested stack margin strictly on each target (Linux and Mac).
- **Acceptance Tests:** Exact command tests comparing `stdout`, `stderr`, and exit statuses for empty input, argument counts (`-n 5`, `-c 100`), invalid options, missing files, obsolete argument rewrites (`-10`), and `q`/`v` headers. Execute block reading tests over large payloads verifying the 64 KiB stream boundaries and confirming the elevated stack parameter prevents corruption.
