# ECHO-02: Retire `echo` Placeholder and Register Pinned NetBSD `echo`

- **Status:** Done; ready for merge.
- **Base SHA:** `652e869` (`origin/main`).
- **Branch:** `work/ECHO-02`.
- **Date:** 2026-09-17.
- **Scope:** Unify `/bin/echo` onto the pinned NetBSD `bin/echo/echo.c` import, retiring the bootstrap placeholder `echo_main` in `src/programs.c` and removing the temporary `netbsdecho` alias across the codebase. Zero ABI modifications (`include/cannedbsd/abi.h` unchanged).

---

## 1. Executive Summary

During `MILESTONE-REMEASURE-01` (`bae004b`), an exploratory audit identified an open naming and registration duality:
- `/bin/echo` was served by the owned bootstrap stand-in `echo_main` (`src/programs.c:44-64`).
- The pinned NetBSD `echo.c` was registered under the distinct alias `netbsdecho` (`src/programs.c:178`).

`ECHO-02` resolves this duality following the pattern established by `CAT-01`:
1. **Placeholder Retirement:** Removed `echo_main` and `echo_program` descriptor from `src/programs.c`.
2. **Standard Registration:** Registered `cb_echo_program` (`commands/echo_module.c`) as `"echo"` backed by `cb_echo_main` in `cb_register_base_programs`.
3. **Alias Removal:** Removed `netbsdecho`. Grep audit confirmed zero external consumers or test requirements depended on the `netbsdecho` name.
4. **Build Parity (`BUILD-SYNC-01`):** Updated both Linux `Makefile` (`-Dmain=cb_echo_main`) and `platform/mac68k/CMakeLists.txt` (`main=cb_echo_main`), and updated `platform/mac68k/acceptance_cases.def`.
5. **Behavior & Diagnostics:** Pinned NetBSD `echo` preserves standard arguments and `-n` behavior while providing proper stdio `fflush`/`ferror` write-error diagnostics (`echo: write error: broken pipe\n`) on stdout write failure.

---

## 2. Red Test Demonstration

### Behavioral Red Test: Write Error Diagnostic
NetBSD `echo.c` executes:
```c
(void)fflush(stdout);
if (ferror(stdout) != 0)
    err(1, "write error");
```
When writing to a sink that closes early (e.g. `echo hello | false`), NetBSD `echo` produces stderr diagnostic `echo: write error: broken pipe\n` and exits with status `1`.

In contrast, bootstrap `echo_main` executed:
```c
if (write_all(api, 1, argv[index], strlen(argv[index])) < 0)
    return 1;
```
returning status `1` with an empty stderr.

### Observed Red Output (before fix on `build/bsdinacan`):
```text
broken pipe write error diagnostic stderr mismatch:
--- /tmp/cannedbsd-echo.lMdnhL/exp_stderr
+++ /tmp/cannedbsd-echo.lMdnhL/err
@@ -1 +0,0 @@
-echo: write error: broken pipe
FAIL: broken pipe write error diagnostic: stderr mismatch
```

### Observed Green Output (after fix):
```text
echo behavioral matrix passed
```

---

## 3. Detailed Changes

1. **`src/programs.c`:**
   - Replaced `extern const struct cb_program_v1 cb_netbsdecho_program;` with `extern const struct cb_program_v1 cb_echo_program;`.
   - Removed `static int echo_main(...)`.
   - Removed `PROGRAM_DESCRIPTOR(echo_program, "echo", echo_main);`.
   - Updated `cb_register_base_programs` to register `&cb_echo_program` (and removed `&echo_program` and `&cb_netbsdecho_program`).

2. **`commands/echo_module.c`:**
   - Declared `int cb_echo_main(int argc, char *argv[]);`.
   - Registered `CB_LIBC_PROGRAM(cb_echo_program, "echo", cb_echo_main);`.

3. **`Makefile`:**
   - Updated `$(ECHO_COMMAND_OBJECT)` build rule to compile with `-Dmain=cb_echo_main`.
   - Updated `sanitize` static analyzer check for `echo.c` to use `-Dmain=cb_echo_main`.

4. **`platform/mac68k/CMakeLists.txt`:**
   - Updated `cb_echo` target to define `main=cb_echo_main`.

5. **`platform/mac68k/acceptance_cases.def`:**
   - Updated mac acceptance cases from `netbsdecho` to `echo`.

6. **`tests/test_netbsd_source.sh`:**
   - Updated symbol check from `cb_netbsdecho_main` to `cb_echo_main`.

7. **`tests/test_echo_state.c`:**
   - Updated entry symbol from `cb_netbsdecho_main` to `cb_echo_main`.
   - Updated test-internal fault program from `netbsdechofault` to `echofault`.

8. **`tests/test_echo_behavior.sh`:**
   - Updated test cases to invoke `echo`.
   - Added broken pipe write error diagnostic test case asserting `echo: write error: broken pipe\n` and exit status `1`.

9. **`UPSTREAM.md` & `BACKLOG.md`:**
   - Updated upstream documentation and backlog entries.

---

## 4. Verification

- **Behavioral Suite:** `tests/test_echo_behavior.sh` (8/8 test cases pass).
- **Core Suite:** `build/test_core` (all state probes and shell pipelines pass).
- **Parity Check:** `python3 tests/test_build_parity.py` pass.
- **Mac Guest Suite:** `python3 tests/test_mac_guest.py` (18/18 tests pass).
- **Full Container CI:** `make LDLIBS=-lucontext SANITIZE_CC=clang ci` green.
