# NEXT-UTIL-02: Next Utility Selection

## Base and Environment
- **Base SHA:** 22aeb5a24b88077a9ca0897aa3f4cc57764b0bf4 (origin/main)
- **Compiler:** Apple clang version 17.0.0 (clang-1700.0.13.5) Target: arm64-apple-darwin24.5.0
- **NetBSD upstream revision pinned:** `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`

## Dependency Audit

I fetched `uniq.c`, `cut.c`, `x_cut.c`, and `tee.c` from the pinned NetBSD repository. To definitively distinguish source inspection from compile evidence, I compiled the raw upstream source files against the private `cannedBSD` headers using `-nostdinc` combined with the Clang builtin `isystem` to ensure zero host-header leakage. Where standard headers were missing, I provided 0-byte empty files to observe the cascade of missing symbols.

**Empty mock headers used during diagnosis:**
- `scratch/include/sys/stat.h` (0 bytes)
- `scratch/include/signal.h` (0 bytes)
- `scratch/include/locale.h` (0 bytes)
- `scratch/include/util.h` (0 bytes)
- `scratch/include/wchar.h` (0 bytes)

**Compiler Diagnostic Command Template:**
`cc -nostdinc -isystem $(cc -print-file-name=include) -D_XOPEN_SOURCE=700 -Iinclude -Isrc -Icompat/netbsd/include -Ilibc/include -Iscratch/include -std=c99 -Wall -Wextra -Wpedantic -g -O2 -c scratch/<util>.c -o scratch/<util>.o`

### `uniq` (NetBSD `usr.bin/uniq/uniq.c`)
- **Source Hash:** `78d561c8817b3476713c23d76235a19aad726b7b22794ad11443c4f91462a195` (3-clause UCB)
- **Status:** Missing extensive dependencies; requires state lifecycle management.
- **Diagnostic Exit:** `1`
- **Missing Interfaces (Compile Evidence):**
  - `strtol` (missing from `stdlib.h`)
  - `fgetln` (missing from `stdio.h`)
  - `asprintf` (missing from `stdio.h` / `stdlib.h`)
- **Missing Interfaces (Source Inspection):**
  - Writable `fopen` (e.g. `fopen(..., "w")`) which is currently architecturally absent.
  - The `-c` option uses `fprintf("%4d %s")`, but the current `cb_libc_fprintf` formatter supports only `%s` and `%%`.
- **State Ownership/Isolation Prerequisites:**
  - `uniq` uses mutable globals. Since tasks share the host process, these globals will leak across multiple executions. Acceptance requires explicitly documenting state ownership and per-execution isolation across yields before importing.

### `cut` (NetBSD `usr.bin/cut/cut.c` & `usr.bin/cut/x_cut.c`)
- **Source Hash (`cut.c`):** `7710a0db344af6cf879cbf5bde89e85a380dee37dda6a9ab08cb6318c36a011f` (3-clause UCB)
- **Source Hash (`x_cut.c`):** `3c7415ac42534c7f75a3a3a232d02a3c6e50b7808b6d94ae43008e0cc12b1271` (3-clause UCB)
- **Status:** Missing extensive dependencies; requires state lifecycle management.
- **Diagnostic Exit:** `1`
- **Missing Interfaces (Compile Evidence):**
  - `_POSIX2_LINE_MAX` (missing from `limits.h`)
  - `ecalloc`, `erealloc` (missing from NetBSD's `util.h` / `stdlib.h` extension)
  - `strtok`, `memset` (missing from `string.h`)
  - `strtol` (missing from `stdlib.h`)
  - `roundup` (missing from `sys/param.h`)
  - `mbstate_t`, `mbrlen` (missing from `wchar.h`)
  - `fgetln` (missing from `stdio.h`)
- **Missing Interfaces in `x_cut.c` (Source Inspection):**
  - Included directly into `cut.c`, requires `wint_t`, `getwc`, `WEOF`, `putwchar`, and the `__unused` macro. (No usage of `iswalnum` exists).
- **State Ownership/Isolation Prerequisites:**
  - `cut` relies on mutable globals. Task isolation requires per-execution isolation across yields; reset alone cannot satisfy interleaving for globals to ensure repeated or interleaved task acceptance passes safely without traversing dangling pointers.

### `tee` (NetBSD `usr.bin/tee/tee.c`)
- **Source Hash:** `ebcf5dcb07756634ba5876630e2567bb7eb318c489a2a8a6c10fb6919f685a53` (3-clause UCB)
- **Status:** Recommended; bounded new scope required.
- **Diagnostic Exit:** `1`
- **Missing Interfaces (Compile Evidence):**
  - `<sys/stat.h>` header, specifically the `DEFFILEMODE` constant used for file creation.
  - `<signal.h>` header, specifically the `signal()` prototype, and the `SIGINT` / `SIG_IGN` constants.
- **Supported Interfaces Validated:** 
  - `open`, `read`, `write`, `close`, `STDIN_FILENO`, `STDOUT_FILENO` (raw I/O bypasses the need for buffered write streams).
  - `err`, `warn`, `fprintf`, `malloc`, `getopt`, `setlocale`.
- **Adapter Progress Contract Required (Source Inspection):**
  - `tee.c` implements raw write loops as `do { if ((wval = write(p->fd, bp, n)) == -1) ... bp += wval; } while (n -= wval);`.
  - If `write` yields `0`, the loop spins infinitely. The underlying adapter must guarantee it never returns a `0` incomplete write.
- **State Ownership/Isolation Prerequisites (Source Inspection):**
  - `tee` uses a global `LIST *head` pointer. `add()` dynamically allocates and prepends to it. There is no `reset()` path, and task exit frees the underlying allocations. Repeated calls will traverse dangling pointers, and concurrent tasks will share the global list. Acceptance must mandate per-execution isolation across yields (reset alone cannot satisfy interleaving) before importing.

## Recommendation

I recommend **`tee`** as the smallest coherent next milestone, with specific prerequisite work required before it can be securely imported.

**Proposed Dependency Task IDs:**
1. **`STAT-01`**: Introduce `libc/include/sys/stat.h` and define `DEFFILEMODE`.
2. **`SIG-01`**: Introduce `libc/include/signal.h` defining `SIGINT`/`SIG_IGN` and a formal `cb_libc_signal` implementation. **Note:** A harmless no-op stub is insufficient because `tee` explicitly ignores the return value of `signal()`, so a failure or no-op cannot establish the expected `tee -i` semantics. The implementation must establish an honest task-owned disposition contract. Import of `tee` is blocked until this disposition management is chosen and established.
3. **`TEE-STATE-01-design`**: Design module state management for `tee` to establish a per-execution isolated context for the global `head` list (e.g., using a tee-specific execution wrapper that delegates to native inner execution, saving/restoring the renamed global list pointer across yields) to securely handle interleaved execution, failure, and cleanup without public ABI changes.
4. **`WRITE-02-design`**: Formulate an adapter progress contract to guarantee `write` never spins infinitely on a `0` incomplete write. (Note: This is already claimed and owned by root at `f734884`).
