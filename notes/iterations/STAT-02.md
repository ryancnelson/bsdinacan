# STAT-02: POSIX `sys/stat.h` veneer and `stat`/`fstat`/`lstat` wiring

- **Status:** Complete and verified; ready for review.
- **Base SHA:** `a9ba8d7` (`origin/main`).
- **Branch:** `work/STAT-02`.
- **Hypothesis:** Wiring POSIX `struct stat` in `libc/include/sys/stat.h` and mapping onto `cb_stat_v1`'s existing 4 fields with synthesized `st_mode` type bits (`S_IFREG`, `S_IFDIR`, `S_IFCHR`, `S_IFIFO`) satisfies standard file type macros (`S_ISREG`, `S_ISDIR`, `S_ISCHR`, `S_ISFIFO`) without expanding `cb_stat_v1` or perturbing the ABI.

---

## 1. Red (Falsifiable TDD Evidence)

- **Command:** `make LDLIBS=-lucontext test`
- **Expected failure:** Compilation failure in `tests/libc_file_probe.c` when using `struct stat`, `stat()`, `fstat()`, `lstat()`, `S_ISREG()`, `S_ISDIR()`, `S_ISCHR()`, `S_ISFIFO()`.
- **Observed failure:**
  ```text
  tests/libc_file_probe.c:22:21: error: storage size of 'sb' isn't known
     22 |         struct stat sb, fsb;
        |                     ^~
  tests/libc_file_probe.c:26:13: error: implicit declaration of function 'stat'
     26 |         if (stat("/tmp/stream-input", &sb) != 0) return 60;
  tests/libc_file_probe.c:27:14: error: implicit declaration of function 'S_ISREG'
     27 |         if (!S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode) ||
  tests/libc_file_probe.c:27:37: error: implicit declaration of function 'S_ISDIR'
  tests/libc_file_probe.c:28:13: error: implicit declaration of function 'S_ISCHR'
  tests/libc_file_probe.c:28:36: error: implicit declaration of function 'S_ISFIFO'
  tests/libc_file_probe.c:34:13: error: implicit declaration of function 'lstat'
  tests/libc_file_probe.c:41:13: error: implicit declaration of function 'fstat'
  ```

---

## 2. Green (Implementation & Verification)

- **Focused command:** `make LDLIBS=-lucontext test`
- **Full command:** `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- **Linux / Alpine container verification:** Clean exit 0 across standard suite, sanitizer suite (AddressSanitizer + UndefinedBehaviorSanitizer), GCC analyzer, architecture boundary checks, and publication hygiene.
- **Mac68k qualification applicability:** Mac68k (Basilisk II) guest qualification is recorded as **PENDING** coordinator execution.

---

## 3. Change & Architecture Review

### A. Header Surface
1. **`libc/include/sys/types.h`**:
   - Defined `mode_t`, `nlink_t`, `uid_t`, `gid_t`, `off_t`, `ssize_t`, `blksize_t`, `blkcnt_t`, `dev_t`.
2. **`libc/include/unistd.h`**:
   - Included `sys/types.h`, removing duplicate typedef definitions.
3. **`include/cannedbsd/libc.h`**:
   - Defined POSIX file type constants: `S_IFMT` (`0170000`), `S_IFIFO` (`0010000`), `S_IFCHR` (`0020000`), `S_IFDIR` (`0040000`), `S_IFBLK` (`0060000`), `S_IFREG` (`0100000`), `S_IFLNK` (`0120000`), `S_IFSOCK` (`0140000`).
   - Defined `struct stat` with fields: `st_ino`, `st_mode`, `st_size`, `st_blksize`, `st_blocks`.
   - Declared `cb_libc_stat`, `cb_libc_fstat`, `cb_libc_lstat`.
4. **`libc/include/sys/stat.h`**:
   - Exposed POSIX test macros: `S_ISFIFO`, `S_ISCHR`, `S_ISDIR`, `S_ISBLK`, `S_ISREG`, `S_ISLNK`, `S_ISSOCK`.
   - Exposed permission constants: `S_IRWXU`, `S_IRUSR`, `S_IWUSR`, `S_IXUSR`, `S_IRWXG`, `S_IRGRP`, `S_IWGRP`, `S_IXGRP`, `S_IRWXO`, `S_IROTH`, `S_IWOTH`, `S_IXOTH`, `DEFFILEMODE`, `ACCESSPERMS`, `ALLPERMS`.
   - Used compiler link-name redirection (`__asm__("cb_libc_*")`) to avoid macro collision between `struct stat` and `stat()`.

### B. Libc Veneer Implementation (`libc/cb_libc.c`)
- `translate_stat()` maps `enum cb_node_type` to POSIX type bits:
  - `CB_NODE_REGULAR` / `CB_NODE_EXECUTABLE` $\rightarrow$ `S_IFREG`
  - `CB_NODE_DIRECTORY` $\rightarrow$ `S_IFDIR`
  - `CB_NODE_TERMINAL` $\rightarrow$ `S_IFCHR`
  - `CB_NODE_PIPE` $\rightarrow$ `S_IFIFO`
- Synthesizes `st_mode = type_bits | (raw_stat->mode & 07777)`.
- Populates `st_ino`, `st_size`.
- Sets `st_blksize = 1024` as an arbitrary I/O buffer sizing hint (read by client stdio and `cat.c` buffer calculations).
- Sets `st_blocks = 0` truthfully reflecting that RAMFS allocates in-memory byte buffers with zero underlying disk block allocation.
- `cb_libc_stat()`, `cb_libc_fstat()`, `cb_libc_lstat()` invoke `bound_api->stat()` / `bound_api->fstat()`, handle `EFAULT` on NULL pointers, and return 0 / -1 with task error.
- `lstat` justification: directly demanded by pinned NetBSD utilities (`rm.c` lines 292, 398; `mv.c` lines 248, 259; `cp` `utils.c` line 150). With no symlink nodes in CannedBSD, `lstat` is identical to `stat`.
- `mkdir`: deliberately omitted from `STAT-02` (undemanded by `cat.c` and deferred until `cp -r` / `mkdir` command import).

### C. ABI & Zero-Growth Invariant
- **`include/cannedbsd/abi.h`**: **Zero changes.** `struct cb_stat_v1` remains strictly frozen at its four existing fields (`inode`, `size`, `mode`, `type`).
- No changes to `src/core.c`, `src/vfs.c`, or `src/ramfs.c`.
- Appending canonical POSIX timestamps (`atime`/`mtime`/`ctime`), `nlink`, `uid`, `gid`, and `dev` remains scheduled under `FS-STAT-01` when a multi-column or timestamp-sensitive consumer is integrated.

---

## 4. Test Matrix & Coverage

1. **Regular file classification:** `S_ISREG` is true; `S_ISDIR`, `S_ISCHR`, `S_ISFIFO` are false.
2. **Directory classification:** `S_ISDIR` is true; `S_ISREG`, `S_ISCHR`, `S_ISFIFO` are false.
3. **Terminal classification:** `S_ISCHR` is true; others false.
4. **Pipe classification:** `S_ISFIFO` is true; others false.
5. **Field agreement:** `stat` and `fstat` report identical `st_ino`, `st_mode`, `st_size`, `st_blksize`, `st_blocks`.
6. **`lstat` equivalence:** `lstat` produces identical results to `stat`.
7. **Error handling:** `EBADF` on closed/invalid fd; `ENOENT` on non-existent path; `EFAULT` on NULL path or buffer.
8. **Source boundary:** Verified in `tests/test_libc_source.sh` that ordinary callers link against `cb_libc_stat`, `cb_libc_fstat`, `cb_libc_lstat` without host library leakage.
