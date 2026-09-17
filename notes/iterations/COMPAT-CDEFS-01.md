# COMPAT-CDEFS-01: `__BEGIN_DECLS`, `__END_DECLS`, and `assert`

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `8a6fe88` (`origin/main`).
- **Branch:** `work/COMPAT-CDEFS-01`.
- **Hypothesis:** C++ linkage header guards (`__BEGIN_DECLS`, `__END_DECLS`) and no-op assertions (`assert(e) ((void)0)`) can be provided in libc/compat headers with zero runtime impact and zero ABI expansion.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of BSD headers including `<sys/cdefs.h>` or `<assert.h>` referencing `__BEGIN_DECLS` or `assert()`.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cp/utils.c:32:10: fatal error: __BEGIN_DECLS undefined / assert undeclared
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

1. `libc/include/sys/cdefs.h`: Defined `__BEGIN_DECLS` and `__END_DECLS` under `#ifdef __cplusplus` (expanding to `extern "C" {` and `}`), expanding to empty tokens in C mode.
2. `compat/netbsd/include/assert.h`: Defined `#define assert(e) ((void)0)` alongside existing `_DIAGASSERT`.
3. Zero ABI changes on `include/cannedbsd/abi.h`.
