# TEE-01 Acceptance Matrix

## Pinned Source
- Source: NetBSD `usr.bin/tee/tee.c`
- Revision: `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`
- SHA-256: `ebcf5dcb07756634ba5876630e2567bb7eb318c489a2a8a6c10fb6919f685a53`

## Acceptance Matrix

| Case | Expected Source-Derived Behavior | Required API / Prerequisite |
| :--- | :--- | :--- |
| **Empty input** | `read` returns `0` immediately. `tee` loops through all open destinations, closes them, and exits `0`. | `cb_libc_read` `cb_libc_close` |
| **Binary input** | `read` and `write` operate on raw bytes using an opaque buffer of `BSIZE` (8192). Null bytes or unprintable characters are copied verbatim without translation or string truncation. Exit `0`. | Opaque `cb_libc_read`/`cb_libc_write` |
| **>8192 input** | `BSIZE` is `8 * 1024`. Input over 8192 bytes requires multiple `read` loop iterations. It processes chunks incrementally and cleanly exits `0` on EOF. | `cb_libc_read` loop |
| **Missing leaf / `O_CREAT`** | Uses `O_WRONLY\|O_CREAT\|O_TRUNC` (or `O_APPEND`). A missing file is successfully created with `DEFFILEMODE` (0666). (Permissions are metadata-only). | `cb_libc_open` |
| **Simultaneous destinations** | Multiple `file ...` arguments produce an open FD for each, pushed via `add()` to global `head`. Writes occur in reverse CLI order (plus `stdout` last) because `add()` prepends. | Global `head` wrapper isolation |
| **Truncation / `-a`** | Default is `O_TRUNC` (size 0). Passing `-a` sets `O_APPEND`. `tee` relies exactly on the open flags, not manual lseek. | `cb_libc_open` `O_APPEND` flag |
| **Missing parent / directory dest** | `open` fails (`ENOENT` or `EISDIR`), triggering `warn("%s", *argv)` and setting `exitval = 1`. Iteration continues to next destination; successful destinations remain open and will receive output. Exit `1`. | `cb_libc_warn` `cb_libc_open` failures |
| **Read errors** | `read` returns `< 0`. Triggers `warn("read")` and `exitval = 1`. Proceeds to close all open destinations. Exit `1`. | `cb_libc_read` error injection |
| **Write errors** | `write` returns `-1`. Triggers `warn("%s", p->name)` and `exitval = 1`. `break`s the `do...while` write loop for that specific FD, but proceeds to the next destination in `p->next`. Exit `1`. | `cb_libc_write` error injection |
| **Close errors** | `close(p->fd)` returns `-1`. Triggers `warn("%s", p->name)` and `exitval = 1`. Proceeds to close remaining destinations. Exit `1`. | `cb_libc_close` error injection |
| **Repeated / interleaved tasks** | `tee` uses global `LIST *head` for its destinations. The custom `tee_wrapper_ops` sidecar must properly isolate and swap this global per-execution to prevent leaking/crashing across interleaved tasks or repeated invocations. | `TEE-STATE-01` wrapper ops |
| **Default `SIGINT`** | By default, a generated `SIGINT` kills `tee` because it does not catch it. | Blocked prerequisite: Signal handling |
| **`-i` flag** | `tee` calls `signal(SIGINT, SIG_IGN)`. It should ignore interrupt signals and run to completion. | Blocked prerequisite: Signal handling |

## Notes
- There are no runtime edits, guest executions, or fake signal stubs performed in this task.
- Executed evidence is pending actual test implementation and signal API resolution in future tickets.
