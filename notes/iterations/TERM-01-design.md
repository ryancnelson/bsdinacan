# TERM-01: Terminal Mode Contract

## Goal
Establish the minimal `termios` contract required to support canonical and raw terminal modes (including echo, erase, and EOF processing) while accounting for actual host adapter behaviors (e.g., Mac UI buffering, Linux TTY inheritance).

## Design

### Shared Terminal-State Owner
Core console descriptors (`0`, `1`, `2`) are currently discrete `cb_open_file` objects, not VFS nodes. 
- **Ownership:** A new `struct cb_terminal_state` will be created natively by the kernel during `cb_kernel_boot` and owned by the `struct cb_kernel`.
- **Sharing:** All three standard console `cb_open_file` instances will hold a reference to this shared state. `tcsetattr` calls on `fd=0` will immediately reflect in the shared structure, dictating input disciplines globally across all tasks sharing the console.

### Host Adapter Capabilities & Fallback
Currently, hosts are falsely assumed to provide raw byte streams: `host_mac.c` explicitly buffers canonical lines with `\b` erase and echo, and Linux inherits the terminal's state. 
- **Adapter Contract:** `struct cb_host_ops_v1` is extended with an optional `int (*console_set_raw)(int enable)` function.
- **ENOSYS Fallback:** If the adapter leaves this `NULL` or returns `-CB_ENOSYS` (e.g., unchanged legacy mock adapters or a Mac UI that cannot natively cede raw control), the core assumes the host natively forces canonical mode. 
  - To prevent double echo/canonical processing, the core disables its internal line discipline.
  - If a guest task calls `tcsetattr` to request raw mode (`~ICANON`), the core returns `-CB_ENOSYS`.
- **Raw Capability:** If the adapter successfully implements `console_set_raw(1)` (e.g., Linux configuring STDIN to raw), the core assumes complete control over the byte stream and performs all echo, erase, and line buffering internally.

### Line Discipline Behavior
When the core manages the line discipline (raw adapter available):
- **Canonical Mode (`ICANON`):** Input bytes are buffered internally up to a fixed line length (e.g., 1024 bytes).
  - **Echo (`ECHO`):** Characters are explicitly written back to the adapter via `console_write`.
  - **Erase (`VERASE`):** Backspace removes the preceding character from the buffer and echoes the destructive sequence (e.g., `\b \b`).
  - **Incomplete Lines:** `poll` returns `0` (not ready) and `read` blocks (yields `CB_TASK_BLOCKED_CONSOLE`) until a line is completed by `\n`, `\r`, or `VEOF`.
  - **Overflow:** If the 1024-byte capacity is reached without a newline, further input is discarded until the buffer is consumed.
- **Partial Reads:** If a guest `read` buffer is smaller than the completed canonical line, the requested bytes are delivered. The remainder of the line remains available for the next `read`.

### Canonical VEOF Handling
The `^D` (`VEOF`) character explicitly terminates a canonical line without injecting a newline.
- **Pending Data:** If the buffer contains data (`"abc\x04"`), the `VEOF` flushes the pending data (`"abc"`) making it immediately available to the reader. The `VEOF` character is consumed.
- **Empty Buffer:** VEOF does *not* permanently toggle an EOF state. It is a discrete event. If the buffer is completely empty when `VEOF` is received, `read` returns `0` signifying EOF. The very next `read` blocks normally waiting for new input.

### Raw Mode Supported Subset
In raw mode (`~ICANON`), input is made available immediately.
- **VMIN / VTIME:** For TERM-01, the core explicitly guarantees support for:
  - `VMIN=1, VTIME=0` (Blocking until at least 1 byte is available).
  - `VMIN=0, VTIME=0` (Strict non-blocking, returning `0` if empty).
- `VTIME > 0` inter-byte timers are deferred to future backlog items. If requested, they fallback safely (e.g., treated as `VTIME=0`).

### Mode Transitions
- **Canonical to Raw:** Any pending unread bytes residing in the canonical line buffer are immediately flushed and become readable as raw bytes.
- **Raw to Canonical:** Existing raw bytes are immediately retro-processed through the line discipline (checking for newlines, erase characters, etc.).

## Deterministic Test Plan

1. **Adapter ENOSYS Fallback:** Verify that using a mock adapter with a `NULL` `console_set_raw` rejects `tcsetattr(~ICANON)` with `CB_ENOSYS`, preserving host canonical guarantees.
2. **Raw Byte Delivery:** Provide a raw mock adapter. Set `~ICANON`, inject `"a\b\n"`. Verify `read` returns `"a\b\n"` exactly, and `poll` reports `POLLIN` immediately upon the first byte.
3. **Canonical Erase & Echo:** Set `ICANON | ECHO`. Inject `"a\bb\n"`. Verify `read` delivers `"b\n"`. Assert the mock `console_write` captured the precise output `"a\b \bb\n"`.
4. **VEOF Non-Stickiness:** Inject `"data\x04\x04more\n"`. Verify the first `read` yields `"data"`, the second `read` yields `0` (EOF), and the third `read` successfully yields `"more\n"`.
5. **Partial Read Persistence:** Inject `"12345\n"`. Read 3 bytes, verifying `"123"`. Read again, verifying `"45\n"`.
6. **VMIN Blocking:** Set `VMIN=1, VTIME=0`. Assert `read` explicitly yields `CB_TASK_BLOCKED_CONSOLE` when no bytes are available.
