# TEE-01 Acceptance Matrix

## Pinned Source
- Source: NetBSD `usr.bin/tee/tee.c`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- SHA-256: `ebcf5dcb07756634ba5876630e2567bb7eb318c489a2a8a6c10fb6919f685a53`

## Concrete Acceptance Input/Output/Status Matrix
*(All expected results below are unexecuted source-derived behaviors based purely on read-only source analysis.)*

| Case | Concrete Input/Setup | Expected Source-Derived Behavior | Source Lines | Required API / Prerequisite |
| :--- | :--- | :--- | :--- | :--- |
| **Empty input** | `stdin` is empty. Command: `tee out.txt` | `read` returns `0` immediately. `out.txt` is created empty. Loop skipped. `close` succeeds. Exit `0`. | 101, 108, 128, 135 | `cb_libc_read`, `cb_libc_close` |
| **A-NUL-B bytes** | Input: `A\0B` (3 bytes). Command: `tee out.txt` | `out.txt` and `stdout` contain exactly `A\0B`. Nulls and unprintable bytes are raw-copied verbatim without string truncation. Exit `0`. | 108-122 | Opaque `cb_libc_read`/`cb_libc_write` |
| **8192 A bytes + final B** | Input: 8192 'A's + 1 'B'. Command: `tee out.txt` | First `read` returns `BSIZE` (8192). Loop writes 8192 bytes. Next `read` returns 1. Loop writes 1 byte. Exit `0` cleanly on EOF. | 74, 108, 115 | `cb_libc_read` loop |
| **Truncate (Default)** | `out.txt` has `OLD`. Command: `tee out.txt`. Input: `NEW` | `open` uses `O_TRUNC`. `out.txt` contains exactly `NEW`. Exit `0`. | 102 | `cb_libc_open` `O_TRUNC` |
| **Append (`-a`)** | `out.txt` has `OLD`. Command: `tee -a out.txt`. Input: `NEW` | `open` uses `O_APPEND`. `out.txt` contains exactly `OLDNEW`. Exit `0`. | 81-83, 101 | `cb_libc_open` `O_APPEND` |
| **Directory / Missing parent errors** | Command: `tee /missing/out /tmp valid.txt`. Input: `OK` | `stderr` emits exact diagnostic strings: `tee: /missing/out: No such file or directory` and `tee: /tmp: Is a directory`. `valid.txt` is opened and receives `OK`. Exit `1`. | 101-105, 115, 135 | `cb_libc_open`, `cb_libc_warn`, core `ENOENT`/`EISDIR` strings |
| **Write error retention (`EIO` / `EPIPE`)** | Command: `tee out1 out2`. Input: "chunk1", "chunk2". `out1` faults `EIO` on chunk 1, succeeds on chunk 2. | `write` on `out1` returns `-1` during chunk 1. `stderr` emits `tee: out1: Input/output error`. `break` exits the *current chunk's* write loop for `out1`. Next read of "chunk2" iterates over `out1` again because it remains in `head`. Chunk 2 is attempted on `out1`. `out2` receives both successfully. Exit `1`. | 109, 115-118 | `cb_libc_write` fault injection |
| **Positive short-write retry** | Input: "data". `write` returns fewer bytes than requested. | `wval > 0` but `< n`. The `do...while` loop correctly iterates, advancing `bp += wval` and decreasing `n -= wval` until the chunk is fully written. | 114-121 | `cb_libc_write` partial success |
| **Zero-progress injection (`WRITE-02`)** | `cb_libc_write` would theoretically yield 0 bytes (blocked/zero-progress at console callback). | NetBSD `tee` does not handle `wval == 0` (it would cause an infinite `do...while` loop at lines 114-121 because `n -= 0` never terminates). The runtime API contract *must* convert 0-progress writes to `EIO` (returning `-1`) to prevent this. Raw write 0 is explicitly forbidden. | 114-121 | `WRITE-02` `EIO` runtime contract |
| **Close errors & Descriptor ownership** | Command: `tee out`. `close` faults on `stdout`. | `tee` adds `STDOUT_FILENO` to `head` and explicitly iterates over `head` to `close()` all destinations. If `close(1)` returns `-1`, it emits `tee: stdout: [Error]`. Exit `1`. The task explicitly claims lifecycle ownership over its standard descriptors. | 98, 128-132 | `cb_libc_close` fault injection |
| **Repeated / Interleaved tasks** | Multiple interleaved invocations of `tee` in same kernel. | `tee` allocates states onto a global mutable `LIST *head` (lines 62, 147). The custom wrapper ops must explicitly isolate and swap this global per-execution to prevent cross-task corruption or dangling pointers. | 57-62, 138-149 | `TEE-STATE-01` wrapper ops |
| **Default `SIGINT` (fresh default disposition)** | Command: `tee out`. `SIGINT` arrives. | `tee` lacks signal handlers by default. A *fresh default disposition* must be enforced by the runtime (ignoring any inherited `SIG_IGN`), causing `tee` to terminate immediately. | 84-86 (absent) | Blocked prerequisite: Signal handling |
| **Ignore `SIGINT` (`-i`)** | Command: `tee -i out`. `SIGINT` arrives. | `tee` executes `signal(SIGINT, SIG_IGN)`. It should ignore the signal and run cleanly to completion. This acts as downstream proof for the actual signal API contract. | 84-86 | Blocked prerequisite: Signal handling |

## Constraints & Notes
- Executed evidence is pending actual test implementation. All described outputs are explicitly labeled **unexecuted source-derived**.
- No runtime edits, testcode modifications, fake signal stubs, or guest executions are included in this read-only task.
- `BSIZE` chunking and `do...while` write retry logic dictate that single-fd write failures drop the *current read chunk* for that fd, but do not remove the fd from the `head` linked list. Subsequent chunks retry writing to the faulty fd.
