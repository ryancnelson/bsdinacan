# TERM-01: Terminal Mode Contract

## Goal
Establish the minimal `termios` contract required to support canonical and raw terminal modes (including echo, erase, and EOF processing) over the existing deterministic host console adapter.

## Design

### Termios State Ownership and Sharing
- **Ownership:** Terminal state (`struct termios`) is owned by the core engine (specifically, the VFS node of type `CB_NODE_TERMINAL`) rather than the host adapter. The host adapter continues to provide only raw byte streams.
- **Sharing:** Since `termios` attributes are tied to the underlying terminal device, they are shared across tasks that inherit, `dup`, or open the same terminal node. Modifying the terminal mode in one task instantly affects all other tasks sharing that terminal.

### Line Discipline: Echo, Erase, and EOF Handling
- **Canonical vs. Raw:** A line discipline buffer is introduced in the core `read` path for `CB_NODE_TERMINAL`.
  - In **Raw mode** (`~ICANON`), reads draw directly from the host adapter and return immediately based on available bytes.
  - In **Canonical mode** (`ICANON`), the core engine buffers incoming bytes until a newline (`\n`), EOF, or carriage return is received.
- **Echo (`ECHO`):** When `ECHO` is enabled, the core engine explicitly writes received characters back to the host adapter (`console_write`) as they are typed.
- **Erase (`VERASE`):** The engine processes the backspace/delete character (typically `^H` or `^?`). It removes the previous character from the line buffer and, if `ECHO` is enabled, outputs the appropriate terminal erase sequence (e.g., `\b \b`) to the host.
- **EOF (`VEOF`):** The `^D` character forces the canonical buffer to be immediately available to the reader without waiting for a newline. If the buffer is empty, it signifies `EOF` (the read returns `0`). `EOF` characters are absorbed and not included in the read buffer.

### Versioned ABI Guards
- `struct cb_api_v1` will be extended with `isatty`, `tcgetattr`, and `tcsetattr` function pointers.
- `libc/include/termios.h` will be populated with standard POSIX flag definitions (`ICANON`, `ECHO`, `TCSANOW`, etc.) and `struct termios`.
- Compatibility with older engine mock structures is preserved: `api_is_usable` linkage validation will allow `struct cb_api_v1` that shrinks precisely to older offset boundaries. Invoking terminal functions on an outdated table will safely return `-1` with `CB_ENOSYS`.

### Current Host Capabilities and Unsupported Cases
- **Host Capabilities:** The current host adapter interfaces (`console_read`, `console_write`, `console_poll`) remain unchanged. They are completely unaware of line disciplines and operate strictly in non-blocking raw mode.
- **Unsupported Cases:** The following are explicitly out-of-scope for TERM-01 and deferred to future backlog items:
  - Window size queries (`TIOCGWINSZ`) and `SIGWINCH` resize notifications.
  - Pseudo-terminals (PTYs) and multiplexing.
  - Session management, foreground process groups, and job control signals (`tcsetpgrp`, `SIGTTOU`, `SIGTTIN`).
  - Signal-generating characters (`VINTR` for `SIGINT`, `VSUSP` for `SIGTSTP`, `VQUIT`).
  - Complex output processing (e.g., `ONLCR` newline translation), except for basic echo.

### Deterministic Test Plan
1. **ABI Guard:** Verify that shrinking `struct cb_api_v1` below the terminal offsets causes `tcgetattr` to safely fail with `CB_ENOSYS`.
2. **Isatty:** Validate that `isatty(0)` returns `1` for the console node, while `isatty` on a pipe or regular file returns `0` with `ENOTTY`.
3. **Canonical State Transitions:** Provide a probe to query `tcgetattr` on `fd=0`, toggle `ICANON` and `ECHO`, call `tcsetattr`, and assert the state persisted properly.
4. **Raw Byte Delivery:** Provide mock host input (e.g., `"a\b\n"`). Verify that in raw mode, the reader receives exactly `"a\b\n"`.
5. **Canonical Line Discipline:** Provide mock host input `"a\bb\n"`. Verify that in canonical mode with erase processing, the reader receives exactly `"b\n"`.
6. **Echo Verification:** Verify that while canonical `ECHO` is on, providing `"abc"` to the host read adapter results in `"abc"` (plus any necessary carriage returns) being written to the host write adapter.
7. **Canonical EOF:** Provide `"data\x04"` (where `\x04` is `VEOF`). Verify the reader receives `"data"` immediately, without a newline, and the subsequent read returns `0` (EOF).
