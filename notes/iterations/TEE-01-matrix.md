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
| **8192 A bytes + final B** | Input: 8192 'A's + 1 'B'. Command: `tee out.txt` | With a controlled RAMFS input delivering full reads, the first read returns 8192 and the second returns 1. Both output and stdout must equal the complete 8193-byte input. A separate short-read fixture must preserve the same bytes; read sizes are not generally guaranteed. Exit `0` on EOF. | 74, 108, 115 | `cb_libc_read` loop |
| **Truncate (Default)** | `out.txt` has `OLD`. Command: `tee out.txt`. Input: `NEW` | `open` uses `O_TRUNC`. `out.txt` contains exactly `NEW`. Exit `0`. | 102 | `cb_libc_open` `O_TRUNC` |
| **Append (`-a`)** | `out.txt` has `OLD`. Command: `tee -a out.txt`. Input: `NEW` | `open` uses `O_APPEND`. `out.txt` contains exactly `OLDNEW`. Exit `0`. | 81-83, 101 | `cb_libc_open` `O_APPEND` |
| **Directory / Missing parent errors** | Command: `tee /missing/out /tmp valid.txt`. Input: `OK` | `stderr` emits exact diagnostic strings: `tee: /missing/out: no such file or directory` and `tee: /tmp: is a directory`. `valid.txt` is opened and receives `OK`. Exit `1`. | 101-105, 115, 135 | `cb_libc_open`, `cb_libc_warn`, core `ENOENT`/`EISDIR` strings |
| **Read error after prefix** | Command `tee out`; controlled reads yield `AB`, then -1/EIO. | Both `out` and stdout retain exactly `AB`; stderr is `tee: read: input/output error\n`, status1. All opened destinations are closed. A separate first-read EIO case leaves both outputs empty with the same diagnostic/status. | 108, 123-135 | Real read fault injection; exact descriptor cleanup |
| **Write error retention (`EIO` / `EPIPE`)** | Command: `tee out1 out2`. Input: "chunk1", "chunk2". `out1` faults `EIO` on chunk 1, succeeds on chunk 2. | `write` on `out1` returns `-1` during chunk 1. `stderr` emits `tee: out1: input/output error`. `break` exits the *current chunk's* write loop for `out1`. Next read of "chunk2" iterates over `out1` again because it remains in `head`. Chunk 2 is attempted on `out1`. `out2` receives both successfully. Exit `1`. | 109, 115-118 | `cb_libc_write` fault injection |
| **Positive short-write retry** | Input: `data`. A controlled sink accepts 2 then 2 bytes. | `wval > 0` but `< n`. The `do...while` loop correctly iterates, advancing `bp += wval` and decreasing `n -= wval` until the chunk is fully written. Expect exactly `data`, no duplicate bytes, empty stderr and status0. | 114-121 | `cb_libc_write` partial success |
| **Zero-progress injection (`WRITE-02`)** | Command `tee out`, input `OK`; its real nonempty console write callback returns zero. | NetBSD `tee` does not handle `wval == 0` (it would cause an infinite `do...while` loop at lines 114-121 because `n -= 0` never terminates). The runtime API contract *must* convert 0-progress writes to `EIO` (returning `-1`) to prevent this. Expect output file `OK`, empty stdout, status1 and `tee: stdout: input/output error\n`; assert a finite callback count. Raw write 0 is explicitly forbidden. | 114-121 | `WRITE-02` `EIO` runtime contract |
| **Close errors & Descriptor ownership** | Command: `tee out`, input `OK`. Inject EIO on `close(1)` after data writes. | `tee` adds `STDOUT_FILENO` to `head` and explicitly iterates over `head` to `close()` all destinations. If `close(1)` returns `-1`, it emits `tee: stdout: input/output error\n` for an injected EIO. Exit `1`. `out` and captured stdout retain `OK`; check closed output descriptors and eventual task-owned descriptor cleanup separately. | 98, 128-132 | `cb_libc_close` fault injection |
| **Repeated / Interleaved tasks** | Multiple interleaved invocations of `tee` in same kernel. | `tee` allocates states onto a global mutable `LIST *head` (lines 62, 147). The custom wrapper ops must explicitly isolate and swap this global per-execution to prevent cross-task corruption or dangling pointers. | 57-62, 138-149 | `TEE-STATE-01` wrapper ops |
| **Default `SIGINT` (fresh default disposition)** | Command: `tee out`. `SIGINT` arrives. | `tee` lacks signal handlers by default. Start this fixture from a fresh task with default disposition; a real pending request terminates it with status 130 at a cooperative delivery boundary. Do not override inherited ignore: a separate inherited-ignore fixture must continue. These are proposed runtime-contract expectations, not source-only proof. | 84-86 (absent) | Blocked prerequisite: Signal handling |
| **Ignore `SIGINT` (`-i`)** | Command: `tee -i out`. `SIGINT` arrives. | `tee` executes `signal(SIGINT, SIG_IGN)`. It should ignore the signal and run cleanly to completion. This acts as downstream proof for the actual signal API contract. | 84-86 | Blocked prerequisite: Signal handling |

## Constraints & Notes
- Executed evidence is pending actual test implementation. All described outputs are explicitly labeled **unexecuted source-derived**.
- No runtime edits, testcode modifications, fake signal stubs, or guest executions are included in this read-only task.
- `BSIZE` chunking and `do...while` write retry logic dictate that single-fd write failures drop the *current read chunk* for that fd, but do not remove the fd from the `head` linked list. Subsequent chunks retry writing to the faulty fd.

## Coordinator clarification

Exact diagnostics use the current lowercase strings in src/core.c and include
a trailing newline. Chunk-boundary fault cases require controlled read results;
a pipe is not guaranteed to return one writer chunk per read. For the two-chunk
write-error case, out1 contains only chunk2, while out2 and stdout contain
chunk1 followed by chunk2. A separate EPIPE variant expects `broken pipe`.
Ownership and signal rows describe required downstream runtime observations,
not properties established merely by reading tee.c. Observe task allocations
and descriptor closure before later kernel teardown can hide missing cleanup.
