# MKDIR-CMD-01: NetBSD `mkdir` command import, libc shims, and session integration

- **Status:** Complete and verified; ready for review.
- **Base SHA:** `e6dcb3d` (`origin/main` after FS-STAT-01 merge).
- **Branch:** `work/MKDIR-CMD-01`.
- **Pinned Upstream Revision:** `b890038f7ae5831ab0b6eda87cb0a2d4aee00c2c`.
- **Imported Upstream Source:**
  - `bin/mkdir/mkdir.c` (SHA-256: `a3abf691d386bd2b8a23483e0dd7abb314403b281031d3b58cd3abec4e0926d8`, RCS: `$NetBSD: mkdir.c,v 1.39 2021/09/13 22:46:02 kre Exp $`)
- **Hypothesis:** Pinned NetBSD `mkdir` (`bin/mkdir/mkdir.c`) can be imported 100% byte-for-byte unmodified by providing the necessary libc string helpers (`strspn`, `strcspn`) and mode shims (`setmode`, `getmode`), exposing the existing `cb_libc_mkdir` / VFS `mkdir` to the shell without expanding the kernel ABI (`include/cannedbsd/abi.h`).

---

## 1. Red (First-Failure Diagnostic Measurement)

- **Measurement 1 (Standalone test suite):** Ran `tests/test_mkdir_behavior.sh` before registering `cb_mkdir_program` in `src/programs.c`.
  - **Observed first failure:**
    ```text
    FAIL: single directory creation: stdout mismatch
    --- /tmp/test_mkdir.tmp.expected
    +++ /tmp/test_mkdir.tmp.actual
    @@ -1 +0,0 @@
    +d1
    ```
    (Exit status 127 from shell on unknown command `mkdir`).
- **Measurement 2 (End-to-End Session):** Extended `tests/test_file_manipulation_session.sh` with test case 6 (`session directory lifecycle: mkdir, nested file creation, directory listing, and recursive deletion via rm -r`).
  - **Observed failure:**
    ```text
    FAIL: session directory lifecycle: expected status 0, got 127
    ```

---

## 2. Green (Implementation & Verification)

- **Libc Support (`libc/cb_libc.c`, `libc/include/string.h`, `libc/include/unistd.h`, `include/cannedbsd/libc.h`):**
  - Implemented `cb_libc_strspn` and `cb_libc_strcspn` (pure string scanning, zero system calls).
  - Implemented `cb_libc_setmode` and `cb_libc_getmode`. `setmode` sets `errno = CB_EINVAL` and returns `NULL`.
- **Mode Parsing & Honesty:**
  - When `-m <mode>` is passed, `mkdir.c` invokes `setmode(optarg)`. Because symbolic mode parsing is not implemented, `setmode` returns `NULL`, causing `mkdir` to cleanly and honestly fail with `mkdir: Cannot set file mode '<mode>': Invalid argument` and exit status 1.
  - Plain `mkdir` and `mkdir -p` do not call `setmode` and succeed unconditionally.
- **Process Model Note:**
  - `mkdir` executes entirely in-process with 0 calls to `vfork`, `fork`, `exec*`, `spawn`, `system`, or `popen`.
- **Target Parity:**
  - Wired `commands/mkdir_module.c` and `upstream/netbsd/bin/mkdir/mkdir.c` into both `Makefile` (`netbsd_mkdir.o`) and `platform/mac68k/CMakeLists.txt` (`cb_mkdir`) in the same commit.

---

## 3. Behavioral Test Suite Matrix (`tests/test_mkdir_behavior.sh`)

Ten behavioral tests verify `mkdir` across all supported invocations:
1. `test_mkdir_basic`: create single directory.
2. `test_mkdir_multiple`: create multiple directories in a single command.
3. `test_mkdir_existing_fails`: creating an already existing directory fails with exit 1 and diagnostic.
4. `test_mkdir_parent_flag_simple`: `mkdir -p` on a single non-existent directory.
5. `test_mkdir_parent_flag_nested`: `mkdir -p` on multi-level nested path (`/tmp/sub1/sub2/sub3`).
6. `test_mkdir_parent_flag_existing`: `mkdir -p` on an existing directory succeeds silently with exit 0.
7. `test_mkdir_trailing_slashes`: creating directory with trailing slashes (`/tmp/slashdir///`).
8. `test_mkdir_mode_fails_honestly`: `mkdir -m 0755` fails honestly with status 1 and diagnostic (`Cannot set file mode`).
9. `test_mkdir_missing_operand_fails`: `mkdir` without arguments prints usage and exits 1.
10. `test_mkdir_nonexistent_parent_without_p_fails`: creating nested path without `-p` fails with exit 1.

---

## 4. Extended End-to-End Session (`tests/test_file_manipulation_session.sh`)

- Added Test Case 6: Continuous session creating nested directory `/tmp/session_dir/nested`, populating it with files via `echo`, inspecting via `cat`, listing via `ls`, and recursively deleting via `rm -r /tmp/session_dir`, asserting clean empty directory listing afterwards.
- This serves as an end-to-end consumer exercising `VFS-05` (`rm -r` directory iteration look-ahead cursor) through the shell.

---

## 5. Zero-Growth ABI Invariant

- **`include/cannedbsd/abi.h`**: **Strictly 0 changes.**
- Zero modification to public kernel ABI or libc veneer interfaces.
