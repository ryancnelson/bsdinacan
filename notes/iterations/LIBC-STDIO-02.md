# LIBC-STDIO-02: `clearerr`, `setbuf`, `fileno`, `BUFSIZ`, `SEEK_*`

- **Status:** Complete and verified; ready for review and landing.
- **Base SHA:** `81e8f62` (`origin/main`).
- **Branch:** `work/LIBC-STDIO-02`.
- **Scope:** Provide `clearerr`, `setbuf`, `fileno`, `BUFSIZ`, and `SEEK_*` in `libc/include/stdio.h`, `include/cannedbsd/libc.h`, and `libc/cb_libc.c`, reusing `SEEK_*` from `unistd.h`, and enforcing honest unbuffered `setbuf` semantics.
- **Zero ABI growth:** Strictly 0 changes to `include/cannedbsd/abi.h`.

---

## 1. Red (First-Failure Diagnostic)

- **Command:** Compilation of ordinary sources calling `clearerr`, `fileno`, or `setbuf` with `<stdio.h>`.
- **Observed failure:**
  ```text
  implicit declaration of function 'clearerr' / 'fileno' / 'setbuf' / undefined reference to 'cb_libc_clearerr'
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
  - `clean test`: Unit tests, probes, and all behavioral tests passed (including `stdio-probe` in `fileprobe`).
  - `sanitize`: ASan and UBSan runs clean with zero leaks or undefined behaviors.
  - `check-build-modes`: Passed.
  - `analyze`: GCC `-fanalyzer` clean.

---

## 3. Changes Made

1. `libc/include/stdio.h`: Included `"unistd.h"` (for unified `SEEK_SET`/`SEEK_CUR`/`SEEK_END`), defined `BUFSIZ` as 1024, declared `cb_libc_clearerr`, `cb_libc_fileno`, `cb_libc_setbuf`, and added macro mappings `#define clearerr cb_libc_clearerr`, `#define fileno cb_libc_fileno`, `#define setbuf cb_libc_setbuf`.
2. `include/cannedbsd/libc.h`: Declared `cb_libc_clearerr`, `cb_libc_fileno`, `cb_libc_setbuf`.
3. `libc/cb_libc.c`: Implemented `cb_libc_clearerr` (clearing eof and error on input streams and sticky errors on stdout/stderr), `cb_libc_fileno` (returning real descriptor or failing with EBADF), and `cb_libc_setbuf` (accepting NULL for unbuffered streams and failing non-NULL buffer installations with ENOSYS).
4. `tests/libc_file_probe.c` & `tests/libc_file_probe_module.c`: Added `stdio-probe` test suite exercising `BUFSIZ`, `SEEK_*`, `fileno`, `setbuf`, `clearerr`, dynamic stream lifecycle, and error conditions.
5. `tests/test_libc_source.sh`: Added `clearerr`, `fileno`, `setbuf` to the verified private symbol list for `file_probe`.
