# LIBC-MMAN-01: `sys/mman.h` declarations and honest `mmap`/`munmap`/`madvise` veneer

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `884a3a3` (`origin/main`).
- **Branch:** `work/LIBC-MMAN-01`.
- **Hypothesis:** Providing standard `sys/mman.h` declarations with honest failure returns for `mmap` (`MAP_FAILED` with `ENOSYS`) and `munmap` (`-1` with `ENOSYS`), along with conforming advisory `0` for `madvise`, satisfies POSIX utility compilation without requiring an internal memory-mapping subsystem, as NetBSD `cp` cleanly and silently falls back to chunked `read()`/`write()` loops on `MAP_FAILED`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of callers referencing `<sys/mman.h>`.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cp/cp.c:51:10: fatal error: sys/mman.h: No such file or directory
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

1. `libc/include/sys/mman.h`: Created header declaring `PROT_*`, `MAP_*`, `MADV_*`, `mmap()`, `munmap()`, `madvise()` with asm redirection to `cb_libc_*`.
2. `include/cannedbsd/libc.h`: Declared `cb_libc_mmap`, `cb_libc_munmap`, `cb_libc_madvise`. Zero changes to `abi.h`.
3. `libc/cb_libc.c`:
   - `cb_libc_mmap`: returns `MAP_FAILED` (`(void *)-1`) with `errno = ENOSYS`.
   - `cb_libc_munmap`: returns `-1` with `errno = ENOSYS` (honest failure, no fabricated success).
   - `cb_libc_madvise`: returns `0` (advisory by POSIX/BSD specification; accepting advice as a no-op is conforming behavior).
4. `tests/libc_mman_probe.c`: Created probe asserting `mmap` fails with `ENOSYS`, `munmap` fails with `ENOSYS`, and `madvise` returns 0.
5. `tests/test_file.c`: Added contract assertions in unit test suite.
