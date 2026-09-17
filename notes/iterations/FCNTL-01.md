# FCNTL-01: `fcntl.h` declarations and honest failure veneer

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `1d661b4` (`origin/main`).
- **Branch:** `work/FCNTL-01`.
- **Scope:** Provide compile-time declarations in `libc/include/fcntl.h` (`O_NONBLOCK`, `F_GETFL`, `F_SETFL`, `F_GETLK`, `F_SETLK`, `F_SETLKW`, `F_RDLCK`, `F_UNLCK`, `F_WRLCK`, and `struct flock`) demanded by NetBSD `cat.c`.
- **Implementation:** `cb_libc_fcntl` returns `-1` with `errno = ENOSYS`. As established in `BACKLOG.md` / `SIG-01-design`, record locking is not faked; returning failure truthfully informs callers without fabricated locks.
- **Zero ABI growth:** Strictly 0 changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of callers referencing `struct flock` or `fcntl(..., F_SETLKW, ...)`.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cat/cat.c: implicit declaration of 'fcntl' / unknown type 'struct flock'
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

1. `libc/include/fcntl.h`: Added `O_NONBLOCK`, `F_*` command constants, `F_*LCK` type constants, `struct flock` definition, and `fcntl` declaration/redirection.
2. `include/cannedbsd/libc.h`: Declared `int cb_libc_fcntl(int fd, int cmd, ...)`.
3. `libc/cb_libc.c`: Implemented `cb_libc_fcntl` returning `-1` with `CB_ENOSYS`.
4. `tests/libc_file_probe.c`: Added test coverage verifying `fcntl` fails with `ENOSYS` when called with `F_SETLKW` and `struct flock`.
