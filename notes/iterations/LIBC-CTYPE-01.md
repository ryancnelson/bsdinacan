# LIBC-CTYPE-01: `isascii`, `toascii`, `iscntrl` C-locale helpers

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `06ee0d1` (`origin/main`).
- **Branch:** `work/LIBC-CTYPE-01`.
- **Scope:** Provide `isascii`, `toascii`, and `iscntrl` for the C locale in `libc/include/ctype.h` and `libc/cb_libc.c`, satisfying compile-time and runtime requirements of NetBSD `cat.c`.
- **Zero ABI growth:** Strictly 0 changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation test of callers referencing `isascii`, `toascii`, or `iscntrl`.
- **Observed failure:**
  ```text
  upstream/netbsd/bin/cat/cat.c: implicit declaration of function 'isascii' / 'toascii' / 'iscntrl'
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

1. `include/cannedbsd/libc.h`: Declared `cb_libc_isascii`, `cb_libc_toascii`, `cb_libc_iscntrl`.
2. `libc/include/ctype.h`: Added `#define isascii`, `#define toascii`, `#define iscntrl` and prototypes.
3. `libc/cb_libc.c`: Implemented `cb_libc_isascii`, `cb_libc_toascii`, `cb_libc_iscntrl` matching standard C-locale semantics.
4. `tests/libc_file_probe.c`: Added test probe verifying `isascii`, `toascii`, and `iscntrl` behavior.
