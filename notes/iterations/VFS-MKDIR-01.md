# VFS-MKDIR-01: Restore `cb_libc_mkdir` on measured `cp` demand

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `548e6d6` (`origin/main`).
- **Branch:** `work/VFS-MKDIR-01`.
- **Justification:** Measured demand from `cp.c:482` (`mkdir(to.p_path, curr->fts_statp->st_mode | S_IRWXU)` during directory tree copying).
- **Hypothesis:** Restoring `cb_libc_mkdir` and exposing `mkdir()` in `libc/include/sys/stat.h` satisfies directory creation demands using the existing `bound_api->mkdir` / `cb_vfs_mkdir_path` without requiring any ABI changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of callers referencing `mkdir()`.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cp/cp.c:482:10: error: implicit declaration of function 'mkdir'
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

1. `libc/include/sys/stat.h`: Declared `int mkdir(const char *path, mode_t mode)` with asm redirection to `cb_libc_mkdir`.
2. `include/cannedbsd/libc.h`: Declared `int cb_libc_mkdir(const char *path, uint32_t mode)`. Strictly 0 changes to `abi.h`.
3. `libc/cb_libc.c`: Implemented `cb_libc_mkdir`, routing to `bound_api->mkdir` with parameter validation (`EFAULT` on NULL path).
4. `tests/libc_file_probe.c`: Added `mkdir-probe` asserting directory creation, mode verification via `stat`, `EEXIST` on collision, `EFAULT` on NULL, and clean cleanup with `rmdir`.
