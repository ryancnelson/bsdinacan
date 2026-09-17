# LIBC-STRTOL-01: `strtol` wrapper over pinned `strtoimax`

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `ec4e749` (`origin/main`).
- **Branch:** `work/LIBC-STRTOL-01`.
- **Scope:** Provide `strtol` in `libc/include/stdlib.h`, `include/cannedbsd/libc.h`, and `libc/cb_libc.c`, reusing pinned `strtoimax` and clamping to `[LONG_MIN, LONG_MAX]` with `ERANGE` reporting and `errno` preservation on success.
- **Zero ABI growth:** Strictly 0 changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation / link of consumers calling `strtol`.
- **Observed failure:**
  ```text
  implicit declaration of function 'strtol' / undefined reference to 'cb_libc_strtol'
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
  - `clean test`: Unit tests, probes, and all behavioral tests passed (including 51 assertions in `libcstrtoimaxprobe`).
  - `sanitize`: ASan and UBSan runs clean with zero leaks or undefined behaviors.
  - `check-build-modes`: Passed.
  - `analyze`: GCC `-fanalyzer` clean.

---

## 3. Changes Made

1. `include/cannedbsd/libc.h`: Declared `cb_libc_strtoimax` and `cb_libc_strtol`.
2. `libc/include/stdlib.h`: Declared `cb_libc_strtol` and added `#define strtol cb_libc_strtol`.
3. `libc/cb_libc.c`: Implemented `cb_libc_strtol` delegating to `cb_libc_strtoimax`, clamping values outside `[LONG_MIN, LONG_MAX]` to `LONG_MIN`/`LONG_MAX` with `CB_ERANGE`, and preserving `errno` on success.
4. `tests/libc_strtoimax_probe.c`: Added comprehensive test coverage for base 0/2/10/16/36, invalid bases, NULL endptr, partial parses, empty strings, sign handling, `LONG_MIN`/`LONG_MAX` bounds, `ERANGE` overflow/underflow clamping, overflow continuation digit scanning, and `errno` preservation via sentinels.
5. `tests/test_libc_source.sh`: Added `strtol` to the private symbol boundary validation list for `strtoimax_probe`.
