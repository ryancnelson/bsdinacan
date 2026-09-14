# CAT-01: NetBSD `cat(1)` import design and prerequisite decomposition

- **Status:** Design note refined with full `fcntl` advisory locking resolution and source trace; awaiting coordinator review
- **Base SHA:** `7e1fd6a` (`origin/main`)
- **Branch:** `work/CAT-01`
- **Scope discipline:** Documentation and design specification only. No code, headers,
  Makefiles, runtime files, or upstream sources were added or modified in the repository.
- **Platform qualification applicability:** Mac68k (Basilisk II) and Solaris 9 guest
  qualifications are **EXPLICITLY NOT APPLICABLE** to this documentation-only design
  deliverable per `AGENTS.md` (no executable C tokens, ABI layouts, or runtime behavior changed).

---

## 1. First-hand verification of measured gaps

Rather than relying on audit notes alone, pinned NetBSD `cat.c` was fetched and verified
directly from `https://github.com/NetBSD/src` at pinned commit `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`:

| Utility | Upstream path | RCS revision | SHA-256 | License |
| --- | --- | --- | --- | --- |
| cat | `bin/cat/cat.c` | 1.60 (2023-12-10) | `2cc2ced0fcc6c143e1406e64cdd64ea768101fcd19b6dad531b911a611697cbd` | 3-clause Regents (1989, 1993) |

Direct compilation of `cat.c` against the public headers (`-Icompat/netbsd/include -Ilibc/include -Iinclude`)
using GCC in C99 mode confirmed **eleven distinct missing interfaces and symbols**, appearing in source order:

1. **`struct flock`, `F_WRLCK`, `F_SETLKW`, `fcntl()`** (`cat.c:78, 124–131`):
   - Under `-l` (line 124), `cat` requests an advisory write lock on `STDOUT_FILENO` via `fcntl(STDOUT_FILENO, F_SETLKW, &stdout_lock)`.
   - If `fcntl` returns `-1`, `cat` immediately aborts via `err(EXIT_FAILURE, "stdout")` (lines 129–130); it does not ignore the error.
   - `libc/include/fcntl.h` currently declares only open flags (`O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_ACCMODE`, `O_APPEND`, `O_CREAT`, `O_TRUNC`) and `#define open cb_libc_open`. `struct flock`, flock commands (`F_WRLCK`, `F_RDLCK`, `F_UNLCK`, `F_SETLK`, `F_SETLKW`, `F_GETLK`), and the `fcntl()` prototype are absent.
2. **`strtol(3)`** (`cat.c:86`):
   - Under `-B bsize` (line 86), `cat` parses buffer size using `strtol(optarg, NULL, 0)`.
   - `libc/include/stdlib.h` currently exposes `malloc`, `calloc`, `realloc`, `free`, `exit`, `getprogname`, `setprogname`. Only `strtoimax` exists in `inttypes.h`. `strtol` is absent.
3. **`setbuf(3)`** (`cat.c:110`):
   - Under `-u` (line 110), `cat` invokes `setbuf(stdout, NULL)` to disable buffering.
   - cannedBSD stdio is unbuffered by design, but the `void setbuf(FILE *stream, char *buf)` prototype is absent from `libc/include/stdio.h`.
4. **`SEEK_SET`** (`cat.c:128`):
   - Used to specify `stdout_lock.l_whence = SEEK_SET`.
   - Absent from both `libc/include/stdio.h` and `libc/include/unistd.h` (only internal `enum cb_seek_whence` exists in `cannedbsd/abi.h`).
5. **`clearerr(3)`** (`cat.c:166, 228`):
   - Used to clear error and EOF indicators on `stdin` between files or after non-fatal read errors.
   - `feof` and `ferror` exist in `libc/include/stdio.h`, but `void clearerr(FILE *stream)` is absent.
6. **`isascii(3)`, `toascii(3)`, `iscntrl(3)`** (`cat.c:209, 212, 214`):
   - Under `-v` (visible control characters), `cat` filters non-ASCII and control characters using these helpers.
   - `libc/include/ctype.h` currently exposes only `isdigit` and `isspace`.
7. **`fileno(3)`** (`cat.c:239, 276, 290`):
   - `raw_args` and `raw_cat` extract the integer descriptor of `stdin` and `stdout` using `fileno(fp)`.
   - `fileno` is absent from `libc/include/stdio.h`.
8. **`struct stat`, `fstat(2)`, `S_ISREG`** (`cat.c:248, 253, 257, 294, 297`):
   - Under `-f` (line 247) and in `raw_cat` (line 294), `cat` checks if input is a regular file via `S_ISREG(st.st_mode)` and checks optimal I/O block size via `sbuf.st_blksize`.
   - `libc/include/sys/stat.h` is currently an inert 2-line stub containing only `#define DEFFILEMODE 0666`.
   - In contrast, the runtime ABI (`include/cannedbsd/abi.h`) already defines `struct cb_stat_v1` and provides working `api->stat` and `api->fstat` function pointers. The gap is entirely unwired libc surface.
9. **`O_NONBLOCK`** (`cat.c:249`):
   - Used with `-f` when opening files: `open(*argv, O_RDONLY|O_NONBLOCK, 0)`.
   - Absent from `libc/include/fcntl.h`.
10. **`warnx(3)`** (`cat.c:259, 304`):
    - Used to emit error messages without appending `strerror(errno)` (e.g. `warnx("%s: not a regular file", *argv)`).
    - `libc/include/err.h` provides `err`, `errx`, and `warn`, but lacks `warnx`.
11. **`BUFSIZ`** (`cat.c:285, 298, 301, 307`):
    - Used as the fallback static buffer dimension `static char fb_buf[BUFSIZ]`.
    - Absent from `libc/include/stdio.h`.

---

## 2. Resolution of the `struct stat` & `cb_stat_v1` design question

### Context and analysis
The runtime ABI in `include/cannedbsd/abi.h` currently defines:
```c
struct cb_stat_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t type;
};
```
Both `api->stat(path, stat_buf)` and `api->fstat(fd, stat_buf)` are already implemented in `src/core.c`, `src/vfs.c`, and `src/ramfs.c`. They populate `inode`, `size`, `mode` (permission bits), and `type` (`enum cb_node_type`: regular, directory, terminal, pipe, executable).

The design question asks:
> **Does `cb_stat_v1` gain timestamp (`atime`/`mtime`/`ctime`), `nlink`, `uid`, and `gid` fields now (an appended-field ABI change requiring struct_size boundary tests), OR does the libc veneer expose only the existing four fields (inode, size, mode, type) and satisfy `cat`'s `S_ISREG` path without them?**

### Decision: Retain existing 4-field `cb_stat_v1`; wire POSIX `struct stat` in the libc veneer

**We decide to keep `struct cb_stat_v1` frozen with its existing four fields, and wire standard POSIX `struct stat` in `libc/include/sys/stat.h` mapping onto those four fields.**

### Rationale:
1. **Zero unnecessary ABI churn:**
   - `cat` requires `st.st_mode` (specifically `S_ISREG(st_mode)`), `st.st_size`, `st.st_ino`, and optionally `st.st_blksize`. It never inspects timestamps, owner UID, group GID, or link count.
   - Modifying `cb_stat_v1` at the ABI level would force RAMFS to allocate and maintain timestamp storage per node (~32-48 bytes extra per file/directory), force clock reads on every `open`/`write`/`truncate`/`read`, require backward-compatibility tests for old caller `struct_size` tables, and perturb all existing host adapters and architecture gates.
2. **Clean mode synthesis:**
   - In cannedBSD, the permission mode and the node type are decoupled in `cb_stat_v1` (`mode` holds `0666`/`0777`/`0555`, while `type` holds `CB_NODE_REGULAR`, `CB_NODE_DIRECTORY`, `CB_NODE_TERMINAL`, `CB_NODE_PIPE`).
   - The libc `stat()` / `fstat()` wrapper in `libc/cb_libc.c` translates `cb_stat_v1` into POSIX `struct stat` by synthesizing standard POSIX file type bits into `st_mode`:
     - `CB_NODE_REGULAR` / `CB_NODE_EXECUTABLE` $\rightarrow$ `st_mode = S_IFREG | (raw_mode & 07777)`
     - `CB_NODE_DIRECTORY` $\rightarrow$ `st_mode = S_IFDIR | (raw_mode & 07777)`
     - `CB_NODE_TERMINAL` $\rightarrow$ `st_mode = S_IFCHR | (raw_mode & 07777)`
     - `CB_NODE_PIPE` $\rightarrow$ `st_mode = S_IFIFO | (raw_mode & 07777)`
   - This makes standard POSIX macros (`S_ISREG`, `S_ISDIR`, `S_ISCHR`, `S_ISFIFO`) evaluate accurately for all file types across the system.
3. **Safe blocksize handling:**
   - `struct stat` in `sys/stat.h` will include `blksize_t st_blksize`. The libc veneer sets `st_blksize = 1024` (or `0`).
   - In `cat.c` (lines 297–309), if `st_blksize` is positive, `cat` uses it as the preferred allocation size; if `0` or $\le$ `sizeof(fb_buf)`, it falls back safely to `sizeof(fb_buf)` (`BUFSIZ`).
4. **Consequences for future utilities (`ls -l`, `stat(1)`):**
   - The current `LS-01` milestone is explicitly scoped as a cannedBSD-owned, single-column listing using the dirent contract (`readdir`), which does not call `stat` or print timestamps/owners.
   - When a full multi-column `ls -l` or file-attribute tool is scheduled in a future milestone, `cb_stat_v1` can be cleanly expanded by appending fields (e.g. `nlink`, `uid`, `gid`, `atime`, `mtime`, `ctime`, `blocks`) under a dedicated filesystem metadata backlog item (`FS-STAT-01`). The versioned ABI `struct_size` mechanism is already designed to support that exact appended-field migration without breaking callers compiled against the 4-field layout.

---

## 3. Resolution of the `fcntl` advisory locking design question (`FCNTL-01`)

### Source trace in NetBSD `cat.c`
Advisory record locking in `cat.c` is confined to lines 78 and 124–131:

```c
/* cat.c:78 */
static struct flock stdout_lock;

/* cat.c:124-131 */
if (lflag) {
    stdout_lock.l_len = 0;
    stdout_lock.l_start = 0;
    stdout_lock.l_type = F_WRLCK;
    stdout_lock.l_whence = SEEK_SET;
    if (fcntl(STDOUT_FILENO, F_SETLKW, &stdout_lock) == -1)
        err(EXIT_FAILURE, "stdout");
}
```

Critical source trace observations:
1. **Control flow under `-l`:** When `-l` is specified on the command line, `cat` configures `stdout_lock` to request an exclusive whole-file write lock (`F_WRLCK`, `SEEK_SET`, start 0, len 0) on `STDOUT_FILENO` with blocking wait (`F_SETLKW`).
2. **Error handling is strict:** If `fcntl(STDOUT_FILENO, F_SETLKW, &stdout_lock)` returns `-1`, `cat` immediately invokes `err(EXIT_FAILURE, "stdout")`, printing the diagnostic and exiting with status 1. **It does not ignore errors.**
3. **Unflagged and standard execution path:** For all other invocations (unflagged, `-b`, `-e`, `-f`, `-n`, `-s`, `-t`, `-u`, `-v`, `-B`), `lflag == 0` and `fcntl()` is **never invoked**.
4. **Flag `-f` uses `open()`, not `fcntl()`:** Line 249 executes `open(*argv, O_RDONLY|O_NONBLOCK, 0)`. The `O_NONBLOCK` flag is an `open()` mode argument and does not route through `fcntl()`.

### Rejection of dishonest locking stubs
Under `AGENTS.md`, interfaces must establish genuine, falsifiable behavior rather than pretending to succeed. An advisory locking stub that unconditionally returns `0` for `F_SETLK`/`F_SETLKW` without enforcing mutual exclusion is a **dishonest surface**:
- It would cause two cooperative tasks concurrently executing `cat -l` directed to the same file/terminal to falsely believe they hold exclusive write locks, silently interleaving and corrupting their output without mutual exclusion.
- Per the precedent in `SIG-01-design.md` (which rejected signal stubs because returning fake success fails to enforce real task semantics), a no-op locking stub is strictly rejected.

### Decision: Implement genuine per-node advisory record locking in `FCNTL-01`

**We decide that `FCNTL-01` must implement genuine per-node advisory record locking across `cb_vfs_node` structures in cannedBSD's cooperative multi-task kernel, with real mutual exclusion, conflict detection, and automatic lifecycle cleanup on descriptor close and task exit.**

### Architecture and semantics for `FCNTL-01`:

1. **ABI Definition (`include/cannedbsd/abi.h`):**
   ```c
   enum cb_flock_type {
       CB_F_RDLCK = 1,
       CB_F_WRLCK = 2,
       CB_F_UNLCK = 3
   };

   enum cb_fcntl_cmd {
       CB_F_DUPFD  = 0,
       CB_F_GETFD  = 1,
       CB_F_SETFD  = 2,
       CB_F_GETFL  = 3,
       CB_F_SETFL  = 4,
       CB_F_GETLK  = 7,
       CB_F_SETLK  = 8,
       CB_F_SETLKW = 9
   };

   struct cb_flock_v1 {
       uint32_t abi_version;
       uint32_t struct_size;
       int16_t l_type;   /* CB_F_RDLCK, CB_F_WRLCK, CB_F_UNLCK */
       int16_t l_whence; /* CB_SEEK_SET, CB_SEEK_CUR, CB_SEEK_END */
       cb_off_t l_start; /* Starting offset */
       cb_off_t l_len;   /* Number of bytes; 0 means to EOF */
       cb_pid_t l_pid;   /* Blocking PID (populated by GETLK) */
   };
   ```
   Add `int (*fcntl)(int fd, int cmd, void *arg);` to `struct cb_api_v1`.

2. **Per-node lock tracking in the VFS layer (`src/vfs.c`, `src/internal.h`):**
   - Each `cb_vfs_node` owns a linked list or tracked array of active record locks:
     ```c
     struct cb_record_lock {
         cb_pid_t pid;
         int type;        /* CB_F_RDLCK or CB_F_WRLCK */
         cb_off_t start;  /* absolute start offset */
         cb_off_t end;    /* absolute end offset, 0 = EOF/infinity */
         struct cb_record_lock *next;
     };
     ```
   - Range conversion evaluates `l_whence`:
     - `CB_SEEK_SET`: `start = l_start`.
     - `CB_SEEK_CUR`: `start = file->offset + l_start`.
     - `CB_SEEK_END`: `start = node_size + l_start`.
     - If `start < 0`, returns `-CB_EINVAL`.
     - `l_len == 0` designates locking from `start` through infinity (`end = 0`).

3. **Conflict detection and POSIX semantics:**
   - Two locks overlap if `start1 < end2` and `start2 < end1` (with `0` treated as $\infty$).
   - A lock request conflicts with an existing active lock if:
     1. The active lock is held by a *different* PID (`lock->pid != current_task->pid`), AND
     2. At least one lock is `CB_F_WRLCK` (write locks conflict with both read and write locks; read locks conflict only with write locks).
   - Locks from the *same* PID never conflict with each other; an `F_SETLK` from the owning PID replaces, extends, or splits existing locks held by that PID.

4. **Operation handling:**
   - **`F_GETLK`:** Inspects locks on the node. If a conflicting lock held by another PID exists, overwrites `struct flock` with that lock's parameters (`l_type`, `l_whence = SEEK_SET`, `l_start`, `l_len`, `l_pid`). If no conflict, sets `l_type = F_UNLCK`.
   - **`F_SETLK` (Non-blocking):** If a conflict exists, returns `-1` with `errno = EAGAIN` (or `EACCES`). If uncontested, inserts/updates the lock record (or removes for `F_UNLCK`) and returns `0`.
   - **`F_SETLKW` (Blocking):** If a conflict exists, yields/blocks the cooperative task (`CB_TASK_BLOCKED_LOCK`) until the conflicting task releases the lock or terminates. If uncontested, grants the lock immediately and returns `0`.

5. **Lifecycle and cleanup invariants:**
   - **Close Invariant (POSIX requirement):** When a task closes *any* file descriptor referring to a node (via `api->close` or kernel cleanup), all record locks held by `task->pid` on that specific node are immediately purged.
   - **Exit Invariant:** When a task terminates (`api->exit` or kernel task destruction), all record locks held by `task->pid` across all nodes in the VFS are automatically purged.
   - **Allocation Hygiene:** All lock record memory is allocated via `cb_allocate` and freed via `cb_release` without leaks.

---

## 4. Decomposition into bounded prerequisite IDs

To comply with AGENTS.md single-behavior increment rules, the 11 gaps are partitioned into **six independent, reviewable prerequisite backlog items**, followed by the terminal integration item:

```
[LIBC-CTYPE-01] ───┐
[LIBC-STDIO-02] ───┤
[LIBC-ERR-02]   ───┼──> [CAT-01 (Import & Integration)]
[LIBC-STRTOL-01]───┤
[STAT-02]       ───┤
[FCNTL-01]      ───┘
```

### 1. `LIBC-CTYPE-01` — Ctype classification helpers (`isascii`, `toascii`, `iscntrl`)
- **Scope:** Add `isascii(c)`, `toascii(c)`, and `iscntrl(c)` to `libc/include/ctype.h` and `libc/cb_libc.c` (or inline macros for the standard ASCII/C-locale range 0..127).
- **Red:** Focused probe `tests/test_core.c --ctype` fails while functions/macros are absent.
- **Accept:** Verify correct boolean classification and mask operations for all inputs in `[-1, 255]`, including `EOF`, standard control codes `0..31`, `127`, printable ASCII `32..126`, and high bytes `128..255`.

### 2. `LIBC-STDIO-02` — Stdio stream utilities and constants (`clearerr`, `setbuf`, `fileno`, `BUFSIZ`, `SEEK_*`)
- **Scope:**
  - `libc/include/stdio.h`: add `BUFSIZ` (1024), `SEEK_SET` (0), `SEEK_CUR` (1), `SEEK_END` (2), and prototypes for `clearerr`, `setbuf`, `fileno`.
  - `libc/include/unistd.h`: add `SEEK_SET`, `SEEK_CUR`, `SEEK_END`.
  - `libc/cb_libc.c`:
    - `void cb_libc_clearerr(FILE *stream)`: clears error indicator (`stream->error = 0; stream->eof = 0`).
    - `void cb_libc_setbuf(FILE *stream, char *buf)`: no-op stub matching unbuffered architecture.
    - `int cb_libc_fileno(FILE *stream)`: returns `stream->descriptor` (or `-1` with `EBADF` if invalid/closed).
- **Red:** Compilation failure on missing symbols; focused probe verifying `fileno(stdin) == 0`, `fileno(stdout) == 1`, `fileno(stderr) == 2`, error flag clear via `clearerr`, and `BUFSIZ` constant.
- **Accept:** Clean lifecycle tests for all three stream functions; no heap allocation or state leak.

### 3. `LIBC-ERR-02` — Diagnostic output helper `warnx(3)`
- **Scope:**
  - `libc/include/err.h`: declare `void cb_libc_warnx(const char *fmt, ...)`.
  - `libc/cb_libc.c`: implement `cb_libc_warnx`, outputting `getprogname()`, `": "`, formatted string, and `"\n"` to `stderr` without appending `strerror(errno)`. Preserves `errno`.
- **Red:** Focused probe verifying `warnx` output format on stderr and verifying `errno` is unchanged across calls.
- **Accept:** Matches BSD `warnx` diagnostic formatting; handles NULL/empty formats; passes `-Wformat` checks.

### 4. `LIBC-STRTOL-01` — String-to-integer conversion `strtol(3)`
- **Scope:**
  - `libc/include/stdlib.h`: declare `long cb_libc_strtol(const char *nptr, char **endptr, int base)`.
  - `libc/cb_libc.c`: implement `strtol` (or adapt existing NetBSD `strtoimax` veneer to provide `strtol` with `LONG_MIN`/`LONG_MAX` clamping and `ERANGE`).
- **Red:** Focused probe verifying base 0, 8, 10, 16 parsing, leading whitespace handling, sign handling, `endptr` assignment, and overflow/underflow clamping to `LONG_MAX`/`LONG_MIN`.
- **Accept:** Full parity with NetBSD/POSIX `strtol` contract on both LP64 and ILP32 word sizes.

### 5. `STAT-02` — POSIX `sys/stat.h` veneer and `stat`/`fstat` wiring
- **Scope:**
  - `libc/include/sys/stat.h`: define `struct stat` (`st_ino`, `st_mode`, `st_size`, `st_blksize`), `ino_t`, `mode_t`, `blksize_t`, `blkcnt_t`, `S_IFMT`, `S_IFREG`, `S_IFDIR`, `S_IFCHR`, `S_IFIFO`, `S_ISREG(m)`, `S_ISDIR(m)`, `S_ISCHR(m)`, `S_ISFIFO(m)`.
  - `libc/cb_libc.c`: implement `cb_libc_stat` and `cb_libc_fstat` calling `api->stat` and `api->fstat`, translating `cb_stat_v1` into `struct stat` with synthesized type bits in `st_mode`.
- **Red:** Probe fails to compile against `sys/stat.h`; then runtime check fails to distinguish regular files from directories/terminals/pipes.
- **Accept:** `fstat` and `stat` return 0 and populate `st_mode`/`st_size`/`st_ino` accurately on regular files (`S_ISREG` true), directories (`S_ISDIR` true), terminal devices (`S_ISCHR` true), and pipes (`S_ISFIFO` true); returns `-1` with `EBADF` on invalid descriptors and `ENOENT` on non-existent paths.

### 6. `FCNTL-01` — File control flags and genuine per-node advisory record locking (`O_NONBLOCK`, `fcntl`)
- **Scope:**
  - `include/cannedbsd/abi.h`: `struct cb_flock_v1`, `enum cb_flock_type`, `enum cb_fcntl_cmd`, `api->fcntl`.
  - `src/internal.h`, `src/vfs.c`, `src/core.c`: per-node lock tracking in `cb_vfs_node`, conflict detection, lock acquisition, unlock, blocking on contested `F_SETLKW`, and automatic lock purge on descriptor `close()` and task `exit()`.
  - `libc/include/fcntl.h`: `O_NONBLOCK`, `struct flock` (`l_start`, `l_len`, `l_pid`, `l_type`, `l_whence`), `F_RDLCK`, `F_WRLCK`, `F_UNLCK`, `F_GETLK`, `F_SETLK`, `F_SETLKW`.
  - `libc/cb_libc.c`: implement `int cb_libc_fcntl(int fd, int cmd, ...)`.
- **Red:** Focused multi-task cooperative locking probe fails while `fcntl` and locking structures are absent.
- **Accept:**
  1. *Acquisition & inspection:* Single task acquires `F_WRLCK` via `F_SETLK`; `F_GETLK` reports no conflict from self; `F_UNLCK` clears lock.
  2. *Cooperative mutual exclusion:* When Task 1 holds `F_WRLCK` on a file, Task 2 calling `F_SETLK` with `F_WRLCK` fails with `EAGAIN`/`EACCES`, and `F_GETLK` reports Task 1's PID.
  3. *Automatic release on close:* Closing a file descriptor referencing the locked node purges that task's locks on the node; Task 2 can subsequently acquire the lock.
  4. *Automatic release on task exit:* When Task 1 exits, all its held locks are purged by the kernel; Task 2 can acquire the lock.
  5. *Error handling:* `EBADF` on invalid descriptors; `EINVAL` on invalid `whence` or negative start offsets.

### 7. `CAT-01` — NetBSD `cat(1)` import and full integration
- **Scope:**
  - Import `upstream/netbsd/bin/cat/cat.c` (RCS 1.60, SHA256 `2cc2ced0fcc6c143e1406e64cdd64ea768101fcd19b6dad531b911a611697cbd`) verbatim.
  - Register `cat` in program registry table (`src/programs.c`, `src/main.c`, `Makefile`).
  - Update `UPSTREAM.md` with file metadata, license, and hash.
- **Red:** `bsdinacan -c 'cat file'` fails as unrecognized command before registration; then behavioral test cases fail before import.
- **Accept:** Meets full acceptance suite below.

---

## 5. Acceptance test matrix for `CAT-01` integration

When the prerequisite chain is complete and `cat` is imported, the integration gate must verify:

1. **Basic file concatenation:** Single file, multiple files in sequence, exact byte stream preserved.
2. **Standard input operand (`-`):** Reading stdin, interleaving `-` with file paths (`cat file1 - file2`).
3. **Empty files & binary data:** Zero-byte files, binary files with arbitrary bytes (`0x00`, `0xFF`, non-ASCII), files larger than `BUFSIZ` (multi-block chunks).
4. **Formatting flags (cooked mode):**
   - `-n`: Number all output lines starting at 1 (`"%6d\t"`).
   - `-b`: Number non-blank output lines only (`-b` implies `-n`).
   - `-s`: Squeeze multiple adjacent blank lines into a single blank line.
   - `-e`: Display non-printing characters and print `$` at end of each line (`-e` implies `-v`).
   - `-t`: Display non-printing characters and print tabs as `^I` (`-t` implies `-v`).
   - `-v`: Display non-printing characters (`^X` for control, `M-X` for meta/high-bit bytes).
5. **Raw mode optimizations:**
   - Unflagged execution runs via `raw_cat`, verifying `read`/`write` fast loop.
   - `-B bsize`: Custom buffer size parsing via `strtol`.
   - `-u`: Unbuffered execution (`setbuf(stdout, NULL)`).
   - `-l`: Advisory locking on stdout (`fcntl(F_SETLKW)`): succeeds cleanly when uncontested; enforces mutual exclusion when contested.
   - `-f`: Regular file gate (`S_ISREG` check; non-regular files skipped with `warnx`).
6. **Error handling and continuation:**
   - Missing input file produces `warn("%s", path)`, sets exit status 1, and continues processing subsequent files.
   - Write error / broken pipe (`EPIPE`) exits with failure.
7. **Resource hygiene:**
   - All opened file descriptors are closed before exit.
   - Allocated memory buffers (e.g. malloc buffer from `-B`) are reclaimed cleanly.
