# LIBC-CP-STUB-01: Stat Timestamps, Node-Type Stubs, Permissions, and String Helpers

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `d5b4f15` (`origin/main`).
- **Branch:** `work/LIBC-CP-STUB-01`.
- **Scope:** Provides the four unblocked prerequisite groups required for `cp` compilation and honest runtime behavior:
  1. **Timestamp fields & setters:** `struct stat` timestamp accessors (`st_atimespec`, `st_mtimespec`, `st_ctimespec`) mapped to `st_mtime`/`st_atime`/`st_ctime` macros, and `cb_libc_lutimens` returning `-1` with `errno = ENOSYS`.
  2. **Unreachable RAMFS node-type branches:** `link`, `symlink`, `readlink`, `mkfifo`, `mknod` in `libc/include/unistd.h` and `libc/include/sys/stat.h`, with `cb_libc_*` returning `-1` with `ENOSYS` (`readlink` returns `EINVAL`).
  3. **Identity, umask & permissions:**
     - `cb_libc_getuid`: returns `0` (POSIX `getuid` has no failure contract; `0` represents cannedBSD's single implicit root identity).
     - `cb_libc_umask`: tracks and returns the task's umask (default `022`).
     - `cb_libc_chmod` / `cb_libc_lchmod`: return `-1` with `errno = ENOSYS` (NetBSD `cp` calls `chmod` on directories upon post-order ascent casting to `(void)chmod(...)`, and in `setfile()` under `-p` which detectably warns; returning `ENOSYS` keeps permissions deferred without breaking unflagged `cp`).
     - `cb_libc_lchown` / `cb_libc_chflags`: return `-1` with `errno = ENOSYS`.
  4. **Constants & string helpers:** `libc/include/string.h` declares `strncat` (`cb_libc_strncat`); `libc/include/fts.h` defines `FTS_ROOTLEVEL 0`; `compat/netbsd/include/sys/param.h` defines `PATH_MAX 1024` and `MAXBSIZE 65536`.
- **Hypothesis:** All four groups provide exact compile-time declarations and honest runtime failure returns without speculative feature implementations or ABI expansion.
- **Zero ABI growth:** Strictly 0 changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of callers referencing `cp` stub interfaces.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cp/cp.c: implicit declaration of 'getuid', 'umask', 'chmod', 'lutimens', 'strncat', 'FTS_ROOTLEVEL'
  ```

---

## 2. Green (Implementation & CI Verification)

- **Test Suite:** `make LDLIBS=-lucontext test`
- **Full CI Gate:** `make LDLIBS=-lucontext SANITIZE_CC=clang ci`
- **Alpine Container Verification:** Clean exit code 0 across all verification stages inside container:
  - `check-linux-write`: Passed.
  - `check-acceptance-output`: Passed.
  - `test_mac_guest.py`: 18 tests passed.
  - `check-publication`: Passed.
  - `clean test`: Unit tests, probes, and all behavioral tests passed.
  - `sanitize`: ASan and UBSan runs clean with zero leaks or undefined behaviors.
  - `check-build-modes`: Passed.
  - `analyze`: GCC `-fanalyzer` clean.

---

## 3. Changes Made

1. `include/cannedbsd/libc.h`: Added `struct timespec` and `st_atimespec`, `st_mtimespec`, `st_ctimespec` fields to `struct stat` with backwards-compatible `st_atime`/`st_mtime`/`st_ctime` accessors. Declared new libc entry points.
2. `libc/include/sys/stat.h`: Declared `chmod`, `lchmod`, `chflags`, `mkfifo`, `mknod`, `umask`.
3. `libc/include/sys/time.h`: Declared `lutimens` with `struct timespec` and `struct timeval` declaration guards.
4. `libc/include/unistd.h`: Declared `getuid`, `link`, `symlink`, `readlink`, `lchown`.
5. `libc/include/string.h`: Declared `strncat` / `cb_libc_strncat`.
6. `libc/include/fts.h`: Defined `FTS_ROOTLEVEL 0`.
7. `compat/netbsd/include/sys/param.h`: Defined `PATH_MAX` and `MAXPATHLEN` derived from `CB_PATH_MAX`, and `MAXBSIZE 65536`.
8. `libc/include/sys/types.h` and `compat/netbsd/include/sys/types.h`: Synchronized type guards across all POSIX type aliases.
9. `libc/cb_libc.c`: Implemented `cb_libc_chmod`, `cb_libc_lchmod`, `cb_libc_chflags`, `cb_libc_lutimens`, `cb_libc_lchown`, `cb_libc_link`, `cb_libc_symlink`, `cb_libc_mkfifo`, `cb_libc_mknod` (`ENOSYS`), `cb_libc_readlink` (`EINVAL`), `cb_libc_getuid` (`0`), `cb_libc_umask` (state tracking), and `cb_libc_strncat`. Updated `convert_stat` to initialize timespec fields.
10. `tests/libc_file_probe.c` and `tests/libc_file_probe_module.c`: Added `cp-stub-probe` validating identity, umask state transitions, honest `ENOSYS`/`EINVAL` errors, `strncat` buffer manipulation, and `stat` timestamp field conversions.
